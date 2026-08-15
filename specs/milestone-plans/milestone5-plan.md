# Milestone 5 — Passthrough Mixed Reality

## Goal

Display one repository-rendered Vulkan object over Quest passthrough using
`XR_FB_passthrough`.

## Scope

Create:

- `apps/05-passthrough`
- `libs/xr_core/include/xr_core/passthrough_interface.h`
- `libs/xr_meta_passthrough`
- `docs/passthrough.md`

Exclude Camera2 access, passthrough styling, projected geometry, triangle
meshes, depth occlusion, anchors, and object placement.

## 1. Extend OpenXR instance configuration

- Allow `XrInstanceOptions` to request additional extensions.
- App 05 requires `XR_FB_passthrough`.
- Query `XrSystemPassthroughProperties2FB`.
- Fail clearly when passthrough capability is unavailable.
- Add the required Android manifest feature:

```xml
<uses-feature
    android:name="com.oculus.feature.PASSTHROUGH"
    android:required="true" />
```

No camera permission is needed because the compositor owns the imagery.

Reference:
[Meta implementation guide](https://developers.meta.com/horizon/llmstxt/documentation/native/android/mobile-passthrough.md)

## 2. Add generic composition hooks

Extend `XrSessionContext` with:

- An optional OpenXR event observer.
- An optional underlay-layer provider.
- Support for submitting passthrough before the Vulkan projection layer.
- Preserve strict `xrWaitFrame → xrBeginFrame → xrEndFrame` pairing.
- Submit zero layers when `shouldRender == false`.

## 3. Implement `MetaPassthroughFB`

The portable interface exposes:

```cpp
Initialize(...)
SetActive(bool)
HandleEvent(...)
AppendUnderlay(...)
Shutdown()
```

The Meta implementation will:

- Resolve required function pointers.
- Create exactly one passthrough feature.
- Create one reconstruction layer.
- Start/resume only while Android is resumed and the XR session runs.
- Pause on Android pause or session stopping.
- Handle recoverable, restored, reinitialization-required, and fatal events.
- Destroy the layer before the feature.

Only `XR_FB_passthrough` is required; `XR_FB_triangle_mesh` remains excluded.

Reference:
[Khronos extension reference](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_FB_passthrough.html)

## 4. Add transparent Vulkan composition

Add a renderer option for mixed reality:

- Clear swapchains to transparent black.
- Keep virtual lines fully opaque.
- Set projection flags for source-alpha and unpremultiplied-alpha composition.
- Submit layers in this order:

```text
Passthrough reconstruction underlay
Vulkan projection layer
```

Keep `XR_ENVIRONMENT_BLEND_MODE_OPAQUE`, as required by Meta.

## 5. Create app 05

Configuration:

- ID: `com.olibartfast.questlab.passthrough`
- Label: `Passthrough Mixed Reality`
- Log tag: `PassthroughMR`
- Deployment:

```bash
./scripts/build_deploy.sh --app 05-passthrough
```

Visual output:

- Physical environment as background.
- Bright wireframe cube fixed two metres ahead at initial eye height.
- Optional small world axes for stability verification.
- No controller input yet.

## 6. Performance discipline

- Keep exactly one active passthrough layer.
- Pause resources whenever passthrough is not visible.
- Device-test at 72 Hz on Quest 3/3S.
- Defer explicit refresh-rate control unless testing shows it is necessary.

Meta recommends minimizing active layers, pausing unused resources, and using
72 Hz for camera/display synchronization.

Reference:
[Meta passthrough best practices](https://developers.meta.com/horizon/llmstxt/documentation/native/android/mobile-passthrough-bp.md)

## Verification

- Run ShellCheck and strict toolchain diagnostics.
- Build apps 01–05 and legacy `XrPassthrough`.
- Verify manifest package, label, and passthrough feature.
- Confirm the real-world background and stable Vulkan cube.
- Open and close the Meta dashboard and verify pause/resume.
- Perform three launch/quit cycles.
- Confirm logs show correct layer order and reverse-order cleanup.
- Confirm no OpenXR or Vulkan errors.

Milestone 5 is complete only after Quest visual and lifecycle acceptance.
