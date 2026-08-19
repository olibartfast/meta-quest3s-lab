package com.olibartfast.questlab.questcamera;

import android.Manifest;
import android.app.NativeActivity;
import android.content.pm.PackageManager;
import android.graphics.ImageFormat;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCaptureSession;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraDevice;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.CaptureRequest;
import android.media.Image;
import android.media.ImageReader;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.util.Log;
import android.util.Size;
import android.view.KeyEvent;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.PrintWriter;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Arrays;
import java.util.Collections;
import java.util.Locale;

public final class QuestCameraActivity extends NativeActivity {
    private static final String TAG = "QuestCamera";
    private static final int CAMERA_PERMISSION_REQUEST = 9009;
    private static final String HEADSET_CAMERA_PERMISSION =
        "horizonos.permission.HEADSET_CAMERA";
    private static final int PASSTHROUGH_SOURCE = 0;
    private static final int LEFT_POSITION = 0;

    static {
        // NativeActivity loads its library as part of super.onCreate(), but
        // permission results can reach this subclass through the Java class
        // loader before Horizon OS associates its native methods. Load it
        // explicitly so every JNI callback resolves deterministically.
        System.loadLibrary(BuildConfig.NATIVE_LIBRARY_NAME);
    }

    private static final CameraCharacteristics.Key<Integer> CAMERA_SOURCE =
        new CameraCharacteristics.Key<>(
            "com.meta.extra_metadata.camera_source", Integer.class);
    private static final CameraCharacteristics.Key<Integer> CAMERA_POSITION =
        new CameraCharacteristics.Key<>(
            "com.meta.extra_metadata.position", Integer.class);

    private HandlerThread cameraThread;
    private Handler cameraHandler;
    private CameraDevice cameraDevice;
    private CameraCaptureSession captureSession;
    private ImageReader imageReader;
    private CameraSelection activeSelection;
    private boolean resumed;
    private boolean nativeRequested;
    private int requestedWidth;
    private int requestedHeight;
    private int requestedFps = 30;
    private volatile boolean captureNextFrame;

    private native void nativeOnPermissionState(boolean granted);
    private native void nativeOnCameraConfigured(
        String cameraId,
        int position,
        int width,
        int height,
        float[] intrinsics,
        float[] distortion,
        float[] rotation,
        float[] translation);
    private native void nativeOnCameraFrame(
        long timestampNanoseconds,
        int width,
        int height,
        byte[] y,
        int yRowStride,
        int yPixelStride,
        byte[] u,
        int uRowStride,
        int uPixelStride,
        byte[] v,
        int vRowStride,
        int vPixelStride);
    private native void nativeOnCameraError(String message);
    private native void nativeOnCaptureSaved(String path, long byteCount);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        ensurePermissions();
    }

    @Override
    protected void onResume() {
        super.onResume();
        resumed = true;
        if (nativeRequested && hasCameraPermissions()) {
            openCamera();
        }
    }

    @Override
    protected void onPause() {
        resumed = false;
        closeCamera();
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        closeCamera();
        super.onDestroy();
    }

    public void startCameraFromNative(int width, int height, int fps) {
        Log.i(TAG, "Camera2 start requested by native code");
        runOnUiThread(() -> {
            requestedWidth = width;
            requestedHeight = height;
            requestedFps = fps > 0 ? fps : 30;
            nativeRequested = true;
            if (!hasCameraPermissions()) {
                ensurePermissions();
            } else if (resumed) {
                Log.i(TAG, "Camera2 permissions and lifecycle are ready");
                openCamera();
            } else {
                Log.i(TAG, "Camera2 start deferred until activity resume");
            }
        });
    }

    public void stopCameraFromNative() {
        runOnUiThread(() -> {
            nativeRequested = false;
            closeCamera();
        });
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_VOLUME_DOWN &&
            event.getRepeatCount() == 0) {
            captureNextFrame = true;
            Log.i(TAG, "One-frame private capture armed");
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    private boolean hasCameraPermissions() {
        return checkSelfPermission(Manifest.permission.CAMERA) ==
                PackageManager.PERMISSION_GRANTED &&
            checkSelfPermission(HEADSET_CAMERA_PERMISSION) ==
                PackageManager.PERMISSION_GRANTED;
    }

    private void ensurePermissions() {
        if (hasCameraPermissions()) {
            nativeOnPermissionState(true);
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
        super.onRequestPermissionsResult(
            requestCode, permissions, grantResults);
        if (requestCode != CAMERA_PERMISSION_REQUEST) {
            return;
        }
        final boolean granted = hasCameraPermissions();
        nativeOnPermissionState(granted);
        if (granted && resumed && nativeRequested) {
            openCamera();
        }
    }

    private void ensureCameraThread() {
        if (cameraThread != null) {
            return;
        }
        cameraThread = new HandlerThread("QuestCamera2");
        cameraThread.start();
        cameraHandler = new Handler(cameraThread.getLooper());
    }

    private void openCamera() {
        if (cameraDevice != null || imageReader != null) {
            return;
        }
        ensureCameraThread();
        Log.i(TAG, "Enumerating Meta passthrough cameras");
        try {
            CameraManager manager =
                (CameraManager) getSystemService(CAMERA_SERVICE);
            CameraSelection selection = selectCamera(manager);
            if (selection == null) {
                nativeOnCameraError(
                    "No Meta passthrough camera with a YUV stream is available");
                closeCamera();
                return;
            }
            imageReader = ImageReader.newInstance(
                selection.size.getWidth(),
                selection.size.getHeight(),
                ImageFormat.YUV_420_888,
                3);
            activeSelection = selection;
            imageReader.setOnImageAvailableListener(
                this::onImageAvailable, cameraHandler);
            nativeOnCameraConfigured(
                selection.cameraId,
                selection.position,
                selection.size.getWidth(),
                selection.size.getHeight(),
                selection.intrinsics,
                selection.distortion,
                selection.rotation,
                selection.translation);
            manager.openCamera(
                selection.cameraId,
                cameraStateCallback,
                cameraHandler);
        } catch (CameraAccessException | SecurityException exception) {
            nativeOnCameraError(
                "Camera2 open failed: " + exception.getMessage());
            closeCamera();
        }
    }

    private CameraSelection selectCamera(CameraManager manager)
        throws CameraAccessException {
        CameraSelection fallback = null;
        for (String cameraId : manager.getCameraIdList()) {
            CameraCharacteristics characteristics =
                manager.getCameraCharacteristics(cameraId);
            Integer source = characteristics.get(CAMERA_SOURCE);
            Integer position = characteristics.get(CAMERA_POSITION);
            if (source == null || source != PASSTHROUGH_SOURCE) {
                continue;
            }
            android.hardware.camera2.params.StreamConfigurationMap map =
                characteristics.get(
                    CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);
            if (map == null) {
                continue;
            }
            Size size = chooseSize(
                map.getOutputSizes(ImageFormat.YUV_420_888));
            if (size == null) {
                continue;
            }
            CameraSelection candidate = new CameraSelection(
                cameraId,
                position != null ? position : -1,
                size,
                valueOrEmpty(
                    characteristics.get(
                        CameraCharacteristics.LENS_INTRINSIC_CALIBRATION)),
                valueOrEmpty(
                    characteristics.get(
                        CameraCharacteristics.LENS_DISTORTION)),
                valueOrEmpty(
                    characteristics.get(
                        CameraCharacteristics.LENS_POSE_ROTATION)),
                valueOrEmpty(
                    characteristics.get(
                        CameraCharacteristics.LENS_POSE_TRANSLATION)));
            if (candidate.position == LEFT_POSITION) {
                return candidate;
            }
            if (fallback == null) {
                fallback = candidate;
            }
        }
        return fallback;
    }

    private Size chooseSize(Size[] sizes) {
        if (sizes == null || sizes.length == 0) {
            return null;
        }
        if (requestedWidth > 0 && requestedHeight > 0) {
            for (Size size : sizes) {
                if (size.getWidth() == requestedWidth &&
                    size.getHeight() == requestedHeight) {
                    return size;
                }
            }
        }
        return Arrays.stream(sizes)
            .filter(size -> size.getWidth() <= 1280)
            .max((left, right) -> Long.compare(
                (long) left.getWidth() * left.getHeight(),
                (long) right.getWidth() * right.getHeight()))
            .orElse(sizes[0]);
    }

    private static float[] valueOrEmpty(float[] value) {
        return value != null ? value : new float[0];
    }

    private final CameraDevice.StateCallback cameraStateCallback =
        new CameraDevice.StateCallback() {
            @Override
            public void onOpened(CameraDevice camera) {
                cameraDevice = camera;
                createCaptureSession();
            }

            @Override
            public void onDisconnected(CameraDevice camera) {
                nativeOnCameraError("Camera2 device disconnected");
                closeCamera();
            }

            @Override
            public void onError(CameraDevice camera, int error) {
                nativeOnCameraError("Camera2 device error " + error);
                closeCamera();
            }
        };

    private void createCaptureSession() {
        if (cameraDevice == null || imageReader == null) {
            return;
        }
        try {
            cameraDevice.createCaptureSession(
                Collections.singletonList(imageReader.getSurface()),
                new CameraCaptureSession.StateCallback() {
                    @Override
                    public void onConfigured(
                        CameraCaptureSession session) {
                        captureSession = session;
                        startRepeatingRequest();
                    }

                    @Override
                    public void onConfigureFailed(
                        CameraCaptureSession session) {
                        nativeOnCameraError(
                            "Camera2 capture-session configuration failed");
                        closeCamera();
                    }
                },
                cameraHandler);
        } catch (CameraAccessException exception) {
            nativeOnCameraError(
                "Camera2 session creation failed: " +
                exception.getMessage());
            closeCamera();
        }
    }

    private void startRepeatingRequest() {
        try {
            CaptureRequest.Builder request = cameraDevice.createCaptureRequest(
                CameraDevice.TEMPLATE_PREVIEW);
            request.addTarget(imageReader.getSurface());
            captureSession.setRepeatingRequest(
                request.build(), null, cameraHandler);
            Log.i(TAG, "Camera session started at requested " +
                requestedFps + " fps");
        } catch (CameraAccessException exception) {
            nativeOnCameraError(
                "Camera2 repeating request failed: " +
                exception.getMessage());
            closeCamera();
        }
    }

    private void onImageAvailable(ImageReader reader) {
        try (Image image = reader.acquireLatestImage()) {
            if (image == null) {
                return;
            }
            Image.Plane[] planes = image.getPlanes();
            if (planes.length != 3) {
                nativeOnCameraError(
                    "Expected three YUV planes, received " + planes.length);
                return;
            }
            byte[][] bytes = new byte[3][];
            for (int index = 0; index < planes.length; ++index) {
                ByteBuffer buffer = planes[index].getBuffer();
                bytes[index] = new byte[buffer.remaining()];
                buffer.get(bytes[index]);
            }
            nativeOnCameraFrame(
                image.getTimestamp(),
                image.getWidth(),
                image.getHeight(),
                bytes[0],
                planes[0].getRowStride(),
                planes[0].getPixelStride(),
                bytes[1],
                planes[1].getRowStride(),
                planes[1].getPixelStride(),
                bytes[2],
                planes[2].getRowStride(),
                planes[2].getPixelStride());
            if (captureNextFrame) {
                captureNextFrame = false;
                savePrivateCapture(
                    image,
                    planes,
                    bytes);
            }
        } catch (RuntimeException exception) {
            nativeOnCameraError(
                "Camera2 image acquisition failed: " +
                exception.getMessage());
        }
    }

    private void savePrivateCapture(
        Image image,
        Image.Plane[] planes,
        byte[][] bytes) {
        File directory = new File(
            getFilesDir(),
            "captures/frame-" + image.getTimestamp());
        if (!directory.mkdirs() && !directory.isDirectory()) {
            nativeOnCameraError(
                "Cannot create private capture directory " + directory);
            return;
        }
        try {
            long byteCount = 0;
            String pixelSha256 = computeFixturePixelSha256(
                image, planes, bytes);
            String[] names = {"y.bin", "u.bin", "v.bin"};
            for (int index = 0; index < names.length; ++index) {
                writeAtomically(new File(directory, names[index]), bytes[index]);
                byteCount += bytes[index].length;
            }
            File manifest = new File(directory, "manifest.qcam");
            File temporary = new File(directory, "manifest.qcam.tmp");
            try (PrintWriter writer = new PrintWriter(temporary)) {
                writer.printf(
                    "QUEST_CAMERA_FIXTURE_V2 %d %d %d %d %d %d %d %d " +
                    "y.bin u.bin v.bin%n",
                    image.getWidth(),
                    image.getHeight(),
                    planes[0].getRowStride(),
                    planes[0].getPixelStride(),
                    planes[1].getRowStride(),
                    planes[1].getPixelStride(),
                    planes[2].getRowStride(),
                    planes[2].getPixelStride());
                writer.printf("pixel_sha256 %s%n", pixelSha256);
                writer.printf(
                    "sensor_timestamp_ns %d%n", image.getTimestamp());
                if (activeSelection != null &&
                    activeSelection.intrinsics.length >= 5) {
                    writer.printf(
                        "intrinsics %.9g %.9g %.9g %.9g %.9g%n",
                        activeSelection.intrinsics[0],
                        activeSelection.intrinsics[1],
                        activeSelection.intrinsics[2],
                        activeSelection.intrinsics[3],
                        activeSelection.intrinsics[4]);
                }
                if (activeSelection != null &&
                    activeSelection.distortion.length >= 5) {
                    writer.printf(
                        "distortion %.9g %.9g %.9g %.9g %.9g%n",
                        activeSelection.distortion[0],
                        activeSelection.distortion[1],
                        activeSelection.distortion[2],
                        activeSelection.distortion[3],
                        activeSelection.distortion[4]);
                }
                if (activeSelection != null &&
                    activeSelection.rotation.length >= 4 &&
                    activeSelection.translation.length >= 3) {
                    writer.printf(
                        "camera_from_head %.9g %.9g %.9g %.9g " +
                        "%.9g %.9g %.9g%n",
                        activeSelection.rotation[0],
                        activeSelection.rotation[1],
                        activeSelection.rotation[2],
                        activeSelection.rotation[3],
                        activeSelection.translation[0],
                        activeSelection.translation[1],
                        activeSelection.translation[2]);
                }
            }
            if (!temporary.renameTo(manifest)) {
                throw new IOException("Cannot commit capture manifest");
            }
            nativeOnCaptureSaved(
                manifest.getAbsolutePath(), byteCount);
        } catch (IOException exception) {
            nativeOnCameraError(
                "Private capture failed: " + exception.getMessage());
        }
    }

    private static String computeFixturePixelSha256(
        Image image,
        Image.Plane[] planes,
        byte[][] bytes) {
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            updateDigestText(digest, "QUEST_CAMERA_PIXEL_SHA256_V2\n");
            updateDigestText(
                digest,
                String.format(
                    Locale.ROOT,
                    "%d %d %d %d %d %d %d %d%n",
                    image.getWidth(),
                    image.getHeight(),
                    planes[0].getRowStride(),
                    planes[0].getPixelStride(),
                    planes[1].getRowStride(),
                    planes[1].getPixelStride(),
                    planes[2].getRowStride(),
                    planes[2].getPixelStride()));
            for (byte[] planeBytes : bytes) {
                updateDigestText(
                    digest,
                    Integer.toString(planeBytes.length) + "\n");
                digest.update(planeBytes);
                updateDigestText(digest, "\n");
            }
            StringBuilder result = new StringBuilder(64);
            for (byte value : digest.digest()) {
                result.append(String.format(
                    Locale.ROOT, "%02x", value & 0xff));
            }
            return result.toString();
        } catch (NoSuchAlgorithmException exception) {
            throw new IllegalStateException(
                "Android runtime has no SHA-256 provider", exception);
        }
    }

    private static void updateDigestText(
        MessageDigest digest,
        String value) {
        digest.update(value.getBytes(StandardCharsets.UTF_8));
    }

    private static void writeAtomically(File target, byte[] bytes)
        throws IOException {
        File temporary = new File(target.getAbsolutePath() + ".tmp");
        try (FileOutputStream stream = new FileOutputStream(temporary)) {
            stream.write(bytes);
            stream.getFD().sync();
        }
        if (!temporary.renameTo(target)) {
            throw new IOException("Cannot commit " + target.getName());
        }
    }

    private synchronized void closeCamera() {
        if (captureSession != null) {
            captureSession.close();
            captureSession = null;
        }
        if (cameraDevice != null) {
            cameraDevice.close();
            cameraDevice = null;
        }
        if (imageReader != null) {
            imageReader.close();
            imageReader = null;
        }
        activeSelection = null;
        if (cameraThread != null) {
            cameraThread.quitSafely();
            cameraThread = null;
            cameraHandler = null;
        }
    }

    private static final class CameraSelection {
        final String cameraId;
        final int position;
        final Size size;
        final float[] intrinsics;
        final float[] distortion;
        final float[] rotation;
        final float[] translation;

        CameraSelection(
            String cameraId,
            int position,
            Size size,
            float[] intrinsics,
            float[] distortion,
            float[] rotation,
            float[] translation) {
            this.cameraId = cameraId;
            this.position = position;
            this.size = size;
            this.intrinsics = intrinsics;
            this.distortion = distortion;
            this.rotation = rotation;
            this.translation = translation;
        }
    }
}
