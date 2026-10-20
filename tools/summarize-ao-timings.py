"""Summarize local AO GPU timestamp CSV files (stdlib only)."""
import argparse
import csv
import glob
import math
import re
import statistics
import sys
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("files", nargs="+", help="CSV paths or wildcard patterns")
args = parser.parse_args()
paths = sorted({p for pattern in args.files for p in glob.glob(pattern)})
if not paths:
    parser.error("No timing CSV files matched")
fields = ["skinning_ms", "prepass_ms", "ssao_ms", "blur_ms", "resolve_ms", "temporal_ms", "main_ms", "total_ms"]
writer = csv.writer(sys.stdout, lineterminator="\n")
writer.writerow(["file", "samples"] + [name for key in fields for name in (key+"_mean", key+"_p95")])
for filename in paths:
    with Path(filename).open(encoding="utf-8") as source:
        metadata = source.readline()
        expected = re.search(r" measured=(\d+)", metadata)
        rows = list(csv.DictReader(source))
    if not expected or len(rows) != int(expected[1]) or len(rows) < 2:
        raise ValueError(f"Incomplete timing capture: {filename}")
    if any(int(row["frame"]) != i for i, row in enumerate(rows)):
        raise ValueError(f"Invalid frame sequence: {filename}")
    result = [filename, len(rows)]
    for field in fields:
        if field in ("temporal_ms", "skinning_ms") and field not in rows[0]:
            result += ["", ""]  # Historic captures predate this pass.
            continue
        values = sorted(float(row[field]) for row in rows)
        if any(not math.isfinite(v) or v < 0 for v in values):
            raise ValueError(f"Invalid GPU timestamp duration: {filename}")
        result += [f"{statistics.mean(values):.6f}", f"{values[math.ceil(.95*len(values))-1]:.6f}"]
    writer.writerow(result)
