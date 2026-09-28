"""Check rendered 2D rotation against direct pixel transposition (no redraw)."""
import json
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

import numpy as np
from PIL import Image

directory = Path(sys.argv[1])
base = Image.open(directory / "controls_base.png").convert("RGB")
results = {}
for name, transform in [
    ("image_cw90.png", Image.Transpose.ROTATE_270),
    ("image_ccw90.png", Image.Transpose.ROTATE_90),
    ("image_180.png", Image.Transpose.ROTATE_180),
    ("image_rect_cw90.png", Image.Transpose.ROTATE_270),
]:
    rectangular = name.startswith("image_rect")
    source = Image.open(directory / "image_rect_base.png").convert("RGB") if rectangular else base
    expected = source.transpose(transform)
    actual = Image.open(directory / name).convert("RGB")
    if rectangular:
        # Compare only the common 800x800 region. A rectangular viewport clips
        # the turned scene and can expose pixels outside the original viewport.
        def central_square(image):
            w, h = image.size
            side = min(w, h)
            return image.crop(((w-side)//2, (h-side)//2, (w+side)//2, (h+side)//2))
        expected, actual = central_square(expected), central_square(actual)
    ref = np.array(expected).astype(np.int16)
    got = np.array(actual).astype(np.int16)
    delta = np.abs(ref - got)
    metrics = {"mean_absolute_channel_error": float(delta.mean()),
               "fraction_pixels_error_gt_8": float((delta.max(axis=2) > 8).mean()),
               "max_channel_error": int(delta.max())}
    results[name] = metrics
    # Rerendering can change edge coverage / top-left triangle rasterization.
    assert metrics["mean_absolute_channel_error"] < 0.6, (name, metrics)
    assert metrics["fraction_pixels_error_gt_8"] < 0.008, (name, metrics)
assert np.array_equal(np.array(base), np.array(Image.open(directory / "image_reset.png").convert("RGB")))

# SVG remains genuine vector geometry. BSP may split equivalent triangles
# differently after rotation: compare filled geometry, not vertex identities.
def svg_triangles(path):
    root = ET.parse(path).getroot()
    assert not root.findall(".//{*}image"), "SVG unexpectedly contains a bitmap"
    triangles = {}
    for polygon in root.findall(".//{*}polygon"):
        color = polygon.get("fill")
        if color == "#ffffff":
            continue
        xy = [[float(v) for v in pair.split(",")] for pair in polygon.get("points").split()]
        assert len(xy) == 3, "Expected GL2PS triangle output"
        triangles.setdefault(color, []).append(xy)
    return {color: np.array(xy) for color, xy in triangles.items()}

def distance_to_geometry(points, triangles):
    # Distance to triangle union: zero inside, otherwise nearest edge.
    a = triangles
    b = np.roll(a, -1, axis=1)
    edge = b - a
    delta = points[:, None, None, :] - a[None, :, :, :]
    cross = edge[None, :, :, 0] * delta[..., 1] - edge[None, :, :, 1] * delta[..., 0]
    u, v = a[:, 1] - a[:, 0], a[:, 2] - a[:, 0]
    area = np.abs(u[:, 0] * v[:, 1] - u[:, 1] * v[:, 0])
    inside = (((cross >= -1e-10).all(axis=2) | (cross <= 1e-10).all(axis=2))
              & (area[None, :] > 1e-10))
    fraction = np.clip((delta * edge[None, :, :, :]).sum(axis=3)
                       / np.maximum((edge * edge).sum(axis=2)[None, :, :], 1e-20), 0, 1)
    separation = delta - fraction[..., None] * edge[None, :, :, :]
    distance = np.sqrt((separation * separation).sum(axis=3)).min(axis=2)
    distance[inside] = 0
    return distance.min(axis=1)

before = svg_triangles(directory / "controls_cell.svg")
after = svg_triangles(directory / "image_cell_cw90.svg")
assert before.keys() == after.keys()
assert before, "SVG contains no test geometry"
worst = 0.0
for color, xy in before.items():
    expected = np.stack((96.0 - xy[:, :, 1], xy[:, :, 0]), axis=2)
    actual = after[color]
    for source, target in [(expected, actual), (actual, expected)]:
        samples = np.concatenate((source.reshape(-1, 2), source.mean(axis=1),
                                  ((source + np.roll(source, -1, axis=1)) / 2).reshape(-1, 2)))
        for start in range(0, len(samples), 100):
            worst = max(worst, float(distance_to_geometry(samples[start:start+100], target).max()))
assert worst < 0.01, worst
results["SVG"] = {"max_sampled_geometry_distance_pt": worst, "embedded_bitmaps": 0}
print(json.dumps(results, indent=2))
print("PASS: CW/CCW/180 pixel rotation; no aspect distortion; reset pixel-exact; SVG geometry rotation")
