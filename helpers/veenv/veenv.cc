// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "helpers/veenv/veenv.h"

#include <string.h>

#include <limits>
#include <map>
#include <string>
#include <vector>

#include <vefs.h>
#include "leveldb/env.h"
#include "leveldb/status.h"
#include "port/port.h"
#include "port/thread_annotations.h"
#include "util/mutexlock.h"

/*static inline uint64_t ve_get() {
  uint64_t ret;
  void* vehva = ((void*)0x000000001000);
  asm volatile("lhm.l %0,0(%1)" : "=r"(ret) : "r"(vehva));
  return ((uint64_t)1000 * ret) / 800;
}*/

uint64_t taken_time = 0;
uint64_t tmp_var = 0;
namespace leveldb {

namespace {

constexpr const size_t kWritableFileBufferSize = 65536;

class FileState {
 public:
  // FileStates are reference counted. The initial reference count is zero
  // and the caller must call Ref() at least once.
  // FileState() : refs_(0), size_(0), vefs_(Vefs::Get()) {}
  FileState() = delete;
  FileState(const std::string& fn) : refs_(0), vefs_(Vefs::Get()) {
    inode_ = vefs_->Create(fn, false);
    size_ = vefs_->GetLen(inode_);
  }

  // No copying allowed.
  FileState(const FileState&) = delete;
  FileState& operator=(const FileState&) = delete;

  // Increase the reference count.
  void Ref() {
    MutexLock lock(&refs_mutex_);
    ++refs_;
  }

  // Decrease the reference count. Delete if this is the last reference.
  void Unref() {
    bool do_delete = false;

    {
      MutexLock lock(&refs_mutex_);
      --refs_;
      assert(refs_ >= 0);
      if (refs_ <= 0) {
        do_delete = true;
      }
    }

    if (do_delete) {
      vefs_->Delete(inode_);
      delete this;
    }
  }

  uint64_t Size() const {
    MutexLock lock(&blocks_mutex_);
    return size_;
  }

  void Truncate() {
    MutexLock lock(&blocks_mutex_);
    vefs_->Truncate(inode_, 0);
  }

  Status Read(uint64_t offset, size_t n, Slice* result, char* scratch) const {
    MutexLock lock(&blocks_mutex_);
    if (offset > size_) {
      return Status::IOError("Offset greater than file size.");
    }
    const uint64_t available = size_ - offset;
    if (n > available) {
      n = static_cast<size_t>(available);
    }
    if (n == 0) {
      *result = Slice();
      return Status::OK();
    }

    uint64_t t = ve_get();
    if (vefs_->Read(inode_, offset, n, scratch) != Vefs::Status::kOk) {
      return Status::IOError("Error in VeFS");
    }
    taken_time += ve_get() - t;

    *result = Slice(scratch, n);
    return Status::OK();
  }

  Status Append(const Slice& data) {
    const char* src = data.data();
    size_t src_len = data.size();

    return AppendSub(src, src_len);
  }

  Status AppendSub(const char* buf, size_t len) {
    MutexLock lock(&blocks_mutex_);
    uint64_t t = ve_get();
    if (vefs_->Append(inode_, buf, len) != Vefs::Status::kOk) {
      return Status::IOError("Error in VeFS");
    }
    taken_time += ve_get() - t;
    size_ += len;

    return Status::OK();
  }

  void Sync() { vefs_->Sync(inode_); }

 private:
  // Private since only Unref() should be used to delete it.
  ~FileState() { Truncate(); }

  port::Mutex refs_mutex_;
  int refs_ GUARDED_BY(refs_mutex_);

  mutable port::Mutex blocks_mutex_;
  uint64_t size_ GUARDED_BY(blocks_mutex_);
  Vefs* vefs_;
  Inode* inode_;
};

class SequentialFileImpl : public SequentialFile {
 public:
  explicit SequentialFileImpl(FileState* file) : file_(file), pos_(0) {
    file_->Ref();
  }

  ~SequentialFileImpl() override { file_->Unref(); }

  Status Read(size_t n, Slice* result, char* scratch) override {
    Status s = file_->Read(pos_, n, result, scratch);
    if (s.ok()) {
      pos_ += result->size();
    }
    return s;
  }

  Status Skip(uint64_t n) override {
    if (pos_ > file_->Size()) {
      return Status::IOError("pos_ > file_->Size()");
    }
    const uint64_t available = file_->Size() - pos_;
    if (n > available) {
      n = available;
    }
    pos_ += n;
    return Status::OK();
  }

 private:
  FileState* file_;
  uint64_t pos_;
};

class RandomAccessFileImpl : public RandomAccessFile {
 public:
  explicit RandomAccessFileImpl(FileState* file) : file_(file) { file_->Ref(); }

  ~RandomAccessFileImpl() override { file_->Unref(); }

  Status Read(uint64_t offset, size_t n, Slice* result,
              char* scratch) const override {
    return file_->Read(offset, n, result, scratch);
  }

 private:
  FileState* file_;
};

class WritableFileImpl : public WritableFile {
 public:
  WritableFileImpl(FileState* file) : file_(file), pos_(0) { file_->Ref(); }

  ~WritableFileImpl() override { file_->Unref(); }

  Status Append(const Slice& data) override {
    size_t write_size = data.size();
    const char* write_data = data.data();

    // Fit as much as possible into buffer.
    size_t copy_size = std::min(write_size, kWritableFileBufferSize - pos_);
    std::memcpy(buf_ + pos_, write_data, copy_size);
    write_data += copy_size;
    write_size -= copy_size;
    pos_ += copy_size;
    if (write_size == 0) {
      return Status::OK();
    }

    // Can't fit in buffer, so need to do at least one write.
    Status status = FlushBuffer();
    if (!status.ok()) {
      return status;
    }

    // Small writes go to buffer, large writes are written directly.
    if (write_size < kWritableFileBufferSize) {
      std::memcpy(buf_, write_data, write_size);
      pos_ = write_size;
      return Status::OK();
    }
    return WriteUnbuffered(write_data, write_size);
  }

  Status Close() override { return FlushBuffer(); }
  Status Flush() override { return FlushBuffer(); }
  Status Sync() override {
    file_->Sync();
    return Status::OK();
  }

 private:
  Status FlushBuffer() {
    Status status = WriteUnbuffered(buf_, pos_);
    pos_ = 0;
    return status;
  }
  Status WriteUnbuffered(const char* data, size_t size) {
    return file_->AppendSub(data, size);
  }
  FileState* file_;
  char buf_[kWritableFileBufferSize];
  size_t pos_;
};

class NoOpLogger : public Logger {
 public:
  void Logv(const char* format, va_list ap) override {}
};

class VeEnv : public EnvWrapper {
 public:
  explicit VeEnv(Env* base_env) : EnvWrapper(base_env) {}

  ~VeEnv() override {
    for (const auto& kvp : file_map_) {
      kvp.second->Unref();
    }
  }

  // Partial implementation of the Env interface.
  Status NewSequentialFile(const std::string& fname,
                           SequentialFile** result) override {
    MutexLock lock(&mutex_);
    if (file_map_.find(fname) == file_map_.end()) {
      *result = nullptr;
      return Status::IOError(fname, "File not found");
    }

    *result = new SequentialFileImpl(file_map_[fname]);
    return Status::OK();
  }

  Status NewRandomAccessFile(const std::string& fname,
                             RandomAccessFile** result) override {
    MutexLock lock(&mutex_);
    if (file_map_.find(fname) == file_map_.end()) {
      *result = nullptr;
      return Status::IOError(fname, "File not found");
    }

    *result = new RandomAccessFileImpl(file_map_[fname]);
    return Status::OK();
  }

  Status NewWritableFile(const std::string& fname,
                         WritableFile** result) override {
    MutexLock lock(&mutex_);
    FileSystem::iterator it = file_map_.find(fname);

    FileState* file;
    if (it == file_map_.end()) {
      // File is not currently open.
      file = new FileState(fname);
      file->Ref();
      file_map_[fname] = file;
    } else {
      file = it->second;
      file->Truncate();
    }

    *result = new WritableFileImpl(file);
    return Status::OK();
  }

  Status NewAppendableFile(const std::string& fname,
                           WritableFile** result) override {
    MutexLock lock(&mutex_);
    FileState** sptr = &file_map_[fname];
    FileState* file = *sptr;
    if (file == nullptr) {
      file = new FileState(fname);
      file->Ref();
    }
    *result = new WritableFileImpl(file);
    return Status::OK();
  }

  bool FileExists(const std::string& fname) override {
    MutexLock lock(&mutex_);
    return file_map_.find(fname) != file_map_.end();
  }

  Status GetChildren(const std::string& dir,
                     std::vector<std::string>* result) override {
    MutexLock lock(&mutex_);
    result->clear();

    for (const auto& kvp : file_map_) {
      const std::string& filename = kvp.first;

      if (filename.size() >= dir.size() + 1 && filename[dir.size()] == '/' &&
          Slice(filename).starts_with(Slice(dir))) {
        result->push_back(filename.substr(dir.size() + 1));
      }
    }

    return Status::OK();
  }

  void DeleteFileInternal(const std::string& fname)
      EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
    if (file_map_.find(fname) == file_map_.end()) {
      return;
    }

    file_map_[fname]->Unref();
    file_map_.erase(fname);
  }

  Status DeleteFile(const std::string& fname) override {
    MutexLock lock(&mutex_);
    if (file_map_.find(fname) == file_map_.end()) {
      return Status::IOError(fname, "File not found");
    }

    DeleteFileInternal(fname);
    return Status::OK();
  }

  Status CreateDir(const std::string& dirname) override { return Status::OK(); }

  Status DeleteDir(const std::string& dirname) override { return Status::OK(); }

  Status GetFileSize(const std::string& fname, uint64_t* file_size) override {
    MutexLock lock(&mutex_);
    if (file_map_.find(fname) == file_map_.end()) {
      return Status::IOError(fname, "File not found");
    }

    *file_size = file_map_[fname]->Size();
    return Status::OK();
  }

  Status RenameFile(const std::string& src,
                    const std::string& target) override {
    MutexLock lock(&mutex_);
    if (file_map_.find(src) == file_map_.end()) {
      return Status::IOError(src, "File not found");
    }

    DeleteFileInternal(target);
    file_map_[target] = file_map_[src];
    file_map_.erase(src);
    return Status::OK();
  }

  Status LockFile(const std::string& fname, FileLock** lock) override {
    *lock = new FileLock;
    return Status::OK();
  }

  Status UnlockFile(FileLock* lock) override {
    delete lock;
    return Status::OK();
  }

  Status GetTestDirectory(std::string* path) override {
    *path = "/test";
    return Status::OK();
  }

  Status NewLogger(const std::string& fname, Logger** result) override {
    *result = new NoOpLogger;
    return Status::OK();
  }

 private:
  // Map from filenames to FileState objects, representing a simple file system.
  typedef std::map<std::string, FileState*> FileSystem;

  port::Mutex mutex_;
  FileSystem file_map_ GUARDED_BY(mutex_);
};

}  // namespace

Env* NewVeEnv(Env* base_env) { return new VeEnv(base_env); }

}  // namespace leveldb
