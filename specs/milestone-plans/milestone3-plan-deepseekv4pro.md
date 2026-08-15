# Milestone 3 — Tracking and Coordinate Systems

## Goal

Understand and expose the spatial foundations required for XR and computer-vision integration. Support `VIEW`, `LOCAL`, and `STAGE` reference spaces; read and log head pose; render coordinate axes; and create a reusable, portable C++ math library that wraps OpenXR transform types safely.

## Architecture

```
libs/xr_math/                           apps/03-head-pose/
├── include/xr_math/                    ├── CMakeLists.txt
│   ├── xr_math_types.h                ├── main.cpp
│   ├── xr_math_vector3.h              ├── README.md
│   ├── xr_math_quat.h                 ├── src/main/
│   ├── xr_math_pose.h                 │   ├── AndroidManifest.xml
│   ├── xr_math_matrix4.h              │   └── cpp/
│   └── xr_math_projections.h         │       └── main.cpp
├── src/                                └── build.gradle
│   ├── xr_math_vector3.cpp
│   ├── xr_math_quat.cpp              docs/
│   ├── xr_math_pose.cpp              └── coordinate-systems.md
│   ├── xr_math_matrix4.cpp
│   └── xr_math_projections.cpp
├── tests/
│   ├── test_vector3.cpp
│   ├── test_quat.cpp
│   ├── test_pose.cpp
│   └── test_matrix4.cpp
└── CMakeLists.txt
```

### Component relationship

```
main.cpp
  ├── XrInstanceContext         (existing, libs/xr_core)
  ├── VulkanSessionBinding      (existing, libs/xr_core — with command pool per M2)
  ├── XrSessionContext          (existing, libs/xr_core — extended)
  │     ├── HeadSpace  (new — XR_REFERENCE_SPACE_TYPE_VIEW)
  │     ├── LocalSpace (existing)
  │     ├── StageSpace (new — XR_REFERENCE_SPACE_TYPE_STAGE, optional)
  │     └── LocateHeadPose()    (new — xrLocateSpace HeadSpace→LocalSpace)
  │         → returns XrPosef (head-in-local)
  ├── VulkanSwapchain           (from M2, libs/vulkan_renderer)
  ├── VulkanPipeline            (from M2, extended with axes geometry)
  ├── XrMath::Matrix4           (new, libs/xr_math)
  │     ├── CreateFromPose()
  │     ├── CreateProjectionFov()
  │     └── CreateFromRigidTransform()
  ├── XrMath::Pose              (new, libs/xr_math)
  │     ├── Compose(A, B) → A * B
  │     ├── Invert()
  │     └── TransformVector3()
  └── Render loop
        ├── xrWaitFrame
        ├── xrBeginFrame
        ├── LocateHeadPose → log position/orientation to logcat
        ├── xrLocateViews (in HeadSpace)
        ├── build view matrices from eye poses via xr_math
        ├── build projection matrices from FOVs via xr_math
        ├── render axes at world origin (LOCAL space, stable)
        ├── render axes at head position (VIEW space, follows user)
        └── if STAGE available → render floor square at stage origin
```

## Portability approach

- **Math library wraps `xr_linear.h`** — The Khronos-provided `xr_linear.h` is portable C99 code under the Apache 2.0 license. `libs/xr_math/` provides a thin, safe C++ layer over it: `XrMath::Vector3`, `XrMath::Quat`, `XrMath::Pose`, `XrMath::Matrix4`. All storage is column-major (`float data[16]` for matrices, matching `xr_linear.h` semantics and Vulkan shader layout).
- **`GraphicsAPI` tag for projections** — `XrMath::CreateProjectionFov(fov, near, far, api)` takes an explicit `GraphicsAPI` enum matching the Vulkan clip-space conventions (`[0,1]` depth, no Y-flip). This pins portability at the call site rather than in a global.
- **No OVR:: dependency** — `libs/xr_math/` does not include or reference `OVR_Math.h`. The legacy `OvrFromXr()` helpers remain in the legacy app only. New code uses `xr_math` directly.
- **`STAGE` space is optional** — queried via `xrGetReferenceSpaceBoundsRect`; if unavailable, the app renders without a floor square and logs the absence. No crash.
- **`VIEW` space is required** — all stereo view locations use `VIEW` space per OpenXR best practice. `LOCAL` space exists for the stable world origin. `STAGE` is additive.

## Component breakdown — `libs/xr_math/`

| Header | Wraps | Provides |
|--------|-------|----------|
| `xr_math_types.h` | (standalone) | `namespace XrMath {}` with `GraphicsAPI` enum, `#include <openxr/openxr.h>`, forward declarations, typedef for `float` precision |
| `xr_math_vector3.h/.cpp` | `XrVector3f` | `XrMath::Vector3` — construct from x/y/z, `Dot`, `Cross`, `Length`, `Normalized`, `Lerp`, `operator+`, `operator-`, `operator*` (scalar), `operator==` tol-erance, implicit `operator XrVector3f&()` and `FromXr`/`ToXr` |
| `xr_math_quat.h/.cpp` | `XrQuaternionf` | `XrMath::Quat` — `Identity()`, `FromAxisAngle()`, `Normalized`, `Inverted`, `Rotate(Vector3)`, `Lerp`, `Slerp`, `operator*` (compose + rotate vector), `ToXr`/`FromXr` |
| `xr_math_pose.h/.cpp` | `XrPosef` | `XrMath::Pose` — `Identity()`, `FromPositionOrientation(pos, quat)`, `Compose(A, B)` → A * B (B first, then A), `Inverse()`, `Transform(Vector3)`, `TransformDirection(Vector3)`, `operator*` (compose), `ToXr`/`FromXr` |
| `xr_math_matrix4.h/.cpp` | `XrMatrix4x4f` | `XrMath::Matrix4` — `Identity()`, `FromPose(Pose)`, `FromRigidTransform(Pose)`, `FromTranslation(Vector3)`, `FromRotation(Quat)`, `FromTRS(trans, rot, scale)`, `Transpose()`, `Inverse()`, `InverseRigid()`, `operator*` (multiply + transform Vector3), `GetTranslation()`, `GetRotation()`, `ToXr`/`FromXr` |
| `xr_math_projections.h/.cpp` | `XrMatrix4x4f` + `XrFovf` | `XrMath::CreateProjectionFov(fov, nearZ, farZ, api)` → Matrix4, `CreateLookAt(eye, center, up, api)` → Matrix4 |

### Design rules

1. Every `XrMath::*` type is trivially copyable and `sizeof(XrMath::Vector3) == sizeof(XrVector3f)`. Layout matches the underlying OpenXR type for zero-copy passage to OpenXR functions.
2. All functions that return a new value return by value (RVO). No heap allocations.
3. Operator overloading follows C++ convention: `a * b` for composition, `pose * vec` for transformation.
4. Column-major matrix indexing: `mat(column, row)` accessor. Data member `float m[16]` in column-major order.
5. Default constructors produce identity/zero. No uninitialized state.
6. No exceptions. Functions that can fail (e.g. matrix inversion of singular matrix) return `false` via an output parameter.

## Component breakdown — `XrSessionContext` extensions

| Addition | Purpose |
|----------|---------|
| `XrSpace headSpace_` | Created at init with `XR_REFERENCE_SPACE_TYPE_VIEW` |
| `XrSpace stageSpace_` | Created at init with `XR_REFERENCE_SPACE_TYPE_STAGE` if available |
| `bool hasStage_` | True when stage space creation succeeds |
| `std::vector<XrView> views_` | Allocated at init with correct view count (2 for primary stereo) |
| `XrViewConfigurationView[] configViews_` | Cached view config views (per-eye resolution, sample count) |
| `LocateHeadPose(predictedDisplayTime)` | `xrLocateSpace(headSpace_, localSpace_, time, &loc)` → returns `XrMath::Pose` or `std::optional<Pose>`. Logs position + Euler angles each frame at `VERBOSE` level. |
| `LocateViews(predictedDisplayTime, viewsOut, countOut)` | `xrLocateViews` using `headSpace_` (not `localSpace_`) as the view space. Returns `XrViewStateFlags`. |
| `LocateStage(predictedDisplayTime)` | `xrLocateSpace(stageSpace_, localSpace_, time, &loc)` → returns `XrMath::Pose` if `hasStage_`. |

## App flow (`main.cpp`)

```
Android entry → attach JNI
→ create XrInstanceContext
→ init VulkanSessionBinding (instance, device, queue, command pool)
→ init XrSessionContext (session, LOCAL space, VIEW space, STAGE space if available)
→ enumerate view config views → create swapchains (per eye)
→ create render pass → create framebuffers
→ load SPIR-V shaders → create pipeline
→ create uniform buffer (UBO: 2x view matrix, 2x projection matrix)
→ create vertex buffer (axes geometry: 3 coloured lines X/Y/Z at origin)
→ allocate frame UBO mapped memory

→ event loop:
    poll Android
    poll OpenXR events
    when running:
        xrWaitFrame → predictedDisplayTime
        xrBeginFrame
        XrMath::Pose headPose = LocateHeadPose(predictedDisplayTime)
            → LOGI("head: pos=%.3f %.3f %.3f orient=%.3f %.3f %.3f %.3f", ...)
        xrLocateViews(headSpace, predictedDisplayTime) → per-eye viewMatrices + FOVs
        build per-eye view matrices: Matrix4::FromRigidTransform(Inverse(eyePose))
        build per-eye proj matrices: CreateProjectionFov(eyeFov, 0.1f, 0.0f, GRAPHICS_VULKAN)
        upload view+proj to UBO per eye
        for each eye:
            acquire swapchain → begin command buffer → begin render pass (clear grey)
            → bind pipeline → bind descriptor set (eye UBO)
            → draw axes: model = worldOrigin (Identity → stable in LOCAL space)
            → draw small axes: model = headPose (translates+rotates with user head)
            → if hasStage: draw stage floor square at stagePose
            → end render pass → end command buffer → submit → release swapchain
        xrEndFrame(projectionLayers: 2 eyes, each with its swapchain + pose + fov)

→ shutdown: destroy spaces (head, stage, local) → destroy session → ... → detach JNI
```

### Axis visualization

- X axis = red line from origin to `(+0.2, 0, 0)`
- Y axis = green line from origin to `(0, +0.2, 0)`
- Z axis = blue line from origin to `(0, 0, -0.2)` (OpenXR convention: -Z is forward)
- Vertex buffer holds 6 positions (2 per axis), coloured per-vertex in the vertex data.
- World axes drawn at `model = Matrix4::Identity()` — stable in LOCAL space.
- Head-aligned axes drawn at `model = Matrix4::FromPose(headPose)` — moves with user.
- Stage floor square drawn at `model = Matrix4::FromPose(stagePose)` — a small cyan quad in the XZ plane (Y=0) to mark the floor.

### Log output design

Each frame at `VERBOSE` level:
```
HeadPose: pos=( 0.12,  1.65, -0.08), orient=( 0.00,  0.71,  0.00,  0.71) yaw=90.0° pitch=0.0° roll=0.0°
STAGE:    pos=( 0.00,  0.00,  0.00), bounds=(2.0m x 2.0m)
```

At `INFO` level once per second:
```
FPS: 72 | Head X: 0.12m | All spaces valid: VIEW=yes LOCAL=yes STAGE=yes
```

## `docs/coordinate-systems.md`

Document covering:

1. **OpenXR coordinate conventions**: +Y up, +X right, -Z forward, right-handed. Units: meters for position, radians for rotation angles.
2. **Reference space hierarchy**: VIEW (head-locked) → LOCAL (world origin at app start, gravity-aligned) → STAGE (floor-level, bounded). Diagram showing each space and the transforms between them.
3. **Pose semantics**: A pose `p` returned by `xrLocateSpace(A, B, ...)` is the position and orientation of space A expressed in space B. That is, `Pose = Transform from B to A`. Applying this pose to a point in A yields the point in B.
4. **Transform multiplication order**: `P3 = P1 * P2` means "apply P2 first, then P1". Matrix multiplication is right-to-left: `M1 * M2 * v` means `v` is transformed by M2, then by M1.
5. **View matrix construction**: `view = inverse(eyePoseInWorld)`. The eye pose from `xrLocateViews` is eye-in-viewSpace. Compose with head-in-local to get eye-in-local, then invert.
6. **Projection matrix conventions**: Vulkan clip space `[0,1]` depth, right-handed NDC. FOV tangents map directly. No Y-flip (unlike OpenGL).
7. **Conversion between OpenXR types and `xr_math`**: zero-copy `reinterpret_cast`-safe because layout matches. `ToXr()`/`FromXr()` convenience methods.
8. **Common gotchas**: column-major vs row-major storage, quaternion `(x,y,z,w)` order (matches OpenXR, differs from some libraries), `predictedDisplayTime` must match between `xrWaitFrame` and `xrLocateViews`, `VIEW` space orientation validity without position validity.

## Tests (`libs/xr_math/tests/`)

Host-compilable C++ tests using a minimal test harness (no external framework):

| Test | Coverage |
|------|----------|
| `test_vector3.cpp` | Construction, addition, subtraction, scalar multiply, dot product, cross product, length, normalization, lerp, equality with tolerance |
| `test_quat.cpp` | Identity, axis-angle create, normalize, invert, rotate vector, compose `q1 * q2`, slerp midpoint, quat-to-rotation-matrix round-trip |
| `test_pose.cpp` | Identity, compose `A * B`, invert `(A.Inverse() * A ≈ Identity)`, transform point, transform direction, compose correctness (transform by B then A) |
| `test_matrix4.cpp` | Identity, from pose, from rigid transform, from TRS, multiply, transpose, inverse rigid, inverse general, transform point, decompose to translation/rotation |

Tests compilable on Linux host with:
```bash
clang++ -std=c++17 -I../../include test_vector3.cpp -o test_vector3 && ./test_vector3
```

All tests can be built via `libs/xr_math/CMakeLists.txt` with a `QUESTLAB_BUILD_TESTS` option defaulting OFF for Android builds and ON for host builds.

## Build system

- `libs/xr_math/CMakeLists.txt` — static library `xr_math`, no dependencies beyond OpenXR headers (for `XrVector3f` etc.). Optionally builds `xr_math_tests` executable on host.
- `apps/03-head-pose/CMakeLists.txt` — shared module `head_pose`, links `xr_math`, `vulkan_renderer` (or inline sources), `android_native_app_glue`, `android`, `log`, `vulkan`, `OpenXR::openxr_loader`.
- `apps/03-head-pose/build.gradle` — mirrors `02-vulkan-stereo-triangle`. Package `com.olibartfast.questlab.headpose`. Library name `head_pose`.
- `apps/03-head-pose/src/main/AndroidManifest.xml` — NativeActivity, immersive HMD intent, vulkan feature declared.
- `settings.gradle` — add `include(":apps:03-head-pose")`.
- `scripts/build_deploy.sh` — add `--app 03-head-pose` option.
- `apps/01-openxr-bootstrap/CMakeLists.txt` and `apps/02-vulkan-stereo-triangle/CMakeLists.txt` — **refactor** to add `xr_math` include path and link dependency if needed. Milestone 3 is the point where `xr_math` becomes a shared dependency for all future apps.

### Refactoring note

By Milestone 3, `libs/xr_core` sources are still compiled inline in each app's CMakeLists.txt (no static library target exists). This is acceptable but should be noted as technical debt. If time permits, create `libs/xr_core/CMakeLists.txt` as a static library in this milestone to clean up the dependency graph:

```
app CMakeLists.txt → links xr_core (static) + vulkan_renderer (static) + xr_math (static)
```

## Definition of Done

1. `libs/xr_math/` builds as a static library with all types and functions documented above.
2. All host-side tests pass (`test_vector3`, `test_quat`, `test_pose`, `test_matrix4`).
3. `apps/03-head-pose/` builds, installs, and launches on Quest 3.
4. Head pose is logged to logcat at `VERBOSE` level every frame, showing position and orientation.
5. Three coloured coordinate axes (red X, green Y, blue Z) are rendered at the LOCAL origin and remain stable in the room as the user moves.
6. A second set of axes tracks the user's head position.
7. If STAGE space is available, a floor square is rendered at stage origin.
8. Per-eye view and projection matrices are constructed using `xr_math` types.
9. `adb logcat -s HeadPose` shows structured head-pose output.
10. Three clean launch/exit cycles without crashes or leaked handles.
11. `docs/coordinate-systems.md` is complete and covers all sections listed above.

## Risks & Open Questions

- **STAGE space availability**: Quest 3 requires a guardian boundary to be set up before `STAGE` space is available. If no boundary is configured, stage creation fails gracefully. The app must handle this at init without crashing.
- **VIEW space orientation only**: `XR_SPACE_LOCATION_ORIENTATION_VALID_BIT` is always set for VIEW space, but `XR_SPACE_LOCATION_POSITION_VALID_BIT` may not be. The app should still render view-aligned content using head-pose position from LOCAL space.
- **Predicted display time consistency**: `xrLocateSpace` and `xrLocateViews` must both use the same `predictedDisplayTime` from `xrWaitFrame`. Using different timestamps produces visibly jittering content.
- **Euler angle extraction**: Logging Euler angles requires `Quat::GetYawPitchRoll()`. Ensure correct convention (OpenXR quaternion order is `x,y,z,w`; the OVR math library already provides this conversion — port the algorithm to `xr_math` without copying OVR_Math.h).
- **Test framework dependency**: The plan uses a minimal hand-rolled test harness to avoid introducing a test framework dependency. If more than ~10 test cases are needed, consider adding Catch2 or doctest as a single-header drop into `libs/xr_math/tests/`.
- **Column-major matrix in UBO**: Vulkan shaders expect column-major matrices. The `xr_math::Matrix4` storage is column-major (matching `xr_linear.h`), so memcpy into the UBO is a direct byte copy. Confirmed by checking `XrMatrix4x4f` layout vs GLSL `mat4` layout.
