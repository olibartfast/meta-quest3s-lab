# RF-DETR C++ offline inference

This C++17 tool owns the Milestone 10 host contract. It loads only
`QUEST_CAMERA_FIXTURE_V2`, verifies the canonical pixel payload checksum, calls
the shared `ConvertYuv420ToRgba`, squash-resizes and normalizes according to the
model manifest, validates ONNX metadata, runs a warm-up plus repeated measured
inferences, and writes machine-readable detections and a labeled PPM preview.

Weight-free build and tests:

```bash
cmake -S tools/rfdetr_inference -B build/rfdetr-tests -DBUILD_TESTING=ON
cmake --build build/rfdetr-tests --parallel
ctest --test-dir build/rfdetr-tests --output-on-failure
```

Pinned ONNX Runtime build:

```bash
onnxruntime_root="$(tools/rfdetr_inference/prepare_onnxruntime.sh)"
cmake -S tools/rfdetr_inference -B build/rfdetr-inference \
  -DBUILD_TESTING=ON \
  -DQUESTLAB_ENABLE_ONNXRUNTIME=ON \
  -DONNXRUNTIME_ROOT="$onnxruntime_root"
cmake --build build/rfdetr-inference --parallel
```

Offline run after exporting the model and pulling an approved Quest fixture:

```bash
build/rfdetr-inference/rfdetr_inference \
  --manifest tools/rfdetr_export/model-manifest.json \
  --model build/models/rfdetr/rfdetr-nano.onnx \
  --fixture path/to/approved/manifest.qcam \
  --iterations 20 \
  --output-prefix build/rfdetr-results/frame-001
```

Add `--expected path/to/reference.json` to enforce exact counts and class IDs,
maximum confidence delta `0.0001`, and minimum matched-box IoU `0.999`.
Inference timing is local-only because the model and approved fixtures are not
stored in Git. The CI build deliberately leaves ONNX Runtime disabled and runs
preprocessing, coordinate, corruption, empty-output, and comparison tests.
