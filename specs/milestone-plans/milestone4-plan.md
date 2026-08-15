# Milestone 4 — Controller Input and Interaction

## Goal

Point at and select a world-space object with either Quest controller using
portable OpenXR actions.

## 0. Close Milestone 3

Before implementation:

- Investigate the Meta-menu Quit failure.
- Pass one orderly teardown cycle.
- Align each app's OpenXR application name with its Android label;
  `XrInstanceContext` currently always reports `OpenXR Bootstrap`.
- Confirm master CI passes.
- Mark Milestone 3 complete only afterward.

## 1. Add reusable controller actions

Create `XrControllerActions` in `xr_core`.

Actions shared through left/right subaction paths:

- Grip pose
- Aim pose
- Trigger value
- Squeeze value
- Thumbstick `Vector2`
- Primary button: X/A
- Secondary button: Y/B
- Thumbstick click
- Core vibration output

Initialization order:

1. Create hand paths and action set.
2. Create every action.
3. Suggest bindings.
4. Attach the action set once.
5. Create left/right grip and aim action spaces.

OpenXR requires actions and bindings to be declared before attachment; action
sets become immutable afterward.

Reference:
[Meta Input API](https://developers.meta.com/horizon/documentation/native/android/mobile-openxr-input/)

## 2. Binding profiles

Fully bind the tested profile:

```text
/interaction_profiles/oculus/touch_controller
```

Bindings:

- `/input/grip/pose`
- `/input/aim/pose`
- `/input/trigger/value`
- `/input/squeeze/value`
- `/input/thumbstick`
- `/input/thumbstick/click`
- Left X/Y and right A/B
- `/output/haptic`

Also suggest a limited Khronos simple-controller fallback for aim, grip,
select, and haptics.

Do not bind the Meta/system button; it is normally reserved by the runtime.
The Touch profile and supported paths are defined by the
[OpenXR specification](https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html).

## 3. Frame integration

Add an optional frame-update interface:

```cpp
struct XrFrameUpdateInfo {
    XrTime predictedDisplayTime;
    XrSpace baseSpace;
};

class XrFrameUpdater {
public:
    virtual bool UpdateFrame(const XrFrameUpdateInfo&) = 0;
};
```

`XrSessionContext::PumpFrame()` will:

1. Wait and begin the frame.
2. Call `xrSyncActions` once.
3. Query all action states.
4. Locate active grip/aim spaces in LOCAL using predicted display time.
5. Render.

Inactive or invalid controllers must disappear when the Meta dashboard opens.
Meta explicitly requires checking both pose-action activity and location
validity.

## 4. Interaction logic

Create a small reusable, host-testable interaction module:

- `Ray`
- Ray/AABB intersection
- Trigger hysteresis
- Rising-edge selection
- Per-hand hover state

Behaviour:

- Aim rays originate from controller aim poses and point along local `-Z`.
- A wireframe cube sits approximately two metres ahead.
- Hovering changes the cube from white to yellow.
- Trigger press while hovering selects it and changes it to green.
- Selection produces a short core `XrHapticVibration`.
- Primary button clears selection.

Core vibration needs no Meta extension.

Reference:
[Meta haptic guidance](https://developers.meta.com/horizon/documentation/native/android/mobile-openxr-haptic/)

## 5. Extend debug rendering

Add procedural debug shapes:

- Ray
- Wireframe box
- Configurable line colour
- Existing axes remain unchanged

Render:

- Small grip-pose axes for each active controller.
- Aim ray for each valid aim pose.
- Target cube with hover/selection colour.

Do not add controller models, textures, a depth buffer, or vendor rendering
extensions.

## 6. Application and documentation

Add:

```text
apps/04-controller-input
libs/xr_interaction
docs/openxr-actions.md
```

Application:

- ID: `com.olibartfast.questlab.controllerinput`
- Label: `Controller Input`
- Log tag: `ControllerInput`
- Deployment:

```bash
./scripts/build_deploy.sh --app 04-controller-input
```

CI will build app 04 and run host interaction tests.

## Acceptance

- Both controllers appear and track independently.
- Grip axes and aim rays use the correct poses.
- Trigger, squeeze, thumbstick, X/Y, A/B, and stick-click states are logged on
  change.
- Either controller can hover and select the cube.
- Selection produces visual feedback and haptics.
- Controllers disappear while the system dashboard owns input.
- Three clean launch/quit cycles.
- No OpenXR/Vulkan errors or leaked handles.
