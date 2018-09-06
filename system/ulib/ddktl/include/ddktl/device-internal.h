// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

// base_device is a tag that default initalizes the zx_protocol_device_t so the mixin classes
// can fill in the table.
struct base_device {
  protected:
    base_device(zx_device_t* parent)
      : parent_(parent) {
        ddk_device_proto_.version = DEVICE_OPS_VERSION;
    }

    zx_protocol_device_t ddk_device_proto_ = {};
    zx_device_t* zxdev_ = nullptr;
    zx_device_t* const parent_;
};

// base_mixin is a tag that all mixins must inherit from.
struct base_mixin {};

// base_protocol is a tag used by protocol implementations
struct base_protocol {
    uint32_t ddk_proto_id_ = 0;
    void* ddk_proto_ops_ = nullptr;

  protected:
    base_protocol() = default;
};

// Deprecation helpers: transition a DDKTL protocol interface when there are implementations outside
// of zircon without breaking any builds.
//
// Example:
// template <typename D, bool NewFooMethod=false>
// class MyProtocol : public internal::base_protocol {
//   public:
//     MyProtocol() {
//         internal::CheckMyProtocol<D, NewMethod>();
//         ops_.foo = Foo;
//
//         // Can only inherit from one base_protocol implemenation
//         ZX_ASSERT(this->ddk_proto_ops_ == nullptr);
//         ddk_proto_id_ = ZX_PROTOCOL_MY_FOO;
//         ddk_proto_ops_ = &ops_;
//     }
//
//   private:
//     DDKTL_DEPRECATED(NewFooMethod)
//     static Foo(void* ctx, uint32_t options, foo_info_t* info) {
//         return static_cast<D*>(ctx)->MyFoo(options);
//     }
//
//     DDKTL_NOTREADY(NewFooMethod)
//     static Foo(void* ctx, uint32_t options, foo_info_t* info) {
//         return static_cast<D*>(ctx)->MyFoo(options, info);
//     }
// };
//
// // This class hasn't been updated yet, so it uses the default value for NewFooMethod and has the
// // old MyFoo method implementation.
// class MyProtocolImpl : public ddk::Device<MyProtocolImpl, /* other mixins */>,
//                        public ddk::MyProtocol<MyProtocolImpl> {
//   public:
//     zx_status_t MyFoo(uint32_t options);
// };
//
// // The implementation transitions as follows:
// class MyProtocolImpl : public ddk::Device<MyProtocolImpl, /* other mixins */>,
//                        public ddk::MyProtocol<MyProtocolImpl, true> {
//   public:
//     zx_status_t MyFoo(uint32_t options, foo_info_t* info);
// };
//
// Now the DDKTL_DEPRECATED method can be removed, along with the NewFooMethod template parameter.
// At no stage should the build be broken. These annotations may also be used to add new methods, by
// providing a no-op in the DDKTL_DEPRECATED static method.
#define DDKTL_DEPRECATED(condition) \
    template <typename T = D, bool En = condition, \
    typename fbl::enable_if<!fbl::integral_constant<bool, En>::value, int>::type = 0>
#define DDKTL_NOTREADY(condition) \
    template <typename T = D, bool En = condition, \
    typename fbl::enable_if<fbl::integral_constant<bool, En>::value, int>::type = 0>

// Mixin checks: ensure that a type meets the following qualifications:
//
// 1) has a method with the correct name (this makes the compiler errors a little more sane),
// 2) inherits from ddk::Device (by checking that it inherits from ddk::internal::base_device), and
// 3) has the correct method signature.
//
// Note that the 3rd requirement supersedes the first, but the static_assert doesn't even compile if
// the method can't be found, leading to a slightly more confusing error message. Adding the first
// check gives a chance to show the user a more intelligible error message.

DECLARE_HAS_MEMBER_FN(has_ddk_get_protocol, DdkGetProtocol);

template <typename D>
constexpr void CheckGetProtocolable() {
    static_assert(has_ddk_get_protocol<D>::value,
                  "GetProtocolable classes must implement DdkGetProtocol");
    static_assert(fbl::is_base_of<base_device, D>::value,
                  "GetProtocolable classes must be derived from ddk::Device<...>.");
    static_assert(fbl::is_same<decltype(&D::DdkGetProtocol),
                                zx_status_t (D::*)(uint32_t, void*)>::value,
                  "DdkGetProtocol must be a public non-static member function with signature "
                  "'zx_status_t DdkGetProtocol(uint32_t, void*)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_open, DdkOpen);

template <typename D>
constexpr void CheckOpenable() {
    static_assert(has_ddk_open<D>::value, "Openable classes must implement DdkOpen");
    static_assert(fbl::is_base_of<base_device, D>::value,
                  "Openable classes must be derived from ddk::Device<...>.");
    static_assert(fbl::is_same<decltype(&D::DdkOpen),
                                zx_status_t (D::*)(zx_device_t**, uint32_t)>::value,
                  "DdkOpen must be a public non-static member function with signature "
                  "'zx_status_t DdkOpen(zx_device_t**, uint32_t)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_open_at, DdkOpenAt);

template <typename D>
constexpr void CheckOpenAtable() {
    static_assert(has_ddk_open_at<D>::value,
                  "OpenAtable classes must implement DdkOpenAt");
    static_assert(fbl::is_base_of<base_device, D>::value,
                  "OpenAtable classes must be derived from ddk::Device<...>.");
    static_assert(
            fbl::is_same<decltype(&D::DdkOpenAt),
                          zx_status_t (D::*)(zx_device_t**, const char*, uint32_t)>::value,
                  "DdkOpenAt must be a public non-static member function with signature "
                  "'zx_status_t DdkOpenAt(zx_device_t**, const char*, uint32_t)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_close, DdkClose);

template <typename D>
constexpr void CheckClosable() {
    static_assert(has_ddk_close<D>::value,
                  "Closable classes must implement DdkClose");
    static_assert(fbl::is_base_of<base_device, D>::value,
                  "Closable classes must be derived from ddk::Device<...>.");
    static_assert(fbl::is_same<decltype(&D::DdkClose), zx_status_t (D::*)(uint32_t)>::value,
                  "DdkClose must be a public non-static member function with signature "
                  "'zx_status_t DdkClose(uint32)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_unbind, DdkUnbind);

template <typename D>
constexpr void CheckUnbindable() {
    static_assert(has_ddk_unbind<D>::value,
                  "Unbindable classes must implement DdkUnbind");
    static_assert(fbl::is_base_of<base_device, D>::value,
                  "Unbindable classes must be derived from ddk::Device<...>.");
    static_assert(fbl::is_same<decltype(&D::DdkUnbind), void (D::*)(void)>::value,
                  "DdkUnbind must be a public non-static member function with signature "
                  "'void DdkUnbind()'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_release, DdkRelease);

template <typename D>
constexpr void CheckReleasable() {
    static_assert(has_ddk_release<D>::value,
                  "Releasable classes must implement DdkRelease");
    // No need to check is_base_of because Releasable is a property of ddk::Device itself
    static_assert(fbl::is_same<decltype(&D::DdkRelease), void (D::*)(void)>::value,
                  "DdkRelease must be a public non-static member function with signature "
                  "'void DdkRelease()'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_read, DdkRead);

template <typename D>
constexpr void CheckReadable() {
    static_assert(has_ddk_read<D>::value, "Readable classes must implement DdkRead");
    static_assert(fbl::is_base_of<base_device, D>::value,
                  "Readable classes must be derived from ddk::Device<...>.");
    static_assert(fbl::is_same<decltype(&D::DdkRead),
                                zx_status_t (D::*)(void*, size_t, zx_off_t, size_t*)>::value,
                  "DdkRead must be a public non-static member function with signature "
                  "'zx_status_t DdkRead(void*, size_t, zx_off_t, size_t*)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_write, DdkWrite);

template <typename D>
constexpr void CheckWritable() {
    static_assert(has_ddk_write<D>::value,
                  "Writable classes must implement DdkWrite");
    static_assert(fbl::is_base_of<base_device, D>::value,
                  "Writable classes must be derived from ddk::Device<...>.");
    static_assert(fbl::is_same<decltype(&D::DdkWrite),
                                zx_status_t (D::*)(const void*, size_t, zx_off_t, size_t*)>::value,
                  "DdkWrite must be a public non-static member function with signature "
                  "'zx_status_t DdkWrite(const void*, size_t, zx_off_t, size_t*)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_get_size, DdkGetSize);

template <typename D>
constexpr void CheckGetSizable() {
    static_assert(has_ddk_get_size<D>::value,
                  "GetSizable classes must implement DdkGetSize");
    static_assert(fbl::is_base_of<base_device, D>::value,
                  "GetSizable classes must be derived from ddk::Device<...>.");
    static_assert(fbl::is_same<decltype(&D::DdkGetSize), zx_off_t (D::*)(void)>::value,
                  "DdkGetSize must be a public non-static member function with signature "
                  "'zx_off_t DdkGetSize()'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_ioctl, DdkIoctl);

template <typename D>
constexpr void CheckIoctlable() {
    static_assert(has_ddk_ioctl<D>::value,
                  "Ioctlable classes must implement DdkIoctl");
    static_assert(fbl::is_base_of<base_device, D>::value,
                  "Ioctlable classes must be derived from ddk::Device<...>.");
    static_assert(fbl::is_same<decltype(&D::DdkIoctl),
                                zx_status_t (D::*)(uint32_t, const void*, size_t,
                                                   void*, size_t, size_t*)>::value,
                  "DdkIoctl must be a public non-static member function with signature "
                  "'zx_status_t DdkIoctl(uint32_t, const void*, size_t, void*, size_t, size_t*)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_suspend, DdkSuspend);

template <typename D>
constexpr void CheckSuspendable() {
    static_assert(has_ddk_suspend<D>::value,
                  "Suspendable classes must implement DdkSuspend");
    static_assert(fbl::is_base_of<base_device, D>::value,
                  "Suspendable classes must be derived from ddk::Device<...>.");
    static_assert(fbl::is_same<decltype(&D::DdkSuspend), zx_status_t (D::*)(uint32_t)>::value,
                  "DdkSuspend must be a public non-static member function with signature "
                  "'zx_status_t DdkSuspend(uint32_t)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_resume, DdkResume);

template <typename D>
constexpr void CheckResumable() {
    static_assert(has_ddk_resume<D>::value,
                  "Resumable classes must implement DdkResume");
    static_assert(fbl::is_base_of<base_device, D>::value,
                  "Resumable classes must be derived from ddk::Device<...>.");
    static_assert(fbl::is_same<decltype(&D::DdkResume), zx_status_t (D::*)(uint32_t)>::value,
                  "DdkResume must be a public non-static member function with signature "
                  "'zx_status_t DdkResume(uint32_t)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_rxrpc, DdkRxrpc);

template <typename D>
constexpr void CheckRxrpcable() {
    static_assert(has_ddk_rxrpc<D>::value,
                  "Rxrpcable classes must implement DdkRxrpc");
    static_assert(fbl::is_base_of<base_device, D>::value,
                  "Rxrpcable classes must be derived from ddk::Device<...>.");
    static_assert(fbl::is_same<decltype(&D::DdkRxrpc), zx_status_t (D::*)(uint32_t)>::value,
                  "DdkRxrpc must be a public non-static member function with signature "
                  "'zx_status_t DdkRxrpc(zx_handle_t)'.");
}

// all_mixins
//
// Checks a list of types to ensure that all of them are ddk mixins (i.e., they inherit from the
// internal::base_mixin tag).
template <typename Base, typename...>
struct all_mixins : fbl::true_type {};

template <typename Base, typename Mixin, typename... Mixins>
struct all_mixins<Base, Mixin, Mixins...>
  : fbl::integral_constant<bool, fbl::is_base_of<Base, Mixin>::value &&
                                  all_mixins<Base, Mixins...>::value> {};

template <typename... Mixins>
constexpr void CheckMixins() {
    static_assert(all_mixins<base_mixin, Mixins...>::value,
            "All mixins must be from the ddk template library");
}

}  // namespace internal
}  // namespace ddk
