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
