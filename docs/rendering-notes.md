# Rendering notes

## Animated mirrors

The main camera rasterizes the scene. Mirror fragments launch inline Ray Query rays into a shared scene acceleration structure. A miss samples the environment; an ordinary hit evaluates surface shading; a mirror hit continues the path. The implementation stores at most three mirror styles and Fresnel weights, then resolves them from the inside out. Surface styles use hit normals, atlas colors and ray-shadow visibility. Outer styles filter the incoming linear radiance rather than lighting the source albedo again. Exposure and final tone mapping occur in the main composition.

Environment misses use a separate detail-preserving filter. Toon blends continuous luminance with soft bands; cool/warm filtering blends a luminance-normalized tint with the original image. The style-strength budget is shared across layers. F9 disables all style layers for a PBR comparison.

Each frame follows:

```text
GPU skinning + surface normals
  -> gather ray-triangle positions/normals/UVs
  -> BLAS update -> TLAS update
  -> shadow / normal-depth-motion prepass / AO
  -> forward shading + mirror queries -> animation panel
```

Compute-to-AS, BLAS-to-TLAS and AS-to-shader dependencies are explicit. Topology remains fixed during updates. The current implementation refits one combined BLAS, prioritizing correctness over separating static and dynamic update costs. Reflection shading supports the character's shared sRGB atlas at LOD 0; ray transparency uses a 0.5 alpha cutoff. Rough multi-bounce transport and independent local shading of intermediate mirrors are not implemented.

## Additional demos

Run these commands from the repository root:

```powershell
# Temporal AO inspection: T toggles history; F2 toggles the spatial filter
.\build\Release\VulkanRenderingLab.exe --ssao-blurred --ao-temporal on --ao-samples 8 --paused

# Multiple prototype groups: C toggles frustum culling; F8 toggles Hi-Z rejection
.\build\Release\VulkanRenderingLab.exe --mixed-scene --hiz-inspector --paused

# Frozen character pose with PBR mirror shading
.\build\Release\VulkanRenderingLab.exe --ray-query-scene --animated-scene assets/models/character/character.glb --animation Hello --animation-time .52 --mirror-styles off --paused
```

Temporal AO is opt-in. It uses motion reprojection and history rejection; half-resolution AO is reconstructed with geometry guidance. Screen-space AO cannot recover off-screen geometry.

Visibility culling compacts IDs into disjoint group partitions and updates indirect instance counts. Shadow draws retain the full instance ranges. Hi-Z uses explicit static occluders and conservative depth tests. Its cost and benefit depend on the workload; reduced instance counts alone do not establish a frame-time improvement.

## Evidence and diagnostics

- `tools/verify-mirror-styles.ps1`: fixed-pose style ON/OFF captures, animated-geometry checks, ray-query checks and synchronization validation.
- `tools/verify-ray-character.ps1`: playback, frozen poses, skinning modes, live controls, resizing, AO-resolution rebuilding and reflection toggling.
- `tools/verify-ssao-temporal.ps1`: temporal AO verification.
- `tools/benchmark-culling.ps1`: controlled visibility workloads; see [measurement conditions and results](benchmarks/README.md).

Nearest-intersection checks do not constitute a complete multi-bounce image reference. Nested reflections and artistic style composition also require visual inspection. GPU validation and CPU readbacks add overhead and must be separated from timing runs.

## Rough ray-traced reflections

The independent rough-reflection scene uses three panels of the same metal with
roughness 0.04, 0.27 and 0.55, from left to right. Identical red spheres make the
change in reflection spread easy to see. The animated character uses the same
dynamic acceleration structure path as the mirror showcase.

```powershell
.\build\Release\VulkanRenderingLab.exe --rough-reflection-scene --animated-scene assets/models/character/character.glb --animation Hello --paused
.\tools\verify-mirror-styles.ps1 -Rough
```

Each receiver defaults to eight GGX NDF half-vector samples using a stratified
sequence scrambled per pixel. Contributions include the single-scattering GGX
BRDF, correlated Smith masking, Schlick Fresnel and the reflection PDF. Rejected
directions contribute zero but remain in the total sample count. F11 compares
scene-ray reflection with the existing environment-only approximation. Receiver
Fresnel is already included in the rough output and is not multiplied again at
composition. The ideal/stylized mirror path retains its existing composition.
Spatial filtering and opt-in static temporal accumulation are available. Motion
reprojection and rough multi-bounce transport are not implemented.


For a preset that selects the Release executable, repository working directory,
ray reflections ON and composite view explicitly, use
` .\tools\run-rough-reflections.ps1 `.
The beginning of the window title shows `RQ ON/OFF`; F11 also prints the current
state to the console. This distinguishes a disabled reflection path from a
rendering fault when the long diagnostic title is truncated.


## Reflection spatial filtering

Rough reflections are traced into a full-resolution RGBA32F image before forward
composition. Depth and view normals come from the existing opaque prepass. This
demo reconstructs the three fixed floor receivers using their scene-space bounds,
and stores their exact roughness tag alongside radiance. This receiver lookup is
specific to this scene, not a general material G-buffer.

Three 5x5 a-trous passes use spacings 1, 2 and 4, weighted by receiver depth and
normal agreement. Neighbor tags must match, preventing cross-panel mixing. Invalid
pixels and the sharp panel are copied exactly. The filtered reflection is composed
after BRDF weighting and before exposure; the main character shading
is unchanged. This is a spatial approximation and can soften reflected details. An additional
reflection-hit guide moderates that smoothing, as described below.

Press **B** for raw/filtered composition; the title shows Spatial ON/OFF.
Use **F10** reflection-radiance view to inspect the selected result, and keep
**F11** ray reflections ON. --reflection-denoise off selects the raw image.
The trace and filter still execute in both modes to keep the comparison identical;
this switch does not measure the total cost of disabling the filter infrastructure.
Resize and AO-resolution rebuilds recreate resources and preserve the selection.

| Raw, 8 samples | Receiver-guided spatial result |
|---|---|
| ![Raw rough reflection](images/rough-reflections-raw.png) | ![Spatially filtered rough reflection](images/rough-reflections.png) |

GPU timestamps report Reflection Trace and Reflection Spatial separately.
These intervals are not added together to substitute for the inclusive total.
tools/verify-reflection-spatial.ps1 checks finite values, exact receiver tags,
zero change on the sharp receiver, zero output outside receivers, reduced local
variation, animated geometry consistency, rebuilds and synchronization validation.
Verification-only image readbacks add overhead and are not performance data.
The checks do not establish ground-truth reflection quality or establish
the accuracy of the incoming scene-shading approximation.


## Reflection-hit-guided filtering

One additional deterministic, ideal-direction query records the reflected hit
normal and ray distance in RGBA32F; distance -1 identifies the environment and zero
identifies an invalid receiver. Alpha-aware candidate testing is shared with the
radiance queries. This is a stable proxy, not an average of the eight stochastic
radiance samples and not a complete description of the rough reflection lobe.

The spatial filter combines the receiver weights with reflected hit-distance,
hit-normal and geometry/environment similarity. Classification differences lower
weights rather than rejecting neighbors entirely. Guidance strength decreases with
roughness to avoid forcing a sharp ideal-ray silhouette onto a broad reflection.

Press **N** to compare hit-guided and receiver-only filtering, while **B** compares
filtered and raw reflections. --reflection-hit-guidance off fixes the receiver-only
comparison. Both modes generate the same raw radiance and guide queries, keeping
the sample budget unchanged. The selected state survives resource rebuilds.
The extra query is included in Reflection Trace timing; normal playback does not
read images back to the CPU.

Verification checks finite guide values, unit hit normals, geometry/environment
sentinels, zero guides outside receivers, sharp-panel invariance and the existing
spatial/ray checks. It does not measure ground-truth reflection error or establish
temporal stability. The ideal-direction proxy can misrepresent very rough lobes;
some detail loss and residual noise remain.

| Receiver guidance only | Receiver + reflection-hit guidance |
|---|---|
| ![Receiver-only filtering](images/rough-reflections-receiver-only.png) | ![Reflection-hit-guided filtering](images/rough-reflections.png) |


## Weighted reflection and finite-sample references

The rough receiver uses GGX NDF sampling (not visible-normal sampling), with
alpha = roughness squared. For a sampled half-vector h, the direction density is
D(h) (N dot h) / (4 V dot h). The sample contribution is incoming radiance times
BRDF times (N dot L), divided by that density. Cancelling D gives the stable
weight F * G2 * (V dot h) / ((N dot V) * (N dot h)). Samples below the hemisphere
are zero contributions; they must not be dropped from the denominator.

The model follows the [microfacet BRDF and reflection Jacobian in PBRT](https://www.pbr-book.org/4ed/Reflection_Models/Roughness_Using_Microfacet_Theory).
Here Fresnel uses the material's RGB F0 with Schlick, rather than complex conductor
IOR. Correlated Smith masking is evaluated directly for this indirect estimator;
the existing direct-light shader remains unchanged. No multiple-scattering energy
compensation or visible-normal sampling is implemented.

--reflection-samples accepts 1..256 (default 8). --reflection-reference fixes 256
samples, selects raw display with temporal accumulation off, freezes the light and camera sweep, and
freezes animated input at 0.52 seconds unless a pose was explicitly selected.
It is an offline inspection mode and can be slow; start at a small resolution.

    .\tools\run-rough-reflections.ps1 --reflection-reference --width 481 --height 321 --no-animation-panel
    .\tools\verify-reflection-brdf.ps1

The verification runner uses a fixed Hello pose, camera and light at 481x321.
It exports scene-linear HDR rather than measuring tone-mapped screenshots:
8-sample raw and filtered results and 64-sample raw are compared against the
same model at 256 samples on the two rougher receiver panels. The 256-sample
image is still noisy and uses the same approximate secondary-hit shading;
it is not full path-traced ground truth.

A local check on 2026-11-06 measured linear HDR RGB RMSE against that reference:

| Configuration | RMSE |
|---|---:|
| 8 samples, raw | 0.10514 |
| 8 samples, spatially filtered | 0.01541 |
| 64 samples, raw | 0.03082 |

These results describe this fixed scene and reference, not general filter quality
or GPU performance. Filtering can reduce variance while introducing detail bias.

Additional checks compare GPU unit-environment output against a CPU estimator,
including a grazing view, and compare final specular-only screenshots to one
HDR-to-exposure/sRGB composition (within one byte in the recorded run). This
detects duplicate receiver Fresnel. Portable tests independently integrate the
hemisphere to check importance weighting, furnace energy and the smooth F0 limit.
--reflection-furnace is diagnostic unit incoming radiance, not scene rendering.
--reflection-dump writes raw/filtered/temporal float32 RGBA plus JSON metadata and requires
--verify-ray-query. These readbacks are excluded from normal playback.


## Static temporal accumulation

    .\tools\run-rough-reflections.ps1 --animation-time .52 --reflection-temporal on
    .\tools\verify-reflection-temporal.ps1

Press **Q** to toggle accumulation; the title shows Accum ON/OFF and H, the
current history length. Keep **RQ ON** and **Spatial ON** to see the accumulated
result. **B** still shows the current raw frame when spatial composition is off.
The stage is opt-in. The launch script freezes the light; --animation-time freezes
the character. Playing the character or moving the camera intentionally resets
history rather than reprojecting it.

A submitted-frame phase scrambles the GGX sequence. The compute pass averages
scene-linear, BRDF-weighted samples **before** spatial filtering, with current
weight 1/(previous frames + 1). After 64 frames the weight stays at 1/64.
Sharp and invalid pixels pass through exactly. A CPU signature compares camera,
geometry pose and skinning modes, light and environment, and reflection settings
to the last submitted frame. A mismatch discards the whole history; stopping
allows accumulation to resume. Rebuilds preserve the toggle but not old pixels.
There is no motion reprojection or moving-history reuse.

The running mean and persistent history use separate RGBA32F images. A copy after
the compute dispatch updates history; barriers protect both the copy and spatial
reads. With accumulation off the stage copies the current frame through, keeping
command buffers and comparison inputs fixed. Reflection Temporal GPU timing
includes the dispatch, synchronization and history copy, even in passthrough mode.

In a fixed 481x321 Hello pose, compared on the two rougher panels with the same
finite 256-sample one-bounce reference:

| Configuration | Linear HDR RMSE | Mean luminance error |
|---|---:|---:|
| One 8-sample frame | 0.10514 | +0.397% |
| 32 frames of 8 samples | 0.02016 | -0.044% |
| 32 frames plus spatial filtering | 0.01308 | -0.070% |

This describes one fixed scene, not ground truth or a performance result. The
verification script checks the mean equation on GPU readback and tests camera,
animation, light, sampling/guide toggles, resize, AO-resolution rebuilds,
disabled mode, unit incoming radiance and the high-sample reference. Python and
NumPy analyze the final HDR buffers. Normal rendering does not read them back.
