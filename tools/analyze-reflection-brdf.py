"""Check HDR weighting against a CPU furnace estimator, then compare sample budgets.

The 256-sample image is a finite-sample reference for the same one-bounce model,
not path-traced ground truth. All error metrics use linear HDR receiver pixels.
"""
import json
import sys
from pathlib import Path
import numpy as np
from PIL import Image

root = Path(sys.argv[1])
report = {}

def load(name, kind="raw"):
    meta = json.loads((root / (name + ".json")).read_text())
    values = np.fromfile(root / (name + "." + kind + ".f32"), dtype="<f4")
    values = values.reshape(meta["height"], meta["width"], 4).astype(np.float64)
    assert np.isfinite(values).all(), name
    return meta, values

def shift(x, y, salt):
    value = (x * 0x9e3779b9 ^ y * 0x85ebca6b ^ salt) & 0xffffffff
    value = ((value ^ (value >> 16)) * 0x7feb352d) & 0xffffffff
    value = ((value ^ (value >> 15)) * 0x846ca68b) & 0xffffffff
    value ^= value >> 16
    return (value >> 8) * 2.0**-24

def cpu_furnace(meta, x, y, roughness):
    inverse_projection = np.array(meta["inverseProjection"]).reshape(4, 4, order="F")
    inverse_view = np.array(meta["inverseView"]).reshape(4, 4, order="F")
    ndc = np.array([(x + .5) / meta["width"] * 2 - 1,
                    (y + .5) / meta["height"] * 2 - 1, 1, 1])
    view_point = inverse_projection @ ndc
    direction = inverse_view[:3, :3] @ (view_point[:3] / view_point[3])
    v = -direction / np.linalg.norm(direction)
    nv = v[1]
    n = meta["samples"]
    radial = np.clip(shift(x, y, 0x6a09e667), .001, .999)
    rotation = shift(x, y, 0xbb67ae85)
    i = np.arange(n)
    # Independent bit reversal, rather than the GPU intrinsic.
    radical = np.array([sum(((int(k) >> bit) & 1) * 2.0**(-bit-1)
                            for bit in range(32)) for k in i])
    u = (i + radial) / n
    phi = 2 * np.pi * (radical + rotation)
    alpha = roughness**2
    nh = np.sqrt((1-u) / (1+(alpha*alpha-1)*u))
    tangent = np.sqrt(np.maximum(0, 1-nh*nh))
    h = np.column_stack([np.sin(phi)*tangent, nh, np.cos(phi)*tangent])
    vh = h @ v
    light = 2 * vh[:, None] * h - v
    nl = light[:, 1]
    valid = (nv > 0) & (nl > 0) & (vh > 0)
    def lam(c):
        return .5 * (np.sqrt(1+alpha*alpha*(1-c*c)/(c*c))-1)
    f0 = np.array([.75, .77, .8])
    fresnel = f0 + (1-f0) * (1-vh[valid, None])**5
    geometry = 1 / (1+lam(nv)+lam(nl[valid]))
    weight = fresnel * (geometry*vh[valid]/(nv*nh[valid]))[:, None]
    return weight.sum(axis=0) / n

for name in ("furnace8", "furnace256", "furnace-grazing"):
    meta, data = load(name)
    errors = []
    for tag in (.04, .27, .55):
        pixels = np.argwhere(np.isclose(np.abs(data[..., 3]), tag, atol=.001))
        assert len(pixels) > 0, tag
        for index in np.linspace(0, len(pixels)-1, 48).astype(int):
            y, x = pixels[index]
            expected = cpu_furnace(meta, int(x), int(y), tag)
            errors.append(float(np.max(np.abs(expected-data[y, x, :3]))))
    error = max(errors)
    assert error < .0015, (name, "CPU/GPU estimator mismatch", error)
    report[name+"_max_cpu_error"] = error
    print(f"PASS {name}: CPU/GPU sample estimator max HDR error {error:.6g}")

# Test the final composition separately: raw output already contains Fresnel.
# Isolated specular view, exposure=1, no panel, lower-floor pixels avoid source meshes.
for name in ("furnace8", "furnace256", "furnace-grazing", "scene8", "scene64", "scene256"):
    meta, data = load(name)
    rgb = np.array(Image.open(root / (name+".png")).convert("RGB")).astype(float)
    linear = 1-np.exp(-np.maximum(data[..., :3], 0))
    srgb = np.where(linear <= .0031308, 12.92*linear,
                    1.055*np.maximum(linear, 0)**(1/2.4)-.055)
    expected = np.clip(np.rint(srgb*255), 0, 255)
    receiver = np.abs(data[..., 3]) > 0
    mask = receiver.copy()
    mask[:int(meta["height"]*.68)] = False
    if not mask.any():
        # At grazing angles the finite floor may occupy only a narrow screen strip.
        # Use its bottommost rows rather than assuming the floor reaches the window edge.
        rows = np.nonzero(receiver)[0]
        assert len(rows) > 0, (name, "No visible receiver")
        cutoff = int(np.quantile(rows, .85))
        mask = receiver.copy()
        mask[:cutoff] = False
    assert mask.any(), (name, "No composition samples")
    errors = np.abs(expected[mask]-rgb[mask])
    assert errors.max() <= 2, (name, "Possible duplicate Fresnel/tone mapping", errors.max())
    report[name+"_composition_max_byte_error"] = float(errors.max())
print("PASS weighted HDR -> one exposure/sRGB composition (no second receiver Fresnel)")

meta, reference = load("scene256")
mask = np.abs(reference[..., 3]) > 0
rough_mask = mask & (np.abs(reference[..., 3]) > .1)
_, low = load("scene8")
_, filtered = load("scene8", "filtered")
_, medium = load("scene64")
for name, image in (("raw8", low), ("filtered8", filtered), ("raw64", medium)):
    delta = image[..., :3]-reference[..., :3]
    rmse = float(np.sqrt(np.mean(delta[rough_mask]**2)))
    bias = float(np.mean(delta[rough_mask]))
    report[name] = {"linear_hdr_rmse_vs_256": rmse, "mean_signed_error": bias}
    print(f"{name}: rough-receiver HDR RMSE vs 256 = {rmse:.6g}, mean error = {bias:.6g}")
assert report["raw64"]["linear_hdr_rmse_vs_256"] < report["raw8"]["linear_hdr_rmse_vs_256"], "No convergence in this fixed scene"
report["reference"] = "Finite 256-sample, single-bounce, same incoming shading; not ground truth."
(root / "analysis.json").write_text(json.dumps(report, indent=2))
