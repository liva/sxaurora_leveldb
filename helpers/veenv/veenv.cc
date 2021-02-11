/**
 * Copyright 2020 NEC Laboratories Europe GmbH
 * All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *
 *    3. Neither the name of NEC Laboratories Europe GmbH nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY NEC Laboratories Europe GmbH AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NEC Laboratories
 * Europe GmbH OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO,  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <string.h>

#include <atomic>
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

namespace leveldb {

namespace {

constexpr const size_t kWritableFileBufferSize = 65536;

class FileState {
 public:
  FileState() = delete;
  FileState(const std::string& fn) : vefs_(Vefs::Get()), refs_(0) {
    inode_ = vefs_->Create(fn, false);
  }
  ~FileState() {}

  // No copying allowed.
  FileState(const FileState&) = delete;
  FileState& operator=(const FileState&) = delete;

  void Ref() { assert(refs_.fetch_add(1) >= 0); }
  void Unref() {
    int old_refs = refs_.fetch_sub(1);
    assert(old_refs > 0);
    if (old_refs == 1) {
      delete this;
    }
  }

  uint64_t Size() const {
    if (inode_ == nullptr) {
      return 0;
    }
    //    MutexLock lock(&blocks_mutex_);
    return vefs_->GetLen(inode_);
  }

  void Truncate() {
    if (inode_ == nullptr) {
      return;
    }
    //    MutexLock lock(&blocks_mutex_);
    vefs_->Truncate(inode_, 0);
  }

  Status Read(uint64_t offset, size_t n, Slice* result, char* scratch) const {
    if (inode_ == nullptr) {
      return Status::OK();
    }
    //    MutexLock lock(&blocks_mutex_);
    if (offset > vefs_->GetLen(inode_)) {
      printf("veenv: error\n");
      fflush(stdout);
      return Status::IOError("Offset greater than file size.");
    }
    const uint64_t available = vefs_->GetLen(inode_) - offset;
    if (n > available) {
      n = static_cast<size_t>(available);
    }
    if (n == 0) {
      *result = Slice();
      return Status::OK();
    }

    if (vefs_->Read(inode_, offset, n, scratch) != Vefs::Status::kOk) {
      printf("veenv: error\n");
      fflush(stdout);
      return Status::IOError("Error in VeFS");
    }

    *result = Slice(scratch, n);
    return Status::OK();
  }

  Status Append(const Slice& data) {
    const char* src = data.data();
    size_t src_len = data.size();

    return Append(src, src_len);
  }

  Status Append(const char* buf, size_t len) {
    if (inode_ == nullptr) {
      return Status::OK();
    }
    //    MutexLock lock(&blocks_mutex_);
    if (vefs_->Append(inode_, buf, len) != Vefs::Status::kOk) {
      printf("veenv: error\n");
      fflush(stdout);
      return Status::IOError("Error in VeFS");
    }
    return Status::OK();
  }

  void Sync() {
    if (inode_ == nullptr) {
      return;
    }
    vefs_->Sync(inode_);
  }

  void Delete() {
    if (inode_ == nullptr) {
      return;
    }
    vefs_->Delete(inode_);
    inode_ = nullptr;
  }

  void Rename(const std::string& fname) {
    if (inode_ == nullptr) {
      return;
    }
    vefs_->Rename(inode_, fname);
  }

 private:
  //  mutable port::Mutex blocks_mutex_;
  Vefs* vefs_;
  Inode* inode_;
  std::atomic<int> refs_;
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
    // TODO should be aligned at 8 if the size is bigger than 32K
    if (write_size > 256 && (pos_) % 4 != 0) {
      printf("WARNING: not aligned. pos: %lu wsize: %lu\n", pos_, write_size);
    }
    //return file_->Append(write_data, write_size);

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
    Status status = FlushBuffer();
    if (!status.ok()) {
      return status;
    }
    file_->Sync();
    return Status::OK();
  }

 private:
  Status FlushBuffer() {
    //return Status::OK();
    Status status = WriteUnbuffered(buf_, pos_);
    pos_ = 0;
    return status;
  }

  Status WriteUnbuffered(const char* data, size_t size) {
    if (size != 0) {
      return file_->Append(data, size);
    } else {
      return Status::OK();
    }
  }
  FileState* file_;
  char buf_[kWritableFileBufferSize];
  size_t pos_;
};

class NoOpLogger : public Logger {
 public:
  void Logv(const char* format, va_list ap) override {}
};
/*
class VeLogger final : public Logger {
 public:
  explicit VeLogger(std::string fname) { }

  ~PosixLogger() override { }

  void Logv(const char* format, va_list arguments) override {
    // Record the time as close to the Logv() call as possible.
    struct ::timeval now_timeval;
    ::gettimeofday(&now_timeval, nullptr);
    const std::time_t now_seconds = now_timeval.tv_sec;
    struct std::tm now_components;
    ::localtime_r(&now_seconds, &now_components);

    // Record the thread ID.
    constexpr const int kMaxThreadIdSize = 32;
    std::ostringstream thread_stream;
    thread_stream << std::this_thread::get_id();
    std::string thread_id = thread_stream.str();
    if (thread_id.size() > kMaxThreadIdSize) {
      thread_id.resize(kMaxThreadIdSize);
    }

    // We first attempt to print into a stack-allocated buffer. If this
attempt
    // fails, we make a second attempt with a dynamically allocated buffer.
    constexpr const int kStackBufferSize = 512;
    char stack_buffer[kStackBufferSize];
    static_assert(sizeof(stack_buffer) ==
static_cast<size_t>(kStackBufferSize), "sizeof(char) is expected to be 1 in
C++");

    int dynamic_buffer_size = 0;  // Computed in the first iteration.
    for (int iteration = 0; iteration < 2; ++iteration) {
      const int buffer_size =
          (iteration == 0) ? kStackBufferSize : dynamic_buffer_size;
      char* const buffer =
          (iteration == 0) ? stack_buffer : new char[dynamic_buffer_size];

      // Print the header into the buffer.
      int buffer_offset = snprintf(
          buffer, buffer_size, "%04d/%02d/%02d-%02d:%02d:%02d.%06d %s ",
          now_components.tm_year + 1900, now_components.tm_mon + 1,
          now_components.tm_mday, now_components.tm_hour,
now_components.tm_min, now_components.tm_sec,
static_cast<int>(now_timeval.tv_usec), thread_id.c_str());

      // The header can be at most 28 characters (10 date + 15 time +
      // 3 delimiters) plus the thread ID, which should fit comfortably into
the
      // static buffer.
      assert(buffer_offset <= 28 + kMaxThreadIdSize);
      static_assert(28 + kMaxThreadIdSize < kStackBufferSize,
                    "stack-allocated buffer may not fit the message header");
      assert(buffer_offset < buffer_size);

      // Print the message into the buffer.
      std::va_list arguments_copy;
      va_copy(arguments_copy, arguments);
      buffer_offset +=
          std::vsnprintf(buffer + buffer_offset, buffer_size - buffer_offset,
                         format, arguments_copy);
      va_end(arguments_copy);

      // The code below may append a newline at the end of the buffer, which
      // requires an extra character.
      if (buffer_offset >= buffer_size - 1) {
        // The message did not fit into the buffer.
        if (iteration == 0) {
          // Re-run the loop and use a dynamically-allocated buffer. The
buffer
          // will be large enough for the log message, an extra newline and a
          // null terminator.
          dynamic_buffer_size = buffer_offset + 2;
          continue;
        }

        // The dynamically-allocated buffer was incorrectly sized. This should
        // not happen, assuming a correct implementation of (v)snprintf. Fail
        // in tests, recover by truncating the log message in production.
        assert(false);
        buffer_offset = buffer_size - 1;
      }

      // Add a newline if necessary.
      if (buffer[buffer_offset - 1] != '\n') {
        buffer[buffer_offset] = '\n';
        ++buffer_offset;
      }

      assert(buffer_offset <= buffer_size);
      std::fwrite(buffer, 1, buffer_offset, fp_);
      std::fflush(fp_);

      if (iteration != 0) {
        delete[] buffer;
      }
      break;
    }
  }

 private:
};
*/
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
    FileState* file = GetFileStateIfExist(fname);
    if (file == nullptr) {
      *result = nullptr;
      return Status::IOError(fname, "File not found");
    }

    *result = new SequentialFileImpl(file);
    return Status::OK();
  }

  Status NewRandomAccessFile(const std::string& fname,
                             RandomAccessFile** result) override {
    FileState* file = GetFileStateIfExist(fname);
    if (file == nullptr) {
      *result = nullptr;
      return Status::IOError(fname, "File not found");
    }

    *result = new RandomAccessFileImpl(file);
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
    }
    file->Truncate();

    *result = new WritableFileImpl(file);
    return Status::OK();
  }

  Status NewAppendableFile(const std::string& fname,
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
    }

    *result = new WritableFileImpl(file);
    return Status::OK();
  }

  bool FileExists(const std::string& fname) override {
    return GetFileStateIfExist(fname) != nullptr;
  }

  Status GetChildren(const std::string& dir,
                     std::vector<std::string>* result) override {
    if (Vefs::Get()->GetChildren(dir, result) == Vefs::Status::kOk) {
      return Status::OK();
    } else {
      return Status::IOError("error in Vefs");
    }
  }

  void DeleteFileInternal(const std::string& fname) {
    MutexLock lock(&mutex_);
    FileSystem::iterator it = file_map_.find(fname);

    if (it == file_map_.end()) {
      return;
    }

    FileState* file = it->second;
    file->Delete();

    file_map_.erase(fname);
    file->Unref();
  }

  Status DeleteFile(const std::string& fname) override {
    FileState* file = GetFileStateIfExist(fname);
    if (file == nullptr) {
      return Status::IOError(fname, "File not found");
    }

    DeleteFileInternal(fname);
    return Status::OK();
  }

  Status CreateDir(const std::string& dirname) override { return Status::OK(); }

  Status DeleteDir(const std::string& dirname) override { return Status::OK(); }

  Status GetFileSize(const std::string& fname, uint64_t* file_size) override {
    FileState* file = GetFileStateIfExist(fname);
    if (file == nullptr) {
      return Status::IOError(fname, "File not found");
    }

    *file_size = file->Size();
    return Status::OK();
  }

  Status RenameFile(const std::string& src,
                    const std::string& target) override {
    FileState* sfile = GetFileStateIfExist(src);
    if (sfile == nullptr) {
      return Status::IOError(src, "File not found");
    }
    FileState* tfile = GetFileStateIfExist(target);
    if (tfile) {
      DeleteFileInternal(target);
    }

    MutexLock lock(&mutex_);

    file_map_[target] = sfile;
    file_map_.erase(src);
    sfile->Rename(target);
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
  FileState* GetFileStateIfExist(const std::string& fname) {
    MutexLock lock(&mutex_);
    FileSystem::iterator it = file_map_.find(fname);

    if (it == file_map_.end()) {
      if (Vefs::Get()->DoesExist(fname)) {
        // File is not currently open.
        FileState* file = new FileState(fname);
        file->Ref();
        file_map_[fname] = file;
        return file;
      } else {
        return nullptr;
      }
    } else {
      return it->second;
    }
  }

  // Map from filenames to FileState objects, representing a simple file
  // system.
  typedef std::map<std::string, FileState*> FileSystem;

  port::Mutex mutex_;
  FileSystem file_map_ GUARDED_BY(mutex_);
};

}  // namespace

Env* NewVeEnv(Env* base_env) { return new VeEnv(base_env); }

}  // namespace leveldb
