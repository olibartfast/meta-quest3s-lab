# Milestone 8 — Persistent Spatial Anchors

## Goal

Create one world-locked marker, persist it on the Quest, and restore it at the
same physical location after the application or headset restarts.

This milestone turns the in-session `LOCAL` pose from Milestones 6 and 7 into a
runtime-owned spatial anchor. It deliberately covers local, single-user
persistence only.

## Repository baseline

Milestone 8 builds on the accepted repository-owned stack:

- `xr_core` owns the OpenXR instance, session, events, reference spaces, and
  predicted-display-time frame loop.
- `xr_meta_passthrough` supplies the passthrough underlay and lifecycle.
- `xr_controller_actions` supplies controller poses, triggers, primary buttons,
  and haptics.
- `vulkan_renderer` renders world-space debug geometry.
- Milestone 6 demonstrates controller placement in `LOCAL`.
- Milestone 7 demonstrates a second per-frame subsystem and asynchronous user
  interaction without changing the renderer contract.

Create:

- `libs/xr_spatial_anchors`
- `apps/08-spatial-anchors`
- `docs/spatial-anchors.md`

Extend:

- `xr_core` with event fan-out so passthrough and anchors can observe the same
  OpenXR event stream.
- Gradle settings, deployment tooling, and Android CI for app 08.

## API decision

### Quest backend for this milestone

Use Meta's production entity/component anchor model:

- `XR_FB_spatial_entity` for anchor creation, UUIDs, and component state.
- `XR_META_spatial_entity_persistence` for batched save and erase.
- `XR_FB_spatial_entity_query` for UUID-filtered restore from local storage.

The Android manifest must contain:

```xml
<uses-permission android:name="com.oculus.permission.USE_ANCHOR_API" />
```

Do not request `IMPORT_EXPORT_IOT_MAP_DATA`. It is required for sharing and
cloud scenarios, neither of which is in Milestone 8.

### Why not use the new portable extension yet

OpenXR now defines `XR_EXT_spatial_entity`, `XR_EXT_spatial_anchor`, and
`XR_EXT_spatial_persistence`. However:

- The Quest runtime observed during Milestone 7 did not advertise these
  extensions.
- Meta's current native Quest documentation still specifies the FB/META
  entity/component APIs.
- Runtime anchor extensions can be hidden until `USE_ANCHOR_API` is declared,
  so app 08 must log the post-permission extension set on device.

Keep the application-facing state free of FB/META structs so a future backend
can adopt the portable extensions without changing rendering or interaction.

### Why restore with FB query

Meta recommends `XR_META_spatial_entity_discovery` for modern single-user
discovery, but the repository-pinned OpenXR Android headers at version 1.1.51
do not contain that extension. They do contain `XR_FB_spatial_entity_query`,
which supports a local-storage location and UUID filter.

Milestone 8 therefore restores the one app-owned UUID with a targeted local
query. Do not perform an unfiltered query because it can return unrelated scene
or system anchors. A later dependency-upgrade milestone can replace this path
with META discovery.

References:

- [Meta Spatial Anchors overview](https://developers.meta.com/horizon/llmstxt/documentation/native/android/openxr-spatial-anchors-overview.md)
- [Meta Spatial Anchors API reference](https://developers.meta.com/horizon/llmstxt/documentation/native/android/openxr-spatial-anchors-api-ref.md)
- [Meta anchor developer setup](https://developers.meta.com/horizon/llmstxt/documentation/native/android/openxr-spatial-anchors-developer-setup.md)
- [Khronos `XR_FB_spatial_entity`](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_FB_spatial_entity.html)
- [Khronos `XR_EXT_spatial_anchor`](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_EXT_spatial_anchor.html)

## 1. Add event fan-out

Anchor creation, component changes, save, query, and erase all complete through
OpenXR events. Passthrough already consumes events through `XrEventObserver`,
but `XrSessionContext::PollEvents` currently accepts only one observer.

Add an `XrEventFanout` implementation in `xr_core`:

```cpp
class XrEventFanout final : public XrEventObserver {
public:
    void AddObserver(XrEventObserver* observer);
    bool HandleEvent(const XrEventDataBuffer& event) override;
    void Clear();

private:
    std::vector<XrEventObserver*> observers_;
};
```

Requirements:

- Preserve registration order.
- Ignore null and duplicate observers.
- Stop and return false when an observer returns false.
- Own no observers and document that they must outlive the fan-out.
- Keep the existing `PollEvents(XrEventObserver*)` signature unchanged.

App 08 registers passthrough first and the anchor manager second, then passes the
fan-out to `PollEvents`.

## 2. Define a portable anchor state

Create `MetaSpatialAnchorManager` in `libs/xr_spatial_anchors`. Keep all
function pointers, request IDs, `XrSpace` handles, and Meta event structs
private.

Expose:

```cpp
struct AnchorUuid {
    std::array<std::uint8_t, XR_UUID_SIZE> bytes{};
};

enum class AnchorLifecycle {
    Empty,
    Restoring,
    Creating,
    EnablingStorage,
    Saving,
    Ready,
    Erasing,
    Error,
};

struct SpatialAnchorState {
    AnchorLifecycle lifecycle = AnchorLifecycle::Empty;
    AnchorUuid uuid{};
    math::Pose pose{};
    bool hasUuid = false;
    bool persisted = false;
    bool positionValid = false;
    bool orientationValid = false;
    bool positionTracked = false;
    bool orientationTracked = false;
    XrResult lastResult = XR_SUCCESS;
};
```

The manager:

- implements `XrEventObserver`;
- receives `XrFrameUpdateInfo` from the app scene each frame;
- supports `RequestCreate`, `RequestErase`, and automatic restore;
- exposes one read-only `SpatialAnchorState`;
- allows only one asynchronous operation at a time.

Although `XrResult` remains in the public diagnostic state, no vendor-specific
types do.

## 3. Initialize and verify capabilities

`Initialize` takes the instance, system ID, session, and app-private storage
directory. It must:

1. Query `XrSystemSpatialEntityPropertiesFB`.
2. Query `XrSystemSpacePersistencePropertiesMETA`.
3. Require both `supportsSpatialEntity` and `supportsSpacePersistence`.
4. Resolve all extension functions with `xrGetInstanceProcAddr`.
5. Load and validate the optional stored UUID record.
6. Remain idle until the OpenXR session is running.

Resolve:

- `xrCreateSpatialAnchorFB`
- `xrGetSpaceUuidFB`
- `xrEnumerateSpaceSupportedComponentsFB`
- `xrGetSpaceComponentStatusFB`
- `xrSetSpaceComponentStatusFB`
- `xrSaveSpacesMETA`
- `xrEraseSpacesMETA`
- `xrQuerySpacesFB`
- `xrRetrieveSpaceQueryResultsFB`

Log the extension versions and system capability flags. Fail initialization
clearly if any required extension or function is unavailable.

The required instance extensions are:

```cpp
XR_FB_SPATIAL_ENTITY_EXTENSION_NAME
XR_META_SPATIAL_ENTITY_PERSISTENCE_EXTENSION_NAME
XR_FB_SPATIAL_ENTITY_QUERY_EXTENSION_NAME
```

App 08 also requests `XR_FB_PASSTHROUGH_EXTENSION_NAME`.

## 4. Store the app-owned UUID safely

The runtime persists the anchor, but the app remains responsible for associating
content with its UUID. Store one versioned UUID record under
`ANativeActivity::internalDataPath`, not shared storage.

Suggested format:

```text
questlab-anchor-v1
7A9C...32 uppercase hexadecimal characters...
```

Add host-testable helpers:

```cpp
std::string FormatAnchorUuid(const AnchorUuid& uuid);
bool ParseAnchorUuid(std::string_view text, AnchorUuid* uuid);
```

Tests cover:

- exact 16-byte round trip;
- upper- and lowercase input;
- wrong length;
- non-hex characters;
- missing or unknown version;
- trailing newline handling.

Write through a temporary file in the same directory and rename it atomically.
Do not update the record until the runtime save-complete event succeeds. Delete
it only after erase completes successfully.

If saving the UUID record fails after the runtime persisted the anchor, request
a compensating erase and report `Error`; do not silently leave an anchor with
no content mapping.

If the record is malformed, log the problem, quarantine or remove it, and enter
`Empty`. Do not use an unfiltered query as recovery.

## 5. Restore at session start

When the first running frame arrives:

- If no UUID record exists, remain `Empty`.
- Otherwise enter `Restoring` and call `xrQuerySpacesFB`.
- Use `XR_SPACE_QUERY_ACTION_LOAD_FB`.
- Chain a local-storage filter and an exact UUID filter.
- Set `maxResultCount` to one.

Handle the query events by matching their request ID:

1. On `XR_TYPE_EVENT_DATA_SPACE_QUERY_RESULTS_AVAILABLE_FB`, retrieve results
   with the two-call idiom.
2. Reject zero results, multiple results, or a mismatched UUID.
3. Retain the returned `XrSpace`.
4. Check the `LOCATABLE` component.
5. If necessary, enable it asynchronously with
   `xrSetSpaceComponentStatusFB`.
6. Enter `Ready` only after the component is enabled.
7. On `XR_TYPE_EVENT_DATA_SPACE_QUERY_COMPLETE_FB`, verify the final result and
   ensure a matching result was received.

Query initiation success is not operation success; both the immediate
`XrResult` and completion-event result must be checked.

If the saved UUID is no longer in runtime storage, remove the stale local
record and return to `Empty`, allowing the user to create a replacement.

## 6. Create and persist an anchor

Allow creation only from `Empty` while the session is running and the proposed
pose is valid.

Call `xrCreateSpatialAnchorFB` with:

- `space = frame.baseSpace` (`LOCAL`);
- the selected pose in that space;
- `time = frame.predictedDisplayTime`.

Creation flow:

```text
Empty
  → Creating
  → EnablingStorage
  → Saving
  → Ready
```

On `XrEventDataSpatialAnchorCreateCompleteFB`:

- match the creation request ID;
- check the event result;
- retain its `XrSpace` and UUID;
- enumerate supported components;
- require `LOCATABLE` and `STORABLE`;
- verify `LOCATABLE` is enabled;
- enable `STORABLE` with `xrSetSpaceComponentStatusFB`.

On the matching `XrEventDataSpaceSetStatusCompleteFB`, verify that `STORABLE`
is enabled, then call `xrSaveSpacesMETA` with the one anchor.

On `XrEventDataSpacesSaveResultMETA`:

- check the request ID and result;
- atomically write the UUID record;
- set `persisted = true`;
- enter `Ready`.

Unexpected, stale, or duplicate events are logged and ignored. A failed stage
enters `Error`, destroys any ephemeral `XrSpace`, and leaves enough diagnostic
state to explain the failure.

## 7. Locate every frame

While an anchor space exists, call:

```cpp
xrLocateSpace(anchorSpace, frame.baseSpace, frame.predictedDisplayTime, ...)
```

Copy pose and the four validity/tracking flags into `SpatialAnchorState`.

Rendering rules:

- Render attached content only when position and orientation are valid.
- Use bright green when both components are tracked.
- Use dim green when valid but inferred.
- Hide the anchor marker when invalid; retain lifecycle diagnostics.
- Log only validity transitions, not every frame.

The app renders directly from the anchor pose. It must not keep rendering the
old `LOCAL` placement pose after anchor creation succeeds, because that would
mask localization failures.

## 8. Erase and destroy safely

Allow erase from `Ready` or recoverable `Error` when a UUID or space exists.

Use `xrEraseSpacesMETA`, preferring the live `XrSpace` and falling back to the
UUID when necessary. On a successful erase-complete event:

1. Delete the local UUID record.
2. Destroy the live `XrSpace` with `xrDestroySpace`.
3. Clear all state.
4. Return to `Empty`.

Erasing persistent storage and destroying the runtime handle are separate
operations. Do not delete the local record or destroy the handle merely because
the erase request was accepted.

At application shutdown, destroy any live `XrSpace` regardless of persistence
state. This releases the runtime instance but must not erase a saved anchor.

## 9. Build app 08

Create `apps/08-spatial-anchors` from the Milestone 6 passthrough and controller
placement structure.

Application configuration:

- package: `com.olibartfast.questlab.spatialanchors`
- label/OpenXR name: `Spatial Anchors`
- native library: `spatial_anchors`
- log tag: `SpatialAnchors`

Interaction:

- Show a yellow placement guide and controller preview while `Empty`.
- A trigger rising edge creates the anchor at that controller's preview pose.
- Disable new placement while an async operation is pending.
- A or X erases the current persistent anchor.
- On startup, automatically restore the UUID from app-private storage.
- Do not allow an existing anchor to be dragged. Erase and recreate it instead;
  anchor mutation is outside this milestone.
- Pulse the creating controller once creation is accepted, then use visual
  status to communicate the asynchronous result.

Visual state:

- yellow: no anchor / placement guide;
- amber: creating, enabling, saving, restoring, or erasing;
- green: persisted and currently locatable;
- dim green: locatable but inferred;
- red: operation failed;
- hidden marker plus a logged transition: temporarily unlocatable.

Keep passthrough and frame-cadence logging from app 06.

## 10. Documentation and repository integration

`docs/spatial-anchors.md` must explain:

- anchor space versus `LOCAL` reference space;
- ephemeral, persisted, erased, and destroyed states;
- UUID/content association;
- required permission and possible user consent;
- the selected FB/META backend;
- why the new EXT backend and META discovery are deferred;
- lighting, mapping, storage, permission, and rate-limit errors;
- the three-metre content-to-anchor guidance from Meta;
- uninstall/orphan behavior and the need to erase test anchors first.

Add app 08 to:

- `settings.gradle`;
- `scripts/build_deploy.sh`;
- `.github/workflows/android-ci.yml`;
- Gradle cache inputs;
- APK artifact upload.

Build command:

```bash
./scripts/build_deploy.sh --app 08-spatial-anchors
adb logcat -s SpatialAnchors:V OpenXR:V '*:S'
```

## 11. Validation

### Host validation

- UUID codec and record parser tests pass.
- Event fan-out ordering, duplicate suppression, and failure propagation are
  tested.
- Anchor lifecycle transition tests use a small pure state reducer or fake
  backend; do not rely exclusively on headset testing for request correlation.
- Existing `xr_math` and `xr_interaction` tests continue to pass.

### Android validation

- App 08 and all apps 01–07 build.
- Legacy passthrough still builds.
- APK contains `USE_ANCHOR_API`, passthrough, ARM64, and the correct native
  activity metadata.
- CI uploads the app 08 APK.

### Quest validation

1. Confirm the permission-gated runtime exposes all three required extensions.
2. Accept any anchor/local-file permission prompt.
3. Create one anchor and observe all async stages complete.
4. Walk around the marker and verify it remains world-locked.
5. Pause/resume and verify localization recovers.
6. Force-stop and relaunch; verify automatic UUID restore at the same physical
   location.
7. Reboot the Quest and verify restore again.
8. Temporarily occlude tracking or reduce lighting; verify the marker hides
   without losing the saved UUID and returns when localization recovers.
9. Erase the anchor, relaunch, and verify it does not return.
10. Run three create/save/relaunch/erase cycles without leaked spaces, stale
    request IDs, crashes, or OpenXR errors.
11. Confirm steady frame cadence remains near the selected headset refresh
    interval.

If the post-permission runtime lacks a required extension, stop device
implementation and record the exact advertised extension list. Do not silently
fall back to a `LOCAL` pose and call it persistent.

## Definition of Done

- The user can create one anchor-backed marker.
- Creation, component enablement, persistence, query, and erase results are
  correlated and visible in logs/state.
- The marker renders from `xrLocateSpace(anchorSpace, LOCAL, predictedTime)`.
- The marker restores after app restart and headset reboot at the same physical
  location.
- Erase prevents future restoration.
- Temporary tracking loss is safe and does not destroy persistence.
- Passthrough lifecycle and frame cadence remain healthy.
- Apps 01–07 and the legacy sample build without regression.

## Explicitly deferred

- Shared anchors, group UUIDs, user IDs, and cloud storage.
- `IMPORT_EXPORT_IOT_MAP_DATA` and Enhanced Spatial Services.
- Multiple anchors and content catalogs.
- Scene anchors, room layout, semantic labels, and plane/mesh discovery.
- Editing or moving an existing anchor.
- Automatic recovery of anchors orphaned by app-data deletion or uninstall.
- Portable `XR_EXT_spatial_anchor` backend.
- `XR_META_spatial_entity_discovery` until the pinned OpenXR dependency exposes
  it and the Quest runtime advertises it.
