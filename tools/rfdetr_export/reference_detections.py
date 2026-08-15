#!/usr/bin/env python3
"""Create official RF-DETR reference detections for an approved Quest fixture."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

import numpy as np
from PIL import Image, ImageDraw

from export_model import CHECKPOINT_SHA256, ensure_checkpoint, sha256_file


def parse_fixture(path: Path) -> dict[str, Any]:
    lines = path.read_text(encoding="utf-8").splitlines()
    if not lines:
        raise ValueError("Quest fixture manifest is empty")
    fields = lines[0].split()
    if len(fields) != 12 or fields[0] != "QUEST_CAMERA_FIXTURE_V2":
        raise ValueError("Expected QUEST_CAMERA_FIXTURE_V2")
    values = [int(value) for value in fields[1:9]]
    width, height = values[:2]
    strides = values[2:]
    plane_names = fields[9:12]
    if width <= 0 or height <= 0 or any(value <= 0 for value in strides):
        raise ValueError("Fixture dimensions and strides must be positive")
    if any(
        Path(name).is_absolute()
        or len(Path(name).parts) != 1
        or name in {".", ".."}
        for name in plane_names
    ):
        raise ValueError("Fixture planes must be plain relative filenames")
    metadata: dict[str, list[str]] = {}
    expected_metadata_lengths = {
        "pixel_sha256": 1,
        "sensor_timestamp_ns": 1,
        "intrinsics": 5,
        "distortion": 5,
        "camera_from_head": 7,
    }
    for line in lines[1:]:
        tokens = line.split()
        if not tokens:
            continue
        if tokens[0] in metadata:
            raise ValueError(f"Duplicate fixture metadata: {tokens[0]}")
        expected_length = expected_metadata_lengths.get(tokens[0])
        if expected_length is None:
            raise ValueError(f"Unknown fixture metadata: {tokens[0]}")
        if len(tokens[1:]) != expected_length:
            raise ValueError(f"Malformed fixture metadata: {tokens[0]}")
        metadata[tokens[0]] = tokens[1:]
    checksum_values = metadata.get("pixel_sha256", [])
    if len(checksum_values) != 1:
        raise ValueError("Fixture requires one pixel_sha256")
    checksum = checksum_values[0]
    if len(checksum) != 64 or any(
        character not in "0123456789abcdef" for character in checksum
    ):
        raise ValueError("Fixture pixel_sha256 must be lowercase hexadecimal")
    planes = [(path.parent / name).read_bytes() for name in plane_names]
    for index, plane in enumerate(planes):
        plane_width = width if index == 0 else (width + 1) // 2
        plane_height = height if index == 0 else (height + 1) // 2
        row_stride = strides[index * 2]
        pixel_stride = strides[index * 2 + 1]
        last_byte = (plane_height - 1) * row_stride + (
            plane_width - 1
        ) * pixel_stride
        if last_byte >= len(plane):
            raise ValueError("Fixture plane is shorter than its geometry")
    capture = {
        "width": width,
        "height": height,
        "strides": strides,
        "planes": planes,
        "pixel_sha256": checksum_values[0],
    }
    actual = fixture_pixel_sha256(capture)
    if actual != capture["pixel_sha256"]:
        raise ValueError(
            f"Fixture pixel checksum mismatch: expected "
            f"{capture['pixel_sha256']}, actual {actual}"
        )
    return capture


def fixture_pixel_sha256(capture: dict[str, Any]) -> str:
    digest = hashlib.sha256()
    digest.update(b"QUEST_CAMERA_PIXEL_SHA256_V2\n")
    geometry = [capture["width"], capture["height"], *capture["strides"]]
    digest.update((" ".join(str(value) for value in geometry) + "\n").encode())
    for plane in capture["planes"]:
        digest.update(f"{len(plane)}\n".encode())
        digest.update(plane)
        digest.update(b"\n")
    return digest.hexdigest()


def convert_yuv420_to_rgba(capture: dict[str, Any]) -> np.ndarray[Any, Any]:
    width = capture["width"]
    height = capture["height"]
    y_row, y_pixel, u_row, u_pixel, v_row, v_pixel = capture["strides"]
    y_plane, u_plane, v_plane = capture["planes"]
    rgba = np.empty((height, width, 4), dtype=np.uint8)
    for y in range(height):
        for x in range(width):
            luminance = max(y_plane[y * y_row + x * y_pixel] - 16, 0)
            chroma_u = u_plane[(y // 2) * u_row + (x // 2) * u_pixel] - 128
            chroma_v = v_plane[(y // 2) * v_row + (x // 2) * v_pixel] - 128
            scaled_y = 298 * luminance
            rgba[y, x, 0] = np.clip(
                (scaled_y + 409 * chroma_v + 128) >> 8, 0, 255
            )
            rgba[y, x, 1] = np.clip(
                (scaled_y - 100 * chroma_u - 208 * chroma_v + 128) >> 8,
                0,
                255,
            )
            rgba[y, x, 2] = np.clip(
                (scaled_y + 516 * chroma_u + 128) >> 8, 0, 255
            )
            rgba[y, x, 3] = 255
    return rgba


def verify_cpp_pixels(
    cpp_tool: Path,
    model_manifest: Path,
    fixture_manifest: Path,
    expected_rgba: bytes,
) -> None:
    with tempfile.TemporaryDirectory(prefix="questlab-rfdetr-rgba-") as directory:
        rgba_path = Path(directory) / "frame.rgba"
        subprocess.run(
            [
                str(cpp_tool),
                "--manifest",
                str(model_manifest),
                "--fixture",
                str(fixture_manifest),
                "--validate-only",
                "--dump-rgba",
                str(rgba_path),
            ],
            check=True,
        )
        actual = rgba_path.read_bytes()
    if actual != expected_rgba:
        raise ValueError(
            "Python fixture decoder differs byte-for-byte from "
            "ConvertYuv420ToRgba"
        )


def draw_preview(image: Image.Image, detections: list[dict[str, Any]]) -> Image.Image:
    preview = image.copy()
    draw = ImageDraw.Draw(preview)
    for detection in detections:
        box = detection["box_xyxy"]
        label = f"{detection['class_name']} {detection['confidence']:.2f}"
        draw.rectangle(box, outline=(255, 48, 48), width=3)
        draw.text((box[0] + 2, max(0, box[1] - 12)), label, fill=(255, 48, 48))
    return preview


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--checkpoint", type=Path, required=True)
    parser.add_argument("--model-manifest", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--cpp-tool", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument(
        "--approved-fixture",
        action="store_true",
        help="Affirm that the real Quest fixture passed privacy review",
    )
    args = parser.parse_args()
    if not args.approved_fixture:
        raise ValueError(
            "Reference generation requires --approved-fixture; public or arbitrary "
            "stand-in images are not accepted"
        )
    ensure_checkpoint(args.checkpoint, allow_download=False)
    if sha256_file(args.checkpoint) != CHECKPOINT_SHA256:
        raise ValueError("Pinned checkpoint checksum changed")
    manifest = json.loads(args.model_manifest.read_text(encoding="utf-8"))
    class_names = {
        int(entry["id"]): str(entry["name"])
        for entry in manifest["classes"]["entries"]
    }
    fixture = parse_fixture(args.fixture)
    rgba = convert_yuv420_to_rgba(fixture)
    verify_cpp_pixels(
        args.cpp_tool,
        args.model_manifest,
        args.fixture,
        rgba.tobytes(),
    )

    from rfdetr import RFDETRNano

    model = RFDETRNano(
        pretrain_weights=str(args.checkpoint.resolve()),
        device="cpu",
    )
    image = Image.fromarray(rgba[:, :, :3], mode="RGB")
    predictions = model.predict(
        image,
        threshold=manifest["postprocessing"]["confidence_threshold"],
        shape=(384, 384),
        include_source_image=False,
    )
    class_names = predictions.data.get("class_name")
    if predictions.class_id is None or predictions.confidence is None or class_names is None:
        raise ValueError("Official RF-DETR result lacks class metadata")
    detections: list[dict[str, Any]] = []
    for box, class_id, class_name, confidence in zip(
        predictions.xyxy,
        predictions.class_id,
        class_names,
        predictions.confidence,
        strict=True,
    ):
        sparse_class_id = int(class_id)
        official_class_name = str(class_name)
        if class_names.get(sparse_class_id) != official_class_name:
            raise ValueError(
                "Official RF-DETR class ID/name differs from the pinned "
                f"sparse COCO map: {sparse_class_id}={official_class_name!r}"
            )
        detections.append(
            {
                "class_id": sparse_class_id,
                "class_name": official_class_name,
                "confidence": float(confidence),
                "box_xyxy": [float(value) for value in box],
            }
        )
    if not detections:
        raise ValueError(
            "Approved fixture produced no detections above the pinned threshold"
        )

    args.output_dir.mkdir(parents=True, exist_ok=True)
    output = {
        "schema": "QUESTLAB_RFDETR_DETECTIONS_V1",
        "frame": {
            "width": fixture["width"],
            "height": fixture["height"],
            "pixel_sha256": fixture["pixel_sha256"],
        },
        "model": {"onnx_sha256": manifest["model"]["onnx"]["sha256"]},
        "detections": detections,
        "reference_runtime": {
            "implementation": "rfdetr.RFDETRNano.predict",
            "rfdetr_version": "1.9.0",
            "checkpoint_sha256": CHECKPOINT_SHA256,
        },
    }
    (args.output_dir / "reference.json").write_text(
        json.dumps(output, indent=2) + "\n", encoding="utf-8"
    )
    draw_preview(image, detections).save(args.output_dir / "reference.png")
    print(f"Saved {len(detections)} reference detections to {args.output_dir}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exception:  # noqa: BLE001 - CLI reports one precise error.
        print(f"Reference generation failed: {exception}", file=sys.stderr)
        sys.exit(1)
