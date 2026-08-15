#!/usr/bin/env python3
"""Evaluate a Quest Camera2 stereo capability report.

The device records facts. This host tool owns the milestone thresholds and
turns those facts into an explicit PASS, PASS_WITH_DEBT, PARTIAL, FAIL, or
INCOMPLETE verdict.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


SCHEMA_VERSION = 1
MAX_BASELINE_ERROR_METERS = 0.005
MIN_MATCHED_FPS = 10.0
MIN_MATCHED_PAIRS = 300
MAX_UNMATCHED_FRACTION = 0.01
MAX_MEDIAN_SKEW_NS = 1_000_000
MAX_P95_SKEW_NS = 5_000_000
MAX_PAIR_WINDOW_NS = 20_000_000


class ReportError(ValueError):
    """Raised when a report cannot be evaluated safely."""


@dataclass(frozen=True)
class Evaluation:
    verdict: str
    gates: dict[str, bool | None]
    reasons: list[str]
    debts: list[str]
    warnings: list[str]

    def as_dict(self) -> dict[str, Any]:
        return {
            "verdict": self.verdict,
            "gates": self.gates,
            "reasons": self.reasons,
            "debts": self.debts,
            "warnings": self.warnings,
        }


def _mapping(value: Any, name: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ReportError(f"{name} must be an object")
    return value


def _list(value: Any, name: str) -> list[Any]:
    if not isinstance(value, list):
        raise ReportError(f"{name} must be an array")
    return value


def _finite_number(value: Any, name: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ReportError(f"{name} must be a number")
    result = float(value)
    if not math.isfinite(result):
        raise ReportError(f"{name} must be finite")
    return result


def _camera_is_calibrated(camera: dict[str, Any]) -> bool:
    pose_reference = camera.get("pose_reference")
    return (
        len(_list(camera.get("intrinsics"), "camera.intrinsics")) >= 5
        and len(_list(camera.get("distortion"), "camera.distortion")) >= 5
        and len(_list(camera.get("pose_rotation"), "camera.pose_rotation")) >= 4
        and len(_list(camera.get("pose_translation"), "camera.pose_translation")) >= 3
        and pose_reference not in (None, "ABSENT", "UNDEFINED")
    )


def evaluate_report(
    report: dict[str, Any],
    physical_baseline_meters: float | None = None,
) -> Evaluation:
    if report.get("schema_version") != SCHEMA_VERSION:
        raise ReportError(
            f"unsupported schema_version {report.get('schema_version')!r}; "
            f"expected {SCHEMA_VERSION}"
        )

    group_a = _mapping(report.get("group_a"), "group_a")
    cameras = [
        _mapping(camera, "group_a.passthrough_cameras[]")
        for camera in _list(
            group_a.get("passthrough_cameras"),
            "group_a.passthrough_cameras",
        )
    ]
    selected_pair = _mapping(
        group_a.get("selected_pair"), "group_a.selected_pair"
    )
    group_b = _mapping(report.get("group_b"), "group_b")

    reasons: list[str] = []
    debts: list[str] = []
    warnings: list[str] = []
    positions = {camera.get("position") for camera in cameras}
    distinct_pair = 0 in positions and 1 in positions
    factory_calibrated = len(cameras) >= 2 and all(
        _camera_is_calibrated(camera) for camera in cameras if camera.get("position") in (0, 1)
    )
    common_sizes = _list(
        selected_pair.get("common_yuv_sizes"),
        "group_a.selected_pair.common_yuv_sizes",
    )
    mechanism = selected_pair.get("mechanism")
    mechanism_available = mechanism in ("LOGICAL", "CONCURRENT")
    shared_reference = selected_pair.get("shared_pose_reference") is True
    derived_baseline = selected_pair.get("derived_baseline_meters")
    baseline_valid = False
    if derived_baseline is not None:
        derived_baseline_value = _finite_number(
            derived_baseline,
            "group_a.selected_pair.derived_baseline_meters",
        )
        baseline_valid = 0.02 <= derived_baseline_value <= 0.20
        if physical_baseline_meters is not None:
            measured = _finite_number(
                physical_baseline_meters, "physical_baseline_meters"
            )
            baseline_valid = (
                baseline_valid
                and abs(derived_baseline_value - measured)
                <= MAX_BASELINE_ERROR_METERS
            )
        else:
            warnings.append(
                "physical camera spacing was not supplied; baseline plausibility "
                "was checked, but the 5 mm physical cross-check remains pending"
            )

    p1a = (
        group_a.get("complete") is True
        and distinct_pair
        and bool(common_sizes)
        and mechanism_available
    )
    p1b = factory_calibrated and shared_reference and baseline_valid
    if not distinct_pair:
        reasons.append("left and right Meta passthrough cameras were not both exposed")
    if not factory_calibrated:
        debts.append(
            "produce and validate a per-device stereo calibration artifact; "
            "Camera2 did not expose complete factory distortion calibration"
        )
    if not shared_reference or not baseline_valid:
        debts.append(
            "derive stereo extrinsics and metric baseline during per-device "
            "calibration"
        )
    if not common_sizes:
        reasons.append("the cameras expose no common YUV_420_888 output size")
    if not mechanism_available:
        reasons.append("Camera2 exposes neither a logical nor concurrent pair")

    if group_b.get("attempted") is not True:
        return Evaluation(
            verdict="INCOMPLETE" if p1a else "FAIL",
            gates={
                "P1A": p1a,
                "P1B": p1b,
                "P2": None,
                "P3": None,
                "P3O": None,
                "P4": None,
            },
            reasons=reasons or ["simultaneous capture has not been measured yet"],
            debts=debts,
            warnings=warnings,
        )

    capture_configured = group_b.get("configured") is True
    matched_fps = _finite_number(group_b.get("matched_fps"), "group_b.matched_fps")
    matched_pairs = _finite_number(
        group_b.get("matched_pairs"), "group_b.matched_pairs"
    )
    left_frames = _finite_number(group_b.get("left_frames"), "group_b.left_frames")
    right_frames = _finite_number(group_b.get("right_frames"), "group_b.right_frames")
    unmatched_left = _finite_number(
        group_b.get("unmatched_left"), "group_b.unmatched_left"
    )
    unmatched_right = _finite_number(
        group_b.get("unmatched_right"), "group_b.unmatched_right"
    )
    total_frames = left_frames + right_frames
    unmatched_fraction = (
        (unmatched_left + unmatched_right) / total_frames
        if total_frames > 0
        else 1.0
    )
    p2 = (
        capture_configured
        and matched_pairs >= MIN_MATCHED_PAIRS
        and matched_fps >= MIN_MATCHED_FPS
        and unmatched_fraction < MAX_UNMATCHED_FRACTION
    )

    skew = _mapping(group_b.get("skew_ns"), "group_b.skew_ns")
    median_skew = _finite_number(skew.get("median"), "group_b.skew_ns.median")
    p95_skew = _finite_number(skew.get("p95"), "group_b.skew_ns.p95")
    max_skew = _finite_number(skew.get("max"), "group_b.skew_ns.max")
    p3 = (
        median_skew <= MAX_MEDIAN_SKEW_NS
        and p95_skew <= MAX_P95_SKEW_NS
        and max_skew <= MAX_PAIR_WINDOW_NS
    )

    optical_sync = group_b.get("optical_sync")
    optical_sync_validated = (
        isinstance(optical_sync, dict)
        and optical_sync.get("validated") is True
    )
    p3o = (
        selected_pair.get("sync_type") == "CALIBRATED"
        or optical_sync_validated
    )
    if not p3o:
        debts.append(
            "validate physical exposure synchronization with an optical timing "
            "test; equal Camera2 timestamps alone are not sufficient evidence"
        )

    if (
        group_b.get("left_timestamp_source") != "REALTIME"
        or group_b.get("right_timestamp_source") != "REALTIME"
    ):
        warnings.append(
            "Camera2 reports a non-REALTIME timestamp source; relative stereo "
            "pairing was measured, but Camera2-to-OpenXR time correlation remains "
            "unproven"
        )
        debts.append(
            "establish Camera2-to-OpenXR time correlation before world-space "
            "pose fusion"
        )

    exposure = _mapping(group_b.get("exposure"), "group_b.exposure")
    p4 = exposure.get("matched") is True or exposure.get("forceable") is True

    gates: dict[str, bool | None] = {
        "P1A": p1a,
        "P1B": p1b,
        "P2": p2,
        "P3": p3,
        "P3O": p3o,
        "P4": p4,
    }
    if p1a and p1b and p2 and p3 and p3o and p4:
        verdict = "PASS"
    elif p1a and p2 and p3 and p4:
        verdict = "PASS_WITH_DEBT"
    elif p1a and p2 and p95_skew <= MAX_P95_SKEW_NS:
        verdict = "PARTIAL"
    else:
        verdict = "FAIL"

    if not p2:
        reasons.append(
            "simultaneous capture did not produce 300 well-matched pairs at the "
            "required rate"
        )
    if not p3:
        reasons.append("timestamp skew failed the stereo-grade P3 limits")
    if not p4:
        reasons.append("exposure parity is neither observed nor controllable")
    return Evaluation(verdict, gates, reasons, debts, warnings)


def _parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("report", type=Path)
    parser.add_argument("--physical-baseline-meters", type=float)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(sys.argv[1:] if argv is None else argv)
    try:
        report = json.loads(args.report.read_text(encoding="utf-8"))
        if not isinstance(report, dict):
            raise ReportError("report root must be an object")
        evaluation = evaluate_report(report, args.physical_baseline_meters)
    except (OSError, json.JSONDecodeError, ReportError) as error:
        print(json.dumps({"verdict": "ERROR", "error": str(error)}, indent=2))
        return 2
    print(json.dumps(evaluation.as_dict(), indent=2))
    return 1 if evaluation.verdict == "FAIL" else 0


if __name__ == "__main__":
    raise SystemExit(main())
