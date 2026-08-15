# Milestone 4 — Controller Input and Interaction

## Goal

Point at and select a world-space object with either Quest controller using portable OpenXR actions. Build reusable action-management and interaction abstractions that compose with the existing `libs/xr_core`, `libs/xr_math`, and `libs/vulkan_renderer` stack from Milestones 2–3.

## Architecture

```
libs/xr_core/                           libs/xr_interaction/
├── include/xr_core/                    ├── include/xr_interaction/
│   ├── xr_actions.h     (new)         │   ├── ray.h
│   ├── xr_controller_state.h (new)    │   ├── intersection.h
│   └── xr_frame_updater.h (new)       │   └── selection.h
├── src/                                ├── src/
│   ├── xr_actions.cpp   (new)         │   ├── ray.cpp
│   ├── xr_session.cpp   (extend)      │   ├── intersection.cpp
│   └── xr_frame_updater.cpp (new)     │   └── selection.cpp
│                                       ├── tests/
│                                       │   ├── test_intersection.cpp
│                                       │   └── test_selection.cpp
apps/04-controller-input/               └── CMakeLists.txt
├── CMakeLists.txt
├── main.cpp                            docs/
├── README.md                           └── openxr-actions.md
└── src/main/
    ├── AndroidManifest.xml
    ├── cpp/
    │   └── main.cpp
    └── build.gradle
```

### Component relationship

```
main.cpp
  ├── XrInstanceContext        (existing, libs/xr_core)
  ├── VulkanSessionBinding     (existing, libs/xr_core)
  ├── XrSessionContext         (existing — extended with action sync + controller state)
  │     ├── xrSyncActions     (new, via XrActionManager)
  │     ├── leftHand          (new, XrHandState)
  │     ├── rightHand         (new, XrHandState)
  │     └── PumpFrame()       (extended — calls action sync, locates controllers)
  ├── XrActionManager          (new, libs/xr_core/xr_actions)
  │     ├── action set, actions, bindings
  │     ├── lazy action spaces (aim/grip, left/right)
  │     ├── query: bool, float, vec2, pose-active
  │     └── haptic output
  ├── SelectionManager         (new, libs/xr_interaction/selection)
  │     ├── per-hand Ray from aim pose
  │     ├── Ray/AABB intersection test
  │     ├── trigger hysteresis (rising-edge detect)
  │     └── hover/select state machine
  ├── VulkanSwapchain          (from M2)
  ├── VulkanPipeline           (from M2 — extended with line-list debug shapes)
  │     ├── axes (world, head, stage)  — from M3
  │     ├── grip axes (per controller) — new
  │     ├── aim rays (per controller)  — new
  │     └── target cube (wireframe)    — new
  └── Render loop
        ├── xrWaitFrame
        ├── xrBeginFrame
        ├── xrSyncActions → query all states → locate controller spaces
        ├── update SelectionManager (rays, intersections, hover/select)
        ├── apply haptics on selection
        ├── locate head + views (xrLocateViews)
        ├── render: world axes + head axes + stage floor         (M3)
        ├── render: grip axes + aim rays + target cube           (M4 new)
        └── xrEndFrame
```

## Portability approach

- **Two binding profiles** — primary profile `/interaction_profiles/oculus/touch_controller` for Quest Touch controllers; fallback profile `/interaction_profiles/khr/simple_controller` for generic 3DOF/6DOF devices. Full bindings on Touch, subset on simple controller (aim pose, grip pose, select boolean, haptics).
- **No Meta controller rendering extension** — grip poses and aim rays are rendered as procedural debug geometry (coloured axes + line segments). No `XR_MSFT_controller_model` or `XR_FB_render_model`.
- **`XrHandState` is runtime-agnostic** — a pure-OpenXR POD struct. No Meta-specific fields or enums.
- **Interaction logic is host-testable** — `libs/xr_interaction` depends only on `xr_math` (no OpenXR linkage on host). `SelectionManager` state machine, `Ray/AABB` intersection, and trigger hysteresis run in plain C++ tests.
- **Meta dashboard input-grab behaviour** — when the system dashboard owns input, pose actions report `isActive == false`. The app responds by clearing controller state and hiding grip axes/aim rays. No explicit dashboard-detection logic needed.

## Component breakdown — `libs/xr_core/` extensions

### `xr_actions.h / xr_actions.cpp` — `XrActionManager`

| Responsibility | Detail |
|----------------|--------|
| **Hand paths** | Resolve `/user/hand/left` and `/user/hand/right` at init. |
| **Action set** | Create single action set `"controller_actions"` with priority 1. |
| **Actions** | Trigger value (`XR_ACTION_TYPE_FLOAT_INPUT`), squeeze value (`FLOAT`), thumbstick (`VECTOR2F`), primary button (`BOOLEAN`), secondary button (`BOOLEAN`), thumbstick click (`BOOLEAN`), aim pose (`POSE_INPUT`), grip pose (`POSE_INPUT`). All pose and scalar actions use left+right subaction paths. |
| **Suggested bindings** | Two `XrInteractionProfileSuggestedBinding` calls: Touch profile (full mapping) and simple-controller profile (subset). Bindings declared before `xrAttachSessionActionSets`. |
| **Action spaces** | Lazy-create left/right aim spaces and left/right grip spaces on the first frame after session becomes active and `xrSyncActions` returns successfully. |
| **Per-frame sync** | `SyncAll(XrTime predictedDisplayTime)` → calls `xrSyncActions` once, queries all action states, locates all active spaces in `baseSpace`, fills `XrHandState&` for each hand. |
| **Haptics** | `ApplyHaptic(XrPath hand, float amplitude, XrDuration duration)` → `xrApplyHapticFeedback`. Core vibration, no Meta extension needed. |
| **Query accessors** | `GetBool()`, `GetFloat()`, `GetVector2()`, `IsPoseActive()` — thin wrappers over `xrGetActionState*`. |

### `xr_controller_state.h` — `XrHandState`

```cpp
namespace questlab {

struct XrHandState {
    bool active = false;
    bool aimPoseValid = false;
    bool gripPoseValid = false;
    XrMath::Pose aimPose;
    XrMath::Pose gripPose;
    float triggerValue = 0.0f;
    float squeezeValue = 0.0f;
    XrMath::Vector2 thumbstick;
    bool primaryPressed = false;
    bool secondaryPressed = false;
    bool thumbstickClicked = false;
};

}  // namespace questlab
```

All fields reset to default before each `SyncAll()` call. `aimPoseValid` is true only when the pose action `isActive` AND `XR_SPACE_LOCATION_POSITION_VALID_BIT` are both true.

### `xr_frame_updater.h` — `XrFrameUpdater`

```cpp
struct XrFrameUpdateInfo {
    XrTime predictedDisplayTime;
    XrSpace baseSpace;
    const XrHandState& leftHand;
    const XrHandState& rightHand;
};

class XrFrameUpdater {
public:
    virtual ~XrFrameUpdater() = default;
    virtual bool UpdateFrame(const XrFrameUpdateInfo& info) = 0;
};
```

`XrSessionContext::PumpFrame()` calls `UpdateFrame()` on registered updaters after action sync and controller-space location. This decouples interaction logic from rendering.

### `XrSessionContext` integration

`PumpFrame()` is extended to:

1. `xrWaitFrame` + `xrBeginFrame`
2. `XrActionManager::SyncAll(predictedDisplayTime, localSpace_)` → fills `leftHand_`, `rightHand_`
3. For each registered `XrFrameUpdater` → `UpdateFrame(info)`
4. `xrLocateViews(headSpace_, predictedDisplayTime)` → populates views
5. Call renderer with `frameRenderInfo` (now containing controller state)
6. `xrEndFrame`

## Component breakdown — `libs/xr_interaction/`

### `ray.h / ray.cpp`

```cpp
struct Ray {
    XrMath::Vector3 origin;
    XrMath::Vector3 direction;   // must be unit-length
};
```

Construct from aim pose: `origin = aimPose.position`, `direction = aimPose.orientation.Rotate(Vector3{0, 0, -1})` (forward along local -Z).

### `intersection.h / intersection.cpp`

```cpp
struct AABB {
    XrMath::Vector3 center;
    XrMath::Vector3 halfExtents;
};

std::optional<float> IntersectRayAABB(const Ray& ray, const AABB& box);
// Returns t at first intersection, or nullopt if no hit.
// Uses slab method (Kay & Kajiya).
```

Host-testable: assert `IntersectRayAABB(ray_through_box)` returns a value, `IntersectRayAABB(ray_away_from_box)` returns nullopt.

### `selection.h / selection.cpp`

```cpp
class SelectionManager {
public:
    enum class Hand : uint8_t { Left, Right };

    enum class TargetState : uint8_t { Idle, Hovered, Selected };

    void Update(const Ray& leftRay,
                const Ray& rightRay,
                const AABB& target,
                float leftTrigger,
                float rightTrigger);

    TargetState State() const { return state_; }

    bool SelectionTriggered() const;    // rising edge, resets after read

    Hand SelectingHand() const { return selectingHand_; }

private:
    TargetState state_ = TargetState::Idle;
    Hand selectingHand_ = Hand::Right;
    bool selectionEdge_ = false;
    bool triggerWasPressed_[2] = {false, false};
};
```

**State machine:**

```
                   hover hover-
                    ↓     ↓
    Idle ──────────→ Hovered ───→ Idle
    ↑                 │
    │ primary button  │ trigger rising edge
    │ (clear)         ↓
    └────────────── Selected
         (no hover or primary)
```

**Trigger hysteresis**: Selection fires on rising edge (`trigger > 0.8 && !triggerWasPressed_`). Deselect waits until trigger falls below 0.2 before re-arming. Primary button clears selection immediately on rising edge.

**Test cases:**
- No intersection → `TargetState::Idle`
- Intersecting ray + low trigger → `TargetState::Hovered`
- Intersecting ray + trigger cross 0.8 → `TargetState::Hovered`
- Intersecting ray + trigger held >0.8 second frame → `TargetState::Selected`
- Already selected + primary button press → `TargetState::Idle`

## Debug rendering extensions

Extend `libs/vulkan_renderer` debug-line support with three new shape types:

| Shape | Geometry | Colour |
|-------|----------|--------|
| **Grip pose axes** | 3 lines (RGB X/Y/Z, length 0.03m) from grip position, oriented by grip rotation | Fixed RGB per axis |
| **Aim ray** | 1 line segment, origin at aim position, length 2.0m, direction along aim orientation -Z | White (idle) / Yellow (hovering) |
| **Wireframe cube** | 12 edges of unit cube, scaled 0.15m, centered at target position ~2m ahead | White (idle) / Yellow (hovered) / Green (selected) |

- Vertices pushed into a per-frame dynamic `VkBuffer` (host-visible, pre-allocated to worst-case capacity).
- Single indexed draw call via `vkCmdDrawIndexed` using `VK_PRIMITIVE_TOPOLOGY_LINE_LIST`.
- Topology and colour are vertex attributes (position + packed RGBA8 colour per vertex). No per-draw uniform updates needed for colour changes.

## Build system

- `libs/xr_interaction/CMakeLists.txt` — static library `xr_interaction`, links `xr_math`. Host-testable with `QUESTLAB_BUILD_TESTS=ON`.
- `libs/xr_core/CMakeLists.txt` — (refactored from inline compilation) static library `xr_core` now includes `xr_actions.cpp`, `xr_frame_updater.cpp`.
- `apps/04-controller-input/CMakeLists.txt` — shared module `controller_input`, links `xr_core`, `vulkan_renderer`, `xr_math`, `xr_interaction`, `android_native_app_glue`, `android`, `log`, `vulkan`, `OpenXR::openxr_loader`.
- `apps/04-controller-input/build.gradle` — package `com.olibartfast.questlab.controllerinput`. Library name `controller_input`. Label `Controller Input`. Log tag `ControllerInput`.
- `apps/04-controller-input/src/main/AndroidManifest.xml` — NativeActivity, immersive HMD, vulkan feature.
- `settings.gradle` — add `include(":apps:04-controller-input")`.
- `scripts/build_deploy.sh` — add `--app 04-controller-input` option.

## App flow (`main.cpp`)

```
Android entry → attach JNI
→ XrInstanceContext.Initialize()
→ VulkanSessionBinding.Initialize()     // device, queue, command pool
→ XrSessionContext.Initialize()         // session, LOCAL, VIEW, STAGE spaces
→ XrActionManager.Initialize()          // action set, actions, bindings, attach
→ VulkanSwapchain.Initialize()          // per-eye color swapchains
→ VulkanPipeline.Initialize()           // render pass, framebuffers, pipelines
→ SelectionManager()                    // init with target AABB at (0, 1.65, -2.0)

→ event loop:
    poll Android
    poll OpenXR events
    when running:
        xrWaitFrame → predictedDisplayTime
        xrBeginFrame
        actionManager.SyncAll(predictedDisplayTime, localSpace)
            → fills leftHand, rightHand
            → locates active controller spaces in LOCAL

        // build rays from aim poses
        if leftHand.aimPoseValid:
            leftRay = {leftHand.aimPose.position, aimForward(leftHand.aimPose)}
        if rightHand.aimPoseValid:
            rightRay = {rightHand.aimPose.position, aimForward(rightHand.aimPose)}

        selectionManager.Update(leftRay, rightRay, targetAABB,
                                leftHand.triggerValue, rightHand.triggerValue)

        if selectionManager.SelectionTriggered():
            actionManager.ApplyHaptic(selectingHand, 0.5f, 50'000'000)

        // locate head + views
        xrLocateViews(headSpace, predictedDisplayTime) → views

        // build render info for VulkanStereoRenderer
        renderInfo.leftHand = leftHand
        renderInfo.rightHand = rightHand
        renderInfo.gripAxesColor = [fixed]
        renderInfo.rayColors = [white|yellow depending on hover]
        renderInfo.targetCube = {center: (0,1.65,-2), halfExt: 0.15, color: per state}

        renderer.RenderFrame(renderInfo)  // stereo render pass, both eyes
        xrEndFrame(projection layers)

    // log input state on change (INFO level)
    if inputStateChanged():
        LOGI("L: trig=%.2f squeeze=%.2f stick=(%.2f,%.2f) prim=%d sec=%d stickclk=%d pose=%d",
             ...)
        LOGI("R: trig=%.2f squeeze=%.2f stick=(%.2f,%.2f) prim=%d sec=%d stickclk=%d pose=%d",
             ...)

→ shutdown:
    selectionManager.~SelectionManager()
    actionManager.Shutdown()            // destroy action spaces, action set
    XrSessionContext.Shutdown()
    VulkanSessionBinding.Shutdown()
    XrInstanceContext.Shutdown()
```

## `docs/openxr-actions.md`

Document covering:

1. **OpenXR action model**: action sets, actions, suggested bindings, interaction profiles, subaction paths.
2. **`XrActionManager` API**: initialization order, per-frame sync, query methods, haptic output.
3. **Binding table**: Touch controller paths → actions, simple-controller paths → actions.
4. **Controller space conventions**: aim vs grip pose, local forward direction (-Z for aim), coordinate frames.
5. **Pose validity**: checking `isActive` AND `XR_SPACE_LOCATION_POSITION_VALID_BIT`.
6. **Meta dashboard interaction**: how input-grab affects action states (controllers disappear, sync continues).
7. **Haptics**: core vibration API, amplitude/duration, no Meta extension required.
8. **Debug output**: examples of logcat output for input state changes.

## Definition of Done

1. Both controllers appear and track independently in the headset.
2. Grip pose axes (RGB X/Y/Z) render at each active controller's grip position.
3. Aim rays (white lines) extend 2m from each active controller's aim pose along local -Z.
4. Trigger, squeeze, thumbstick, primary button, secondary button, and stick-click states are logged on change at INFO level.
5. A wireframe cube is rendered ~2m ahead of the user.
6. Either controller ray intersecting the cube turns it yellow (hover).
7. Trigger press (crossing 0.8 threshold) while hovering turns the cube green (selected) and vibrates the selecting controller.
8. Primary button clears the selection, returning the cube to white.
9. Controllers disappear (no grip axes, no aim rays) while the Meta system dashboard owns input.
10. Three clean launch/quit cycles without crashes or leaked handles.
11. No OpenXR or Vulkan validation errors.
12. Host interaction tests (`test_intersection`, `test_selection`) pass.

## Risks & Open Questions

- **Binding before attach**: OpenXR requires all suggested bindings before `xrAttachSessionActionSets`. The action set is immutable after attachment. The plan's init order enforces this.
- **Simple-controller runtime support**: The Khronos simple-controller profile may not be supported by all runtimes. The app should check `XrInteractionProfileState::isActive` after `xrSyncActions` and log a warning if neither profile is active, but not crash.
- **Dynamic vertex buffer sizing**: Pre-allocating worst-case debug-line vertex capacity avoids per-frame allocations. Worst case: 24 vertices (grip axes: 2 hands × 6 verts × 3 axes) + 4 vertices (aim rays: 2 hands × 2 verts) + 24 vertices (wireframe cube: 12 edges × 2 verts) = 52 vertices. Allocate buffer for 128 vertices at 16 bytes each = 2KB.
- **`xr_core` static library refactoring**: If `libs/xr_core/CMakeLists.txt` hasn't been created by M3, it must be done now so that `xr_actions.cpp` and `xr_frame_updater.cpp` compile as part of the library rather than inline in each app. This is a one-time refactor.
- **XrInstanceContext application name**: The plan section 0 in the existing `specs/milestone-plans/milestone4-plan.md` notes that `XrInstanceContext` always reports `"OpenXR Bootstrap"`. This should be parameterized so each app sets its own name. Add a `SetApplicationName()` or constructor parameter.
- **Ray representation**: Rendering rays as 2m line segments works for nearby targets but rays conceptually extend infinitely. AABB intersection tests the infinite ray regardless of the rendered length. This is correct.
- **Concurrent grip axes + aim rays**: Both grip axes and aim rays for each hand are rendered simultaneously. They shouldn't be toggled independently in M4.
