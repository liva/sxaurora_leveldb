// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/log_writer.h"

#include <stdint.h>

#include "leveldb/env.h"
#include "util/coding.h"
#include "util/crc32c.h"

#include "rtc.h"
#include "leveldb_autogen_conf.h"

namespace leveldb {
namespace log {

static void InitTypeCrc(uint32_t* type_crc) {
  for (int i = 0; i <= kMaxRecordType; i++) {
    char t = static_cast<char>(i);
    type_crc[i] = crc32c::Value(&t, 1);
  }
}

Writer::Writer(WritableFile* dest) : dest_(dest), block_offset_(0) {
  InitTypeCrc(type_crc_);
}

Writer::Writer(WritableFile* dest, uint64_t dest_length)
    : dest_(dest), block_offset_(dest_length % kBlockSize) {
  InitTypeCrc(type_crc_);
}

Writer::~Writer() = default;

  static size_t align4(size_t len) {
    return (len / 4) * 4;
  }

  static size_t alignup4(size_t len) {
    return ((len + 3) / 4) * 4;
  }

Status Writer::AddRecord(const Slice& slice) {
  const char* ptr = slice.data();
  size_t left = slice.size();

  // Fragment the record if necessary and emit it.  Note that if slice
  // is empty, we still want to iterate once to emit a single
  // zero-length record
  Status s;
  bool begin = true;
  do {
    const int leftover = kBlockSize - block_offset_;
    assert(leftover >= 0);
    if (leftover < kHeaderSize) {
      // Switch to a new block
      if (leftover > 0) {
#ifndef VE_OPT
        // Fill the trailer (literal below relies on kHeaderSize being 7)
        static_assert(kHeaderSize == 7, "");
        dest_->Append(Slice("\x00\x00\x00\x00\x00\x00", leftover));
#else
        // Fill the trailer (literal below relies on kHeaderSize being 8)
        static_assert(kHeaderSize == 8, "");
        dest_->Append(Slice("\x00\x00\x00\x00\x00\x00\x00", leftover));
#endif
      }
      block_offset_ = 0;
    }

    // Invariant: we never leave < kHeaderSize bytes in a block.
    assert(kBlockSize - block_offset_ - kHeaderSize >= 0);

#ifndef VE_OPT
    const size_t avail = kBlockSize - block_offset_ - kHeaderSize;
#else
    const size_t avail = align4(kBlockSize - block_offset_ - kHeaderSize);
#endif
    const size_t fragment_length = (left < avail) ? left : avail;

    RecordType type;
    const bool end = (left == fragment_length);
    if (begin && end) {
      type = kFullType;
    } else if (begin) {
      type = kFirstType;
    } else if (end) {
      type = kLastType;
    } else {
      type = kMiddleType;
    }

    s = EmitPhysicalRecord(type, ptr, fragment_length);
    ptr += fragment_length;
    left -= fragment_length;
    begin = false;
  } while (s.ok() && left > 0);
  return s;
}

Status Writer::EmitPhysicalRecord(RecordType t, const char* ptr,
                                  size_t length) {
  assert(length <= 0xffff);  // Must fit in two bytes
#ifndef VE_OPT
  assert(block_offset_ + kHeaderSize + length <= kBlockSize);
#else
  assert(block_offset_ + kHeaderSize + alignup4(length) <= kBlockSize);
  const size_t padding = alignup4(length) - length;
#endif

  // Format the header
  char buf[kHeaderSize];
  buf[4] = static_cast<char>(length & 0xff);
  buf[5] = static_cast<char>(length >> 8);
#ifndef VE_OPT
  buf[6] = static_cast<char>(t);
#else
  buf[6] = padding;
  buf[7] = static_cast<char>(t);
#endif

  // Compute the crc of the record type and the payload.
  uint32_t crc = crc32c::Extend(type_crc_[t], ptr, length);
  crc = crc32c::Mask(crc);  // Adjust for storage
  EncodeFixed32(buf, crc);

  // Write the header and the payload
  Status s = dest_->Append(Slice(buf, kHeaderSize));
  if (s.ok()) {
    s = dest_->Append(Slice(ptr, length));
    if (s.ok()) {
#ifndef VE_OPT
#else
      if (padding > 0) {
        char tmp[padding];
        dest_->Append(Slice(tmp, padding));
      }
#endif
      s = dest_->Flush();
    }
  }
#ifndef VE_OPT
  block_offset_ += kHeaderSize + length;
#else
  block_offset_ += kHeaderSize + length + padding;
#endif
  return s;
}

}  // namespace log
}  // namespace leveldb
