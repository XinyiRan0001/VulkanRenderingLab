# Culling benchmark sample

One local run on NVIDIA GeForce RTX 4050 Laptop GPU, 1280 x 720, Release build.
Driver version is stored as the raw Vulkan vendor-specific integer in the CSV.
Three instance counts, three fixed views (yaw 0 / 35 / 90 degrees), pitch 25
degrees, camera distance 8, PCSS (shadow mode 3), static point light, IBL enabled.
Same opaque wall and low-detail mesh in every mode. All shadow instances are
retained; the existing light frustum does not cover the full large sphere field.

For every view and mode: 30 warmup frames, 120 measured frames, 3 repeats,
rotating order direct/frustum/Hi-Z, frustum/Hi-Z/direct, Hi-Z/direct/frustum.
Each table cell averages 360 samples. Validation layers were not explicitly
enabled for this performance run; separate correctness runs enabled Vulkan
validation and synchronization validation. No GPU clock or power locking was used.
This is a descriptive local measurement, not a statistically established speedup.

Mean inclusive GPU time, milliseconds (lower is better):

| Instances | View | Direct | Frustum | Frustum + Hi-Z |
|---:|---|---:|---:|---:|
| 1600 | wall_front | 1.372 | 1.301 | 1.315 |
| 1600 | wall_edge | 1.318 | 1.280 | 1.341 |
| 1600 | side_view | 1.317 | 1.307 | 1.427 |
| 6400 | wall_front | 2.014 | 1.762 | 1.646 |
| 6400 | wall_edge | 2.541 | 1.760 | 1.897 |
| 6400 | side_view | 1.896 | 1.832 | 1.874 |
| 10000 | wall_front | 2.643 | 2.273 | 2.264 |
| 10000 | wall_edge | 2.584 | 2.261 | 2.347 |
| 10000 | side_view | 2.504 | 2.321 | 2.403 |

The full summary CSV includes mean, median, nearest-rank p95, mean pass times,
visible counts and test settings. Raw per-frame CSVs remain in the ignored
`build/culling-benchmark/` directory; regenerate them with the command below.

The front view rejects 92 additional spheres through Hi-Z at each count; the
edge view rejects 24, the side view 6. At 10000 instances in the front view,
frustum culling cuts the visible set from 10000 to 3016; Hi-Z retains 2924.
The direct-to-frustum time reduction is substantial in this sample, while the
2.273 versus 2.264 ms frustum/Hi-Z difference is too small to establish a benefit.
Hi-Z can lose overall even when it reduces main-pass work. The 6400-instance
front-view result is more favorable in this run, but needs independent repeat
runs before drawing a stable conclusion. Do not generalize these numbers to
other models, GPUs, resolutions or shadow configurations.

## Reproduce

From the repository root:

```powershell
cmake --build build --config Release
.\tools\benchmark-culling.ps1
```

For one count or shorter experiments:

```powershell
.\build\Release\VulkanRenderingLab.exe --stress-count 1600 --benchmark-culling build/culling.csv --benchmark-warmup 30 --benchmark-samples 120 --benchmark-repeats 3
```

The benchmark forces the static wall scene and pauses light animation. Its fixed
views override yaw, while pitch, distance, target offset and shadow mode can be
set before the benchmark starts. Input callbacks are disabled during collection;
closing the window aborts the run. Window/swapchain changes invalidate the run.
The runner writes `<path>` raw samples and `<path>.summary.csv` aggregates.
Optional `--benchmark-captures` saves one image per view/mode during warmup;
leave it off for performance collection. Captures and full-ID verification are
not part of the published timings.

## What is measured

- **direct:** no cull/compact dispatch, no depth prepass or pyramid; direct
  instanced main draws fetch the original contiguous instance range.
- **frustum:** compute rejection + compaction + indirect main draws; no Hi-Z
  prepass/pyramid. An already initialized fallback texture replaces the pyramid
  descriptor, so skipped work does not leave a sampled image in an invalid layout.
- **frustum_hiz:** current-frame wall depth + pyramid + compute rejection + indirect
  main draws. Hi-Z allocations remain resident in all modes, so this measures
  execution cost rather than memory savings.

This is different from interactive C/F8 toggles: those retain the existing
compaction/prepass infrastructure to isolate rejection correctness.

All modes render the same character, floor, wall, materials and shadows. Debug
frustum drawing is omitted in all benchmark modes. Pipeline/descriptor changes,
command recording, initialization and warmup are outside sampled intervals.
The GPU total spans timestamp 6 before Hi-Z through timestamp 3 after main.
It does not time CPU acquire/present calls or end-to-end FPS. GPU-side waits
inside the measured interval, including dependencies or an acquire semaphore
wait, can contribute to the measured total.
Subpass timing brackets use top/bottom stages and can overlap; their sum need not
equal the inclusive total. Use the inclusive total for mode comparisons.
Small telemetry transfers remain in the compute interval. One frame is in flight;
per-frame fence waits allow safe current-frame query/telemetry readback but are
outside GPU total time. Presentation pacing and laptop clocks still affect load.

## Correctness

A separate short run covering all modes, views and three rotated repeats passed
Vulkan validation and synchronization validation. All three views had
pixel-identical direct/frustum/Hi-Z captures at 1600 instances (maximum RGB difference 0).
The frozen-camera observer regression still matched CPU/GPU visible IDs at
762/1600. Five CTest targets pass, including statistics edge cases. The published
9720 per-frame rows were checked for sample counts, finite nonnegative times,
visibility counter identities and summary agreement with raw mean/median.

Reference: [Vulkan query specification](https://docs.vulkan.org/spec/latest/chapters/queries.html).

## Mixed-prototype runner

`tools/benchmark-culling.ps1 -MixedScene` tests the sphere/cube scene and names
outputs `mixed-<count>.csv`, leaving the sphere-only sample files separate. The
current exporter adds `geometry` to raw/summary rows and a `.batches.csv` file for
per-group counts and address ranges. Counts describe unique objects per group,
not the sum of instance counts across material draws. The sample table above
remains the historical sphere-only run; no mixed-scene speedup is implied.
