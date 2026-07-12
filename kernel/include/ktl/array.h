// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_INCLUDE_KTL_ARRAY_H_
#define ZIRCON_KERNEL_INCLUDE_KTL_ARRAY_H_

#include <stddef.h>

// The 2018 kernel predates the ktl library. Upstream's ktl::array is a thin
// alias for std::array (C++17 / unavailable in this C++14 kernel), so provide a
// minimal std::array-compatible aggregate covering the subset used by ported
// code: aggregate/zero initialization, indexing, size(), begin()/end() and
// element-wise equality. Unlike a plain C array, this can be a class member with
// a default member initializer and can be compared with ==, both of which the
// page queues rely on.
namespace ktl {

template <typename T, size_t N>
struct array {
    // Public data member so the type remains an aggregate, matching std::array.
    // Aggregate initialization such as `= {0}` and `= {}` therefore works.
    T data_[N];

    T& operator[](size_t i) { return data_[i]; }
    const T& operator[](size_t i) const { return data_[i]; }

    T* data() { return data_; }
    const T* data() const { return data_; }

    T* begin() { return data_; }
    const T* begin() const { return data_; }
    T* end() { return data_ + N; }
    const T* end() const { return data_ + N; }

    constexpr size_t size() const { return N; }

    bool operator==(const array& other) const {
        for (size_t i = 0; i < N; i++) {
            if (!(data_[i] == other.data_[i])) {
                return false;
            }
        }
        return true;
    }
    bool operator!=(const array& other) const { return !(*this == other); }
};

}  // namespace ktl

#endif  // ZIRCON_KERNEL_INCLUDE_KTL_ARRAY_H_
