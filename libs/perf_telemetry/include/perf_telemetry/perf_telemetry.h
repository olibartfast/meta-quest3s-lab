#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace questlab::perf {

using SteadyClock = std::chrono::steady_clock;
using Nanoseconds = std::chrono::nanoseconds;

struct DurationSample {
    Nanoseconds duration{};
};

struct CounterSample {
    std::string_view name;
    uint64_t value = 0;
};

struct PercentileSummary {
    uint64_t count = 0;
    uint64_t overflowCount = 0;
    double meanMilliseconds = 0.0;
    double p50Milliseconds = 0.0;
    double p95Milliseconds = 0.0;
    double p99Milliseconds = 0.0;
    double maximumMilliseconds = 0.0;
};

class PerformanceSnapshot final {
public:
    PerformanceSnapshot(
        std::string_view category,
        std::string_view metricName,
        std::string_view unit,
        int64_t windowStartMonotonicNanoseconds,
        int64_t windowEndMonotonicNanoseconds,
        bool warmup,
        PercentileSummary summary);

    std::string_view Category() const { return category_; }
    std::string_view MetricName() const { return metricName_; }
    std::string_view Unit() const { return unit_; }
    int64_t WindowStartMonotonicNanoseconds() const {
        return windowStartMonotonicNanoseconds_;
    }
    int64_t WindowEndMonotonicNanoseconds() const {
        return windowEndMonotonicNanoseconds_;
    }
    bool IsWarmup() const { return warmup_; }
    const PercentileSummary& Summary() const { return summary_; }

private:
    const std::string_view category_;
    const std::string_view metricName_;
    const std::string_view unit_;
    const int64_t windowStartMonotonicNanoseconds_;
    const int64_t windowEndMonotonicNanoseconds_;
    const bool warmup_;
    const PercentileSummary summary_;
};

int64_t SteadyNowNanoseconds();

template <size_t Capacity>
class DurationRing final {
    static_assert(Capacity > 0, "A duration ring must have capacity");

public:
    void AddSample(DurationSample sample) noexcept {
        const int64_t value = std::max<int64_t>(sample.duration.count(), 0);
        samples_[nextIndex_] = value;
        nextIndex_ = (nextIndex_ + 1) % Capacity;
        if (sampleCount_ < Capacity) {
            ++sampleCount_;
        } else {
            ++overflowCount_;
        }
    }

    PerformanceSnapshot Snapshot(
        std::string_view category,
        std::string_view metricName,
        int64_t windowStartMonotonicNanoseconds,
        int64_t windowEndMonotonicNanoseconds,
        bool warmup) const {
        std::array<int64_t, Capacity> sorted{};
        std::copy_n(samples_.begin(), sampleCount_, sorted.begin());
        std::sort(sorted.begin(), sorted.begin() + sampleCount_);

        PercentileSummary summary;
        summary.count = sampleCount_;
        summary.overflowCount = overflowCount_;
        if (sampleCount_ > 0) {
            long double totalNanoseconds = 0.0L;
            for (size_t index = 0; index < sampleCount_; ++index) {
                totalNanoseconds += sorted[index];
            }
            summary.meanMilliseconds = static_cast<double>(
                totalNanoseconds /
                static_cast<long double>(sampleCount_) /
                1'000'000.0L);
            summary.p50Milliseconds = ToMilliseconds(
                sorted[NearestRankIndex(50)]);
            summary.p95Milliseconds = ToMilliseconds(
                sorted[NearestRankIndex(95)]);
            summary.p99Milliseconds = ToMilliseconds(
                sorted[NearestRankIndex(99)]);
            summary.maximumMilliseconds = ToMilliseconds(
                sorted[sampleCount_ - 1]);
        }
        return PerformanceSnapshot(
            category,
            metricName,
            "milliseconds",
            windowStartMonotonicNanoseconds,
            windowEndMonotonicNanoseconds,
            warmup,
            summary);
    }

    void Clear() noexcept {
        sampleCount_ = 0;
        nextIndex_ = 0;
        overflowCount_ = 0;
    }

    size_t Size() const noexcept { return sampleCount_; }
    static constexpr size_t MaximumSize() noexcept { return Capacity; }
    uint64_t OverflowCount() const noexcept { return overflowCount_; }

private:
    size_t NearestRankIndex(size_t percentile) const noexcept {
        const size_t rank =
            (sampleCount_ * percentile + 99U) / 100U;
        return std::max<size_t>(rank, 1U) - 1U;
    }

    static double ToMilliseconds(int64_t nanoseconds) noexcept {
        return static_cast<double>(nanoseconds) / 1'000'000.0;
    }

    std::array<int64_t, Capacity> samples_{};
    size_t sampleCount_ = 0;
    size_t nextIndex_ = 0;
    uint64_t overflowCount_ = 0;
};

template <size_t Capacity>
class ScopedDuration final {
public:
    explicit ScopedDuration(
        DurationRing<Capacity>* destination,
        bool enabled = true) noexcept
        : destination_(enabled ? destination : nullptr) {
        if (destination_ != nullptr) {
            start_ = SteadyClock::now();
        }
    }

    ~ScopedDuration() {
        if (destination_ != nullptr) {
            destination_->AddSample({
                std::chrono::duration_cast<Nanoseconds>(
                    SteadyClock::now() - start_),
            });
        }
    }

    ScopedDuration(const ScopedDuration&) = delete;
    ScopedDuration& operator=(const ScopedDuration&) = delete;

private:
    DurationRing<Capacity>* destination_ = nullptr;
    SteadyClock::time_point start_{};
};

class ReportCadence final {
public:
    explicit ReportCadence(Nanoseconds interval) noexcept;

    bool ShouldReport(SteadyClock::time_point now) noexcept;
    void Reset() noexcept;

private:
    Nanoseconds interval_;
    SteadyClock::time_point nextReport_{};
    bool initialized_ = false;
};

}  // namespace questlab::perf
