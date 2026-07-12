// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_INCLUDE_KTL_OPTIONAL_H_
#define ZIRCON_KERNEL_INCLUDE_KTL_OPTIONAL_H_

#include <zxcpp/new.h>

#include <fbl/type_support.h>

namespace ktl {

// Tag type used to construct an empty optional; mirrors std::nullopt_t.
struct nullopt_t {
    explicit constexpr nullopt_t(int) {}
};
constexpr nullopt_t nullopt{0};

// Minimal stand-in for std::optional. The 2018 kernel is built with
// -std=c++14, where std::optional (C++17) is unavailable, so upstream's
// `using std::optional` ktl alias cannot be used. Only the subset the kernel
// consumes is implemented: nullopt/emplace construction, has_value, reset, and
// dereference. TODO(fxbug.dev/31000): replace with the std::optional alias once
// the kernel is built as C++17.
template <typename T>
class optional {
public:
    optional() = default;
    optional(nullopt_t) {}

    // Construct from a value, mirroring std::optional's converting constructors.
    optional(const T& value) { emplace(value); }
    optional(T&& value) { emplace(fbl::move(value)); }

    optional(const optional& other) {
        if (other.has_value_) {
            emplace(*other);
        }
    }
    optional(optional&& other) {
        if (other.has_value_) {
            emplace(fbl::move(*other));
        }
    }
    ~optional() { reset(); }

    optional& operator=(nullopt_t) {
        reset();
        return *this;
    }
    optional& operator=(const optional& other) {
        if (this != &other) {
            reset();
            if (other.has_value_) {
                emplace(*other);
            }
        }
        return *this;
    }
    optional& operator=(optional&& other) {
        if (this != &other) {
            reset();
            if (other.has_value_) {
                emplace(fbl::move(*other));
            }
        }
        return *this;
    }

    template <typename... Args>
    T& emplace(Args&&... args) {
        reset();
        new (&storage_.value) T(fbl::forward<Args>(args)...);
        has_value_ = true;
        return storage_.value;
    }

    void reset() {
        if (has_value_) {
            storage_.value.~T();
            has_value_ = false;
        }
    }

    bool has_value() const { return has_value_; }
    explicit operator bool() const { return has_value_; }

    // Comparison against nullopt, mirroring std::optional. Only the nullopt
    // comparisons used by ported code are provided.
    bool operator==(nullopt_t) const { return !has_value_; }
    bool operator!=(nullopt_t) const { return has_value_; }

    T& operator*() { return storage_.value; }
    const T& operator*() const { return storage_.value; }
    T* operator->() { return &storage_.value; }
    const T* operator->() const { return &storage_.value; }

private:
    // Storing the payload as a real union member (rather than a reinterpret_cast
    // over a byte buffer) keeps every access typed as T, so no -Wstrict-aliasing
    // suppression is needed. The union's lifetime is managed manually below.
    union Storage {
        Storage() {}
        ~Storage() {}
        char dummy;
        T value;
    } storage_;
    bool has_value_ = false;
};

} // namespace ktl

#endif // ZIRCON_KERNEL_INCLUDE_KTL_OPTIONAL_H_
