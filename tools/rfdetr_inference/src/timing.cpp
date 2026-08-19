#include "rfdetr_inference/timing.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace questlab::rfdetr {
namespace {

double Percentile(const std::vector<double>& sorted, double probability) {
    if (sorted.empty()) {
        return 0.0;
    }
    const double position =
        probability * static_cast<double>(sorted.size() - 1U);
    const size_t lower = static_cast<size_t>(std::floor(position));
    const size_t upper = static_cast<size_t>(std::ceil(position));
    const double fraction = position - static_cast<double>(lower);
    return sorted[lower] * (1.0 - fraction) + sorted[upper] * fraction;
}

}  // namespace

TimingSummary SummarizeTimings(
    const std::vector<double>& milliseconds) {
    TimingSummary result;
    result.count = milliseconds.size();
    if (milliseconds.empty()) {
        return result;
    }
    std::vector<double> sorted = milliseconds;
    std::sort(sorted.begin(), sorted.end());
    result.meanMilliseconds =
        std::accumulate(sorted.begin(), sorted.end(), 0.0) /
        static_cast<double>(sorted.size());
    result.p50Milliseconds = Percentile(sorted, 0.50);
    result.p95Milliseconds = Percentile(sorted, 0.95);
    result.p99Milliseconds = Percentile(sorted, 0.99);
    result.maximumMilliseconds = sorted.back();
    return result;
}

}  // namespace questlab::rfdetr
