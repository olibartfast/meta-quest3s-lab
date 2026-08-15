package com.olibartfast.questlab.stereoprobe;

import android.graphics.ImageFormat;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCaptureSession;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraDevice;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.CameraMetadata;
import android.hardware.camera2.CaptureRequest;
import android.hardware.camera2.CaptureResult;
import android.hardware.camera2.TotalCaptureResult;
import android.media.Image;
import android.media.ImageReader;
import android.os.Handler;
import android.os.HandlerThread;
import android.util.Log;
import android.util.Size;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicBoolean;

final class ConcurrentCaptureProbe {
    interface Listener {
        void onComplete(JSONObject groupB);
    }

    private static final String TAG = "StereoProbe";
    private static final int CAPTURE_FRAMES = 320;
    private static final int REQUIRED_MATCHED_PAIRS = 300;
    private static final long TIMEOUT_MILLISECONDS = 30_000L;
    private static final long MATCH_WINDOW_NANOSECONDS = 20_000_000L;

    private final CameraManager manager;
    private final String leftId;
    private final String rightId;
    private final Size size;
    private final Listener listener;
    private final HandlerThread cameraThread =
        new HandlerThread("StereoProbeCapture");
    private final AtomicBoolean finished = new AtomicBoolean(false);
    private final List<Long> leftTimestamps = new ArrayList<>();
    private final List<Long> rightTimestamps = new ArrayList<>();
    private final Map<Long, CaptureMetadata> leftMetadata = new HashMap<>();
    private final Map<Long, CaptureMetadata> rightMetadata = new HashMap<>();

    private Handler handler;
    private ImageReader leftReader;
    private ImageReader rightReader;
    private CameraDevice leftCamera;
    private CameraDevice rightCamera;
    private CameraCaptureSession leftSession;
    private CameraCaptureSession rightSession;
    private boolean leftConfigured;
    private boolean rightConfigured;
    private boolean captureStarted;
    private boolean manualExposureAvailable;
    private boolean aeLockAvailable;
    private String leftTimestampSource = "ABSENT";
    private String rightTimestampSource = "ABSENT";

    ConcurrentCaptureProbe(
        CameraManager manager,
        String leftId,
        String rightId,
        Size size,
        Listener listener) {
        this.manager = manager;
        this.leftId = leftId;
        this.rightId = rightId;
        this.size = size;
        this.listener = listener;
    }

    void start() {
        cameraThread.start();
        handler = new Handler(cameraThread.getLooper());
        handler.post(this::startOnCameraThread);
    }

    void close() {
        if (handler == null) {
            return;
        }
        handler.post(() -> finish(false, "activity paused before completion"));
    }

    private void startOnCameraThread() {
        try {
            CameraCharacteristics leftCharacteristics =
                manager.getCameraCharacteristics(leftId);
            CameraCharacteristics rightCharacteristics =
                manager.getCameraCharacteristics(rightId);
            manualExposureAvailable = hasCapability(
                leftCharacteristics,
                CameraMetadata.REQUEST_AVAILABLE_CAPABILITIES_MANUAL_SENSOR) &&
                hasCapability(
                    rightCharacteristics,
                    CameraMetadata.REQUEST_AVAILABLE_CAPABILITIES_MANUAL_SENSOR);
            aeLockAvailable = Boolean.TRUE.equals(leftCharacteristics.get(
                    CameraCharacteristics.CONTROL_AE_LOCK_AVAILABLE)) &&
                Boolean.TRUE.equals(rightCharacteristics.get(
                    CameraCharacteristics.CONTROL_AE_LOCK_AVAILABLE));
            leftTimestampSource = timestampSourceName(leftCharacteristics.get(
                CameraCharacteristics.SENSOR_INFO_TIMESTAMP_SOURCE));
            rightTimestampSource = timestampSourceName(rightCharacteristics.get(
                CameraCharacteristics.SENSOR_INFO_TIMESTAMP_SOURCE));

            leftReader = ImageReader.newInstance(
                size.getWidth(), size.getHeight(), ImageFormat.YUV_420_888, 4);
            rightReader = ImageReader.newInstance(
                size.getWidth(), size.getHeight(), ImageFormat.YUV_420_888, 4);
            leftReader.setOnImageAvailableListener(
                reader -> onImageAvailable(reader, true), handler);
            rightReader.setOnImageAvailableListener(
                reader -> onImageAvailable(reader, false), handler);

            // Android's concurrent-camera contract requires both devices to be
            // opened before either capture session is configured.
            manager.openCamera(leftId, cameraStateCallback(true), handler);
            manager.openCamera(rightId, cameraStateCallback(false), handler);
            handler.postDelayed(
                () -> finish(false, "timed out before collecting 300 frame pairs"),
                TIMEOUT_MILLISECONDS);
        } catch (CameraAccessException | SecurityException exception) {
            finish(false, "cannot open concurrent cameras: " + exception.getMessage());
        }
    }

    private CameraDevice.StateCallback cameraStateCallback(boolean left) {
        return new CameraDevice.StateCallback() {
            @Override
            public void onOpened(CameraDevice camera) {
                if (left) {
                    leftCamera = camera;
                } else {
                    rightCamera = camera;
                }
                if (leftCamera != null && rightCamera != null) {
                    configureSessions();
                }
            }

            @Override
            public void onDisconnected(CameraDevice camera) {
                finish(false, "camera " + camera.getId() + " disconnected");
            }

            @Override
            public void onError(CameraDevice camera, int error) {
                finish(false, "camera " + camera.getId() + " error " + error);
            }
        };
    }

    private void configureSessions() {
        try {
            leftCamera.createCaptureSession(
                Collections.singletonList(leftReader.getSurface()),
                sessionStateCallback(true),
                handler);
            rightCamera.createCaptureSession(
                Collections.singletonList(rightReader.getSurface()),
                sessionStateCallback(false),
                handler);
        } catch (CameraAccessException exception) {
            finish(false, "cannot configure concurrent sessions: " +
                exception.getMessage());
        }
    }

    private CameraCaptureSession.StateCallback sessionStateCallback(boolean left) {
        return new CameraCaptureSession.StateCallback() {
            @Override
            public void onConfigured(CameraCaptureSession session) {
                if (left) {
                    leftSession = session;
                    leftConfigured = true;
                } else {
                    rightSession = session;
                    rightConfigured = true;
                }
                if (leftConfigured && rightConfigured) {
                    startRepeatingRequests();
                }
            }

            @Override
            public void onConfigureFailed(CameraCaptureSession session) {
                finish(false, "one concurrent capture session was rejected");
            }
        };
    }

    private void startRepeatingRequests() {
        try {
            CaptureRequest.Builder leftRequest = leftCamera.createCaptureRequest(
                CameraDevice.TEMPLATE_RECORD);
            leftRequest.addTarget(leftReader.getSurface());
            CaptureRequest.Builder rightRequest = rightCamera.createCaptureRequest(
                CameraDevice.TEMPLATE_RECORD);
            rightRequest.addTarget(rightReader.getSurface());
            leftSession.setRepeatingRequest(
                leftRequest.build(), captureCallback(true), handler);
            rightSession.setRepeatingRequest(
                rightRequest.build(), captureCallback(false), handler);
            captureStarted = true;
            Log.i(TAG, "Concurrent capture started cameras=" + leftId + "," +
                rightId + " size=" + size);
        } catch (CameraAccessException exception) {
            finish(false, "cannot start concurrent capture: " +
                exception.getMessage());
        }
    }

    private CameraCaptureSession.CaptureCallback captureCallback(boolean left) {
        return new CameraCaptureSession.CaptureCallback() {
            @Override
            public void onCaptureCompleted(
                CameraCaptureSession session,
                CaptureRequest request,
                TotalCaptureResult result) {
                Long timestamp = result.get(CaptureResult.SENSOR_TIMESTAMP);
                if (timestamp == null) {
                    return;
                }
                CaptureMetadata metadata = new CaptureMetadata(
                    result.get(CaptureResult.SENSOR_EXPOSURE_TIME),
                    result.get(CaptureResult.SENSOR_SENSITIVITY),
                    result.get(CaptureResult.CONTROL_AE_MODE));
                (left ? leftMetadata : rightMetadata).put(timestamp, metadata);
            }
        };
    }

    private void onImageAvailable(ImageReader reader, boolean left) {
        try (Image image = reader.acquireNextImage()) {
            if (image == null || finished.get()) {
                return;
            }
            (left ? leftTimestamps : rightTimestamps).add(image.getTimestamp());
            if (leftTimestamps.size() >= CAPTURE_FRAMES &&
                rightTimestamps.size() >= CAPTURE_FRAMES) {
                finish(true, null);
            }
        } catch (RuntimeException exception) {
            finish(false, "image acquisition failed: " + exception.getMessage());
        }
    }

    private void finish(boolean succeeded, String error) {
        if (!finished.compareAndSet(false, true)) {
            return;
        }
        JSONObject groupB;
        try {
            groupB = buildResult(succeeded, error);
        } catch (JSONException exception) {
            groupB = new JSONObject();
            try {
                groupB.put("attempted", true);
                groupB.put("configured", false);
                groupB.put("error", "result serialization failed: " +
                    exception.getMessage());
            } catch (JSONException ignored) {
                // JSONObject accepts these primitive values on Android.
            }
        }
        closeResources();
        listener.onComplete(groupB);
    }

    private JSONObject buildResult(boolean succeeded, String error)
        throws JSONException {
        Pairing pairing = pairFrames(leftTimestamps, rightTimestamps);
        JSONObject result = new JSONObject()
            .put("attempted", true)
            .put("configured", leftConfigured && rightConfigured && captureStarted)
            .put("mechanism", "CONCURRENT")
            .put("left_id", leftId)
            .put("right_id", rightId)
            .put("width", size.getWidth())
            .put("height", size.getHeight())
            .put("left_frames", leftTimestamps.size())
            .put("right_frames", rightTimestamps.size())
            .put("required_matched_pairs", REQUIRED_MATCHED_PAIRS)
            .put("matched_pairs", pairing.skews.size())
            .put("unmatched_left", pairing.unmatchedLeft)
            .put("unmatched_right", pairing.unmatchedRight)
            .put("matched_fps", pairing.matchedFps())
            .put("left_timestamp_source", leftTimestampSource)
            .put("right_timestamp_source", rightTimestampSource)
            .put("skew_ns", statistics(pairing.skews))
            .put("left_interval_ns", intervalStatistics(leftTimestamps))
            .put("right_interval_ns", intervalStatistics(rightTimestamps))
            .put("exposure", exposureResult(pairing))
            .put("success", succeeded);
        if (error != null) {
            result.put("error", error);
        }
        return result;
    }

    private JSONObject exposureResult(Pairing pairing) throws JSONException {
        int comparable = 0;
        int parity = 0;
        for (TimestampPair pair : pairing.pairs) {
            CaptureMetadata left = leftMetadata.get(pair.left);
            CaptureMetadata right = rightMetadata.get(pair.right);
            if (left == null || right == null ||
                left.exposureNanoseconds == null ||
                right.exposureNanoseconds == null ||
                left.sensitivity == null || right.sensitivity == null) {
                continue;
            }
            ++comparable;
            double exposureDifference = relativeDifference(
                left.exposureNanoseconds, right.exposureNanoseconds);
            double sensitivityDifference = relativeDifference(
                left.sensitivity, right.sensitivity);
            if (exposureDifference <= 0.05 && sensitivityDifference <= 0.05) {
                ++parity;
            }
        }
        double parityFraction = comparable > 0
            ? (double) parity / comparable
            : 0.0;
        return new JSONObject()
            .put("comparable_pairs", comparable)
            .put("parity_fraction", parityFraction)
            .put("matched", comparable >= REQUIRED_MATCHED_PAIRS / 2 &&
                parityFraction >= 0.95)
            .put("forceable", manualExposureAvailable)
            .put("manual_sensor", manualExposureAvailable)
            .put("ae_lock_available", aeLockAvailable);
    }

    private static double relativeDifference(long first, long second) {
        long denominator = Math.max(Math.abs(first), Math.abs(second));
        return denominator > 0
            ? (double) Math.abs(first - second) / denominator
            : 0.0;
    }

    private static JSONObject intervalStatistics(List<Long> timestamps)
        throws JSONException {
        List<Long> sorted = new ArrayList<>(timestamps);
        sorted.sort(Long::compare);
        List<Long> intervals = new ArrayList<>();
        for (int index = 1; index < sorted.size(); ++index) {
            intervals.add(sorted.get(index) - sorted.get(index - 1));
        }
        JSONObject statistics = statistics(intervals);
        if (!intervals.isEmpty()) {
            long median = percentile(intervals, 0.50);
            List<Long> jitter = new ArrayList<>();
            for (long interval : intervals) {
                jitter.add(Math.abs(interval - median));
            }
            statistics.put("p95_jitter", percentile(jitter, 0.95));
        }
        return statistics;
    }

    private static JSONObject statistics(List<Long> values)
        throws JSONException {
        if (values.isEmpty()) {
            return new JSONObject()
                .put("min", 0)
                .put("median", 0)
                .put("p95", 0)
                .put("max", 0);
        }
        List<Long> sorted = new ArrayList<>(values);
        sorted.sort(Long::compare);
        return new JSONObject()
            .put("min", sorted.get(0))
            .put("median", percentile(sorted, 0.50))
            .put("p95", percentile(sorted, 0.95))
            .put("max", sorted.get(sorted.size() - 1));
    }

    private static long percentile(List<Long> sortedOrUnsorted, double fraction) {
        List<Long> sorted = new ArrayList<>(sortedOrUnsorted);
        sorted.sort(Long::compare);
        int index = (int) Math.ceil(fraction * sorted.size()) - 1;
        return sorted.get(Math.max(0, Math.min(index, sorted.size() - 1)));
    }

    private static Pairing pairFrames(List<Long> left, List<Long> right) {
        List<Long> sortedLeft = new ArrayList<>(left);
        List<Long> sortedRight = new ArrayList<>(right);
        sortedLeft.sort(Long::compare);
        sortedRight.sort(Long::compare);
        List<TimestampPair> pairs = new ArrayList<>();
        List<Long> skews = new ArrayList<>();
        int unmatchedLeft = 0;
        int unmatchedRight = 0;
        int leftIndex = 0;
        int rightIndex = 0;
        while (leftIndex < sortedLeft.size() &&
            rightIndex < sortedRight.size()) {
            long leftTimestamp = sortedLeft.get(leftIndex);
            long rightTimestamp = sortedRight.get(rightIndex);
            long difference = leftTimestamp - rightTimestamp;
            long absoluteDifference = Math.abs(difference);
            long nextRightDifference = rightIndex + 1 < sortedRight.size()
                ? Math.abs(leftTimestamp - sortedRight.get(rightIndex + 1))
                : Long.MAX_VALUE;
            long nextLeftDifference = leftIndex + 1 < sortedLeft.size()
                ? Math.abs(sortedLeft.get(leftIndex + 1) - rightTimestamp)
                : Long.MAX_VALUE;
            if (nextRightDifference < absoluteDifference) {
                ++unmatchedRight;
                ++rightIndex;
                continue;
            }
            if (nextLeftDifference < absoluteDifference) {
                ++unmatchedLeft;
                ++leftIndex;
                continue;
            }
            if (absoluteDifference <= MATCH_WINDOW_NANOSECONDS) {
                pairs.add(new TimestampPair(leftTimestamp, rightTimestamp));
                skews.add(absoluteDifference);
                ++leftIndex;
                ++rightIndex;
            } else if (difference < 0) {
                ++unmatchedLeft;
                ++leftIndex;
            } else {
                ++unmatchedRight;
                ++rightIndex;
            }
        }
        unmatchedLeft += sortedLeft.size() - leftIndex;
        unmatchedRight += sortedRight.size() - rightIndex;
        return new Pairing(pairs, skews, unmatchedLeft, unmatchedRight);
    }

    private void closeResources() {
        if (leftSession != null) {
            leftSession.close();
            leftSession = null;
        }
        if (rightSession != null) {
            rightSession.close();
            rightSession = null;
        }
        if (leftCamera != null) {
            leftCamera.close();
            leftCamera = null;
        }
        if (rightCamera != null) {
            rightCamera.close();
            rightCamera = null;
        }
        if (leftReader != null) {
            leftReader.close();
            leftReader = null;
        }
        if (rightReader != null) {
            rightReader.close();
            rightReader = null;
        }
        cameraThread.quitSafely();
    }

    private static boolean hasCapability(
        CameraCharacteristics characteristics, int expected) {
        int[] capabilities = characteristics.get(
            CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES);
        if (capabilities == null) {
            return false;
        }
        for (int capability : capabilities) {
            if (capability == expected) {
                return true;
            }
        }
        return false;
    }

    private static String timestampSourceName(Integer value) {
        if (value == null) {
            return "ABSENT";
        }
        if (value == CameraMetadata.SENSOR_INFO_TIMESTAMP_SOURCE_REALTIME) {
            return "REALTIME";
        }
        if (value == CameraMetadata.SENSOR_INFO_TIMESTAMP_SOURCE_UNKNOWN) {
            return "UNKNOWN";
        }
        return "UNKNOWN_" + value;
    }

    private static final class CaptureMetadata {
        final Long exposureNanoseconds;
        final Integer sensitivity;
        final Integer aeMode;

        CaptureMetadata(
            Long exposureNanoseconds, Integer sensitivity, Integer aeMode) {
            this.exposureNanoseconds = exposureNanoseconds;
            this.sensitivity = sensitivity;
            this.aeMode = aeMode;
        }
    }

    private static final class TimestampPair {
        final long left;
        final long right;

        TimestampPair(long left, long right) {
            this.left = left;
            this.right = right;
        }
    }

    private static final class Pairing {
        final List<TimestampPair> pairs;
        final List<Long> skews;
        final int unmatchedLeft;
        final int unmatchedRight;

        Pairing(
            List<TimestampPair> pairs,
            List<Long> skews,
            int unmatchedLeft,
            int unmatchedRight) {
            this.pairs = pairs;
            this.skews = skews;
            this.unmatchedLeft = unmatchedLeft;
            this.unmatchedRight = unmatchedRight;
        }

        double matchedFps() {
            if (pairs.size() < 2) {
                return 0.0;
            }
            long first = Math.min(pairs.get(0).left, pairs.get(0).right);
            TimestampPair lastPair = pairs.get(pairs.size() - 1);
            long last = Math.max(lastPair.left, lastPair.right);
            double durationSeconds = (last - first) / 1.0e9;
            return durationSeconds > 0.0
                ? (pairs.size() - 1) / durationSeconds
                : 0.0;
        }
    }
}
