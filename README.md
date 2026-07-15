# Vulkan Rendering Lab

A modern real-time renderer built with **C++17** and **Vulkan**, featuring multi-pass rendering, real-time shadow mapping, Percentage-Closer Filtering (PCF), Percentage-Closer Soft Shadows (PCSS), GPU profiling, and benchmark-driven rendering optimization.

This project extends concepts studied in the **GAMES202: High-Quality Real-Time Rendering** course by implementing them within a custom Vulkan renderer instead of the original WebGL framework.

Rather than reproducing the original coursework, the project focuses on building a modular Vulkan renderer from scratch, integrating multiple shadow algorithms, profiling GPU execution using Vulkan Timestamp Queries, and evaluating rendering performance through benchmark-driven optimisation.

---

# Features

## Rendering

- Vulkan 1.3 Renderer
- Forward Rendering
- Multi-pass Rendering
- OBJ Model Loading
- Multiple Materials
- Texture Mapping
- Dynamic Lighting
- Orbit Camera
- Runtime Shadow Mode Switching

---

## Shadow Mapping

Implemented multiple real-time shadow techniques:

- Hard Shadow
- Percentage-Closer Filtering (PCF)
- Percentage-Closer Soft Shadows (PCSS)
- Shadow Map Debug View

The renderer generates a shadow map from the light's point of view and samples it during the main rendering pass.

---

## Dynamic Lighting

A movable point light is supported.

The light automatically orbits around the scene while continuously updating:

- Light position
- Light View Matrix
- Shadow Projection Matrix

This enables fully dynamic shadows without restarting the renderer.

---

## Orbit Camera

The renderer provides an interactive orbit camera supporting:

- Mouse rotation
- Zoom
- Camera reset
- Adjustable distance
- Adjustable pitch
- Adjustable yaw

---

## Shadow Map Debug

A dedicated debug mode visualises the generated shadow map directly on screen.

Useful for inspecting:

- Shadow camera coverage
- Depth precision
- Shadow bias
- Projection artifacts

---

# Runtime Controls

| Key | Function |
|-----|----------|
| **1** | Hard Shadow |
| **2** | Percentage-Closer Filtering (PCF) |
| **3** | Percentage-Closer Soft Shadows (PCSS) |
| **4** | Shadow Map Debug |
| **P** | Toggle Dynamic Lighting |
| **Space** | Pause / Resume Light Animation |
| **M** | Toggle Orbit / Manual Light Mode |
| **J / L** | Move Light (X Axis) |
| **I / K** | Move Light (Z Axis) |
| **U / O** | Move Light (Y Axis) |
| **Left Mouse** | Rotate Camera |
| **Mouse Wheel** | Zoom Camera |
| **R** | Reset Camera |
| **B** | Run 5-second GPU Benchmark |
| **Esc** | Exit |

---

# Project Highlights

- Modular Vulkan renderer architecture
- Multi-pass rendering pipeline
- Runtime shadow algorithm switching
- GPU profiling using Vulkan Timestamp Queries
- Benchmark-driven rendering optimisation
- GAMES202-inspired rendering research

# Rendering Pipeline

The renderer uses two primary rendering passes:

1. Shadow Pass
2. Main Camera Pass

```text
Scene Geometry
      |
      v
Shadow Pass
      |
      v
Shadow Depth Image
      |
      v
Main Camera Pass
      |
      +----------------------+
      |                      |
      v                      v
Material Texture        Shadow Map
      |                      |
      +----------+-----------+
                 |
                 v
        Lighting Calculation
                 |
                 v
           Final Image
```

## Shadow Pass

The shadow pass renders scene geometry from the active shadow-casting light.

It performs the following operations:

1. Uses the light view-projection matrix
2. Renders all scene submeshes
3. Writes depth values into a 2048 กั 2048 shadow depth image
4. Makes the resulting depth image available to the main camera pass

The pass uses a dedicated:

- Shadow Render Pass
- Shadow Framebuffer
- Shadow Graphics Pipeline
- Depth Image
- Vertex Shader
- Descriptor Set

Only depth information is required, so the shadow pass does not produce a colour attachment.

## Main Camera Pass

The main pass renders the scene from the active camera.

It performs the following operations:

1. Samples material textures
2. Calculates direct lighting
3. Transforms world positions into shadow-map coordinates
4. Samples the shadow depth image
5. Applies the selected shadow algorithm
6. Writes the final image to the swapchain framebuffer

The active shadow mode is passed to the fragment shader through the shared uniform buffer.

---

# Shadow Algorithms

## Hard Shadow

Hard Shadow performs a single comparison between the receiver depth and the stored shadow-map depth.

```text
Receiver Depth
      |
      v
Shadow Depth Comparison
      |
      +---- Visible
      |
      +---- Shadowed
```

This technique is computationally inexpensive but produces sharp, aliased shadow boundaries.

It is used as the baseline for visual and performance comparisons.

## Percentage-Closer Filtering

Percentage-Closer Filtering performs multiple depth comparisons in a neighbourhood around the shadow coordinate.

The visibility results are averaged to produce a smoother shadow boundary.

```text
Neighbouring Shadow Samples
      |
      v
Multiple Depth Comparisons
      |
      v
Average Visibility
      |
      v
Filtered Shadow
```

The current implementation uses a fixed sampling kernel.

Compared with Hard Shadow, PCF reduces visible aliasing at the cost of additional shadow-map texture samples.

## Percentage-Closer Soft Shadows

Percentage-Closer Soft Shadows approximates contact-hardening shadows.

The algorithm consists of two main stages:

### 1. Blocker Search

The shader searches a neighbourhood around the receiver to find shadow-map samples that are closer to the light than the receiver.

The average blocker depth is then calculated.

### 2. Variable-Radius Filtering

The distance between the blocker and receiver is used to estimate the penumbra width.

```text
Blocker Search
      |
      v
Average Blocker Depth
      |
      v
Receiver-to-Blocker Distance
      |
      v
Penumbra Estimation
      |
      v
Variable-Radius PCF
```

This produces shadows that remain relatively sharp near contact areas and become softer as the receiver moves farther away from the blocker.

Because the shadow map is generated using a perspective projection, stored depth values are linearised before blocker-distance and penumbra calculations.

---

# GPU Profiling

The renderer includes a lightweight GPU profiling system implemented with Vulkan Timestamp Queries.

Timestamp queries are recorded directly into each command buffer around the two rendering passes.

```text
Timestamp 0
      |
      v
Shadow Pass
      |
      v
Timestamp 1

Timestamp 2
      |
      v
Main Camera Pass
      |
      v
Timestamp 3
```

The profiler calculates:

- Shadow Pass GPU time
- Main Camera Pass GPU time
- Total measured GPU time
- Continuously averaged GPU timings
- Sample count for each reporting interval

GPU timestamp ticks are converted into nanoseconds using the physical device's `timestampPeriod` value and then converted into milliseconds.

## Timestamp Query Sequence

Each recorded command buffer performs the following operations:

```text
Reset Timestamp Query Pool
      |
      v
Write Shadow Start Timestamp
      |
      v
Execute Shadow Pass
      |
      v
Write Shadow End Timestamp
      |
      v
Write Main Pass Start Timestamp
      |
      v
Execute Main Camera Pass
      |
      v
Write Main Pass End Timestamp
```

Query results are read only after the associated in-flight fence has completed.

This ensures that the GPU has finished executing the command buffer before timing data is accessed.

## Runtime Timing Output

GPU timings are accumulated and reported approximately every 0.5 seconds.

Example:

```text
[GPU Timing Average] Shadow: 0.159 ms | Main: 0.187 ms | Total: 0.348 ms | Samples: 145
```

Averaging multiple samples reduces the effect of individual-frame timing fluctuations.

---

# Automated GPU Benchmark

Pressing `B` starts a five-second benchmark for the currently selected shadow mode.

During the benchmark, the renderer:

1. Records the active shadow mode
2. Resets benchmark accumulators
3. Collects GPU timings continuously
4. Calculates average pass times
5. Reports the final result with the total sample count

Example output:

```text
========================================
[GPU Benchmark Result]
Mode:        PCSS
Shadow Pass: 0.164 ms
Main Pass:   0.231 ms
Total GPU:   0.396 ms
Samples:     1392
========================================
```

The benchmark mode supports:

- Hard Shadow
- PCF
- PCSS
- Shadow Map Debug View

The selected mode is stored when the benchmark begins, preventing the final result from being incorrectly labelled if runtime controls are used.

---

# GPU Performance Benchmark

GPU execution time was measured using Vulkan Timestamp Queries.

Each shadow mode was benchmarked continuously for five seconds.

| Shadow Mode | Shadow Pass | Main Pass | Total GPU |
|---|---:|---:|---:|
| Hard Shadow | 0.160 ms | 0.060 ms | 0.221 ms |
| PCF | 0.184 ms | 0.120 ms | 0.306 ms |
| PCSS (Optimized) | 0.164 ms | 0.231 ms | 0.396 ms |

> Results were captured in the current demo scene and should be interpreted as a comparison between algorithms rather than a universal hardware benchmark.

## Performance Analysis

The Shadow Pass performs approximately the same work for every shadow mode because all three modes use the same generated shadow map.

The most meaningful comparison is therefore the Main Pass, where the filtering algorithms are executed.

| Shadow Mode | Main Pass | Relative Cost |
|---|---:|---:|
| Hard Shadow | 0.060 ms | 1.00กั |
| PCF | 0.120 ms | 2.00กั |
| PCSS (Optimized) | 0.231 ms | 3.85กั |

PCF approximately doubled the measured Main Pass time compared with Hard Shadow.

PCSS remained the most expensive mode because it performs both:

- Blocker-search samples
- Variable-radius filtering samples

The benchmark demonstrates the visual-quality and GPU-cost trade-off between the implemented shadow algorithms.

## Measurement Notes

Benchmark values may vary depending on:

- GPU clock state
- Background applications
- Window size
- Build configuration
- Camera position
- Light position
- Visible shadow coverage
- Driver behaviour

For consistent comparisons, the same scene, camera position, lighting configuration, window size, and build configuration should be used.

---

# PCSS Performance Optimisation

GPU profiling identified the PCSS Main Pass as the primary shadow-related bottleneck.

The baseline PCSS implementation could perform:

- 25 blocker-search samples
- 49 variable-radius filtering samples
- Up to 74 shadow-map texture samples per affected fragment

To reduce unnecessary filtering work, an adaptive early-exit strategy was implemented.

## Early-Exit Conditions

### Fully Lit Fragments

When the blocker search finds no blockers, the fragment is considered fully visible.

The shader returns immediately without running the variable-radius filtering stage.

```text
No Blockers Found
      |
      v
Fully Lit
      |
      v
Skip Penumbra Filtering
```

### Fully Shadowed Fragments

When every blocker-search sample is classified as a blocker, the fragment is considered to be inside a fully shadowed region.

The shader returns a fully shadowed result without running the additional filtering stage.

```text
All Search Samples Are Blockers
      |
      v
Fully Shadowed
      |
      v
Skip Penumbra Filtering
```

### Penumbra Fragments

Only fragments located near a potential shadow boundary continue to the expensive variable-radius filtering stage.

```text
Mixed Blocker Results
      |
      v
Potential Penumbra
      |
      v
Calculate Penumbra Width
      |
      v
Run Variable-Radius Filtering
```

## Optimisation Result

The initial PCSS benchmark measured approximately:

```text
Main Pass: 0.248 ms
```

After introducing adaptive early exits, the measured PCSS Main Pass time decreased to approximately:

```text
Main Pass: 0.231 ms
```

This represents an initial measured reduction of approximately 7% in PCSS Main Pass GPU time.

The optimisation preserves the full filtering path for penumbra regions while avoiding redundant samples in clearly lit or deeply shadowed areas.

## Optimisation Workflow

The performance work followed this process:

```text
Implement PCSS
      |
      v
Add Vulkan Timestamp Queries
      |
      v
Profile Shadow and Main Passes
      |
      v
Identify PCSS Main Pass Bottleneck
      |
      v
Analyse Shadow-Map Sample Count
      |
      v
Implement Adaptive Early Exit
      |
      v
Re-run GPU Benchmark
      |
      v
Compare Before and After Results
```

This workflow was designed to reflect a profiling-driven graphics programming process rather than applying unmeasured changes.

---

# Visual Quality and Performance Trade-Off

Each implemented algorithm serves a different rendering goal.

| Technique | Visual Result | GPU Cost | Use Case |
|---|---|---:|---|
| Hard Shadow | Sharp edges | Lowest | Baseline or highly constrained rendering |
| PCF | Uniformly softened edges | Moderate | General-purpose filtered shadows |
| PCSS | Contact-hardening soft shadows | Highest | Higher-quality dynamic shadows |

Hard Shadow offers the best performance but the lowest visual quality.

PCF provides a practical balance between quality and cost.

PCSS provides the most realistic result of the three techniques but requires substantially more shadow-map sampling.

The runtime mode-switching and benchmark tools make these trade-offs directly observable within the application.