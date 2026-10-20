"""Analyze paired diagnostic PNG sequences; requires numpy, Pillow, matplotlib.

Values are captured display brightness, not linear AO or a ground-truth error.
Static frames 8..23 follow warmup. Moving frames are excluded from variance.
"""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("directory", type=Path)
args = parser.parse_args()
root = args.directory
stacks = {}
for mode in ("off", "on"):
    frames = [np.asarray(Image.open(root / f"{mode}.frame{i}.png").convert("RGB"), dtype=np.float32)[..., 0] / 255 for i in range(48)]
    stacks[mode] = np.stack(frames)
if stacks["off"].shape != stacks["on"].shape:
    raise ValueError("Different sequence sizes")
off, on = stacks["off"], stacks["on"]
if not np.array_equal(off[0], on[0]):
    raise ValueError("First/reset frames differ; camera or sample inputs are not matched")
static = slice(8, 24)
# A common static AO region; do not include the mostly white background.
mask = off[static].mean(axis=0) < .995
if mask.sum() < 100:
    raise ValueError("Insufficient occluded pixels for a useful comparison")
deviation = {mode: stacks[mode][static].std(axis=0) for mode in stacks}
metrics = {"domain": "8-bit captured display brightness / 255 (not linear AO)",
           "static_frames_inclusive": [8, 23], "common_mask_pixels": int(mask.sum()),
           "off_mean_pixel_std": float(deviation["off"][mask].mean()),
           "on_mean_pixel_std": float(deviation["on"][mask].mean()),
           "mean_brightness_shift_on_minus_off": float((on[static].mean(axis=0)-off[static].mean(axis=0))[mask].mean()),
           "notes": "Static fluctuation only; reduced variance is not proof of accuracy or absence of ghosting."}
metrics["std_reduction_fraction"] = 1 - metrics["on_mean_pixel_std"] / max(metrics["off_mean_pixel_std"], 1e-12)
(root / "metrics.json").write_text(json.dumps(metrics, indent=2) + "\n", encoding="utf-8")
print(json.dumps(metrics, indent=2))
if metrics["off_mean_pixel_std"] < 1e-4:
    raise ValueError("Varying sampling did not produce a useful measurable stimulus")

# Pick a reproducible high-fluctuation ROI using ONLY the off sequence.
# Both modes then use the same crop and the same display mapping.
height, width = off.shape[1:]
window = 128
scores = [(float(deviation["off"][y:y+window, x:x+window].mean()), x, y)
          for y in range(0, height-window+1, 16) for x in range(0, width-window+1, 16)]
_, x0, y0 = max(scores)
roi = (slice(y0, y0+window), slice(x0, x0+window))
figure, axes = plt.subplots(2, 2, figsize=(9, 7), constrained_layout=True)
for column, mode in enumerate(("off", "on")):
    axes[0, column].imshow(stacks[mode][23][roi], cmap="gray", vmin=0, vmax=1)
    axes[0, column].set_title(f"Temporal {mode.upper()}: frame 23, same ROI")
    heat = axes[1, column].imshow(deviation[mode][roi], cmap="magma", vmin=0,
                                  vmax=max(float(deviation["off"][roi].max()), .001))
    axes[1, column].set_title("Static pixel standard deviation, frames 8–23")
    for row in range(2): axes[row, column].axis("off")
figure.colorbar(heat, ax=axes[1, :], label="Captured display brightness standard deviation")
figure.savefig(root / "comparison.png", dpi=130)
plt.close(figure)

# Literal AO captures, cropped and enlarged with nearest sampling; no contrast boost.
animation = []
for frame in range(48):
    canvas = Image.new("RGB", (512, 286), "#202020")
    draw = ImageDraw.Draw(canvas)
    for column, mode in enumerate(("off", "on")):
        pixels = np.rint(stacks[mode][frame][roi] * 255).astype(np.uint8)
        panel = Image.fromarray(pixels).convert("RGB").resize((256, 256), Image.Resampling.NEAREST)
        canvas.paste(panel, (column * 256, 30))
        draw.text((column * 256 + 6, 4), f"Temporal {mode.upper()}", fill="white")
    animation.append(canvas)
animation[0].save(root / "comparison.gif", save_all=True, append_images=animation[1:], duration=100,
                  loop=0, optimize=False)
print(f"ROI x={x0} y={y0} size={window}; GIF: {root / 'comparison.gif'}")
