#pragma once

#define QUESTLAB_PERF_DETAIL_JOIN_INNER(left, right) left##right
#define QUESTLAB_PERF_DETAIL_JOIN(left, right) \
    QUESTLAB_PERF_DETAIL_JOIN_INNER(left, right)

#if defined(QUEST_ENABLE_PERFETTO_TRACING) && defined(__ANDROID__)

#include <android/trace.h>

namespace questlab::perf {

class ATraceSpan final {
public:
    explicit ATraceSpan(const char* name) noexcept
        : enabled_(ATrace_isEnabled()) {
        if (enabled_) {
            ATrace_beginSection(name);
        }
    }

    ~ATraceSpan() {
        if (enabled_) {
            ATrace_endSection();
        }
    }

    ATraceSpan(const ATraceSpan&) = delete;
    ATraceSpan& operator=(const ATraceSpan&) = delete;

private:
    bool enabled_ = false;
};

}  // namespace questlab::perf

#define QUESTLAB_ATRACE_SCOPE(name) \
    ::questlab::perf::ATraceSpan QUESTLAB_PERF_DETAIL_JOIN( \
        questlabTraceSpan_, __LINE__)(name)

#else

// The argument is deliberately discarded by the preprocessor. Disabled
// builds retain neither a per-frame object nor its trace-section string.
#define QUESTLAB_ATRACE_SCOPE(name) static_cast<void>(0)

#endif
