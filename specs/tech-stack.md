# Tech Stack

| Layer | Choice | Version | Notes |
|-------|--------|---------|-------|
| Language | C++ | C++17 | `-Wall -Wextra -Wpedantic`; native application layer, no engine |
| Target devices | Meta Quest 3 / Quest 3S | — | ARM64 (`arm64-v8a`) only |
| XR API | OpenXR loader for Android | 1.1.51 | `org.khronos.openxr:openxr_loader_for_android`, consumed via prefab |
| Vendor extensions | Meta OpenXR extensions | — | Passthrough, spatial anchors, environment depth; isolated behind `libs/xr_meta_*` |
| Rendering | Vulkan | Android system loader | Stereo swapchains from OpenXR graphics requirements |
| Platform | Android NativeActivity | compileSdk/targetSdk 34, minSdk 34 | Java bridge only for permissions and Camera2 |
| NDK | Android NDK | r29 (`29.0.14206865`) | `ANDROID_STL=c++_static` |
| Native build | CMake + Ninja | 3.22.1 | Per-app `CMakeLists.txt`; `libs/` added as subdirectories |
| Android build | Gradle + AGP | 8.5 / 8.2.0 | Java 21 toolchain, Gradle runtime Java 17, `sourceCompatibility` 17 |
| Build variants | debug / release / benchmark | — | `benchmark` = release, debug-signed, `.benchmark` suffix |
| Camera | Android Camera2 (Meta Passthrough Camera API) | — | `libs/camera_source`, Meta and replay adapters behind one interface |
| Depth | `XR_META_environment_depth` | — | `libs/depth_source`, capability-checked adapter |
| Inference runtime | ONNX Runtime | 1.21.0 | Prebuilt archive, pinned SHA-256, `CPUExecutionProvider`, 1 intra/inter-op thread |
| Model | RF-DETR Nano (ONNX) | rfdetr 1.9.0, opset 17 | `1x3x384x384` NCHW, sigmoid scores, no NMS; identity verified at load |
| Model export | Python + uv | 3.11, torch 2.10.0+cpu | `tools/rfdetr_export`, pinned via `uv.lock` and `model-manifest.json` |
| Host reference oracle | `tools/rfdetr_inference` | — | C++ reference the on-device path is measured against |
| Telemetry | `libs/perf_telemetry` + Android ATrace | — | Allocation-free sampling; Perfetto spans behind `QUEST_ENABLE_PERFETTO_TRACING` |
| Integrity | `libs/artifact_integrity` | — | Checksums for provisioned models and recorded fixtures |
| Testing | Host CTest binaries with `assert` | — | No external test framework; device acceptance is manual and documented |
| CI | GitHub Actions | `ubuntu-22.04` | Native Android builds for every app on `master` and `agent/**` |
| Deployment | `scripts/build_deploy.sh` + ADB | — | Builds, installs, provisions models, grants permissions, launches |

Toolchain versions are declared once in `scripts/toolchain_config.sh`. Change them there,
not in individual `build.gradle` files. Do not introduce Unity, Unreal, or a large
third-party engine.
