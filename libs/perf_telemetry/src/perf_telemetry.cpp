#include "perf_telemetry/perf_telemetry.h"

#include <algorithm>

namespace questlab::perf {

PerformanceSnapshot::PerformanceSnapshot(
    std::string_view category,
    std::string_view metricName,
    std::string_view unit,
    int64_t windowStartMonotonicNanoseconds,
    int64_t windowEndMonotonicNanoseconds,
    bool warmup,
    PercentileSummary summary)
    : category_(category),
      metricName_(metricName),
      unit_(unit),
      windowStartMonotonicNanoseconds_(windowStartMonotonicNanoseconds),
      windowEndMonotonicNanoseconds_(windowEndMonotonicNanoseconds),
      warmup_(warmup),
      summary_(summary) {}

int64_t SteadyNowNanoseconds() {
    return std::chrono::duration_cast<Nanoseconds>(
        SteadyClock::now().time_since_epoch()).count();
}

ReportCadence::ReportCadence(Nanoseconds interval) noexcept
    : interval_(std::max(interval, Nanoseconds(1))) {}

bool ReportCadence::ShouldReport(SteadyClock::time_point now) noexcept {
    if (!initialized_) {
        initialized_ = true;
        nextReport_ = now + interval_;
        return false;
    }
    if (now < nextReport_) {
        return false;
    }
    nextReport_ = now + interval_;
    return true;
}

void ReportCadence::Reset() noexcept {
    initialized_ = false;
    nextReport_ = {};
}

}  // namespace questlab::perf
