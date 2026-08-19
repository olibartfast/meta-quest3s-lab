#include "perf_telemetry/perf_telemetry.h"
#include "perf_telemetry/trace_span.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <type_traits>

namespace {

using namespace std::chrono_literals;

int failures = 0;

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void ExpectNear(double actual, double expected, const char* message) {
    Expect(std::abs(actual - expected) < 0.0001, message);
}

void TestEmptyWindow() {
    questlab::perf::DurationRing<8> ring;
    const auto snapshot = ring.Snapshot(
        "test", "empty", 10, 20, true);
    Expect(snapshot.Summary().count == 0, "empty count");
    Expect(snapshot.Summary().overflowCount == 0, "empty overflow");
    ExpectNear(snapshot.Summary().meanMilliseconds, 0.0, "empty mean");
    ExpectNear(snapshot.Summary().p99Milliseconds, 0.0, "empty p99");
    Expect(snapshot.IsWarmup(), "empty window preserves warm-up state");
}

void TestPercentileBoundaries() {
    questlab::perf::DurationRing<100> ring;
    for (int milliseconds = 1; milliseconds <= 100; ++milliseconds) {
        ring.AddSample({std::chrono::milliseconds(milliseconds)});
    }
    const auto summary = ring.Snapshot(
        "test", "boundaries", 0, 1, false).Summary();
    Expect(summary.count == 100, "percentile count");
    ExpectNear(summary.meanMilliseconds, 50.5, "percentile mean");
    ExpectNear(summary.p50Milliseconds, 50.0, "nearest-rank p50");
    ExpectNear(summary.p95Milliseconds, 95.0, "nearest-rank p95");
    ExpectNear(summary.p99Milliseconds, 99.0, "nearest-rank p99");
    ExpectNear(summary.maximumMilliseconds, 100.0, "percentile maximum");
}

void TestRingOverflow() {
    questlab::perf::DurationRing<3> ring;
    ring.AddSample({1ms});
    ring.AddSample({2ms});
    ring.AddSample({3ms});
    ring.AddSample({4ms});
    const auto summary = ring.Snapshot(
        "test", "overflow", 0, 1, false).Summary();
    Expect(ring.Size() == 3, "ring remains bounded");
    Expect(summary.overflowCount == 1, "ring reports overwrite");
    ExpectNear(summary.p50Milliseconds, 3.0, "ring retains latest values");
    ExpectNear(summary.maximumMilliseconds, 4.0, "ring latest maximum");
    ring.Clear();
    Expect(ring.Size() == 0, "clear removes samples");
    Expect(ring.OverflowCount() == 0, "clear resets overflow");
}

void TestSnapshotImmutability() {
    static_assert(
        !std::is_copy_assignable_v<questlab::perf::PerformanceSnapshot>,
        "Snapshots must not be assignable after publication");
    static_assert(
        std::is_copy_constructible_v<questlab::perf::PerformanceSnapshot>,
        "Snapshots remain cheap value objects");
    questlab::perf::DurationRing<1> ring;
    ring.AddSample({5ms});
    const auto snapshot = ring.Snapshot(
        "frame", "update", 100, 200, false);
    ring.Clear();
    Expect(snapshot.Category() == "frame", "snapshot category is retained");
    Expect(snapshot.MetricName() == "update", "snapshot metric is retained");
    Expect(snapshot.Summary().count == 1, "snapshot owns its summary");
}

void TestRateLimiting() {
    questlab::perf::ReportCadence cadence(1s);
    const auto start = questlab::perf::SteadyClock::time_point(10s);
    Expect(!cadence.ShouldReport(start), "first cadence call starts window");
    Expect(!cadence.ShouldReport(start + 999ms), "cadence blocks early report");
    Expect(cadence.ShouldReport(start + 1s), "cadence opens at interval");
    Expect(!cadence.ShouldReport(start + 1s), "cadence blocks duplicate report");
    Expect(cadence.ShouldReport(start + 2s), "cadence opens next interval");
    cadence.Reset();
    Expect(!cadence.ShouldReport(start + 3s), "cadence reset starts new window");
}

void TestDisabledInstrumentation() {
    questlab::perf::DurationRing<2> ring;
    {
        questlab::perf::ScopedDuration<2> timer(&ring, false);
        QUESTLAB_ATRACE_SCOPE("disabled_trace_string_must_not_be_retained");
    }
    Expect(ring.Size() == 0, "disabled scoped duration records nothing");
}

}  // namespace

int main() {
    TestEmptyWindow();
    TestPercentileBoundaries();
    TestRingOverflow();
    TestSnapshotImmutability();
    TestRateLimiting();
    TestDisabledInstrumentation();
    if (failures == 0) {
        std::puts("All performance telemetry tests passed");
    }
    return failures == 0 ? 0 : 1;
}
