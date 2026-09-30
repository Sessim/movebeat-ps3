#!/usr/bin/env python3
"""Summarize MoveBeat's raw PS3 libgem diagnostic CSV capture."""

from __future__ import annotations

import argparse
import csv
import math
import statistics
from collections import Counter, defaultdict
from pathlib import Path
from typing import Dict, Iterable, List, Mapping


REQUIRED_COLUMNS = {
    "frame", "controller", "result", "camera_timestamp", "gem_time",
    "tracking", "buttons", "trigger", "pos_x", "pos_y", "pos_z",
    "handle_x", "handle_y", "handle_z",
}
IMAGE_COLUMNS = {
    "image_result", "image_u", "image_v", "image_radius",
    "image_distance", "image_visible", "image_radius_valid",
}


def load_rows(path: Path) -> List[Dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as source:
        reader = csv.DictReader(source)
        missing = REQUIRED_COLUMNS.difference(reader.fieldnames or [])
        if missing:
            raise ValueError("missing CSV columns: " + ", ".join(sorted(missing)))
        return list(reader)


def _integer(row: Mapping[str, str], name: str) -> int:
    try:
        return int(row[name])
    except (KeyError, ValueError) as exc:
        raise ValueError(f"invalid {name!r} in frame {row.get('frame', '?')}") from exc


def _number(row: Mapping[str, str], name: str) -> float:
    try:
        return float(row[name])
    except (KeyError, ValueError) as exc:
        raise ValueError(f"invalid {name!r} in frame {row.get('frame', '?')}") from exc


def _longest_false_run(values: Iterable[bool]) -> int:
    longest = current = 0
    for value in values:
        if value:
            current = 0
        else:
            current += 1
            longest = max(longest, current)
    return longest


def analyze_rows(rows: Iterable[Mapping[str, str]]) -> Dict[str, object]:
    ordered = sorted(rows, key=lambda row: (_integer(row, "frame"),
                                            _integer(row, "controller")))
    if not ordered:
        raise ValueError("capture contains no samples")

    timestamps = {}
    grouped = defaultdict(list)
    for row in ordered:
        frame = _integer(row, "frame")
        timestamps.setdefault(frame, _integer(row, "camera_timestamp"))
        grouped[_integer(row, "controller")].append(row)

    frame_times = [timestamps[frame] for frame in sorted(timestamps)]
    deltas = [right - left for left, right in zip(frame_times, frame_times[1:])
              if right > left]
    duration_s = ((frame_times[-1] - frame_times[0]) / 1_000_000.0
                  if len(frame_times) > 1 else 0.0)
    rate_hz = ((len(frame_times) - 1) / duration_s if duration_s > 0 else 0.0)

    controllers = {}
    for controller, samples in sorted(grouped.items()):
        successes = [row for row in samples if _integer(row, "result") == 0]
        tracked_flags = [
            _integer(row, "result") == 0 and
            (_integer(row, "tracking") & 3) == 3
            for row in samples
        ]
        ranges = {}
        for name in ("pos_x", "pos_y", "pos_z",
                     "handle_x", "handle_y", "handle_z"):
            values = [_number(row, name) for row in successes]
            ranges[name] = (min(values), max(values)) if values else None
        controllers[controller] = {
            "samples": len(samples),
            "successful": len(successes),
            "tracked": sum(tracked_flags),
            "longest_dropout_frames": _longest_false_run(tracked_flags),
            "results": dict(sorted(Counter(
                _integer(row, "result") for row in samples).items())),
            "ranges": ranges,
        }
        if IMAGE_COLUMNS.issubset(samples[0]):
            visible_images = [row for row in samples
                              if _integer(row, "image_result") == 0 and
                              _integer(row, "image_visible") != 0 and
                              math.isfinite(_number(row, "image_u")) and
                              math.isfinite(_number(row, "image_v"))]
            controllers[controller]["image_visible_samples"] = len(visible_images)
            controllers[controller]["image_ranges"] = {
                axis: (min(_number(row, axis) for row in visible_images),
                       max(_number(row, axis) for row in visible_images))
                if visible_images else None
                for axis in ("image_u", "image_v", "image_radius")
            }

    return {
        "frames": len(frame_times),
        "duration_s": duration_s,
        "rate_hz": rate_hz,
        "median_delta_ms": statistics.median(deltas) / 1000.0 if deltas else 0.0,
        "max_delta_ms": max(deltas) / 1000.0 if deltas else 0.0,
        "controllers": controllers,
    }


def tracked_pose_medians(rows: Iterable[Mapping[str, str]]) -> Dict[int, tuple]:
    positions = defaultdict(lambda: {"x": [], "y": [], "z": []})
    for row in rows:
        if _integer(row, "result") != 0 or (_integer(row, "tracking") & 3) != 3:
            continue
        controller = _integer(row, "controller")
        values = (_number(row, "pos_x"), _number(row, "pos_y"),
                  _number(row, "pos_z"))
        if not all(math.isfinite(value) for value in values):
            continue
        for axis, value in zip(("x", "y", "z"), values):
            positions[controller][axis].append(value)
    result = {}
    for controller, axes in positions.items():
        if len(axes["x"]) < 5:
            continue
        result[controller] = tuple(statistics.median(axes[axis])
                                   for axis in ("x", "y", "z"))
    return result


def compare_pose_captures(center: Iterable[Mapping[str, str]],
                          right: Iterable[Mapping[str, str]],
                          up: Iterable[Mapping[str, str]]) -> Dict[int, dict]:
    poses = {
        "center": tracked_pose_medians(center),
        "right": tracked_pose_medians(right),
        "up": tracked_pose_medians(up),
    }
    common = set(poses["center"]) & set(poses["right"]) & set(poses["up"])
    if not {0, 1}.issubset(common):
        raise ValueError("both controllers need five tracked samples in all three poses")
    comparison = {}
    for controller in sorted(common):
        anchor = poses["center"][controller]
        comparison[controller] = {
            "center": anchor,
            "right_delta": tuple(poses["right"][controller][axis] - anchor[axis]
                                 for axis in range(3)),
            "up_delta": tuple(poses["up"][controller][axis] - anchor[axis]
                              for axis in range(3)),
        }
    return comparison


def print_pose_comparison(comparison: Mapping[int, dict]) -> None:
    print("pose shifts (raw GEM units; target movement about 20 cm):")
    for controller, data in comparison.items():
        right = data["right_delta"]
        up = data["up_delta"]
        print(f"controller {controller}: right dX/Y/Z "
              f"{right[0]:+.1f}/{right[1]:+.1f}/{right[2]:+.1f}; "
              f"up dX/Y/Z {up[0]:+.1f}/{up[1]:+.1f}/{up[2]:+.1f}")
        if right[0] <= 0 or up[1] <= 0:
            print("  WARNING: measured X-right/Y-up signs differ from "
                  "the current input adapter")
        if abs(right[0]) <= max(abs(right[1]), abs(right[2])) or \
           abs(up[1]) <= max(abs(up[0]), abs(up[2])):
            print("  WARNING: intended right/up motion did not dominate "
                  "the expected axes; repeat the captures")


def print_report(report: Mapping[str, object]) -> None:
    print(f"frames: {report['frames']}")
    print(f"duration: {report['duration_s']:.3f} s")
    print(f"camera rate: {report['rate_hz']:.2f} Hz")
    print(f"frame delta median/max: {report['median_delta_ms']:.2f} / "
          f"{report['max_delta_ms']:.2f} ms")
    controllers = report["controllers"]
    assert isinstance(controllers, dict)
    for controller, data in controllers.items():
        assert isinstance(data, dict)
        samples = int(data["samples"])
        successful = int(data["successful"])
        tracked = int(data["tracked"])
        print(f"controller {controller}:")
        print(f"  successful: {successful}/{samples} "
              f"({100.0 * successful / samples:.1f}%)")
        print(f"  fully tracked: {tracked}/{samples} "
              f"({100.0 * tracked / samples:.1f}%)")
        print(f"  longest dropout: {data['longest_dropout_frames']} frames")
        print(f"  result codes: {data['results']}")
        ranges = data["ranges"]
        assert isinstance(ranges, dict)
        for axis, bounds in ranges.items():
            if bounds is not None:
                print(f"  {axis}: {bounds[0]:.3f} .. {bounds[1]:.3f}")
        if "image_visible_samples" in data:
            print(f"  visible image samples: {data['image_visible_samples']}/{samples}")
            for axis, bounds in data["image_ranges"].items():
                if bounds is not None:
                    print(f"  {axis}: {bounds[0]:.3f} .. {bounds[1]:.3f}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path, help="gem_capture.csv from the PS3")
    parser.add_argument("--right", type=Path,
                        help="second capture after moving both Move spheres right")
    parser.add_argument("--up", type=Path,
                        help="third capture after moving both Move spheres up")
    args = parser.parse_args()
    if bool(args.right) != bool(args.up):
        parser.error("--right and --up must be supplied together")
    try:
        center_rows = load_rows(args.capture)
        print_report(analyze_rows(center_rows))
        if args.right and args.up:
            print_pose_comparison(compare_pose_captures(
                center_rows, load_rows(args.right), load_rows(args.up)))
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
