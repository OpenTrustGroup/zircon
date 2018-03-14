// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <perftest/results.h>

#include <math.h>

#include <fbl/algorithm.h>
#include <zircon/assert.h>

namespace perftest {
namespace {

double Mean(const fbl::Vector<double>& values) {
    double sum = fbl::accumulate(values.begin(), values.end(), 0.0);
    return sum / static_cast<double>(values.size());
}

double Min(const fbl::Vector<double>& values) {
    return *fbl::min_element(values.begin(), values.end());
}

double Max(const fbl::Vector<double>& values) {
    return *fbl::max_element(values.begin(), values.end());
}

double StdDev(const fbl::Vector<double>& values, double mean) {
    double sum_of_squared_diffs = 0.0;
    for (double value : values) {
        double diff = value - mean;
        sum_of_squared_diffs += diff * diff;
    }
    return sqrt(sum_of_squared_diffs / static_cast<double>(values.size()));
}

} // namespace

SummaryStatistics TestCaseResults::GetSummaryStatistics() const {
    ZX_ASSERT(values_.size() > 0);
    double mean = Mean(values_);
    return SummaryStatistics{
        .min = Min(values_),
        .max = Max(values_),
        .mean = mean,
        .std_dev = StdDev(values_, mean),
    };
}

void WriteJSONString(FILE* out_file, const char* string) {
    fputc('"', out_file);
    for (const char* ptr = string; *ptr; ptr++) {
        uint8_t c = *ptr;
        if (c == '"') {
            fputs("\\\"", out_file);
        } else if (c == '\\') {
            fputs("\\\\", out_file);
        } else if (c < 32 || c >= 128) {
            // Escape non-printable characters (<32) and top-bit-set
            // characters (>=128).
            //
            // TODO(TO-824): Handle top-bit-set characters better.  Ideally
            // we should treat the input string as UTF-8 and preserve the
            // encoded Unicode in the JSON.  We could interpret the UTF-8
            // sequences and convert them to \uXXXX escape sequences.
            // Alternatively we could pass through UTF-8, but if we do
            // that, we ought to block overlong UTF-8 sequences to prevent
            // closing quotes from being encoded as overlong UTF-8
            // sequences.
            //
            // The current code treats the input string as a byte array
            // rather than UTF-8, which isn't *necessarily* what we want,
            // but will at least result in valid JSON and make the data
            // recoverable.
            fprintf(out_file, "\\u%04x", c);
        } else {
            fputc(c, out_file);
        }
    }
    fputc('"', out_file);
}

void TestCaseResults::WriteJSON(FILE* out_file) const {
    fprintf(out_file, "{\"label\":");
    WriteJSONString(out_file, label_.c_str());
    fprintf(out_file, ",\"unit\":");
    WriteJSONString(out_file, unit_.c_str());
    fprintf(out_file, ",\"samples\":[");

    fprintf(out_file, "{\"values\":[");
    bool first = true;
    for (const auto value : values_) {
        if (!first) {
            fprintf(out_file, ",");
        }
        fprintf(out_file, "%f", value);
        first = false;
    }
    fprintf(out_file, "]}");

    fprintf(out_file, "]}");
}

TestCaseResults* ResultsSet::AddTestCase(const fbl::String& label,
                                         const fbl::String& unit) {
    TestCaseResults test_case(label, unit);
    results_.push_back(fbl::move(test_case));
    return &results_[results_.size() - 1];
}

void ResultsSet::WriteJSON(FILE* out_file) const {
    fprintf(out_file, "[");
    bool first = true;
    for (const auto& test_case_results : results_) {
        if (!first) {
            fprintf(out_file, ",\n");
        }
        test_case_results.WriteJSON(out_file);
        first = false;
    }
    fprintf(out_file, "]");
}

} // namespace perftest
