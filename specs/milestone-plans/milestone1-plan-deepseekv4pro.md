# Milestone 1 — Native OpenXR Bootstrap

## Goal

Create the smallest application owned by this repository that correctly manages the Android and OpenXR lifecycle.

## Architecture

```
libs/xr_core/                        apps/01-openxr-bootstrap/
├── include/xr_core/                 ├── CMakeLists.txt
│   ├── xr_error.h                   ├── main.cpp
│   ├── xr_logging.h                 ├── README.md
│   ├── xr_extensions.h              ├── Projects/Android/
│   ├── xr_instance.h                │   ├── build.gradle
│   ├── xr_session.h                 │   ├── settings.gradle
│   └── xr_reference_space.h         │   ├── AndroidManifest.xml
├── src/                             │   └── java/.../NativeActivity.java
│   ├── xr_error.cpp                 └── assets/
│   ├── xr_logging.cpp                   └── donotedelete.txt
│   ├── xr_extensions.cpp
│   ├── xr_instance.cpp
│   ├── xr_session.cpp
│   └── xr_reference_space.cpp
└── CMakeLists.txt
```

## Portability approach

- **No Meta-specific types** — standard C++ (`std::vector`, `uint32_t`) plus OpenXR types
- **`xr_linear.h`** relocated to `libs/xr_core/include/` (Khronos Apache 2.0, portable)
- **`FromXrTime()`/`ToXrTime()`** inlined in `xr_logging.h` (trivial nanos-to-seconds conversion)
- **Optional Meta extensions** (perf settings, thread settings) gated behind `#if defined(ANDROID)` — no hard dependency
- **EGL/GLES3** graphics binding (same pattern as existing code, works on Quest and other Android XR devices)

## Component breakdown

| File | Responsibility |
|------|---------------|
| `xr_logging` | Cross-platform `LOGE`/`LOGV`/`LOGI` macros |
| `xr_error` | `XrCheck()` macro wrapping `xrResultToString` |
| `xr_extensions` | Enumerate & validate instance extensions, log all |
| `xr_instance` | Loader init, `XrInstance` creation, system query, runtime logging |
| `xr_session` | `XrSession` creation with EGL binding, event loop, session state transitions |
| `xr_reference_space` | Enumerate & create VIEW/LOCAL/STAGE spaces |

## App flow (`main.cpp`)

```
Android entry → attach JNI → xrInitializeLoaderKHR
→ enumerate extensions → validate required
→ create XrInstance → query XrSystemId
→ get GLES graphics requirements → create EGL context
→ create XrSession → enumerate view configs → create reference spaces
→ event loop:
    poll Android events → poll XR events
    state transitions: READY→xrBeginSession, STOPPING→xrEndSession, EXITING→exit
    minimal frame loop when active (wait/begin/end frame, no rendering)
→ cleanup: spaces → session → EGL → instance → detach JNI
```

## Build system

- `libs/xr_core/CMakeLists.txt` — static library
- `apps/01-openxr-bootstrap/CMakeLists.txt` — shared module, links `xr_core`
- New Gradle project under `apps/01-openxr-bootstrap/Projects/Android/` with package `com.openxr.bootstrap`
- Same OpenXR AAR dependency (`org.khronos.openxr:openxr_loader_for_android:1.1.51`)

## Definition of Done

The application launches on Quest 3, enters an active OpenXR session, logs lifecycle transitions, and exits without validation errors or leaked OpenXR handles.
