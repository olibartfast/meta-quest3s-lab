#!/usr/bin/env python3
"""Download, verify, export, and inspect the pinned RF-DETR Nano model."""

from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
import shutil
import sys
import urllib.request
from pathlib import Path
from typing import Any

CHECKPOINT_URL = (
    "https://storage.googleapis.com/rfdetr/nano_coco/"
    "checkpoint_best_regular.pth"
)
CHECKPOINT_SHA256 = (
    "d8d6b9ee57d4d0ed2b1f305163624712a0532cb7bce0c747317984fc5457440d"
)
EXPECTED_INPUT = ("input", [1, 3, 384, 384])
EXPECTED_OUTPUTS = {
    "dets": [1, 300, 4],
    "labels": [1, 300, 91],
}
EXPECTED_VERSIONS = {
    "rfdetr": "1.9.0",
    "torch": "2.10.0+cpu",
    "torchvision": "0.25.0+cpu",
    "onnx": "1.20.1",
    "onnxruntime": "1.21.0",
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def ensure_checkpoint(path: Path, allow_download: bool) -> None:
    if not path.exists():
        if not allow_download:
            raise FileNotFoundError(
                f"Checkpoint is missing: {path}. Pass --download-checkpoint."
            )
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_suffix(path.suffix + ".tmp")
        try:
            with urllib.request.urlopen(CHECKPOINT_URL) as response:
                with temporary.open("wb") as output:
                    shutil.copyfileobj(response, output, 1024 * 1024)
            temporary.replace(path)
        finally:
            temporary.unlink(missing_ok=True)
    actual = sha256_file(path)
    if actual != CHECKPOINT_SHA256:
        raise ValueError(
            f"Checkpoint SHA-256 mismatch: expected {CHECKPOINT_SHA256}, "
            f"actual {actual}"
        )


def tensor_shape(value_info: Any) -> list[int]:
    dimensions: list[int] = []
    for dimension in value_info.type.tensor_type.shape.dim:
        if not dimension.HasField("dim_value"):
            raise ValueError(f"Dynamic dimension is not allowed: {value_info.name}")
        dimensions.append(int(dimension.dim_value))
    return dimensions


def inspect_onnx(path: Path) -> dict[str, Any]:
    import onnx

    model = onnx.load(path)
    onnx.checker.check_model(model)
    inputs = {value.name: tensor_shape(value) for value in model.graph.input}
    outputs = {value.name: tensor_shape(value) for value in model.graph.output}
    expected_input_name, expected_input_shape = EXPECTED_INPUT
    if inputs != {expected_input_name: expected_input_shape}:
        raise ValueError(f"Unexpected ONNX inputs: {inputs}")
    if outputs != EXPECTED_OUTPUTS:
        raise ValueError(f"Unexpected ONNX outputs: {outputs}")
    return {"inputs": inputs, "outputs": outputs}


def inspect_environment() -> dict[str, str]:
    if sys.version_info[:2] != (3, 11):
        raise ValueError(
            "RF-DETR export requires Python 3.11; actual "
            f"{sys.version.split()[0]}"
        )
    versions = {
        package: importlib.metadata.version(package)
        for package in EXPECTED_VERSIONS
    }
    if versions != EXPECTED_VERSIONS:
        raise ValueError(
            f"Export environment differs from the pin: expected "
            f"{EXPECTED_VERSIONS}, actual {versions}"
        )
    return {"python": sys.version.split()[0], **versions}


def update_manifest(
    manifest_path: Path, onnx_filename: str, onnx_sha256: str
) -> None:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    expected_filename = manifest["model"]["onnx"]["filename"]
    if onnx_filename != expected_filename:
        raise ValueError(
            f"Unexpected ONNX filename: expected {expected_filename}, "
            f"actual {onnx_filename}"
        )
    manifest["model"]["onnx"]["sha256"] = onnx_sha256
    manifest_path.write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--checkpoint", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--download-checkpoint", action="store_true")
    parser.add_argument("--update-manifest", action="store_true")
    args = parser.parse_args()

    versions = inspect_environment()
    ensure_checkpoint(args.checkpoint, args.download_checkpoint)

    from rfdetr import RFDETRNano

    args.output_dir.mkdir(parents=True, exist_ok=True)
    model = RFDETRNano(
        pretrain_weights=str(args.checkpoint.resolve()),
        device="cpu",
    )
    exported = Path(
        model.export(
            format="onnx",
            output_dir=str(args.output_dir),
            shape=(384, 384),
            batch_size=1,
            dynamic_batch=False,
            opset_version=17,
            verbose=False,
            notes={
                "contract": "QUESTLAB_RFDETR_MODEL_V1",
                "rfdetr": "1.9.0",
                "variant": "nano",
            },
        )
    )
    if not exported.is_absolute():
        exported = Path.cwd() / exported
    exported = exported.resolve()
    inspection = inspect_onnx(exported)
    onnx_sha256 = sha256_file(exported)
    if args.update_manifest:
        update_manifest(args.manifest, exported.name, onnx_sha256)

    record = {
        "schema": "QUESTLAB_RFDETR_EXPORT_RECORD_V1",
        "command": "uv run export_model.py --checkpoint <path> "
        "--output-dir <path> --manifest model-manifest.json "
        "--update-manifest",
        "checkpoint_sha256": CHECKPOINT_SHA256,
        "onnx_path": str(exported),
        "onnx_sha256": onnx_sha256,
        "versions": versions,
        **inspection,
    }
    record_path = args.output_dir / "export-record.json"
    record_path.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(record, indent=2))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exception:  # noqa: BLE001 - CLI reports one precise error.
        print(f"RF-DETR export failed: {exception}", file=sys.stderr)
        sys.exit(1)
