#pragma once

#include <cstddef>
#include <vector>

namespace questlab::rfdetr {

struct TimingSummary {
    size_t count = 0;
    double meanMilliseconds = 0.0;
    double p50Milliseconds = 0.0;
    double p95Milliseconds = 0.0;
    double p99Milliseconds = 0.0;
    double maximumMilliseconds = 0.0;
};

TimingSummary SummarizeTimings(
    const std::vector<double>& milliseconds);

}  // namespace questlab::rfdetr
