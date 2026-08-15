import copy
import unittest

from evaluate import ReportError, evaluate_report


def complete_report():
    camera = {
        "capabilities": ["LOGICAL_MULTI_CAMERA"],
        "logical_multi_camera": True,
        "physical_camera_ids": ["0", "1"],
        "sync_type": "CALIBRATED",
        "timestamp_source": "REALTIME",
        "intrinsics": [500.0, 500.0, 640.0, 480.0, 0.0],
        "distortion": [0.0, 0.0, 0.0, 0.0, 0.0],
        "pose_rotation": [0.0, 0.0, 0.0, 1.0],
        "pose_translation": [-0.032, 0.0, 0.0],
        "pose_reference": "GYROSCOPE",
        "yuv_sizes": [{"width": 1280, "height": 960}],
    }
    left = dict(camera, camera_id="0", position=0)
    right = dict(
        camera,
        camera_id="1",
        position=1,
        pose_translation=[0.032, 0.0, 0.0],
    )
    return {
        "schema_version": 1,
        "device": {"model": "Quest 3", "sdk": 34},
        "group_a": {
            "complete": True,
            "passthrough_cameras": [left, right],
            "concurrent_camera_sets": [["0", "1"]],
            "selected_pair": {
                "left_id": "0",
                "right_id": "1",
                "mechanism": "LOGICAL",
                "sync_type": "CALIBRATED",
                "common_yuv_sizes": [{"width": 1280, "height": 960}],
                "shared_pose_reference": True,
                "derived_baseline_meters": 0.064,
            },
        },
        "group_b": {
            "attempted": True,
            "configured": True,
            "left_frames": 300,
            "right_frames": 300,
            "matched_pairs": 300,
            "unmatched_left": 0,
            "unmatched_right": 0,
            "matched_fps": 30.0,
            "skew_ns": {
                "min": 20_000,
                "median": 200_000,
                "p95": 800_000,
                "max": 1_200_000,
            },
            "exposure": {"matched": True, "forceable": False},
        },
        "errors": [],
    }


class EvaluateReportTests(unittest.TestCase):
    def test_pass(self):
        result = evaluate_report(complete_report(), 0.064)
        self.assertEqual("PASS", result.verdict)
        self.assertEqual(
            {
                "P1A": True,
                "P1B": True,
                "P2": True,
                "P3": True,
                "P3O": True,
                "P4": True,
            },
            result.gates,
        )

    def test_partial_for_approximate_but_bounded_skew(self):
        report = complete_report()
        report["group_b"]["skew_ns"]["median"] = 2_000_000
        result = evaluate_report(report, 0.064)
        self.assertEqual("PARTIAL", result.verdict)
        self.assertFalse(result.gates["P3"])

    def test_fail_without_concurrent_or_logical_pair(self):
        report = complete_report()
        report["group_a"]["selected_pair"]["mechanism"] = "NONE"
        result = evaluate_report(report, 0.064)
        self.assertEqual("FAIL", result.verdict)
        self.assertFalse(result.gates["P1A"])

    def test_group_a_only_is_incomplete(self):
        report = complete_report()
        report["group_b"] = {"attempted": False}
        result = evaluate_report(report, 0.064)
        self.assertEqual("INCOMPLETE", result.verdict)
        self.assertTrue(result.gates["P1A"])
        self.assertIsNone(result.gates["P2"])

    def test_baseline_mismatch_becomes_calibration_debt(self):
        result = evaluate_report(complete_report(), 0.080)
        self.assertEqual("PASS_WITH_DEBT", result.verdict)
        self.assertFalse(result.gates["P1B"])

    def test_truncated_report_is_rejected(self):
        report = complete_report()
        del report["group_a"]["selected_pair"]
        with self.assertRaises(ReportError):
            evaluate_report(report)

    def test_missing_factory_calibration_is_recoverable_debt(self):
        report = copy.deepcopy(complete_report())
        report["group_a"]["passthrough_cameras"][0]["intrinsics"] = []
        result = evaluate_report(report, 0.064)
        self.assertEqual("PASS_WITH_DEBT", result.verdict)
        self.assertFalse(result.gates["P1B"])

    def test_absent_declared_sync_is_optical_validation_debt(self):
        report = complete_report()
        report["group_a"]["selected_pair"]["sync_type"] = "ABSENT"
        result = evaluate_report(report, 0.064)
        self.assertEqual("PASS_WITH_DEBT", result.verdict)
        self.assertFalse(result.gates["P3O"])
        self.assertTrue(any("physical exposure" in debt for debt in result.debts))

    def test_fewer_than_300_pairs_fails_p2(self):
        report = complete_report()
        report["group_b"]["matched_pairs"] = 299
        result = evaluate_report(report, 0.064)
        self.assertEqual("FAIL", result.verdict)
        self.assertFalse(result.gates["P2"])


if __name__ == "__main__":
    unittest.main()
