/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ART_SRC_INVOKE_ARG_ARRAY_BUILDER_H_
#define ART_SRC_INVOKE_ARG_ARRAY_BUILDER_H_

#include "mirror/object.h"
#include "scoped_thread_state_change.h"

namespace art {

static inline size_t NumArgArrayBytes(const char* shorty, uint32_t shorty_len) {
  size_t num_bytes = 0;
  for (size_t i = 1; i < shorty_len; ++i) {
    char ch = shorty[i];
    if (ch == 'D' || ch == 'J') {
      num_bytes += 8;
    } else if (ch == 'L') {
      // Argument is a reference or an array.  The shorty descriptor
      // does not distinguish between these types.
      num_bytes += sizeof(mirror::Object*);
    } else {
      num_bytes += 4;
    }
  }
  return num_bytes;
}

class ArgArray {
 public:
  explicit ArgArray(const char* shorty, uint32_t shorty_len)
      : shorty_(shorty), shorty_len_(shorty_len), num_bytes_(0) {
    // TODO: This code is conservative. The multiply by 2 is to handle the case where all args are
    // doubles or longs. We could scan the shorty to use the arg array more often.
    if (shorty_len * 2 <= kSmallArgArraySize) {
      arg_array_ = small_arg_array_;
    } else {
      large_arg_array_.reset(new uint32_t[shorty_len_ * 2]);
      arg_array_ = large_arg_array_.get();
    }
  }

  uint32_t* GetArray() {
    return arg_array_;
  }

  uint32_t GetNumBytes() {
    return num_bytes_;
  }

  void Append(uint32_t value) {
    arg_array_[num_bytes_ / 4] = value;
    num_bytes_ += 4;
  }

  void AppendWide(uint64_t value) {
    arg_array_[num_bytes_ / 4] = value;
    arg_array_[(num_bytes_ / 4) + 1] = value >> 32;
    num_bytes_ += 8;
  }

  void BuildArgArray(const ScopedObjectAccess& soa, mirror::Object* receiver, va_list ap)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    // Set receiver if non-null (method is not static)
    if (receiver != NULL) {
      Append(reinterpret_cast<int32_t>(receiver));
    }
    for (size_t i = 1; i < shorty_len_; ++i) {
      switch (shorty_[i]) {
        case 'Z':
        case 'B':
        case 'C':
        case 'S':
        case 'I':
          Append(va_arg(ap, jint));
          break;
        case 'F': {
          JValue value;
          value.SetF(va_arg(ap, jdouble));
          Append(value.GetI());
          break;
        }
        case 'L':
          Append(reinterpret_cast<int32_t>(soa.Decode<mirror::Object*>(va_arg(ap, jobject))));
          break;
        case 'D': {
          JValue value;
          value.SetD(va_arg(ap, jdouble));
          AppendWide(value.GetJ());
          break;
        }
        case 'J': {
          AppendWide(va_arg(ap, jlong));
          break;
        }
      }
    }
  }

  void BuildArgArray(const ScopedObjectAccess& soa, mirror::Object* receiver, jvalue* args)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    // Set receiver if non-null (method is not static)
    if (receiver != NULL) {
      Append(reinterpret_cast<int32_t>(receiver));
    }
    for (size_t i = 1, args_offset = 0; i < shorty_len_; ++i, ++args_offset) {
      switch (shorty_[i]) {
        case 'Z':
          Append(args[args_offset].z);
          break;
        case 'B':
          Append(args[args_offset].b);
          break;
        case 'C':
          Append(args[args_offset].c);
          break;
        case 'S':
          Append(args[args_offset].s);
          break;
        case 'I':
        case 'F':
          Append(args[args_offset].i);
          break;
        case 'L':
          Append(reinterpret_cast<int32_t>(soa.Decode<mirror::Object*>(args[args_offset].l)));
          break;
        case 'D':
        case 'J':
          AppendWide(args[args_offset].j);
          break;
      }
    }
  }

  void BuildArgArray(const ShadowFrame& shadow_frame, mirror::Object* receiver, uint32_t range_start)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    // Set receiver if non-null (method is not static)
    if (receiver != NULL) {
      Append(reinterpret_cast<int32_t>(receiver));
    }
    for (size_t i = 1, reg_offset = 0; i < shorty_len_; ++i, ++reg_offset) {
      switch (shorty_[i]) {
        case 'Z':
        case 'B':
        case 'C':
        case 'S':
        case 'I':
        case 'F':
          Append(shadow_frame.GetVReg(range_start + reg_offset));
          break;
        case 'L':
          Append(reinterpret_cast<int32_t>(shadow_frame.GetVRegReference(range_start + reg_offset)));
          break;
        case 'D':
        case 'J':
          AppendWide(shadow_frame.GetVRegLong(range_start + reg_offset));
          reg_offset++;
          break;
      }
    }
  }

  void BuildArgArray(const ShadowFrame& shadow_frame, mirror::Object* receiver, const uint32_t* arg_regs)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    // Set receiver if non-null (method is not static)
    if (receiver != NULL) {
      Append(reinterpret_cast<int32_t>(receiver));
    }
    for (size_t i = 1, reg_offset = 0; i < shorty_len_; ++i, ++reg_offset) {
      switch (shorty_[i]) {
        case 'Z':
        case 'B':
        case 'C':
        case 'S':
        case 'I':
        case 'F':
          Append(shadow_frame.GetVReg(arg_regs[reg_offset]));
          break;
        case 'L':
          Append(reinterpret_cast<int32_t>(shadow_frame.GetVRegReference(arg_regs[reg_offset])));
          break;
        case 'D':
        case 'J':
          AppendWide(shadow_frame.GetVRegLong(arg_regs[reg_offset]));
          reg_offset++;
          break;
      }
    }
  }

 private:
  enum { kSmallArgArraySize = 16 };
  const char* const shorty_;
  const uint32_t shorty_len_;
  uint32_t num_bytes_;
  uint32_t* arg_array_;
  uint32_t small_arg_array_[kSmallArgArraySize];
  UniquePtr<uint32_t[]> large_arg_array_;
};

}  // namespace art

#endif  // ART_SRC_INVOKE_ARG_ARRAY_BUILDER_H_
