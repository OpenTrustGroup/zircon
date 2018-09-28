// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fixture.h"

#include <threads.h>

#include <fbl/function.h>
#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <fbl/vector.h>
#include <lib/zx/event.h>
#include <trace/event.h>
#include <trace/handler.h>

namespace {
int RunClosure(void* arg) {
    auto closure = static_cast<fbl::Closure*>(arg);
    (*closure)();
    delete closure;
    return 0;
}

void RunThread(fbl::Closure closure) {
    thrd_t thread;
    int result = thrd_create(&thread, RunClosure,
                             new fbl::Closure(fbl::move(closure)));
    ZX_ASSERT(result == thrd_success);

    result = thrd_join(thread, nullptr);
    ZX_ASSERT(result == thrd_success);
}

bool test_normal_shutdown() {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();
    fixture_stop_tracing();
    EXPECT_EQ(ZX_OK, fixture_get_disposition());

    END_TRACE_TEST;
}

bool test_hard_shutdown() {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();
    fixture_stop_tracing_hard();
    EXPECT_EQ(ZX_ERR_CANCELED, fixture_get_disposition());

    END_TRACE_TEST;
}

bool test_state() {
    BEGIN_TRACE_TEST;

    EXPECT_EQ(TRACE_STOPPED, trace_state());

    fixture_start_tracing();
    EXPECT_EQ(TRACE_STARTED, trace_state());

    fixture_stop_tracing();
    EXPECT_EQ(TRACE_STOPPED, trace_state());

    END_TRACE_TEST;
}

bool test_is_enabled() {
    BEGIN_TRACE_TEST;

    EXPECT_FALSE(trace_is_enabled());

    fixture_start_tracing();
    EXPECT_TRUE(trace_is_enabled());

    fixture_stop_tracing();
    EXPECT_FALSE(trace_is_enabled());

    END_TRACE_TEST;
}

bool test_is_category_enabled() {
    BEGIN_TRACE_TEST;

    EXPECT_FALSE(trace_is_category_enabled("+enabled"));
    EXPECT_FALSE(trace_is_category_enabled("-disabled"));
    EXPECT_FALSE(trace_is_category_enabled(""));

    fixture_start_tracing();
    EXPECT_TRUE(trace_is_category_enabled("+enabled"));
    EXPECT_FALSE(trace_is_category_enabled("-disabled"));
    EXPECT_FALSE(trace_is_category_enabled(""));

    fixture_stop_tracing();
    EXPECT_FALSE(trace_is_category_enabled("+enabled"));
    EXPECT_FALSE(trace_is_category_enabled("-disabled"));
    EXPECT_FALSE(trace_is_category_enabled(""));

    END_TRACE_TEST;
}

bool test_generate_nonce() {
    BEGIN_TRACE_TEST;

    uint64_t nonce1 = trace_generate_nonce();
    EXPECT_NE(0u, nonce1, "nonce is never 0");

    uint64_t nonce2 = trace_generate_nonce();
    EXPECT_NE(0u, nonce2, "nonce is never 0");

    EXPECT_NE(nonce1, nonce2, "nonce is unique");

    END_TRACE_TEST;
}

bool test_observer() {
    const size_t kBufferSize = 4096u;

    // This test needs the trace engine to run in the same thread as the test:
    // We need to control when state change signalling happens.
    BEGIN_TRACE_TEST_ETC(kAttachToThread,
                         TRACE_BUFFERING_MODE_ONESHOT, kBufferSize);

    zx::event event;
    EXPECT_EQ(ZX_OK, zx::event::create(0u, &event));

    EXPECT_EQ(ZX_OK, trace_register_observer(event.get()));
    EXPECT_EQ(ZX_ERR_TIMED_OUT, event.wait_one(ZX_EVENT_SIGNALED, zx::time(), nullptr));

    fixture_start_tracing();
    EXPECT_EQ(ZX_OK, event.wait_one(ZX_EVENT_SIGNALED, zx::time(), nullptr));
    EXPECT_EQ(TRACE_STARTED, trace_state());

    EXPECT_EQ(ZX_OK, event.signal(ZX_EVENT_SIGNALED, 0u));
    EXPECT_EQ(ZX_ERR_TIMED_OUT, event.wait_one(ZX_EVENT_SIGNALED, zx::time(), nullptr));

    fixture_stop_engine();

    // Now walk the dispatcher loop an event at a time so that we see both
    // the TRACE_STOPPING event and the TRACE_STOPPED event.
    EXPECT_EQ(TRACE_STOPPING, trace_state());
    EXPECT_EQ(ZX_OK, event.wait_one(ZX_EVENT_SIGNALED, zx::time(), nullptr));
    EXPECT_EQ(ZX_OK, event.signal(ZX_EVENT_SIGNALED, 0u));
    zx_status_t status = ZX_OK;
    while (status == ZX_OK && trace_state() != TRACE_STOPPED) {
        status = async_loop_run(fixture_async_loop(), zx_deadline_after(0), true);
        EXPECT_EQ(ZX_OK, status);
        if (trace_state() == TRACE_STOPPED) {
            EXPECT_EQ(ZX_OK, event.wait_one(ZX_EVENT_SIGNALED, zx::time(), nullptr));
        }
    }

    fixture_shutdown();
    EXPECT_EQ(ZX_OK, trace_unregister_observer(event.get()));

    END_TRACE_TEST;
}

bool test_observer_errors() {
    BEGIN_TRACE_TEST;

    zx::event event;
    EXPECT_EQ(ZX_OK, zx::event::create(0u, &event));

    EXPECT_EQ(ZX_OK, trace_register_observer(event.get()));
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, trace_register_observer(event.get()));

    EXPECT_EQ(ZX_OK, trace_unregister_observer(event.get()));
    EXPECT_EQ(ZX_ERR_NOT_FOUND, trace_unregister_observer(event.get()));

    END_TRACE_TEST;
}

bool test_register_current_thread() {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    trace_thread_ref_t t1, t2;
    {
        auto context = trace::TraceContext::Acquire();

        trace_context_register_current_thread(context.get(), &t1);
        trace_context_register_current_thread(context.get(), &t2);
    }

    EXPECT_TRUE(trace_is_indexed_thread_ref(&t1));
    EXPECT_TRUE(trace_is_indexed_thread_ref(&t2));
    EXPECT_EQ(t1.encoded_value, t2.encoded_value);

    ASSERT_RECORDS(R"X(String(index: 1, "process")
KernelObject(koid: <>, type: thread, name: "initial-thread", {process: koid(<>)})
Thread(index: 1, <>)
)X",
                   "");

    END_TRACE_TEST;
}

bool test_register_current_thread_multiple_threads() {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    trace_thread_ref_t t1;
    {
        auto context = trace::TraceContext::Acquire();

        trace_context_register_current_thread(context.get(), &t1);
    }

    trace_thread_ref_t t2;
    RunThread([&t2] {
        auto context = trace::TraceContext::Acquire();

        trace_context_register_current_thread(context.get(), &t2);
    });

    EXPECT_TRUE(trace_is_indexed_thread_ref(&t1));
    EXPECT_TRUE(trace_is_indexed_thread_ref(&t2));
    EXPECT_NE(t1.encoded_value, t2.encoded_value);

    ASSERT_RECORDS(R"X(String(index: 1, "process")
KernelObject(koid: <>, type: thread, name: "initial-thread", {process: koid(<>)})
Thread(index: 1, <>)
String(index: 2, "process")
KernelObject(koid: <>, type: thread, name: "thrd_t:<>/TLS=<>", {process: koid(<>)})
Thread(index: 2, <>)
)X",
                   "");

    END_TRACE_TEST;
}

bool test_register_string_literal() {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    trace_string_ref_t empty;
    trace_string_ref_t null;
    trace_string_ref_t a1, a2, a3;
    trace_string_ref_t b1, b2, b3;
    {
        auto context = trace::TraceContext::Acquire();

        trace_context_register_string_literal(context.get(), "", &empty);

        trace_context_register_string_literal(context.get(), nullptr, &null);

        trace_context_register_string_literal(context.get(), "string1", &a1);
        trace_context_register_string_literal(context.get(), "string2", &a2);
        trace_context_register_string_literal(context.get(), "string3", &a3);

        trace_context_register_string_literal(context.get(), "string1", &b1);
        trace_context_register_string_literal(context.get(), "string2", &b2);
        trace_context_register_string_literal(context.get(), "string3", &b3);
    }

    EXPECT_TRUE(trace_is_empty_string_ref(&empty));
    EXPECT_TRUE(trace_is_empty_string_ref(&null));

    EXPECT_TRUE(trace_is_indexed_string_ref(&a1));
    EXPECT_TRUE(trace_is_indexed_string_ref(&a2));
    EXPECT_TRUE(trace_is_indexed_string_ref(&a3));

    EXPECT_TRUE(trace_is_indexed_string_ref(&b1));
    EXPECT_TRUE(trace_is_indexed_string_ref(&b2));
    EXPECT_TRUE(trace_is_indexed_string_ref(&b3));

    EXPECT_EQ(a1.encoded_value, b1.encoded_value);
    EXPECT_EQ(a2.encoded_value, b2.encoded_value);
    EXPECT_EQ(a3.encoded_value, b3.encoded_value);

    EXPECT_NE(a1.encoded_value, a2.encoded_value);
    EXPECT_NE(a1.encoded_value, a3.encoded_value);
    EXPECT_NE(a2.encoded_value, a3.encoded_value);

    ASSERT_RECORDS(R"X(String(index: 1, "string1")
String(index: 2, "string2")
String(index: 3, "string3")
)X",
                   "");

    END_TRACE_TEST;
}

bool test_register_string_literal_multiple_threads() {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    trace_string_ref_t a1;
    trace_string_ref_t a2;
    {
        auto context = trace::TraceContext::Acquire();

        trace_context_register_string_literal(context.get(), "string1", &a1);
        trace_context_register_string_literal(context.get(), "string2", &a2);
    }

    trace_string_ref_t b1;
    trace_string_ref_t b2;
    RunThread([&b1, &b2] {
        auto context = trace::TraceContext::Acquire();

        trace_context_register_string_literal(context.get(), "string1", &b1);
        trace_context_register_string_literal(context.get(), "string2", &b2);
    });

    EXPECT_TRUE(trace_is_indexed_string_ref(&a1));
    EXPECT_TRUE(trace_is_indexed_string_ref(&a2));

    EXPECT_TRUE(trace_is_indexed_string_ref(&b1));
    EXPECT_TRUE(trace_is_indexed_string_ref(&b2));

    EXPECT_NE(a1.encoded_value, a2.encoded_value);
    EXPECT_NE(b1.encoded_value, b2.encoded_value);

    // Each thread has its own string pool.
    EXPECT_NE(a1.encoded_value, b1.encoded_value);
    EXPECT_NE(a2.encoded_value, b2.encoded_value);

    ASSERT_RECORDS(R"X(String(index: 1, "string1")
String(index: 2, "string2")
String(index: 3, "string1")
String(index: 4, "string2")
)X",
                   "");

    END_TRACE_TEST;
}

bool test_register_string_literal_table_overflow() {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    fbl::Vector<fbl::String> strings;

    {
        auto context = trace::TraceContext::Acquire();

        unsigned n = 0;
        for (n = 0; n < TRACE_ENCODED_STRING_REF_MAX_INDEX; n++) {
            trace_string_ref_t r;
            fbl::String string = fbl::StringPrintf("string%d", n);
            strings.push_back(string);
            trace_context_register_string_literal(context.get(), string.c_str(), &r);
            if (trace_is_inline_string_ref(&r))
                break;
        }
        EXPECT_GT(n, 100); // at least 100 string can be cached per thread
    }

    END_TRACE_TEST;
}

bool test_maximum_record_length() {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    {
        auto context = trace::TraceContext::Acquire();

        EXPECT_NONNULL(trace_context_alloc_record(context.get(), 0));
        EXPECT_NONNULL(trace_context_alloc_record(context.get(), 8));
        EXPECT_NONNULL(trace_context_alloc_record(context.get(), 16));
        EXPECT_NONNULL(trace_context_alloc_record(
            context.get(), TRACE_ENCODED_RECORD_MAX_LENGTH));

        EXPECT_NULL(trace_context_alloc_record(
            context.get(), TRACE_ENCODED_RECORD_MAX_LENGTH + 8));
        EXPECT_NULL(trace_context_alloc_record(
            context.get(), TRACE_ENCODED_RECORD_MAX_LENGTH + 16));
    }

    END_TRACE_TEST;
}

bool test_event_with_inline_everything() {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    trace_string_ref_t cat = trace_make_inline_c_string_ref("cat");
    trace_string_ref_t name = trace_make_inline_c_string_ref("name");
    trace_thread_ref_t thread = trace_make_inline_thread_ref(123, 456);
    trace_arg_t args[] = {
        trace_make_arg(trace_make_inline_c_string_ref("argname"),
                       trace_make_string_arg_value(trace_make_inline_c_string_ref("argvalue")))};

    {
        auto context = trace::TraceContext::Acquire();

        trace_context_write_instant_event_record(context.get(), zx_ticks_get(),
                                                 &thread, &cat, &name,
                                                 TRACE_SCOPE_GLOBAL,
                                                 args, fbl::count_of(args));
    }

    ASSERT_RECORDS(R"X(Event(ts: <>, pt: <>, category: "cat", name: "name", Instant(scope: global), {argname: string("argvalue")})
)X",
                   "");

    END_TRACE_TEST;
}

bool test_circular_mode() {
    const size_t kBufferSize = 4096u;
    BEGIN_TRACE_TEST_ETC(kNoAttachToThread,
                         TRACE_BUFFERING_MODE_CIRCULAR, kBufferSize);

    fixture_start_tracing();

    // Fill the buffers with one kind of record, then fill them with another.
    // We should see only the second kind remaining.

    for (size_t i = 0; i < kBufferSize / 8; ++i) {
        TRACE_INSTANT("+enabled", "name", TRACE_SCOPE_GLOBAL,
                      "k1", TA_INT32(1));
    }

    // IWBN to verify the contents of the buffer at this point, but that
    // requires stopping the trace. There's no current way to pause it.

    // Now fill the buffer with a different kind of record.

    for (size_t i = 0; i < kBufferSize / 8; ++i) {
        TRACE_INSTANT("+enabled", "name", TRACE_SCOPE_GLOBAL,
                      "k2", TA_INT32(2));
    }

    // TODO(dje): There is a 1-second wait here. Not sure what to do about it.
    EXPECT_FALSE(fixture_wait_buffer_full_notification());

    // Prepare a squelcher to remove timestamps.
    FixtureSquelch* ts_squelch;
    ASSERT_TRUE(fixture_create_squelch("ts: ([0-9]+)", &ts_squelch));

    // These records come from the durable buffer.
    const char expected_initial_records[] = "\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"k1\")\n\
String(index: 3, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 4, \"name\")\n\
String(index: 5, \"k2\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", Instant(scope: global), {k2: int32(2)})\n\
";

    fbl::Vector<trace::Record> records;
    const size_t kDataRecordOffset = 7;
    ASSERT_N_RECORDS(kDataRecordOffset + 1, /*empty*/, expected_initial_records,
                     &records);

    // Verify all trailing records are the same (sans timestamp).
    auto test_record = records[kDataRecordOffset].ToString();
    auto test_str = fixture_squelch(ts_squelch, test_record.c_str());
    for (size_t i = kDataRecordOffset + 1; i < records.size(); ++i) {
        // FIXME(dje): Moved this here from outside the for loop to get things
        // working. Why was it necessary?
        auto test_cstr = test_str.c_str();
        auto record_str = fixture_squelch(ts_squelch,
                                          records[i].ToString().c_str());
        auto record_cstr = record_str.c_str();
        EXPECT_STR_EQ(test_cstr, record_cstr, "bad data record");
    }

    fixture_destroy_squelch(ts_squelch);

    END_TRACE_TEST;
}

bool test_streaming_mode() {
    const size_t kBufferSize = 4096u;
    BEGIN_TRACE_TEST_ETC(kNoAttachToThread,
                         TRACE_BUFFERING_MODE_STREAMING, kBufferSize);

    fixture_start_tracing();

    // Fill the buffers with one kind of record.
    // Both buffers should fill since there's no one to save them.

    for (size_t i = 0; i < kBufferSize / 8; ++i) {
        TRACE_INSTANT("+enabled", "name", TRACE_SCOPE_GLOBAL,
                      "k1", TA_INT32(1));
    }

    EXPECT_TRUE(fixture_wait_buffer_full_notification());
    EXPECT_EQ(fixture_get_buffer_full_wrapped_count(), 0);
    fixture_reset_buffer_full_notification();

    // N.B. While we're examining the header we assume tracing is paused.

    trace_buffer_header header;
    fixture_snapshot_buffer_header(&header);

    EXPECT_EQ(header.version, 0);
    EXPECT_EQ(header.buffering_mode, TRACE_BUFFERING_MODE_STREAMING);
    EXPECT_EQ(header.reserved1, 0);
    EXPECT_EQ(header.wrapped_count, 1);
    EXPECT_EQ(header.total_size, kBufferSize);
    EXPECT_NE(header.durable_buffer_size, 0);
    EXPECT_NE(header.rolling_buffer_size, 0);
    EXPECT_EQ(sizeof(header) + header.durable_buffer_size + 
              2 * header.rolling_buffer_size, kBufferSize);
    EXPECT_NE(header.durable_data_end, 0);
    EXPECT_LE(header.durable_data_end, header.durable_buffer_size);
    EXPECT_NE(header.rolling_data_end[0], 0);
    EXPECT_LE(header.rolling_data_end[0], header.rolling_buffer_size);
    EXPECT_NE(header.rolling_data_end[1], 0);
    EXPECT_LE(header.rolling_data_end[1], header.rolling_buffer_size);
    // All the records are the same size, so each buffer should end up at
    // the same place.
    EXPECT_EQ(header.rolling_data_end[0], header.rolling_data_end[1]);

    // Try to fill the buffer with a different kind of record.
    // These should all be dropped.

    for (size_t i = 0; i < kBufferSize / 8; ++i) {
        TRACE_INSTANT("+enabled", "name", TRACE_SCOPE_GLOBAL,
                      "k2", TA_INT32(2));
    }

    // TODO(dje): There is a 1-second wait here. Not sure what to do about it.
    EXPECT_FALSE(fixture_wait_buffer_full_notification());

    // Pretend to save the older of the two buffers.
    {
        auto context = trace::TraceProlongedContext::Acquire();
        trace_context_snapshot_buffer_header(context.get(), &header);
    }
    EXPECT_EQ(header.wrapped_count, 1);

    // Buffer zero is older.
    trace_engine_mark_buffer_saved(0, 0);

    {
        auto context = trace::TraceProlongedContext::Acquire();
        trace_context_snapshot_buffer_header(context.get(), &header);
    }
    EXPECT_EQ(header.rolling_data_end[0], 0);
    // The wrapped count shouldn't be updated until the next record is written.
    EXPECT_EQ(header.wrapped_count, 1);

    // Fill the buffer with a different kind of record.

    for (size_t i = 0; i < kBufferSize / 8; ++i) {
        TRACE_INSTANT("+enabled", "name", TRACE_SCOPE_GLOBAL,
                      "k3", TA_INT32(3));
    }

    EXPECT_TRUE(fixture_wait_buffer_full_notification());
    EXPECT_EQ(fixture_get_buffer_full_wrapped_count(), 1);

    {
        auto context = trace::TraceProlongedContext::Acquire();
        trace_context_snapshot_buffer_header(context.get(), &header);
    }
    EXPECT_EQ(header.wrapped_count, 2);
    EXPECT_NE(header.rolling_data_end[0], 0);
    EXPECT_EQ(header.rolling_data_end[0], header.rolling_data_end[1]);

    // One buffer should now have the first kind of record, and the other
    // should have the new kind of record. And the newer records should be
    // read after the older ones.

    FixtureSquelch* ts_squelch;
    ASSERT_TRUE(fixture_create_squelch("ts: ([0-9]+)", &ts_squelch));

    const char expected_initial_records[] =
        // These records come from the durable buffer.
        "\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"k1\")\n\
String(index: 3, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 4, \"name\")\n\
String(index: 5, \"k2\")\n\
String(index: 6, \"k3\")\n"
        // This record is the first record in the rolling buffer
        "\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", Instant(scope: global), {k1: int32(1)})\n";

    // There is no DATA2_RECORD, those records are dropped (buffer is full).
    const char data3_record[] = "\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", Instant(scope: global), {k3: int32(3)})\n";

    fbl::Vector<trace::Record> records;
    const size_t kDataRecordOffset = 8;
    ASSERT_N_RECORDS(kDataRecordOffset + 1, /*empty*/, expected_initial_records,
                     &records);

    // Verify the first set of data records are the same (sans timestamp).
    auto test_record = records[kDataRecordOffset].ToString();
    auto test_str = fixture_squelch(ts_squelch, test_record.c_str());
    size_t num_data_records = 1;
    size_t i;
    for (i = kDataRecordOffset + 1; i < records.size(); ++i) {
        auto test_cstr = test_str.c_str();
        auto record_str = fixture_squelch(ts_squelch,
                                          records[i].ToString().c_str());
        auto record_cstr = record_str.c_str();
        if (strcmp(test_cstr, record_cstr) != 0)
            break;
        ++num_data_records;
    }
    EXPECT_GE(num_data_records, 2);
    // The records are all of equal size, therefore they should evenly fit
    // in the number of bytes written. Buffer 1 holds the older records.
    EXPECT_EQ(header.rolling_data_end[1] % num_data_records, 0);

    // There should be the same number of records remaining.
    EXPECT_EQ(num_data_records, records.size() - i);

    // The next record should be |data3_record|.
    EXPECT_TRUE(fixture_compare_raw_records(records, i, 1, data3_record));

    // All remaining records should match (sans timestamp).
    test_record = records[i].ToString();
    test_str = fixture_squelch(ts_squelch, test_record.c_str());
    for (i = i + 1; i < records.size(); ++i) {
        auto test_cstr = test_str.c_str();
        auto record_str = fixture_squelch(ts_squelch,
                                          records[i].ToString().c_str());
        auto record_cstr = record_str.c_str();
        EXPECT_STR_EQ(test_cstr, record_cstr, "bad data record");
    }

    fixture_destroy_squelch(ts_squelch);

    END_TRACE_TEST;
}

// This test exercises DX-441 where a buffer becomes full and immediately
// thereafter tracing is stopped. This causes the "please save buffer"
// processing to run when tracing is not active.
bool test_shutdown_when_full() {
    const size_t kBufferSize = 4096u;

    // This test needs the trace engine to run in the same thread as the test:
    // We need to control when buffer full handling happens.
    BEGIN_TRACE_TEST_ETC(kAttachToThread,
                         TRACE_BUFFERING_MODE_STREAMING, kBufferSize);

    fixture_start_tracing();

    // Keep writing records until we just fill the buffer.
    // Since the engine loop is on the same loop as us, we can't rely on
    // handler notifications: They won't get run.
    {
        auto context = trace::TraceProlongedContext::Acquire();
        for (;;) {
            TRACE_INSTANT("+enabled", "name", TRACE_SCOPE_GLOBAL,
                          "k1", TA_INT32(1));

            trace_buffer_header header;
            trace_context_snapshot_buffer_header(context.get(), &header);
            if (header.wrapped_count > 0) {
                break;
            }
        }
    }

    // At this point there should be no references to the context except for
    // the engine's. Then when remaining tasks in the loop are run the
    // |trace_engine_request_save_buffer()| task will have no context in
    // which to process the request and should gracefully fail.
    fixture_stop_tracing();

    END_TRACE_TEST;
}

// NOTE: The functions for writing trace records are exercised by other trace tests.

} // namespace

BEGIN_TEST_CASE(engine_tests)
RUN_TEST(test_normal_shutdown)
RUN_TEST(test_hard_shutdown)
RUN_TEST(test_is_enabled)
RUN_TEST(test_is_category_enabled)
RUN_TEST(test_generate_nonce)
RUN_TEST(test_observer)
RUN_TEST(test_observer_errors)
RUN_TEST(test_register_current_thread)
RUN_TEST(test_register_current_thread_multiple_threads)
RUN_TEST(test_register_string_literal)
RUN_TEST(test_register_string_literal_multiple_threads)
RUN_TEST(test_register_string_literal_table_overflow)
RUN_TEST(test_maximum_record_length)
RUN_TEST(test_event_with_inline_everything)
RUN_TEST(test_circular_mode)
RUN_TEST(test_streaming_mode)
RUN_TEST(test_shutdown_when_full)
END_TEST_CASE(engine_tests)
