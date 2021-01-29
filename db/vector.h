#ifndef STORAGE_LEVELDB_DB_VECTOR_H_
#define STORAGE_LEVELDB_DB_VECTOR_H_

#include <vector>
#include <memory>
#include <algorithm>
#include <atomic>

class Spinlock
{
public:
    Spinlock(std::atomic<int> &lock) : lock_(lock)
    {
        while (lock_.fetch_or(1) == 1)
        {
            asm volatile("" ::
                             : "memory");
        }
    }
    static bool IsAcquired(std::atomic<int> &lock)
    {
        return lock == 1;
    }
    ~Spinlock()
    {
        lock_ = 0;
    }

private:
    std::atomic<int> &lock_;
};

namespace leveldb {

class Arena;

template <typename Key, class Comparator>
class SkipList {
private:
  using Bucket = typename std::vector<Key>;
  using Iter = typename Bucket::const_iterator;
public:
  explicit SkipList(Comparator cmp, Arena* arena) : bucket_(new Bucket), compare_(cmp), lock_(0) {
  }

  SkipList(const SkipList&) = delete;
  SkipList& operator=(const SkipList&) = delete;
  ~SkipList() {
    delete bucket_;
  }
  void Insert(const Key& key) {
    Spinlock l(lock_);
    bucket_->push_back(key);
  }
  bool Contains(const Key& key) const {
    Spinlock l(lock_);
    return std::find(bucket_->begin(), bucket_->end(), key) != bucket_->end();
  }

  class Iterator {
  private:
  public:
    explicit Iterator(const SkipList* list) : list_(list), bucket_(new Bucket(*(list_->bucket_))), compare_(list_->compare_), cit_(bucket_->end()), sorted_(false) {
    }
    ~Iterator() {
      delete bucket_;
    }
    bool Valid() const {
      return cit_ != bucket_->end();
    }
    const Key& key() const {
      return *cit_;
    }
    void Next() {
      DoSortIfNeeded();
      if (cit_ == bucket_->end()) {
        return;
      }
      ++cit_;
    }
    void Prev() {
      DoSortIfNeeded();
      if (cit_ == bucket_->begin()) {
        cit_ = bucket_->end();
      } else {
        --cit_;
      }
    }
    void Seek(const Key& target) {
      DoSortIfNeeded();
      cit_ = std::equal_range(bucket_->begin(),
                              bucket_->end(),
                              target,
                              [this] (const Key &a, const Key &b) {
                                return compare_(a, b) < 0;
                              }).first;
    }
    void SeekToFirst() {
      DoSortIfNeeded();
      cit_ = bucket_->begin();
    }
    void SeekToLast() {
      DoSortIfNeeded();
      cit_ = bucket_->end();
      if (bucket_->size() != 0) {
        --cit_;
      }
    }

  private:
    void DoSortIfNeeded() {
      if (sorted_) {
        return;
      }
      std::sort(bucket_->begin(), bucket_->end(),
                [this] (const Key &a, const Key &b) {
                  return compare_(a, b) < 0;
                });
      cit_ = bucket_->begin();
      sorted_ = true;
    }
    
    const SkipList* const list_;
    Bucket * const bucket_;
    Comparator const compare_;
    Iter mutable cit_;
    bool sorted_;
  };

private:
  friend class Iterator;
  Bucket * const bucket_;
  Comparator const compare_;
  std::atomic<int> mutable lock_;
};

}  // namespace leveldb
  
#endif // STORAGE_LEVELDB_DB_VECTOR_H_
