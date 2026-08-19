# Quest Camera Capture

## Architecture

Milestone 9 keeps Android camera ownership out of the XR renderer:

```text
QuestCameraActivity / Camera2
          |
          | owned Y, U, V byte arrays + metadata
          v
MetaCamera2Adapter -> LatestFrameQueue -> stride-aware YUV conversion
                                              |
                                              v
                                    Vulkan diagnostic quad
```

`IRgbCameraSource` is the portable boundary. Its captures contain frame and
sensor timestamps, dimensions, pixel format, owned planes with row and pixel
strides, intrinsics, and Camera2 calibration extrinsics. The reported rotation
maps Android sensor/head axes into camera optical axes, while the reported
translation is the optical-center position in sensor/head axes; together they
must not be treated as an ordinary rigid pose. The interface contains no Camera2, JNI,
OpenXR, or Vulkan handles.

`LatestFrameQueue` has capacity one by design. Publishing while a frame is
pending replaces the old frame and increments the overwrite counter. Camera
callbacks therefore never wait for the OpenXR render loop.

The replay adapter consumes the same contract. Its version-two manifest is a
whitespace-delimited record:

```text
QUEST_CAMERA_FIXTURE_V2 width height yRow yPixel uRow uPixel vRow vPixel y.bin u.bin v.bin
pixel_sha256 <lowercase SHA-256>
```

Plane paths must be plain relative filenames. The checksum covers a canonical
header containing the dimensions and six strides plus the lengths and exact
owned bytes of the Y, U, and V planes. Including padding bytes makes the
fixture immutable byte-for-byte without introducing a second colour
converter. Optional version-two metadata lines
store the sensor timestamp, five intrinsic values, five distortion values,
and Camera2 pose-rotation and pose-translation values. The format is intentionally
small and versioned; missing checksums, malformed geometry, path traversal,
unknown metadata, `V1`, and unknown future versions fail closed.

## Platform selection

The implementation follows Meta's current passthrough-camera guidance:

- require Horizon OS v76 or newer and Android API 34;
- request both `android.permission.CAMERA` and
  `horizonos.permission.HEADSET_CAMERA` at runtime;
- enumerate Camera2 IDs;
- read `com.meta.extra_metadata.camera_source` and accept source value `0`;
- prefer position value `0` (left), with the first passthrough source as a
  deterministic fallback;
- enumerate `YUV_420_888` sizes and use an explicitly requested size when
  supported, otherwise the largest size no wider than 1280 pixels.

Sources:

- [Meta Android Native Camera2 API](https://developers.meta.com/horizon/llmstxt/documentation/native/android/pca-native-documentation.md)
- [Android Camera2 API](https://developer.android.com/media/camera/camera2)
- [Android runtime permissions](https://developer.android.com/training/permissions/requesting)

## Lifecycle

The native application requests capture only while the activity is resumed.
Java owns the Camera2 callback thread and closes the repeating session,
camera device, image reader, and handler thread during pause. Resume creates a
fresh session. OpenXR and passthrough continue when permission is denied or
the camera is unavailable.

Each `Image` is closed on the callback thread after all three planes have been
copied. The callback publishes only repository-owned bytes.

## Diagnostic rendering

The CPU conversion handles independent row and pixel strides for all three
planes and converts limited-range BT.601 YUV to RGBA. The renderer uploads a
new RGBA image only when the camera frame ID changes, preserves aspect ratio,
and samples it on a head-relative Vulkan quad. Resolution changes recreate
the sampled image after the GPU becomes idle.

This is a correctness path, not the final performance path. A later milestone
may replace it with hardware-buffer import or shader-based YUV conversion
after camera ownership and color correctness pass device acceptance.

## Privacy and fixture workflow

Recording is opt-in. Pressing Volume Down arms exactly one frame, which is
written atomically to app-private storage. Logs reveal the path and byte
count, not pixel contents.

Before adding a fixture:

1. Capture a scene prepared for repository use.
2. List private files with `run-as`.
3. Pull only the selected capture.
4. Review every pixel for people, screens, documents, addresses, and other
   private content.
5. Record approval and calibration provenance.
6. Add the approved manifest and planes under a clearly named fixture
   directory.
7. Run the replay adapter and compare its RGBA output byte-for-byte with the
   live conversion output.

Milestone 10's reference generator performs an additional independent Python
decode and compares it byte-for-byte with the C++
`ConvertYuv420ToRgba` output before allowing pixels to reach RF-DETR.

## Validation checklist

- Build host tests and APK.
- Verify first-run grant and denial paths.
- Confirm logs select a real passthrough source without a hard-coded ID.
- Confirm correct orientation, aspect ratio, and color on the Vulkan quad.
- Pause/resume ten times and verify capture returns each time.
- Run capture for 15 minutes and confirm bounded memory and advancing
  overwritten-frame counts under load.
- Capture one opt-in frame, pull it, complete privacy review, and replay it.
- Inspect logcat for Camera2, OpenXR, Vulkan, or lifecycle errors.
