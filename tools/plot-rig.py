"""Plot C++ --inspect-animation/--dump-rig CSV; CPU joints, not a GPU render."""
import argparse
import csv
from pathlib import Path
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("csv", type=Path)
parser.add_argument("--clip", type=int, default=3)
parser.add_argument("--output", type=Path, default=Path("build/character-rig.png"))
args = parser.parse_args()
with args.csv.open(encoding="utf-8") as source:
    rows = [row for row in csv.DictReader(source) if int(row["clip"]) == args.clip]
if not rows:
    parser.error("Requested clip is absent from CSV")
points = np.array([[float(row[key]) for key in ("x", "y", "z")] for row in rows])
if not np.isfinite(points).all():
    raise ValueError("Nonfinite joint world positions")
low, high = points.min(axis=0), points.max(axis=0)
center, radius = (low + high) / 2, max(float(np.max(high-low)) * .55, .01)
figure = plt.figure(figsize=(12, 5), constrained_layout=True)
for column, step in enumerate(range(4)):
    ax = figure.add_subplot(1, 4, column+1, projection="3d")
    for row in rows:
        if int(row["step"]) != step:
            continue
        p = [float(row[key]) for key in ("x", "y", "z")]
        q = [float(row[key]) for key in ("parent_x", "parent_y", "parent_z")]
        ax.plot([p[0], q[0]], [p[2], q[2]], [p[1], q[1]], color="#2769a1", linewidth=.8)
    ax.set(xlim=(center[0]-radius, center[0]+radius),
           ylim=(center[2]-radius, center[2]+radius),
           zlim=(center[1]-radius, center[1]+radius),
           xlabel="X", ylabel="Z", zlabel="", title=f"Clip {args.clip}: {step*25}%")
    ax.set_box_aspect((1, 1, 1))
    ax.view_init(elev=10, azim=70)
figure.suptitle("CPU joint hierarchy at sampled poses; GPU skinning is not connected")
figure.savefig(args.output, dpi=140)
plt.close(figure)
print(args.output)
