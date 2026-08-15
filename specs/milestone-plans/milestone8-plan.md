# Milestone 8 — Persistent Spatial Anchors

## Goal

Create one world-locked marker, save it on the Quest, restore it at the same
physical location after app and headset restarts, and erase it explicitly.

Milestone 8 replaces the in-session `LOCAL` pose used by app 06 with a
runtime-owned spatial anchor. It covers local, single-user persistence only.

## Comparison with the DeepSeek proposal

The DeepSeek proposal is preserved at
`specs/milestone-plans/milestone8-plan-deepseekv4pro.md`.

Adopt from it:

- one-anchor scope;
- Meta's entity/component anchor lifecycle;
- event fan-out for passthrough and anchor events;
- strict asynchronous request-ID correlation;
- app-private, atomically written UUID metadata;
- component checks before save and locate;
- visible pending, ready, inferred, invalid, and error states;
- restart, reboot, tracking-loss, and erase validation.

Refine for this repository:

- Use app 06 controller placement rather than adding hand tracking to the
  anchor milestone. This isolates anchor lifecycle behavior and reuses tested
  trigger, primary-button, pose, and haptic paths.
- Use `XR_FB_spatial_entity` for creation/components,
  `XR_META_spatial_entity_persistence` for save/erase, and targeted local
  `XR_FB_spatial_entity_query` for restore.
- Do not plan `XR_META_spatial_entity_discovery`: OpenXR Android 1.1.51 is
  pinned across the repo and its headers do not contain that extension.
- Do not plan the new portable `XR_EXT_spatial_anchor` backend: it was not
  advertised by the Quest runtime validated during Milestone 7.
- Treat the post-permission extension list as a device gate. Never fall back to
  rendering a `LOCAL` pose and report it as a persistent anchor.
- Store exactly one UUID and query exactly that UUID. An unfiltered query can
  return unrelated scene or system anchors.
- Keep vendor structs private so a portable backend can be added later.

## Scope

Create:

- `libs/xr_spatial_anchors`
- `apps/08-spatial-anchors`
- `docs/spatial-anchors.md`

Extend:

- `xr_core` with reusable event fan-out;
- Gradle settings;
- `scripts/build_deploy.sh`;
- Android CI and APK artifacts.

Exclude:

- shared or cloud anchors;
- group UUIDs and Meta user IDs;
- multiple anchors and content catalogs;
- scene anchors, rooms, planes, and meshes;
- moving an existing anchor;
- automatic orphan recovery after app-data deletion;
- portable EXT and META discovery backends.

## API and permission decision

Request these instance extensions:

```cpp
XR_FB_PASSTHROUGH_EXTENSION_NAME
XR_FB_SPATIAL_ENTITY_EXTENSION_NAME
XR_META_SPATIAL_ENTITY_PERSISTENCE_EXTENSION_NAME
XR_FB_SPATIAL_ENTITY_QUERY_EXTENSION_NAME
```

Declare:

```xml
<uses-permission android:name="com.oculus.permission.USE_ANCHOR_API" />
<uses-feature
    android:name="com.oculus.feature.PASSTHROUGH"
    android:required="true" />
```

Do not declare `IMPORT_EXPORT_IOT_MAP_DATA`; it is for sharing and cloud
storage. Local persistence does not require Enhanced Spatial Services.

At startup, log the permission-gated extension versions and query:

- `XrSystemSpatialEntityPropertiesFB::supportsSpatialEntity`;
- `XrSystemSpacePersistencePropertiesMETA::supportsSpacePersistence`.

If a required extension, function, or capability is absent, initialization
fails with an exact diagnostic.

References:

- [Meta Spatial Anchors overview](https://developers.meta.com/horizon/llmstxt/documentation/native/android/openxr-spatial-anchors-overview.md)
- [Meta Spatial Anchors API reference](https://developers.meta.com/horizon/llmstxt/documentation/native/android/openxr-spatial-anchors-api-ref.md)
- [Meta anchor developer setup](https://developers.meta.com/horizon/llmstxt/documentation/native/android/openxr-spatial-anchors-developer-setup.md)
- [Khronos `XR_FB_spatial_entity`](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_FB_spatial_entity.html)
- [Khronos `XR_EXT_spatial_anchor`](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_EXT_spatial_anchor.html)

## 1. Add OpenXR event fan-out

`XrSessionContext::PollEvents` currently accepts one `XrEventObserver`, while
app 08 needs both passthrough and the anchor manager to see events.

Add to `xr_core`:

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

Behavior:

- preserve registration order;
- ignore null and duplicate observers;
- stop and return false when an observer fails;
- own no observers;
- preserve the existing `PollEvents(XrEventObserver*)` API.

Register passthrough first and the anchor manager second.

Add host tests for ordering, duplicate suppression, null handling, and failure
propagation.

## 2. Add portable application-facing state

`MetaSpatialAnchorManager` owns all Meta/OpenXR details privately.

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
- accepts `XrFrameUpdateInfo` from the app scene;
- supports create, automatic restore, erase, locate, and shutdown;
- permits one outstanding async operation;
- exposes read-only state;
- ignores stale, duplicate, or unrelated request IDs.

Keep `XrSpace`, Meta structs, function pointers, and async request IDs private.

## 3. Initialize the Meta backend

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

`Initialize` takes the instance, system ID, session, and
`ANativeActivity::internalDataPath`.

It validates capabilities and loads the optional UUID record, but it does not
start query/create/save operations until the session is running.

## 4. Persist the app/content association

The runtime saves the anchor; the app must remember which UUID belongs to its
marker.

Store one versioned record under the app-private data directory:

```text
questlab-anchor-v1
7A9C...32 uppercase hexadecimal characters...
```

Provide host-testable helpers:

```cpp
std::string FormatAnchorUuid(const AnchorUuid& uuid);
bool ParseAnchorUuid(std::string_view text, AnchorUuid* uuid);
std::string SerializeAnchorRecord(const AnchorUuid& uuid);
bool ParseAnchorRecord(std::string_view text, AnchorUuid* uuid);
```

Test round trips, lowercase input, wrong length, non-hex input, unknown record
version, missing lines, and trailing newlines.

Write to a temporary file in the same directory and atomically rename it.

- Write only after the save-complete event succeeds.
- Delete only after the erase-complete event succeeds.
- If the record write fails after runtime save, issue a compensating erase and
  enter `Error`.
- If the record is malformed, log and quarantine/delete it, then enter
  `Empty`; do not run an unfiltered query.

## 5. Restore the saved anchor

On the first running frame:

1. If no UUID record exists, remain `Empty`.
2. Enter `Restoring`.
3. Call `xrQuerySpacesFB` with:
   - `XR_SPACE_QUERY_ACTION_LOAD_FB`;
   - local storage;
   - one exact UUID filter;
   - `maxResultCount = 1`.
4. Match query events by request ID.
5. On results available, retrieve results with the two-call idiom.
6. Reject zero, multiple, or mismatched results.
7. Retain the returned `XrSpace`.
8. Check and, if needed, enable the `LOCATABLE` component.
9. Enter `Ready` only after query completion and locatable enablement succeed.

Immediate API success means only that the operation started. Check the result
inside every completion event.

If the UUID no longer exists in runtime storage, remove the stale local record
and return to `Empty`.

## 6. Create and save

Permit creation only from `Empty`, with a valid controller preview pose and a
running session.

Call `xrCreateSpatialAnchorFB` with:

- `space = frame.baseSpace` (`LOCAL`);
- the preview pose in that space;
- `time = frame.predictedDisplayTime`.

Lifecycle:

```text
Empty
  → Creating
  → EnablingStorage
  → Saving
  → Ready
```

On create completion:

- match the request ID and check the event result;
- retain the returned `XrSpace` and UUID;
- enumerate components;
- require `LOCATABLE` and `STORABLE`;
- verify `LOCATABLE` is enabled;
- request `STORABLE` enablement.

On the matching component-status completion, call `xrSaveSpacesMETA`.

On save completion:

- verify request ID and event result;
- atomically save the UUID record;
- set `persisted = true`;
- enter `Ready`.

Any failure records `lastResult`, destroys an ephemeral live space when safe,
and enters `Error`. Never show the preview pose as though anchor creation
succeeded.

## 7. Locate every rendered frame

When a live anchor space exists:

```cpp
xrLocateSpace(
    anchorSpace,
    frame.baseSpace,
    frame.predictedDisplayTime,
    &location);
```

Copy the pose plus valid/tracked flags into `SpatialAnchorState`.

- Render only when position and orientation are valid.
- Bright green means tracked.
- Dim green means valid but inferred.
- Hide the marker when invalid.
- Log validity transitions only.

Render the marker from the located anchor pose, not the old placement pose.
This makes localization failures visible instead of masking them.

## 8. Erase and destroy

Allow erase when a UUID or live space exists and no other operation is pending.

Call `xrEraseSpacesMETA`, preferring the live space and using the UUID when no
space is loaded.

Only after successful erase completion:

1. remove the local UUID record;
2. destroy the live `XrSpace`;
3. clear state;
4. enter `Empty`.

Erasing persistence and destroying a runtime handle are different operations.
Shutdown always destroys the live space but never erases a saved anchor.

## 9. Build app 08

Create `apps/08-spatial-anchors` from app 06.

Configuration:

- package: `com.olibartfast.questlab.spatialanchors`
- label/OpenXR application name: `Spatial Anchors`
- native library: `spatial_anchors`
- log tag: `SpatialAnchors`

Interaction:

- `Empty`: show the yellow guide and controller previews.
- Trigger rising edge: create an anchor at that preview pose.
- A or X: erase the saved anchor.
- Pending operation: block new placement and erase requests.
- Startup: restore automatically when a UUID record exists.
- Existing anchor: erase and recreate rather than drag.
- Accepted creation: pulse the initiating controller.

Visual state:

- yellow: no anchor / placement guide;
- amber: restore, create, component enable, save, or erase pending;
- bright green: persisted and tracked;
- dim green: valid but inferred;
- red: failed;
- no marker: temporarily unlocatable.

Retain passthrough lifecycle and frame-cadence logging.

## 10. Repository integration and documentation

Add app 08 to:

- `settings.gradle`;
- `scripts/build_deploy.sh`;
- `.github/workflows/android-ci.yml`;
- Gradle cache inputs;
- APK artifact upload.

Create `docs/spatial-anchors.md` covering:

- anchor space versus `LOCAL`;
- ephemeral, persisted, erased, and destroyed states;
- UUID/content association;
- permission and consent behavior;
- FB/META backend choice;
- why EXT and META discovery are deferred;
- lighting, mapping, storage, permission, and rate-limit failures;
- Meta's recommendation to keep attached content within three metres of its
  anchor;
- uninstall/orphan behavior.

Commands:

```bash
./scripts/build_deploy.sh --app 08-spatial-anchors
adb logcat -s SpatialAnchors:V OpenXR:V '*:S'
```

## 11. Validation

Host:

- event fan-out tests;
- UUID codec and record parser tests;
- pure lifecycle/request-correlation tests;
- existing math and interaction tests.

Build:

- apps 01–08;
- legacy passthrough;
- shell checks;
- APK manifest/ABI/native-activity inspection;
- CI artifact upload.

Quest:

1. Confirm all required extensions appear after installing the APK with
   `USE_ANCHOR_API`.
2. Accept any anchor/local-storage consent prompt.
3. Create one anchor and observe create, component, and save completion.
4. Walk around it and verify world locking.
5. Pause/resume and verify recovery.
6. Force-stop/relaunch and verify restore.
7. Reboot/relaunch and verify restore at the same physical location.
8. Cause temporary tracking loss and verify the marker hides, then returns.
9. Erase, relaunch, and verify the anchor does not return.
10. Run three full create/save/restore/erase cycles without leaks, stale
    requests, crashes, or OpenXR errors.
11. Verify steady frame cadence near the selected refresh interval.

If the permission-gated runtime lacks a required extension, record the exact
list and stop. Do not substitute a `LOCAL` pose.

## Definition of Done

- One anchor can be created, saved, located, restored, and erased.
- The marker restores after app restart and headset reboot at the same physical
  location.
- Every async stage is correlated and visible in logs/state.
- Temporary tracking loss is safe.
- Erase prevents future restoration.
- Passthrough and frame cadence remain healthy.
- Apps 01–07 and the legacy sample build without regression.
