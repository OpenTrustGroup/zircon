// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddktl/device.h>
#include <ddktl/protocol/ethernet.h>
#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>
#include <unittest/unittest.h>

namespace {

// These tests are testing interfaces that get included via multiple inheritance, and thus we must
// make sure we get all the casts correct. We record the value of the "this" pointer in the
// constructor, and then verify in each call the "this" pointer was the same as the original. (The
// typical way for this to go wrong is to take a EthmacIfc<D>* instead of a D* in a function
// signature.)
#define get_this() reinterpret_cast<uintptr_t>(this)

class TestEthmacIfc : public ddk::Device<TestEthmacIfc>,
                      public ddk::EthmacIfc<TestEthmacIfc> {
  public:
    TestEthmacIfc() : ddk::Device<TestEthmacIfc>(nullptr) {
        this_ = get_this();
    }

    void DdkRelease() {}

    void EthmacStatus(uint32_t status) {
        status_this_ = get_this();
        status_called_ = true;
    }

    void EthmacRecv(void* data, size_t length, uint32_t flags) {
        recv_this_ = get_this();
        recv_called_ = true;
    }

    void EthmacCompleteTx(ethmac_netbuf_t* netbuf, zx_status_t status) {
        complete_tx_this_ = get_this();
        complete_tx_called_ = true;
    }

    bool VerifyCalls() const {
        BEGIN_HELPER;
        EXPECT_EQ(this_, status_this_, "");
        EXPECT_EQ(this_, recv_this_, "");
        EXPECT_EQ(this_, complete_tx_this_, "");
        EXPECT_TRUE(status_called_, "");
        EXPECT_TRUE(recv_called_, "");
        EXPECT_TRUE(complete_tx_called_, "");
        END_HELPER;
    }

    zx_status_t StartProtocol(ddk::EthmacProtocolProxy* proxy) {
        return proxy->Start(this);
    }

  private:
    uintptr_t this_ = 0u;
    uintptr_t status_this_ = 0u;
    uintptr_t recv_this_ = 0u;
    uintptr_t complete_tx_this_ = 0u;
    bool status_called_ = false;
    bool recv_called_ = false;
    bool complete_tx_called_ = false;
};

class TestEthmacProtocol : public ddk::Device<TestEthmacProtocol, ddk::GetProtocolable>,
                           public ddk::EthmacProtocol<TestEthmacProtocol> {
  public:
    TestEthmacProtocol()
      : ddk::Device<TestEthmacProtocol, ddk::GetProtocolable>(nullptr) {
        this_ = get_this();
    }

    zx_status_t DdkGetProtocol(uint32_t proto_id, void* out) {
        if (proto_id != ZX_PROTOCOL_ETHERNET_IMPL) return ZX_ERR_INVALID_ARGS;
        ddk::AnyProtocol* proto = static_cast<ddk::AnyProtocol*>(out);
        proto->ops = ddk_proto_ops_;
        proto->ctx = this;
        return ZX_OK;
    }

    void DdkRelease() {}

    zx_status_t EthmacQuery(uint32_t options, ethmac_info_t* info) {
        query_this_ = get_this();
        query_called_ = true;
        return ZX_OK;
    }

    void EthmacStop() {
        stop_this_ = get_this();
        stop_called_ = true;
    }

    zx_status_t EthmacStart(fbl::unique_ptr<ddk::EthmacIfcProxy> proxy) {
        start_this_ = get_this();
        proxy_.swap(proxy);
        start_called_ = true;
        return ZX_OK;
    }

    zx_status_t EthmacQueueTx(uint32_t options, ethmac_netbuf_t* netbuf) {
        queue_tx_this_ = get_this();
        queue_tx_called_ = true;
        return ZX_OK;
    }

    zx_status_t EthmacSetParam(uint32_t param, int32_t value, void* data) {
        set_param_this_ = get_this();
        set_param_called_ = true;
        return ZX_OK;
    }
    zx_handle_t EthmacGetBti() { return ZX_HANDLE_INVALID;}


    bool VerifyCalls() const {
        BEGIN_HELPER;
        EXPECT_EQ(this_, query_this_, "");
        EXPECT_EQ(this_, start_this_, "");
        EXPECT_EQ(this_, stop_this_, "");
        EXPECT_EQ(this_, queue_tx_this_, "");
        EXPECT_EQ(this_, set_param_this_, "");
        EXPECT_TRUE(query_called_, "");
        EXPECT_TRUE(start_called_, "");
        EXPECT_TRUE(stop_called_, "");
        EXPECT_TRUE(queue_tx_called_, "");
        EXPECT_TRUE(set_param_called_, "");
        END_HELPER;
    }

    bool TestIfc() {
        if (!proxy_) return false;
        // Use the provided proxy to test the ifc proxy.
        proxy_->Status(0);
        proxy_->Recv(nullptr, 0, 0);
        proxy_->CompleteTx(nullptr, ZX_OK);
        return true;
    }

  private:
    uintptr_t this_ = 0u;
    uintptr_t query_this_ = 0u;
    uintptr_t stop_this_ = 0u;
    uintptr_t start_this_ = 0u;
    uintptr_t queue_tx_this_ = 0u;
    uintptr_t set_param_this_ = 0u;
    bool query_called_ = false;
    bool stop_called_ = false;
    bool start_called_ = false;
    bool queue_tx_called_ = false;
    bool set_param_called_ = false;

    fbl::unique_ptr<ddk::EthmacIfcProxy> proxy_;
};

static bool test_ethmac_ifc() {
    BEGIN_TEST;

    TestEthmacIfc dev;

    auto ifc = dev.ethmac_ifc();
    ifc->status(&dev, 0);
    ifc->recv(&dev, nullptr, 0, 0);
    ifc->complete_tx(&dev, nullptr, ZX_OK);

    EXPECT_TRUE(dev.VerifyCalls(), "");

    END_TEST;
}

static bool test_ethmac_ifc_proxy() {
    BEGIN_TEST;

    TestEthmacIfc dev;
    ddk::EthmacIfcProxy proxy(dev.ethmac_ifc(), &dev);

    proxy.Status(0);
    proxy.Recv(nullptr, 0, 0);
    proxy.CompleteTx(nullptr, ZX_OK);

    EXPECT_TRUE(dev.VerifyCalls(), "");

    END_TEST;
}

static bool test_ethmac_protocol() {
    BEGIN_TEST;

    TestEthmacProtocol dev;

    // Normally we would use device_op_get_protocol, but we haven't added the device to devmgr so
    // its ops table is currently invalid.
    ethmac_protocol_t proto;
    auto status = dev.DdkGetProtocol(0, reinterpret_cast<void*>(&proto));
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, status, "");

    status = dev.DdkGetProtocol(ZX_PROTOCOL_ETHERNET_IMPL, reinterpret_cast<void*>(&proto));
    EXPECT_EQ(ZX_OK, status, "");

    EXPECT_EQ(ZX_OK, proto.ops->query(proto.ctx, 0, nullptr), "");
    proto.ops->stop(proto.ctx);
    EXPECT_EQ(ZX_OK, proto.ops->start(proto.ctx, nullptr, nullptr), "");
    ethmac_netbuf_t netbuf = {};
    EXPECT_EQ(ZX_OK, proto.ops->queue_tx(proto.ctx, 0, &netbuf), "");
    EXPECT_EQ(ZX_OK, proto.ops->set_param(proto.ctx, 0, 0, nullptr), "");

    EXPECT_TRUE(dev.VerifyCalls(), "");

    END_TEST;
}

static bool test_ethmac_protocol_proxy() {
    BEGIN_TEST;

    // The EthmacProtocol device to wrap. This would live in the parent device
    // our driver was binding to.
    TestEthmacProtocol protocol_dev;

    ethmac_protocol_t proto;
    auto status = protocol_dev.DdkGetProtocol(ZX_PROTOCOL_ETHERNET_IMPL, reinterpret_cast<void*>(&proto));
    EXPECT_EQ(ZX_OK, status, "");
    // The proxy device to wrap the ops + device that represent the parent
    // device.
    ddk::EthmacProtocolProxy proxy(&proto);
    // The EthmacIfc to hand to the parent device.
    TestEthmacIfc ifc_dev;

    EXPECT_EQ(ZX_OK, proxy.Query(0, nullptr), "");
    proxy.Stop();
    EXPECT_EQ(ZX_OK, proxy.Start(&ifc_dev), "");
    ethmac_netbuf_t netbuf = {};
    EXPECT_EQ(ZX_OK, proxy.QueueTx(0, &netbuf), "");
    EXPECT_EQ(ZX_OK, proxy.SetParam(0, 0, nullptr));

    EXPECT_TRUE(protocol_dev.VerifyCalls(), "");

    END_TEST;
}

static bool test_ethmac_protocol_ifc_proxy() {
    BEGIN_TEST;

    // We create a protocol device that we will start from an ifc device. The protocol device will
    // then use the pointer passed to it to call methods on the ifc device. This ensures the void*
    // casting is correct.
    TestEthmacProtocol protocol_dev;

    ethmac_protocol_t proto;
    auto status = protocol_dev.DdkGetProtocol(ZX_PROTOCOL_ETHERNET_IMPL, reinterpret_cast<void*>(&proto));
    EXPECT_EQ(ZX_OK, status, "");

    ddk::EthmacProtocolProxy proxy(&proto);
    TestEthmacIfc ifc_dev;
    EXPECT_EQ(ZX_OK, ifc_dev.StartProtocol(&proxy), "");

    // Execute the EthmacIfc methods
    ASSERT_TRUE(protocol_dev.TestIfc(), "");
    // Verify that they were called
    EXPECT_TRUE(ifc_dev.VerifyCalls(), "");

    END_TEST;
}


}  // namespace

BEGIN_TEST_CASE(ddktl_ethernet_device)
RUN_NAMED_TEST("ddk::EthmacIfc", test_ethmac_ifc);
RUN_NAMED_TEST("ddk::EthmacIfcProxy", test_ethmac_ifc_proxy);
RUN_NAMED_TEST("ddk::EthmacProtocol", test_ethmac_protocol);
RUN_NAMED_TEST("ddk::EthmacProtocolProxy", test_ethmac_protocol_proxy);
RUN_NAMED_TEST("EthmacProtocol using EthmacIfcProxy", test_ethmac_protocol_ifc_proxy);
END_TEST_CASE(ddktl_ethernet_device)

test_case_element* test_case_ddktl_ethernet_device = TEST_CASE_ELEMENT(ddktl_ethernet_device);
