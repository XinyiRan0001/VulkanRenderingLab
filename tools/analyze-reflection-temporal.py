"""Static temporal mean against the same single-bounce finite-sample reference."""
from pathlib import Path
import json
import sys
import numpy as np
root = Path(sys.argv[1])
def load(name, kind):
    meta = json.loads((root/(name+".json")).read_text())
    data = np.fromfile(root/(name+"."+kind+".f32"), dtype="<f4")
    return meta, data.reshape(meta["height"],meta["width"],4).astype(float)
single_meta, single = load("single","temporal")
mean_meta, mean = load("accumulated","temporal")
_, raw32 = load("accumulated","raw")
_, spatial32 = load("accumulated","filtered")
reference_meta, reference = load("reference","raw")
assert single_meta["phase"] == 0 and single_meta["temporal_previous"] == 0
assert mean_meta["phase"] == 31 and mean_meta["temporal_previous"] == 31
assert not reference_meta["temporal_enabled"] and reference_meta["samples"] == 256
_, raw1 = load("single","raw")
assert np.array_equal(single,raw1), "Reset frame must be exactly current input"
for name in ("single","accumulated"):
    _, raw = load(name,"raw")
    _, averaged = load(name,"temporal")
    sharp = (np.abs(raw[...,3]) > 0) & (np.abs(raw[...,3]) < .1)
    assert np.array_equal(raw[sharp],averaged[sharp]), "Sharp receiver was temporally altered"
mask = np.abs(reference[...,3]) > .1
report = {}
luma = np.array([.2126,.7152,.0722])
reference_luma = float(np.mean(reference[mask,:3] @ luma))
for label, image in (("single8",single),("current8_at_frame32",raw32),("temporal32x8",mean),("temporal_plus_spatial",spatial32)):
    error = image[mask,:3] - reference[mask,:3]
    report[label] = {"linear_hdr_rmse_vs_256": float(np.sqrt(np.mean(error**2))),
                     "mean_signed_error": float(np.mean(error)),
                     "relative_mean_luminance_error_percent": float(100*np.mean(error @ luma)/reference_luma)}
    print(label,report[label])
assert report["temporal32x8"]["linear_hdr_rmse_vs_256"] < report["single8"]["linear_hdr_rmse_vs_256"], "No static variance reduction"
assert abs(report["temporal32x8"]["relative_mean_luminance_error_percent"]) < 1.0, "Mean luminance differs from reference by more than one percent"
# This finite-reference error is a diagnostic, not a proof of global unbiasedness.
report["reference"] = "Same one-bounce model, finite 256-sample reference; not ground truth."
(root/"analysis.json").write_text(json.dumps(report,indent=2))
print("PASS static noise reduction; reset/sharp invariance; reference mode remains temporal OFF")
