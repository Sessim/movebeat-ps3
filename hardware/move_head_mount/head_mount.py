#!/usr/bin/env python3
"""PS Move head mount: two snap clips on a spine with a GoPro-style mount.

The Move lies horizontally with its buttons up and the sphere forward. Two
C-shaped clips snap over the handle; a rear stop keeps it from sliding back.
Under the spine, two GoPro "fingers" (3 mm, M5 screw) fit the three-finger
buckle of any action-camera head strap; its hinge levels the controller.

    python head_mount.py            # writes the STL files next to this script
    python head_mount.py --preview  # also renders preview PNGs

Print the mount standing on its rear stop (the flat face at x = -4.5), with
a brim: every layer of a clip is then a whole C, so the snap flexes along
the layers instead of splitting them.

Coordinates in mm: x forward (toward the sphere), y left, z up; the
controller's axis runs along x at height AXIS_Z, its bottom end at x = 0.
Requires: pip install manifold3d trimesh numpy (matplotlib for --preview).
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import trimesh
from manifold3d import Manifold, set_circular_segments

set_circular_segments(96)

# --- Measure your Move and adjust -------------------------------------------
# Handle width (left-right) and height (buttons to trigger side) where the
# clips sit, measured from the bottom end of the controller. The defaults are
# estimates; print fit_gauge.stl first to find your sizes.
REAR_CLIP_X = 25.0
REAR_WIDTH = 33.0
REAR_HEIGHT = 31.0
FRONT_CLIP_X = 85.0      # stay clear of the trigger near the sphere
FRONT_WIDTH = 35.0
FRONT_HEIGHT = 33.0
FIT_CLEARANCE = 0.3      # per side; PLA/PETG at 0.2 mm layers

# --- Shape --------------------------------------------------------------------
CLIP_LENGTH = 16.0       # along the handle
CLIP_WALL = 3.2
CLIP_OPENING = 0.80      # mouth width as a share of the handle width (< 1 snaps)
SPINE_WIDTH = 16.0
SPINE_THICKNESS = 6.0
REAR_STOP_HEIGHT = 12.0  # how far the stop rises above the handle's bottom
MOUNT_X = 25.0           # GoPro hinge under the rear clip: the bottom end
                         # must not reach back to the forehead
MOUNT_DROP = 14.0        # hinge centre below the spine

# GoPro male mount (two fingers into a three-finger buckle).
GOPRO_FINGER = 3.0
GOPRO_GAP = 3.2
GOPRO_RADIUS = 7.5
GOPRO_HOLE = 5.3         # M5 thumb screw

AXIS_Z = max(REAR_HEIGHT, FRONT_HEIGHT) / 2 + CLIP_WALL + SPINE_THICKNESS


def box(x0, x1, y0, y1, z0, z1) -> Manifold:
    return Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0])


def elliptic_rod_x(x0, length, width, height) -> Manifold:
    """A rod along x with an elliptic section, centred on the handle axis."""
    rod = Manifold.cylinder(length, 0.5).rotate([0, 90, 0])  # along +x, r = 0.5
    return rod.scale([1.0, width, height]).translate([x0, 0.0, AXIS_Z])


def clip(x_mid: float, width: float, height: float) -> Manifold:
    inner_w = width + 2 * FIT_CLEARANCE
    inner_h = height + 2 * FIT_CLEARANCE
    x0 = x_mid - CLIP_LENGTH / 2
    ring = (elliptic_rod_x(x0, CLIP_LENGTH, inner_w + 2 * CLIP_WALL, inner_h + 2 * CLIP_WALL)
            - elliptic_rod_x(x0 - 1, CLIP_LENGTH + 2, inner_w, inner_h))
    # The mouth: open at the top (the button side), narrower than the handle
    # so the clip snaps over its widest point.
    half_mouth = inner_w * CLIP_OPENING / 2
    mouth_z = AXIS_Z + inner_h / 2 * np.sqrt(1 - CLIP_OPENING ** 2)
    ring -= box(x0 - 1, x0 + CLIP_LENGTH + 1, -half_mouth, half_mouth, mouth_z, AXIS_Z + 60)
    return ring


def spine() -> Manifold:
    bottom = AXIS_Z - max(REAR_HEIGHT, FRONT_HEIGHT) / 2 - CLIP_WALL - SPINE_THICKNESS + 1.0
    stop_x0, stop_x1 = -4.5, -0.5
    x1 = FRONT_CLIP_X + CLIP_LENGTH / 2
    beam = box(stop_x0, x1, -SPINE_WIDTH / 2, SPINE_WIDTH / 2, bottom,
               bottom + SPINE_THICKNESS + 4)
    # Rear stop: a wall just behind the controller's bottom end.
    stop_top = AXIS_Z - REAR_HEIGHT / 2 + REAR_STOP_HEIGHT
    stop = box(stop_x0, stop_x1, -REAR_WIDTH / 2.6, REAR_WIDTH / 2.6, bottom, stop_top)
    return beam + stop


def gopro_fingers() -> Manifold:
    spine_bottom = AXIS_Z - max(REAR_HEIGHT, FRONT_HEIGHT) / 2 - CLIP_WALL - SPINE_THICKNESS + 1.0
    hinge_z = spine_bottom - MOUNT_DROP
    fingers = Manifold()
    for side in (-1, 1):
        y0 = side * GOPRO_GAP / 2 if side > 0 else -GOPRO_GAP / 2 - GOPRO_FINGER
        disc = (Manifold.cylinder(GOPRO_FINGER, GOPRO_RADIUS).rotate([-90, 0, 0])
                .translate([MOUNT_X, y0, hinge_z]))
        web = box(MOUNT_X - GOPRO_RADIUS, MOUNT_X + GOPRO_RADIUS, y0, y0 + GOPRO_FINGER,
                  hinge_z, spine_bottom + 1)
        fingers += disc + web
    hole = (Manifold.cylinder(40, GOPRO_HOLE / 2).rotate([-90, 0, 0])
            .translate([MOUNT_X, -20, hinge_z]))
    return fingers - hole


def handle_cavity() -> Manifold:
    """The controller's handle (with clearance) between and around the clips."""
    rear = elliptic_rod_x(-5, FRONT_CLIP_X + CLIP_LENGTH, REAR_WIDTH + 2 * FIT_CLEARANCE,
                          REAR_HEIGHT + 2 * FIT_CLEARANCE)
    front = elliptic_rod_x(REAR_CLIP_X, FRONT_CLIP_X, FRONT_WIDTH + 2 * FIT_CLEARANCE,
                           FRONT_HEIGHT + 2 * FIT_CLEARANCE)
    return rear + front - box(-50, 0, -50, 50, -50, 100)


def head_mount() -> Manifold:
    body = (clip(REAR_CLIP_X, REAR_WIDTH, REAR_HEIGHT) +
            clip(FRONT_CLIP_X, FRONT_WIDTH, FRONT_HEIGHT) + spine() + gopro_fingers())
    return body - handle_cavity()


def fit_gauge() -> Manifold:
    """Open rings 30-40 mm on a strip: the one that snaps on gives the size."""
    gauge = Manifold()
    sizes = [30, 32, 34, 36, 38, 40]
    for index, size in enumerate(sizes):
        # Two rows of three, so the strip fits a 220 mm bed.
        x = (index % 3) * 56.0
        y_row = (index // 3) * 62.0
        inner = size + 2 * FIT_CLEARANCE
        ring = (Manifold.cylinder(8, inner / 2 + CLIP_WALL) - Manifold.cylinder(8, inner / 2))
        half_mouth = inner * CLIP_OPENING / 2
        ring -= box(-half_mouth, half_mouth, inner / 2 * np.sqrt(1 - CLIP_OPENING ** 2),
                    60, -1, 9)
        # The size as notches on the base tab: one notch per 2 mm above 28.
        tab = box(-8, 8, -inner / 2 - CLIP_WALL - 6, -inner / 2 - CLIP_WALL + 1, 0, 3)
        for notch in range((size - 28) // 2):
            tab -= box(-7 + notch * 2.5, -6 + notch * 2.5, -inner / 2 - CLIP_WALL - 7,
                       -inner / 2 - CLIP_WALL - 4.5, -1, 4)
        gauge += (ring + tab).translate([x, y_row, 0])
    return gauge


def to_trimesh(part: Manifold) -> trimesh.Trimesh:
    mesh = part.to_mesh()
    return trimesh.Trimesh(vertices=np.asarray(mesh.vert_properties)[:, :3],
                           faces=np.asarray(mesh.tri_verts), process=True)


def controller_model() -> Manifold:
    """A rough PS Move for previews: tapered handle, neck and 46 mm sphere."""
    handle = (elliptic_rod_x(0, REAR_CLIP_X, REAR_WIDTH, REAR_HEIGHT) +
              elliptic_rod_x(REAR_CLIP_X, 150 - REAR_CLIP_X, FRONT_WIDTH, FRONT_HEIGHT))
    neck = elliptic_rod_x(148, 12, 30, 30)
    sphere = Manifold.sphere(23).translate([177, 0, AXIS_Z])
    return handle + neck + sphere


def render(parts, path: Path, title: str, elev=22, azim=-58) -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from mpl_toolkits.mplot3d.art3d import Poly3DCollection

    figure = plt.figure(figsize=(9, 6), dpi=120)
    axes = figure.add_subplot(111, projection="3d")
    light = np.array([0.4, -0.5, 0.8])
    light /= np.linalg.norm(light)
    lows, highs = [], []
    for part, color in parts:
        mesh = to_trimesh(part)
        shade = np.clip(mesh.face_normals @ light, 0.15, 1.0)[:, None]
        colors = np.hstack([np.asarray(color[:3]) * (0.35 + 0.65 * shade),
                            np.full((len(shade), 1), color[3])])
        axes.add_collection3d(Poly3DCollection(mesh.triangles, facecolors=colors,
                                               edgecolor="none"))
        lows.append(mesh.bounds[0])
        highs.append(mesh.bounds[1])
    low, high = np.min(lows, axis=0), np.max(highs, axis=0)
    centre, span = (low + high) / 2, (high - low).max() / 2
    for setter, value in zip((axes.set_xlim, axes.set_ylim, axes.set_zlim), centre):
        setter(value - span, value + span)
    axes.set_box_aspect((1, 1, 1))
    axes.view_init(elev=elev, azim=azim)
    axes.set_axis_off()
    axes.set_title(title)
    figure.tight_layout()
    figure.savefig(path)
    plt.close(figure)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--preview", action="store_true")
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    mount = head_mount()
    gauge = fit_gauge()
    for name, part in (("move_head_mount.stl", mount), ("fit_gauge.stl", gauge)):
        mesh = to_trimesh(part)
        mesh.export(here / name)
        size = mesh.bounds[1] - mesh.bounds[0]
        print(f"{name}: {len(mesh.faces)} triangles, {size[0]:.1f} x {size[1]:.1f} x "
              f"{size[2]:.1f} mm, watertight={mesh.is_watertight}, "
              f"volume={mesh.volume / 1000:.1f} cm3")
    if args.preview:
        orange = (1.0, 0.55, 0.15, 1.0)
        grey = (0.55, 0.6, 0.7, 0.35)
        render([(mount, orange)], here / "preview_mount.png", "Move head mount")
        render([(mount, orange), (controller_model(), grey)], here / "preview_assembly.png",
               "With a PS Move (approximate)")
        render([(mount, orange)], here / "preview_side.png", "Side view", elev=0, azim=-90)
        render([(gauge, orange)], here / "preview_gauge.png", "Fit gauge 30-40 mm", elev=55)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
