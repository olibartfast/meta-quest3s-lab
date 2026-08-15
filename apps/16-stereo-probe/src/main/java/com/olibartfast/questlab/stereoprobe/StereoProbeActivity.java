package com.olibartfast.questlab.stereoprobe;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.ImageFormat;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.CameraMetadata;
import android.hardware.camera2.params.StreamConfigurationMap;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.util.Size;
import android.view.Gravity;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.atomic.AtomicBoolean;

public final class StereoProbeActivity extends Activity {
    private static final String TAG = "StereoProbe";
    private static final int CAMERA_PERMISSION_REQUEST = 9016;
    private static final String HEADSET_CAMERA_PERMISSION =
        "horizonos.permission.HEADSET_CAMERA";
    private static final int PASSTHROUGH_SOURCE = 0;
    private static final int LEFT_POSITION = 0;
    private static final int RIGHT_POSITION = 1;
    private static final String REPORT_NAME = "stereo-probe-report.json";

    private static final CameraCharacteristics.Key<Integer> CAMERA_SOURCE =
        new CameraCharacteristics.Key<>(
            "com.meta.extra_metadata.camera_source", Integer.class);
    private static final CameraCharacteristics.Key<Integer> CAMERA_POSITION =
        new CameraCharacteristics.Key<>(
            "com.meta.extra_metadata.position", Integer.class);

    private final AtomicBoolean probeStarted = new AtomicBoolean(false);
    private TextView statusView;
    private volatile ConcurrentCaptureProbe captureProbe;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        statusView = new TextView(this);
        statusView.setBackgroundColor(Color.BLACK);
        statusView.setTextColor(Color.WHITE);
        statusView.setTextSize(24.0F);
        statusView.setGravity(Gravity.CENTER);
        statusView.setPadding(48, 48, 48, 48);
        statusView.setText("Quest stereo capability probe\nWaiting for camera permission…");
        setContentView(statusView);
        ensurePermissions();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (hasCameraPermissions()) {
            startProbeOnce();
        }
    }

    @Override
    protected void onPause() {
        ConcurrentCaptureProbe activeProbe = captureProbe;
        if (activeProbe != null) {
            activeProbe.close();
            captureProbe = null;
        }
        super.onPause();
    }

    private boolean hasCameraPermissions() {
        return checkSelfPermission(Manifest.permission.CAMERA) ==
                PackageManager.PERMISSION_GRANTED &&
            checkSelfPermission(HEADSET_CAMERA_PERMISSION) ==
                PackageManager.PERMISSION_GRANTED;
    }

    private void ensurePermissions() {
        if (hasCameraPermissions()) {
            startProbeOnce();
            return;
        }
        requestPermissions(
            new String[] {
                Manifest.permission.CAMERA,
                HEADSET_CAMERA_PERMISSION,
            },
            CAMERA_PERMISSION_REQUEST);
    }

    @Override
    public void onRequestPermissionsResult(
        int requestCode,
        String[] permissions,
        int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode != CAMERA_PERMISSION_REQUEST) {
            return;
        }
        if (hasCameraPermissions()) {
            startProbeOnce();
        } else {
            setStatus("Camera permission denied; no probe report was written.");
        }
    }

    private void startProbeOnce() {
        if (!probeStarted.compareAndSet(false, true)) {
            return;
        }
        setStatus("Reading Camera2 topology and calibration…");
        Thread worker = new Thread(this::runGroupAProbe, "StereoProbeGroupA");
        worker.start();
    }

    private void runGroupAProbe() {
        JSONObject report = new JSONObject();
        JSONArray errors = new JSONArray();
        try {
            report.put("schema_version", 1);
            report.put("device", buildDeviceRecord());
            CameraManager manager =
                (CameraManager) getSystemService(CAMERA_SERVICE);
            report.put("group_a", buildGroupARecord(manager, errors));
            report.put("group_b", new JSONObject().put("attempted", false));
            report.put("errors", errors);
            File reportFile = writeReportAtomically(report);
            JSONObject pair = report.getJSONObject("group_a")
                .getJSONObject("selected_pair");
            String mechanism = pair.getString("mechanism");
            JSONArray commonSizes = pair.getJSONArray("common_yuv_sizes");
            if (!"CONCURRENT".equals(mechanism) || commonSizes.length() == 0) {
                String message = String.format(
                    Locale.ROOT,
                    "Group A complete\nPair mechanism: %s\nCommon YUV sizes: %d\n" +
                        "Group B not attempted\nReport: %s",
                    mechanism,
                    commonSizes.length(),
                    reportFile.getAbsolutePath());
                Log.i(TAG, message.replace('\n', ' '));
                setStatus(message);
                probeStarted.set(false);
                return;
            }
            Size captureSize = chooseCaptureSize(commonSizes);
            setStatus(String.format(
                Locale.ROOT,
                "Group A complete: concurrent pair found\n" +
                    "Measuring 300+ pairs at %dx%d…",
                captureSize.getWidth(),
                captureSize.getHeight()));
            captureProbe = new ConcurrentCaptureProbe(
                manager,
                pair.getString("left_id"),
                pair.getString("right_id"),
                captureSize,
                groupB -> finishGroupB(report, groupB));
            captureProbe.start();
        } catch (CameraAccessException | JSONException | IOException exception) {
            Log.e(TAG, "Stereo probe failed", exception);
            setStatus("Stereo probe failed: " + exception.getMessage());
            probeStarted.set(false);
        }
    }

    private void finishGroupB(JSONObject report, JSONObject groupB) {
        try {
            report.put("group_b", groupB);
            File reportFile = writeReportAtomically(report);
            String message = String.format(
                Locale.ROOT,
                "Group B complete\nConfigured: %s\nMatched pairs: %d\n" +
                    "Report: %s",
                groupB.optBoolean("configured", false),
                groupB.optInt("matched_pairs", 0),
                reportFile.getAbsolutePath());
            Log.i(TAG, message.replace('\n', ' '));
            setStatus(message);
        } catch (JSONException | IOException exception) {
            Log.e(TAG, "Cannot commit Group B report", exception);
            setStatus("Cannot commit Group B report: " + exception.getMessage());
        } finally {
            captureProbe = null;
            probeStarted.set(false);
        }
    }

    private static Size chooseCaptureSize(JSONArray commonSizes)
        throws JSONException {
        Size fallback = null;
        for (int index = 0; index < commonSizes.length(); ++index) {
            JSONObject size = commonSizes.getJSONObject(index);
            int width = size.getInt("width");
            int height = size.getInt("height");
            if (width == 640 && height == 480) {
                return new Size(width, height);
            }
            if (fallback == null ||
                (long) width * height <
                    (long) fallback.getWidth() * fallback.getHeight()) {
                fallback = new Size(width, height);
            }
        }
        if (fallback == null) {
            throw new JSONException("No common capture size");
        }
        return fallback;
    }

    private JSONObject buildDeviceRecord() throws JSONException {
        return new JSONObject()
            .put("manufacturer", Build.MANUFACTURER)
            .put("model", Build.MODEL)
            .put("device", Build.DEVICE)
            .put("display", Build.DISPLAY)
            .put("fingerprint", Build.FINGERPRINT)
            .put("sdk", Build.VERSION.SDK_INT);
    }

    private JSONObject buildGroupARecord(
        CameraManager manager,
        JSONArray errors) throws CameraAccessException, JSONException {
        String[] topLevelIds = manager.getCameraIdList();
        Map<String, CameraRecord> allRecords = new LinkedHashMap<>();
        List<LogicalGroup> logicalGroups = new ArrayList<>();

        for (String cameraId : topLevelIds) {
            CameraCharacteristics characteristics =
                manager.getCameraCharacteristics(cameraId);
            CameraRecord record = readCameraRecord(
                manager, cameraId, true, characteristics, errors);
            allRecords.put(cameraId, record);
            if (record.logicalMultiCamera) {
                LogicalGroup group = new LogicalGroup(
                    cameraId,
                    record.syncType,
                    new ArrayList<>(characteristics.getPhysicalCameraIds()));
                logicalGroups.add(group);
                for (String physicalId : group.physicalIds) {
                    if (allRecords.containsKey(physicalId)) {
                        continue;
                    }
                    try {
                        CameraCharacteristics physicalCharacteristics =
                            manager.getCameraCharacteristics(physicalId);
                        allRecords.put(
                            physicalId,
                            readCameraRecord(
                                manager,
                                physicalId,
                                false,
                                physicalCharacteristics,
                                errors));
                    } catch (IllegalArgumentException exception) {
                        errors.put("Cannot read physical camera " + physicalId +
                            ": " + exception.getMessage());
                    }
                }
            }
        }

        JSONArray concurrentSets = new JSONArray();
        Set<Set<String>> concurrentCameraIds = manager.getConcurrentCameraIds();
        for (Set<String> cameraSet : concurrentCameraIds) {
            List<String> sorted = new ArrayList<>(cameraSet);
            Collections.sort(sorted);
            concurrentSets.put(toJsonArray(sorted));
        }

        List<CameraRecord> passthroughCameras = new ArrayList<>();
        for (CameraRecord record : allRecords.values()) {
            if (record.source != null && record.source == PASSTHROUGH_SOURCE &&
                (record.position == LEFT_POSITION ||
                 record.position == RIGHT_POSITION)) {
                passthroughCameras.add(record);
            }
        }
        passthroughCameras.sort(Comparator.comparingInt(record -> record.position));

        CameraRecord left = findByPosition(passthroughCameras, LEFT_POSITION);
        CameraRecord right = findByPosition(passthroughCameras, RIGHT_POSITION);
        PairSelection selection = selectPair(
            left, right, logicalGroups, concurrentCameraIds);

        JSONArray cameraRecords = new JSONArray();
        for (CameraRecord record : passthroughCameras) {
            cameraRecords.put(record.toJson());
        }
        JSONArray logicalRecords = new JSONArray();
        for (LogicalGroup group : logicalGroups) {
            logicalRecords.put(group.toJson());
        }

        return new JSONObject()
            .put("complete", true)
            .put("all_camera_ids", toJsonArray(Arrays.asList(topLevelIds)))
            .put("passthrough_cameras", cameraRecords)
            .put("logical_groups", logicalRecords)
            .put("concurrent_camera_sets", concurrentSets)
            .put("selected_pair", selection.toJson());
    }

    private CameraRecord readCameraRecord(
        CameraManager manager,
        String cameraId,
        boolean topLevel,
        CameraCharacteristics characteristics,
        JSONArray errors) throws JSONException {
        Integer source = safeGet(characteristics, CAMERA_SOURCE, cameraId, errors);
        Integer position = safeGet(
            characteristics, CAMERA_POSITION, cameraId, errors);
        int[] capabilities = safeGet(
            characteristics,
            CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES,
            cameraId,
            errors);
        boolean logical = contains(
            capabilities,
            CameraMetadata.REQUEST_AVAILABLE_CAPABILITIES_LOGICAL_MULTI_CAMERA);
        Integer syncType = safeGet(
            characteristics,
            CameraCharacteristics.LOGICAL_MULTI_CAMERA_SENSOR_SYNC_TYPE,
            cameraId,
            errors);
        Integer poseReference = safeGet(
            characteristics,
            CameraCharacteristics.LENS_POSE_REFERENCE,
            cameraId,
            errors);
        Integer timestampSource = safeGet(
            characteristics,
            CameraCharacteristics.SENSOR_INFO_TIMESTAMP_SOURCE,
            cameraId,
            errors);
        StreamConfigurationMap streamMap = safeGet(
            characteristics,
            CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP,
            cameraId,
            errors);
        Size[] yuvSizes = streamMap != null
            ? streamMap.getOutputSizes(ImageFormat.YUV_420_888)
            : null;
        return new CameraRecord(
            cameraId,
            topLevel,
            source,
            position != null ? position : -1,
            logical,
            new ArrayList<>(characteristics.getPhysicalCameraIds()),
            syncTypeName(syncType),
            timestampSourceName(timestampSource),
            valueOrEmpty(safeGet(
                characteristics,
                CameraCharacteristics.LENS_INTRINSIC_CALIBRATION,
                cameraId,
                errors)),
            valueOrEmpty(safeGet(
                characteristics,
                CameraCharacteristics.LENS_DISTORTION,
                cameraId,
                errors)),
            valueOrEmpty(safeGet(
                characteristics,
                CameraCharacteristics.LENS_POSE_ROTATION,
                cameraId,
                errors)),
            valueOrEmpty(safeGet(
                characteristics,
                CameraCharacteristics.LENS_POSE_TRANSLATION,
                cameraId,
                errors)),
            poseReferenceName(poseReference),
            yuvSizes != null ? Arrays.asList(yuvSizes) : Collections.emptyList());
    }

    private static PairSelection selectPair(
        CameraRecord left,
        CameraRecord right,
        List<LogicalGroup> logicalGroups,
        Set<Set<String>> concurrentSets) {
        if (left == null || right == null) {
            return PairSelection.none(left, right);
        }
        String mechanism = "NONE";
        String logicalId = null;
        String syncType = "ABSENT";
        for (LogicalGroup group : logicalGroups) {
            if (group.physicalIds.contains(left.cameraId) &&
                group.physicalIds.contains(right.cameraId)) {
                mechanism = "LOGICAL";
                logicalId = group.cameraId;
                syncType = group.syncType;
                break;
            }
        }
        if ("NONE".equals(mechanism)) {
            for (Set<String> cameraSet : concurrentSets) {
                if (cameraSet.contains(left.cameraId) &&
                    cameraSet.contains(right.cameraId)) {
                    mechanism = "CONCURRENT";
                    break;
                }
            }
        }
        return new PairSelection(
            left,
            right,
            mechanism,
            logicalId,
            syncType,
            commonSizes(left.yuvSizes, right.yuvSizes));
    }

    private static List<Size> commonSizes(List<Size> left, List<Size> right) {
        Set<String> rightKeys = new HashSet<>();
        for (Size size : right) {
            rightKeys.add(size.getWidth() + "x" + size.getHeight());
        }
        List<Size> result = new ArrayList<>();
        for (Size size : left) {
            if (rightKeys.contains(size.getWidth() + "x" + size.getHeight())) {
                result.add(size);
            }
        }
        result.sort((first, second) -> Long.compare(
            (long) second.getWidth() * second.getHeight(),
            (long) first.getWidth() * first.getHeight()));
        return result;
    }

    private static CameraRecord findByPosition(
        List<CameraRecord> cameras, int position) {
        for (CameraRecord camera : cameras) {
            if (camera.position == position) {
                return camera;
            }
        }
        return null;
    }

    private File writeReportAtomically(JSONObject report)
        throws IOException, JSONException {
        File target = new File(getFilesDir(), REPORT_NAME);
        File temporary = new File(getFilesDir(), REPORT_NAME + ".tmp");
        byte[] bytes = (report.toString(2) + "\n")
            .getBytes(StandardCharsets.UTF_8);
        try (FileOutputStream stream = new FileOutputStream(temporary)) {
            stream.write(bytes);
            stream.getFD().sync();
        }
        if (target.exists() && !target.delete()) {
            throw new IOException("Cannot replace existing report");
        }
        if (!temporary.renameTo(target)) {
            throw new IOException("Cannot commit stereo probe report");
        }
        return target;
    }

    private void setStatus(String status) {
        runOnUiThread(() -> statusView.setText(status));
    }

    private static boolean contains(int[] values, int expected) {
        if (values == null) {
            return false;
        }
        for (int value : values) {
            if (value == expected) {
                return true;
            }
        }
        return false;
    }

    private static float[] valueOrEmpty(float[] value) {
        return value != null ? value : new float[0];
    }

    private static <T> T safeGet(
        CameraCharacteristics characteristics,
        CameraCharacteristics.Key<T> key,
        String cameraId,
        JSONArray errors) {
        try {
            return characteristics.get(key);
        } catch (IllegalArgumentException | AssertionError exception) {
            errors.put("Camera " + cameraId + " cannot read " +
                key.getName() + ": " + exception.getMessage());
            return null;
        }
    }

    private static String syncTypeName(Integer value) {
        if (value == null) {
            return "ABSENT";
        }
        if (value == CameraMetadata.LOGICAL_MULTI_CAMERA_SENSOR_SYNC_TYPE_CALIBRATED) {
            return "CALIBRATED";
        }
        if (value == CameraMetadata.LOGICAL_MULTI_CAMERA_SENSOR_SYNC_TYPE_APPROXIMATE) {
            return "APPROXIMATE";
        }
        return "UNKNOWN_" + value;
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

    private static String poseReferenceName(Integer value) {
        if (value == null) {
            return "ABSENT";
        }
        switch (value) {
            case CameraMetadata.LENS_POSE_REFERENCE_PRIMARY_CAMERA:
                return "PRIMARY_CAMERA";
            case CameraMetadata.LENS_POSE_REFERENCE_GYROSCOPE:
                return "GYROSCOPE";
            case CameraMetadata.LENS_POSE_REFERENCE_UNDEFINED:
                return "UNDEFINED";
            case CameraMetadata.LENS_POSE_REFERENCE_AUTOMOTIVE:
                return "AUTOMOTIVE";
            default:
                return "UNKNOWN_" + value;
        }
    }

    private static JSONArray toJsonArray(List<String> values)
        throws JSONException {
        JSONArray result = new JSONArray();
        for (String value : values) {
            result.put(value);
        }
        return result;
    }

    private static JSONArray floatsToJson(float[] values)
        throws JSONException {
        JSONArray result = new JSONArray();
        for (float value : values) {
            result.put(value);
        }
        return result;
    }

    private static JSONArray sizesToJson(List<Size> sizes) throws JSONException {
        JSONArray result = new JSONArray();
        for (Size size : sizes) {
            result.put(new JSONObject()
                .put("width", size.getWidth())
                .put("height", size.getHeight()));
        }
        return result;
    }

    private static final class CameraRecord {
        final String cameraId;
        final boolean topLevel;
        final Integer source;
        final int position;
        final boolean logicalMultiCamera;
        final List<String> physicalCameraIds;
        final String syncType;
        final String timestampSource;
        final float[] intrinsics;
        final float[] distortion;
        final float[] poseRotation;
        final float[] poseTranslation;
        final String poseReference;
        final List<Size> yuvSizes;

        CameraRecord(
            String cameraId,
            boolean topLevel,
            Integer source,
            int position,
            boolean logicalMultiCamera,
            List<String> physicalCameraIds,
            String syncType,
            String timestampSource,
            float[] intrinsics,
            float[] distortion,
            float[] poseRotation,
            float[] poseTranslation,
            String poseReference,
            List<Size> yuvSizes) {
            this.cameraId = cameraId;
            this.topLevel = topLevel;
            this.source = source;
            this.position = position;
            this.logicalMultiCamera = logicalMultiCamera;
            this.physicalCameraIds = physicalCameraIds;
            this.syncType = syncType;
            this.timestampSource = timestampSource;
            this.intrinsics = intrinsics;
            this.distortion = distortion;
            this.poseRotation = poseRotation;
            this.poseTranslation = poseTranslation;
            this.poseReference = poseReference;
            this.yuvSizes = yuvSizes;
        }

        JSONObject toJson() throws JSONException {
            return new JSONObject()
                .put("camera_id", cameraId)
                .put("top_level", topLevel)
                .put("source", source != null ? source : JSONObject.NULL)
                .put("position", position)
                .put("logical_multi_camera", logicalMultiCamera)
                .put("physical_camera_ids", toJsonArray(physicalCameraIds))
                .put("sync_type", syncType)
                .put("timestamp_source", timestampSource)
                .put("intrinsics", floatsToJson(intrinsics))
                .put("distortion", floatsToJson(distortion))
                .put("pose_rotation", floatsToJson(poseRotation))
                .put("pose_translation", floatsToJson(poseTranslation))
                .put("pose_reference", poseReference)
                .put("yuv_sizes", sizesToJson(yuvSizes));
        }
    }

    private static final class LogicalGroup {
        final String cameraId;
        final String syncType;
        final List<String> physicalIds;

        LogicalGroup(
            String cameraId, String syncType, List<String> physicalIds) {
            this.cameraId = cameraId;
            this.syncType = syncType;
            this.physicalIds = physicalIds;
        }

        JSONObject toJson() throws JSONException {
            return new JSONObject()
                .put("camera_id", cameraId)
                .put("sync_type", syncType)
                .put("physical_camera_ids", toJsonArray(physicalIds));
        }
    }

    private static final class PairSelection {
        final CameraRecord left;
        final CameraRecord right;
        final String mechanism;
        final String logicalId;
        final String syncType;
        final List<Size> commonSizes;

        PairSelection(
            CameraRecord left,
            CameraRecord right,
            String mechanism,
            String logicalId,
            String syncType,
            List<Size> commonSizes) {
            this.left = left;
            this.right = right;
            this.mechanism = mechanism;
            this.logicalId = logicalId;
            this.syncType = syncType;
            this.commonSizes = commonSizes;
        }

        static PairSelection none(CameraRecord left, CameraRecord right) {
            return new PairSelection(
                left, right, "NONE", null, "ABSENT", Collections.emptyList());
        }

        JSONObject toJson() throws JSONException {
            boolean sharedReference = left != null && right != null &&
                left.poseReference.equals(right.poseReference) &&
                !"ABSENT".equals(left.poseReference) &&
                !"UNDEFINED".equals(left.poseReference);
            Double baseline = deriveBaseline(left, right);
            return new JSONObject()
                .put("left_id", left != null ? left.cameraId : JSONObject.NULL)
                .put("right_id", right != null ? right.cameraId : JSONObject.NULL)
                .put("mechanism", mechanism)
                .put("logical_id", logicalId != null ? logicalId : JSONObject.NULL)
                .put("sync_type", syncType)
                .put("common_yuv_sizes", sizesToJson(commonSizes))
                .put("shared_pose_reference", sharedReference)
                .put("derived_baseline_meters",
                    baseline != null ? baseline : JSONObject.NULL);
        }

        private static Double deriveBaseline(
            CameraRecord left, CameraRecord right) {
            if (left == null || right == null ||
                left.poseTranslation.length < 3 ||
                right.poseTranslation.length < 3) {
                return null;
            }
            double x = right.poseTranslation[0] - left.poseTranslation[0];
            double y = right.poseTranslation[1] - left.poseTranslation[1];
            double z = right.poseTranslation[2] - left.poseTranslation[2];
            return Math.sqrt(x * x + y * y + z * z);
        }
    }
}
