import unittest

from tools.analyze_gem_capture import analyze_rows, compare_pose_captures


def sample(frame, controller, timestamp, result=0, tracking=3, x=0.0):
    return {
        "frame": str(frame), "controller": str(controller),
        "result": str(result), "camera_timestamp": str(timestamp),
        "gem_time": str(timestamp), "tracking": str(tracking),
        "buttons": "0", "trigger": "0", "pos_x": str(x),
        "pos_y": "0", "pos_z": "1000", "handle_x": str(x - 20),
        "handle_y": "0", "handle_z": "1000",
    }


class GemCaptureAnalysisTests(unittest.TestCase):
    def test_reports_rate_tracking_dropouts_and_ranges(self):
        rows = []
        for frame in range(4):
            timestamp = 1_000_000 + frame * 20_000
            rows.append(sample(frame, 0, timestamp,
                               tracking=0 if frame in (1, 2) else 3,
                               x=float(frame)))
            rows.append(sample(frame, 1, timestamp,
                               result=5 if frame == 2 else 0,
                               x=float(-frame)))

        report = analyze_rows(reversed(rows))
        self.assertEqual(report["frames"], 4)
        self.assertAlmostEqual(report["duration_s"], 0.06)
        self.assertAlmostEqual(report["rate_hz"], 50.0)
        self.assertAlmostEqual(report["median_delta_ms"], 20.0)
        left = report["controllers"][0]
        right = report["controllers"][1]
        self.assertEqual(left["tracked"], 2)
        self.assertEqual(left["longest_dropout_frames"], 2)
        self.assertEqual(left["ranges"]["pos_x"], (0.0, 3.0))
        self.assertEqual(right["successful"], 3)
        self.assertEqual(right["results"], {0: 3, 5: 1})

    def test_empty_capture_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "no samples"):
            analyze_rows([])

    def test_optional_camera_image_ranges_are_reported(self):
        rows = [sample(frame, 0, 1_000_000 + frame * 20_000)
                for frame in range(3)]
        for frame, row in enumerate(rows):
            row.update(image_result="0", image_u=str(300 + frame * 10),
                       image_v=str(220 + frame * 5), image_radius="9",
                       image_distance="1200", image_visible="1",
                       image_radius_valid="1")
        rows[1]["image_visible"] = "0"
        report = analyze_rows(rows)
        self.assertEqual(report["controllers"][0]["image_visible_samples"], 2)
        self.assertEqual(report["controllers"][0]["image_ranges"]["image_u"],
                         (300.0, 320.0))

    def test_pose_comparison_uses_only_tracked_medians(self):
        center, right, up = [], [], []
        for frame in range(10):
            timestamp = 1_000_000 + frame * 20_000
            for controller in (0, 1):
                base_x = -250.0 if controller == 0 else 250.0
                center.append(sample(frame, controller, timestamp, x=base_x))
                shifted = sample(frame, controller, timestamp, x=base_x + 200)
                shifted["pos_y"] = "5"
                right.append(shifted)
                raised = sample(frame, controller, timestamp, x=base_x + 5)
                raised["pos_y"] = "180"
                up.append(raised)
        right[0]["tracking"] = "0"
        right[0]["pos_x"] = "90000"
        comparison = compare_pose_captures(center, right, up)
        self.assertEqual(set(comparison), {0, 1})
        self.assertEqual(comparison[0]["right_delta"], (200.0, 5.0, 0.0))
        self.assertEqual(comparison[1]["up_delta"], (5.0, 180.0, 0.0))

    def test_pose_comparison_rejects_untracked_captures(self):
        center = [sample(frame, 0, frame * 20_000) for frame in range(6)]
        right = [dict(row, tracking="0") for row in center]
        with self.assertRaisesRegex(ValueError, "five tracked samples"):
            compare_pose_captures(center, right, center)

    def test_pose_comparison_requires_both_move_controllers(self):
        center = [sample(frame, 0, frame * 20_000) for frame in range(6)]
        with self.assertRaisesRegex(ValueError, "both controllers"):
            compare_pose_captures(center, center, center)


if __name__ == "__main__":
    unittest.main()
