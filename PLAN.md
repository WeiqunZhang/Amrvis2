# Amrvis Modernization Plan

> **Document status:** Living implementation plan  
> **Primary language:** C++20  
> **Initial UI:** Qt 6 Widgets  
> **Initial rendering:** `QImage` and `QPainter`  
> **Future optional 3-D:** VTK  
> **Core requirement:** Demand-driven, AMR-aware data access with bounded memory use

---

## 1. Purpose

Modernize Amrvis into a portable, maintainable C++ application that preserves its traditional capability for visualizing AMReX `FArrayBox`, `MultiFab`, and plotfile data while removing its dependency on X11, Xt, and Motif.

The new implementation must preserve one of Amrvis's most important operational properties:

> Only the data needed for the current visualization request should be loaded and retained.

Opening a dataset must not load all field values. Viewing a slice, line, point, subregion, or bounded volume must not require materializing the complete finest-resolution dataset.

Qt 6 Widgets will provide the desktop application framework. `QImage` and `QPainter` will implement the initial 1-D and 2-D presentation layer. Optional VTK-based 3-D volume rendering may be added later through the same demand-driven data service.

---

## 2. Instructions for the Coding Agent

This file is a **living plan**. The coding agent is explicitly allowed and expected to modify it as implementation proceeds.

A copy of the legacy Amrvis source is available at `external/Amrvis/` for coding agents to inspect as a behavioral and implementation reference.

### 2.1 Permitted plan updates

Except for the human-maintainer-controlled requirements identified in Sections 4 and 24.5, the Phase 0 gate, and the `Required` classifications in Section 23, the agent may:

- Mark tasks and milestones as complete.
- Add newly discovered implementation tasks.
- Split large tasks into smaller tasks.
- Update estimates of complexity or ordering.
- Record design decisions and their rationale.
- Record risks, blockers, failed approaches, and technical debt.
- Add acceptance tests discovered during implementation.
- Adjust internal interfaces when implementation evidence justifies the change.
- Add links to relevant commits, pull requests, issues, benchmarks, and design documents.
- Move deferred work into active phases when prerequisites are complete.
- Update the compatibility matrix as features are verified.

### 2.2 Rules for modifying the plan

The agent must:

1. Preserve the project mission, scope, and non-negotiable requirements unless a human maintainer explicitly changes them.
2. Never silently remove a required feature or acceptance criterion.
3. Record material architectural changes in the **Decision Log**.
4. Record changes to scope, ordering, or major milestones in the **Plan Change Log**.
5. Keep completed items visible rather than deleting them.
6. Distinguish clearly between:
   - completed work,
   - work in progress,
   - planned work,
   - deferred work,
   - rejected approaches.
7. Update the **Current Status** section whenever a milestone materially advances.
8. Prefer small, reviewable plan edits tied to concrete implementation findings.
9. Avoid rewriting this entire document merely to change formatting.
10. Treat tests, benchmarks, documentation, and memory measurements as implementation deliverables, not optional cleanup.
11. Treat the non-negotiable requirements in Section 4 and the demand-loading tests in Section 24.5 as human-maintainer-controlled tripwires. An agent must not delete, weaken, reword, defer, make optional, or otherwise reclassify them. Only a human maintainer may change them, and every such change must be explicitly recorded as human-authorized in the **Plan Change Log**.
12. Update evidence, implementation status, and reference tests in Section 23, but never change a capability's `Required` classification without explicit human-maintainer authorization in the **Plan Change Log**.
13. Never skip or defer Phase 0, weaken its exit criteria, or mark a later phase complete before its exit criteria are met. Only a human maintainer may change this gate, with the exact human-authorized change recorded in the **Plan Change Log**.

### 2.3 Status notation

Use these markers throughout the plan:

- `[ ]` Not started
- `[~]` In progress
- `[x]` Complete
- `[!]` Blocked
- `[-]` Deferred
- `[r]` Rejected

### 2.4 Current Status

| Item | Status | Notes |
|---|---:|---|
| Architecture approved | `[x]` | Qt 6 Widgets, toolkit-independent core, demand-driven AMR service |
| Phase 0 compatibility corpus | `[ ]` | Critical-path gate; no later phase may be marked complete until the versioned reference artifact and requirement evidence are complete |
| Repository skeleton | `[ ]` | |
| Metadata reader | `[ ]` | |
| Selective FAB reads | `[ ]` | |
| Bounded cache | `[ ]` | |
| Slice query | `[ ]` | |
| Minimal Qt viewer | `[ ]` | |
| Traditional feature parity | `[ ]` | |
| Optional VTK volume rendering | `[-]` | Later phase |

---

## 3. Scope

### 3.1 In scope

The modernized application shall support traditional Amrvis workflows:

- Open and inspect standalone `FArrayBox` data.
- Open and inspect standalone `MultiFab` data.
- Open and inspect AMReX plotfiles containing multiple AMR levels.
- Select a scalar component.
- Support a registry of derived fields.
- Display 1-D and 2-D data.
- Display orthogonal slices of 3-D data.
- Navigate slices interactively.
- Zoom, pan, probe, and select subregions.
- Display grid boundaries and AMR levels.
- Use file, level, subregion, visible-region, and user-specified value ranges.
- Display palettes and color bars.
- Draw contours.
- Draw vector overlays when suitable components are available.
- Produce line plots.
- Animate slices and plotfile sequences.
- Export images and extracted data.
- Preserve fine-over-coarse AMR composition semantics.
- Preserve demand-driven, region-aware data loading.
- Enforce configurable memory budgets.
- Support Linux and macOS as first-class platforms.
- Remain compatible with Windows where practical.
- Permit optional VTK-based 3-D volume rendering later.
- Permit optional remote data access later.

### 3.2 Explicitly out of scope

- Profiling-result visualization.
- Call-trace visualization.
- Communication profiling.
- Region profiling.
- Reimplementation of deprecated profiling parsers.
- A line-by-line translation of the X11/Motif implementation.
- A mandatory VTK dependency for 1-D or 2-D visualization.
- Loading the complete finest-level domain to render a small view.
- Preserving X11 as a permanent backend.
- Reproducing obsolete machine-specific build branches.
- Reproducing unused legacy parallel volume-rendering experiments without a new use case.

### 3.3 Deferred compatibility features

These should be supported by the architecture but do not block the first usable release:

- Embedded-boundary body masking using volume fractions.
- Terrain-specific visualization behavior.
- Remote visualization over a network.
- In-situ attachment to a running simulation.
- Native AMR-aware VTK volume rendering.
- Advanced GPU rendering for large 2-D views.
- Plugin APIs for external derived fields.

---

## 4. Non-Negotiable Requirements

> **Human-maintainer-controlled:** Coding agents must not edit, weaken, defer, or reclassify the requirements in this section. Only a human maintainer may change them, with the authorization and exact change recorded in the **Plan Change Log**.

1. The implementation is written in C++.
2. The primary desktop UI uses Qt 6 Widgets.
3. The initial 2-D renderer uses `QImage` and `QPainter`.
4. The AMR data core does not depend on Qt or VTK.
5. Dataset opening loads metadata, not all field values.
6. Data requests identify field, component, space, level, and resolution explicitly.
7. Only intersecting FAB data needed by a request is loaded.
8. Memory use is bounded by configurable cache budgets.
9. Fine data overrides coarse data during AMR composition.
10. Disk I/O and expensive computation do not run on the Qt GUI thread.
11. Requests can be cancelled or made harmless through stale-result rejection.
12. VTK remains an optional module.
13. A small visible region must not cause an unconditional full-domain read.
14. Tests must verify numerical results, AMR coverage, bytes read, and resident cache size.
15. Public APIs must not expose hidden I/O through innocent-looking getters.

---

## 5. Technical Baseline

### 5.1 Language and build

- Baseline language standard: C++20.
- Require C++20 support throughout the project compiler matrix.
- Build system: CMake.
- UI toolkit: Qt 6 Widgets.
- Initial rendering: `QImage` and `QPainter`.
- Optional 3-D rendering: VTK through `QVTKOpenGLNativeWidget`.
- Unit and integration tests: CTest plus a lightweight C++ test framework.
- Formatting: `clang-format`.
- Static analysis: `clang-tidy` where practical.
- Runtime checks:
  - AddressSanitizer,
  - UndefinedBehaviorSanitizer,
  - ThreadSanitizer for selected tests where supported.

C++20 is selected as the project baseline. Public APIs may use C++20 library types where they improve clarity and correctness. A project cancellation abstraction should use `std::stop_token` where it fits the request and backend contracts.

### 5.2 Supported configurations

The build must support:

```text
AMRVIS_ENABLE_QT=ON
AMRVIS_ENABLE_VTK=OFF
AMRVIS_ENABLE_MPI=OFF
AMRVIS_BUILD_TESTS=ON
```

Later configurations may include:

```text
AMRVIS_ENABLE_VTK=ON
AMRVIS_ENABLE_MPI=ON
AMRVIS_ENABLE_REMOTE=ON
```

The basic 1-D and 2-D viewer must not require VTK or MPI.

---

## 6. Architecture

```text
+---------------------------------------------------------------+
| Qt application                                                |
| MainWindow, controllers, dialogs, menus, coordinated views    |
+-------------------------------+-------------------------------+
                                |
                                | typed asynchronous requests
                                v
+---------------------------------------------------------------+
| AMR data client/service                                       |
| planning, composition, caching, scheduling, cancellation      |
+-------------------------------+-------------------------------+
                                |
                                | selective block requests
                                v
+---------------------------------------------------------------+
| Backends                                                      |
| plotfile, standalone FAB, MultiFab, live/in-situ, remote      |
+---------------------------------------------------------------+

+---------------------------------------------------------------+
| Rendering adapters                                            |
| toolkit-neutral 2-D scene -> Qt adapter                       |
| optional volume results -> VTK adapter                        |
+---------------------------------------------------------------+
```

### 6.1 Major libraries

```text
amrvis_core
    immutable metadata
    request and result types
    coordinate conversion
    AMR query planning
    fine-over-coarse composition
    derived fields
    scheduling and cancellation

amrvis_io
    plotfile backend
    FArrayBox backend
    MultiFab backend
    selective FAB reads
    per-block metadata/statistics

amrvis_cache
    bounded raw-block cache
    bounded computed-result cache
    pinning and eviction
    instrumentation

amrvis_render2d
    scalar-to-color mapping
    palette handling
    contours
    vector glyph generation
    line and box primitives
    toolkit-neutral image buffers

amrvis_qt
    application
    widgets
    controllers
    Qt image adapter
    dialogs and menus

amrvis_vtk
    optional
    VTK data adapters
    volume view
    transfer functions
```

### 6.2 Dependency rules

- `amrvis_core` must not link Qt or VTK.
- `amrvis_io` may link AMReX I/O facilities but not Qt or VTK.
- `amrvis_render2d` must not use `QImage`.
- `amrvis_qt` may depend on core, I/O service interfaces, and render2d.
- `amrvis_vtk` may depend on core and VTK.
- Backends must not call Qt event APIs.
- Views must not call `VisMF` directly.
- GUI objects must not own loaded FAB cache entries longer than necessary.

---

## 7. Suggested Repository Layout

```text
amrvis/
├── CMakeLists.txt
├── PLAN.md
├── cmake/
├── docs/
│   ├── architecture.md
│   ├── data-query-model.md
│   ├── compatibility-matrix.md
│   └── developer-guide.md
├── src/
│   ├── core/
│   │   ├── dataset/
│   │   ├── metadata/
│   │   ├── query/
│   │   ├── composition/
│   │   ├── scheduler/
│   │   └── derived/
│   ├── io/
│   │   ├── plotfile/
│   │   ├── fab/
│   │   ├── multifab/
│   │   └── remote/
│   ├── cache/
│   ├── render2d/
│   │   ├── image/
│   │   ├── palette/
│   │   ├── contour/
│   │   ├── vector/
│   │   └── overlays/
│   ├── qt/
│   │   ├── app/
│   │   ├── widgets/
│   │   ├── controllers/
│   │   └── models/
│   └── vtk/
│       ├── volume/
│       └── adapters/
├── tests/
│   ├── unit/
│   ├── integration/
│   ├── reference/
│   ├── performance/
│   └── data/
└── tools/
    ├── dataset_inspect/
    ├── query_benchmark/
    └── reference_capture/
```

The `src/vtk` directory and all VTK dependencies must be optional.

---

## 8. Core Design Principles

### 8.1 Metadata is cheap and immutable

Opening a dataset should load only structural information:

- dimensionality,
- number of levels,
- variable names,
- component mappings,
- centering,
- level domains,
- refinement ratios,
- cell sizes,
- physical geometry,
- box arrays,
- file locations and offsets,
- available per-FAB statistics,
- time and timestep metadata.

Field values must not be loaded merely to open a dataset or populate menus.

### 8.2 Requests are explicit

Every operation that may require data access must be represented by a typed request.

Avoid APIs such as:

```cpp
MultiFab& getData();
```

Prefer APIs such as:

```cpp
Future<SliceResult> requestSlice(const SliceRequest&);
```

### 8.3 Results are proportional to the request

A 1200 by 900 slice request should return a bounded plane or tile set. It must not return the entire source hierarchy.

### 8.4 AMR composition is deterministic

- Fine cells override covered coarse cells.
- Coarse cells fill only uncovered output.
- Exact-level and finest-available modes are distinct.
- Statistics must not double-count covered coarse cells.
- Optional source-level masks should be available for debugging.

### 8.5 Memory is bounded

Use separate configurable budgets for:

- raw source blocks,
- derived blocks,
- composed planes,
- contours and other computed products,
- volume bricks.

### 8.6 The GUI never blocks on I/O

All reads and expensive computations occur off the GUI thread. The UI must remain interactive during:

- variable changes,
- slice movement,
- zooming,
- panning,
- range calculations,
- contour generation,
- animation,
- plotfile transitions.

### 8.7 Toolkit independence

- `QImage` is an adapter target, not the canonical core image type.
- VTK data objects are created only in the VTK adapter.
- Core request/results remain usable by headless tests and command-line tools.

---

## 9. Metadata Model

Define immutable types similar to:

```cpp
struct FieldMetadata {
    std::string name;
    int component_count = 1;
    amrex::IndexType centering;
    std::vector<std::string> component_names;
};

struct LevelMetadata {
    int level = 0;
    amrex::Box domain;
    amrex::IntVect refinement_ratio_to_next;
    amrex::RealVect cell_size;
    amrex::BoxArray boxes;
};

struct DatasetMetadata {
    int dimension = 0;
    int finest_level = 0;
    double time = 0.0;
    int coordinate_system = 0;

    amrex::RealBox physical_domain;
    std::vector<LevelMetadata> levels;
    std::vector<FieldMetadata> fields;
};
```

Requirements:

- Metadata is read-only after dataset creation.
- Nonuniform refinement ratios use `IntVect`.
- Centering is explicit.
- Logical fields may map to multiple physical file components.
- Standalone FAB and MultiFab inputs appear as one-level datasets.
- Metadata parsing is tested independently from data reads.

Use stable identifiers rather than exposing raw pointers:

```cpp
struct DatasetId {
    std::uint64_t value = 0;
};

struct FieldId {
    std::uint32_t value = 0;
};
```

---

## 10. Backend Interface

Define an interface similar to:

```cpp
class IAmrDataBackend {
public:
    virtual ~IAmrDataBackend() = default;

    virtual const DatasetMetadata& metadata() const = 0;

    virtual std::shared_ptr<const FabBlock>
    readBlock(const BlockRequest& request,
              const CancellationToken& cancellation) = 0;

    virtual std::optional<BlockStatistics>
    blockStatistics(const BlockRequest& request) const = 0;
};
```

### 10.1 `PlotfileBackend`

Responsibilities:

- Parse plotfile headers and level metadata.
- Build logical-field to physical-component mappings.
- Build an index of level, grid, file, offset, and component information.
- Read only requested grids and components.
- Expose per-FAB statistics when present.
- Avoid permanently populated per-component `MultiFab`s.
- Hide `VisMF` and format-specific details behind the backend.
- Instrument bytes read and read latency.

### 10.2 `FabBackend`

Responsibilities:

- Expose a standalone `FArrayBox` as a one-level, one-grid dataset.
- Read selected components where the format permits.
- Use the same request path as plotfiles.

### 10.3 `MultiFabBackend`

Responsibilities:

- Read a serialized standalone `MultiFab`, or wrap a live `MultiFab`.
- Expose box and component metadata.
- Avoid copying complete live data when block views are sufficient.
- Define synchronization and lifetime rules for in-situ use.

### 10.4 Future `RemoteBackend`

Responsibilities:

- Serialize typed requests.
- Return blocks, slices, line data, or volume bricks.
- Preserve the same client-facing API.
- Run near the HPC filesystem.
- Enforce server-side resource limits.

Do not implement networking until the in-process query contract is stable and tested.

---

## 11. Request Types

### 11.1 Block request

```cpp
struct BlockRequest {
    DatasetId dataset;
    int timestep = 0;
    int level = 0;
    int grid_index = 0;

    FieldId field;
    int first_component = 0;
    int component_count = 1;
    int ghost_width = 0;
};
```

### 11.2 Slice request

```cpp
enum class SamplingPolicy {
    Nearest,
    PiecewiseConstant,
    Linear
};

enum class CompositionPolicy {
    FinestAvailable,
    ExactLevel
};

struct SliceRequest {
    DatasetId dataset;
    FieldId field;
    int component = 0;

    int normal_direction = 2;
    double physical_position = 0.0;
    amrex::RealBox visible_region;

    int maximum_level = 0;
    std::array<int, 2> output_size{0, 0};

    SamplingPolicy sampling = SamplingPolicy::PiecewiseConstant;
    CompositionPolicy composition = CompositionPolicy::FinestAvailable;
};
```

### 11.3 Point request

```cpp
struct PointRequest {
    DatasetId dataset;
    FieldId field;
    int component = 0;
    amrex::RealVect position;
    int maximum_level = 0;
};
```

### 11.4 Line request

```cpp
struct LineRequest {
    DatasetId dataset;
    FieldId field;
    int component = 0;

    amrex::RealVect start;
    amrex::RealVect end;
    int maximum_level = 0;
    int requested_samples = 0;
};
```

### 11.5 Statistics request

```cpp
struct StatisticsRequest {
    DatasetId dataset;
    FieldId field;
    int component = 0;

    amrex::RealBox region;
    int maximum_level = 0;

    bool exact = true;
    bool ignore_nan = true;
};
```

### 11.6 Volume request

```cpp
enum class VolumeRepresentation {
    UniformComposite,
    AmrBlocks,
    Bricks
};

struct VolumeRequest {
    DatasetId dataset;
    FieldId field;
    int component = 0;

    amrex::RealBox region;
    int maximum_level = 0;
    std::array<int, 3> target_dimensions{0, 0, 0};

    VolumeRepresentation representation =
        VolumeRepresentation::UniformComposite;
};
```

Every volume request must specify a bounded region and target resolution, or an explicit block/byte budget.

---

## 12. Result Types

Results should be visualization-oriented and bounded.

Examples:

```cpp
struct PointResult {
    bool valid = false;
    double value = 0.0;
    int source_level = -1;
    amrex::Box source_grid;
};

struct ScalarPlane {
    int width = 0;
    int height = 0;
    amrex::RealBox physical_region;

    std::vector<float> values;
    std::vector<std::uint8_t> valid;
    std::vector<std::uint8_t> source_level;
};

struct LineResult {
    std::vector<double> distance;
    std::vector<double> value;
    std::vector<int> source_level;
};

struct StatisticsResult {
    bool valid = false;
    bool exact = false;
    double minimum = 0.0;
    double maximum = 0.0;
    std::uint64_t valid_samples = 0;
    std::uint64_t nan_samples = 0;
};
```

For large requests, support tiled or chunked responses so the UI can display partial results progressively.

---

## 13. Query Planning

The query planner converts high-level requests into exact source block operations.

For each request:

1. Validate the dataset, field, component, level, and region.
2. Convert physical coordinates to level-specific index space.
3. Intersect the request with each relevant level's domain and `BoxArray`.
4. Identify exact grid indices.
5. Determine required physical source components.
6. Determine required ghost width.
7. Determine fine coverage and uncovered coarse regions.
8. Reuse already cached blocks.
9. Deduplicate concurrent reads for identical blocks.
10. Schedule only missing reads.
11. Compose, interpolate, derive, or sample into the bounded result.
12. Release unpinned source handles after completion.
13. Record query statistics.

A query trace should be optionally available:

```text
request id
requested field and region
levels considered
grids intersected
cache hits and misses
bytes read
composition time
total latency
peak pinned bytes
```

---

## 14. AMR Composition

Implement fine-over-coarse composition independently of the GUI.

### 14.1 Required behavior

- Respect the requested maximum level.
- Clip every level to the requested region.
- Process fine levels before coarse levels, or maintain an explicit coverage mask.
- Fill output only where it is not already covered by finer data.
- Handle partially intersecting FABs.
- Preserve cell-centered semantics initially.
- Add node-, face-, and edge-centered semantics explicitly rather than treating all fields as cell-centered.
- Make interpolation policy explicit.
- Produce optional source-level and validity masks.

### 14.2 Tests

Include synthetic hierarchies where:

- fine data fully covers coarse data,
- fine data partially covers coarse data,
- adjacent fine grids meet exactly,
- fine grids leave holes,
- the request intersects only a small corner,
- refinement ratios differ by direction,
- domains do not begin at index zero,
- the request lies outside the physical domain.

---

## 15. Cache Design

### 15.1 Raw block cache

Use a key similar to:

```cpp
struct BlockKey {
    DatasetId dataset;
    int timestep = 0;
    int level = 0;
    int grid = 0;
    FieldId field;
    int first_component = 0;
    int component_count = 1;
    int ghost_width = 0;

    auto operator<=>(const BlockKey&) const = default;
};
```

Required behavior:

- Configurable byte budget.
- LRU or segmented-LRU eviction.
- Pinned entries while a request uses them.
- Concurrent request deduplication.
- Accurate byte accounting.
- Explicit dataset invalidation.
- Separate accounting by dataset and field.
- Cache statistics exposed to diagnostics.
- No eviction of pinned entries.
- Clear behavior when a request exceeds the entire cache budget.

### 15.2 Computed-result cache

Use a separate cache for:

- composed planes,
- derived blocks,
- line results,
- contours,
- volume bricks.

Raw data and computed data must not compete invisibly under one unobservable limit.

### 15.3 Memory acceptance criteria

Automated tests must verify:

- Cache size remains within budget plus documented temporary overhead.
- Repeated nearby slice requests reuse blocks.
- Moving far away causes old blocks to be evicted.
- Switching variables does not retain every previously viewed component.
- Cancellation releases pinned data.
- Opening multiple datasets applies a predictable global or per-dataset policy.

---

## 16. Statistics and Range Queries

Preserve metadata-only optimizations.

For a statistics request:

1. Use stored per-FAB min/max values for fully covered blocks when valid.
2. Read only partially covered boundary blocks.
3. Exclude coarse cells covered by fine data.
4. Handle NaNs according to request policy.
5. Support:
   - approximate immediate ranges,
   - exact asynchronous ranges.
6. Report whether the result is exact.

Range modes:

- Current level.
- All visible levels.
- Visible region.
- Selected subregion.
- Whole file.
- User fixed range.
- Time-sequence range, computed lazily.

---

## 17. Derived Fields

Derived fields must be represented as an explicit dependency graph.

```cpp
struct DerivedFieldDefinition {
    std::string name;
    std::vector<FieldId> inputs;
    int required_ghost_width = 0;
    amrex::IndexType output_centering;

    DerivedEvaluator evaluate;
};
```

The query planner must know:

- source fields,
- component ranges,
- ghost requirements,
- output centering,
- whether evaluation is block-local,
- whether coarse/fine interpolation is required,
- whether results are cacheable.

Initial derived-field support should be minimal. Do not recreate historical implicit naming heuristics until a tested use case requires them.

---

## 18. Scheduling, Cancellation, and Concurrency

### 18.1 Priorities

Use request priorities:

1. Visible current image.
2. Current point probe and line plot.
3. Contours and overlays.
4. Predicted adjacent slices.
5. Animation prefetch.
6. Background whole-file statistics.

### 18.2 Cancellation

Changing any of the following should cancel or invalidate obsolete work:

- dataset,
- field,
- component,
- slice position,
- visible region,
- level cap,
- output dimensions,
- range mode,
- animation frame.

Use generation IDs even when low-level I/O cannot be interrupted:

```cpp
if (result.generation != current_generation) {
    discard(result);
}
```

### 18.3 Concurrency limits

Backend I/O must support three explicit execution modes:

1. **Globally serialized:** one AMReX I/O operation in the process at a time. This is the required default until narrower concurrency is proven safe.
2. **Per-file serialized:** different files may be read concurrently, but each file has at most one active read. Enable only after verification.
3. **Concurrent:** multiple reads may proceed only for specific AMReX I/O paths demonstrated to be thread-safe by tests and supported configurations.

- Keep computation and I/O pools separate so composition and rendering work can proceed while the I/O lane is serialized.
- In globally serialized or per-file serialized modes, prioritize visible requests, coalesce compatible reads, and discard stale queued work before it reaches the backend.
- Permit animation and adjacent-slice prefetch only while the serialized I/O lane would otherwise be idle; prefetch must never delay a visible request.
- Bound the pending I/O queue as well as the worker count.
- Record queue wait time separately from read and computation time.
- Avoid excessive random reads on shared filesystems.
- Allow computation and I/O pools to be tuned separately.
- Keep the number of worker threads configurable.
- Treat concurrency as an optional throughput optimization, not a prerequisite for correctness, cancellation, bounded memory, or GUI responsiveness.
- Ensure AMReX I/O calls used concurrently are documented and tested as safe; fall back to global serialization otherwise.

---

## 19. 2-D Rendering Core

The toolkit-neutral renderer should produce:

```cpp
struct ImageBuffer {
    int width = 0;
    int height = 0;
    int stride_bytes = 0;
    std::vector<std::uint32_t> rgba;
};

struct LinePrimitive {
    float x0 = 0;
    float y0 = 0;
    float x1 = 0;
    float y1 = 0;
    std::uint32_t rgba = 0;
    float width = 1.0f;
};

struct RenderScene2D {
    ImageBuffer raster;
    std::vector<LinePrimitive> lines;
    // rectangles, text annotations, arrows, and markers as needed
};
```

### 19.1 Responsibilities

- Value normalization.
- Linear and logarithmic mapping.
- NaN and invalid-value colors.
- Palette mapping.
- Optional masked-body color.
- Grid box primitives.
- Crosshairs.
- Selection rectangle.
- Contours.
- Vector glyphs.
- Axes and labels represented independently from Qt where practical.

### 19.2 Qt adapter

The Qt adapter may wrap or copy `ImageBuffer` into `QImage`.

Ownership must be explicit. A `QImage` that references external memory must not outlive its backing buffer.

Use `QPainter` for:

- raster display,
- grid boxes,
- crosshairs,
- contours,
- vector arrows,
- selection,
- color bars,
- labels.

---

## 20. Qt Application

### 20.1 Initial window structure

```text
MainWindow
├── central slice view
├── field/component selector
├── level selector
├── range controls
├── slice controls
├── palette/color bar
├── status/probe display
└── optional docked line plot
```

### 20.2 Controller responsibilities

Controllers should translate UI state into typed requests.

They should not:

- parse files,
- inspect `VisMF`,
- walk `MultiFab`s,
- compute AMR coverage,
- retain raw data blocks,
- perform synchronous disk reads.

### 20.3 First usable Qt milestone

The first functional application must:

- open one plotfile,
- show dataset metadata,
- select one scalar component,
- select a level cap,
- display one 2-D slice,
- zoom and pan,
- probe values,
- show a color bar,
- remain responsive while loading,
- report bytes read and cache use in a diagnostics panel or log.

---

## 21. Optional VTK Volume Rendering

VTK is a later optional module.

### 21.1 Integration

- Embed `QVTKOpenGLNativeWidget` in a Qt Widget application.
- Create the required OpenGL surface format before `QApplication`.
- Keep VTK classes out of the core API.
- Create a separate `VolumeView` or dock/window.
- Share ordinary visualization state with the 2-D views.

### 21.2 Initial volume path

Start with:

```text
bounded VolumeRequest
    -> demand-driven AMR block reads
    -> explicit fine-over-coarse resampling
    -> bounded uniform volume
    -> vtkImageData
    -> VTK GPU volume mapper
```

Never construct an unconditional full-domain finest-resolution volume.

### 21.3 Later alternatives

Experiment behind an interface with:

- `vtkOverlappingAMR`,
- AMR block transfer,
- bricked volume rendering,
- camera-driven refinement,
- progressive resolution.

The data service, not VTK alone, remains responsible for the memory budget and requested region.

---

## 22. Migration Strategy

Do not attempt an in-place widget-by-widget translation.

Use the existing Amrvis as a behavioral reference while building a new architecture. Coding agents can inspect the available copy at `external/Amrvis/`.

### Phase 0: Baseline and reference data

> **Critical-path gate:** Phase 0 may not be skipped. Exploratory work on later phases may proceed, but no later phase may be marked complete until the Phase 0 exit criteria are satisfied. Capture the legacy behavior before its source, dependencies, build environment, or representative datasets drift.

- [ ] Select representative datasets.
- [ ] Record the exact legacy Amrvis source revision or source-tree identity.
- [ ] Record the OS, compiler, AMReX revision, dependencies, build options, and commands used for the reference executable.
- [ ] Record existing Amrvis screenshots.
- [ ] Record probe values.
- [ ] Record min/max results.
- [ ] Record line plots.
- [ ] Record grid overlays.
- [ ] Record slice navigation behavior.
- [ ] Identify features that are actually used.
- [ ] Record the source of usage evidence for every capability proposed as required in Section 23.
- [ ] Document expected fine-over-coarse behavior.
- [ ] Measure old Amrvis memory use and bytes read for selected workflows.
- [ ] Create a manifest mapping each dataset and workflow to its command, inputs, outputs, and expected result.
- [ ] Package the datasets, manifest, numerical outputs, and images as a versioned, immutable archive artifact in a durable project-controlled location.
- [ ] Generate and publish SHA-256 checksums for the archive and its manifest.
- [ ] Restore the archive into a clean location and verify its checksums and documented reproduction commands.

Representative datasets must include:

- single-level 2-D,
- multilevel 2-D,
- 3-D orthogonal slices,
- standalone FAB,
- standalone MultiFab,
- multiple components,
- constant field,
- NaNs or invalid values,
- partial fine coverage,
- nonzero domain origins,
- EB data if retained,
- a time sequence.

**Exit criteria:** A versioned compatibility-corpus artifact is archived in a durable project-controlled location; its manifest identifies the legacy source and reference environment, its SHA-256 checksums have been verified after a clean restore, and every `Yes` commitment in Section 23 cites measured usage evidence or an explicit human-maintainer scope decision.

### Phase 1: Repository and build foundation

- [ ] Create CMake project.
- [ ] Define library boundaries.
- [ ] Add formatting and test infrastructure.
- [ ] Add Linux CI.
- [ ] Add macOS CI.
- [ ] Add sanitizer configuration.
- [ ] Add optional Qt build.
- [ ] Add optional VTK placeholder option, default off.
- [ ] Document supported compiler matrix.

**Exit criteria:** Empty application and core libraries build and test on Linux and macOS.

### Phase 2: Immutable metadata reader

- [ ] Define metadata types.
- [ ] Implement plotfile header parsing.
- [ ] Implement standalone FAB metadata.
- [ ] Implement standalone MultiFab metadata.
- [ ] Preserve component mappings.
- [ ] Preserve level geometry.
- [ ] Preserve per-block statistics metadata where available.
- [ ] Add `dataset_inspect` command-line tool.
- [ ] Verify opening a dataset allocates no field arrays.

**Exit criteria:** All reference datasets can be inspected without loading field values.

### Phase 3: Selective block backend

- [ ] Define `BlockRequest`.
- [ ] Implement one-grid, one-component reads.
- [ ] Implement multi-component reads where efficient.
- [ ] Add read instrumentation.
- [ ] Add cancellation checks.
- [ ] Compare loaded values with existing AMReX/AmrData paths.
- [ ] Verify a request for one grid does not load unrelated grids.
- [ ] Verify a request for one component does not retain all components.

**Exit criteria:** Exact selected FAB data is read on demand with measured bytes and latency.

### Phase 4: Bounded caches

- [ ] Implement raw block cache.
- [ ] Implement pinning.
- [ ] Implement LRU or segmented-LRU eviction.
- [ ] Implement concurrent-load deduplication.
- [ ] Implement cache diagnostics.
- [ ] Implement dataset invalidation.
- [ ] Implement computed-result cache.
- [ ] Add strict memory-budget tests.

**Exit criteria:** Repeated requests reuse data and resident cache memory remains bounded.

### Phase 5: AMR slice query

- [ ] Define `SliceRequest` and `ScalarPlane`.
- [ ] Implement coordinate conversion.
- [ ] Implement level/grid intersection planning.
- [ ] Implement fine coverage masks.
- [ ] Implement finest-available composition.
- [ ] Implement exact-level mode.
- [ ] Implement piecewise-constant sampling.
- [ ] Add source-level debug output.
- [ ] Compare values with current Amrvis.
- [ ] Add performance and bytes-read tests.

**Exit criteria:** Correct bounded 2-D slice results are produced from multilevel 3-D data.

### Phase 6: Minimal Qt viewer

- [ ] Create `QApplication` and `MainWindow`.
- [ ] Add dataset open workflow.
- [ ] Add metadata model.
- [ ] Add field/component selector.
- [ ] Add level selector.
- [ ] Add slice position control.
- [ ] Add asynchronous request controller.
- [ ] Add Qt image adapter.
- [ ] Add zoom, pan, and probe.
- [ ] Add color bar.
- [ ] Add loading/error states.
- [ ] Add diagnostics for cache and bytes read.

**Exit criteria:** A user can open a plotfile and interactively inspect a scalar slice without UI blocking.

### Phase 7: Traditional 2-D capability

- [ ] Palette loading and editing.
- [ ] Linear and logarithmic mapping.
- [ ] User min/max.
- [ ] Visible-region min/max.
- [ ] File and level min/max.
- [ ] Grid-box overlays.
- [ ] Crosshair coordination.
- [ ] Subregion selection.
- [ ] Contours.
- [ ] Vector overlays.
- [ ] Line plots.
- [ ] Image export.
- [ ] Data export.
- [ ] Keyboard and mouse bindings.
- [ ] Preference persistence.

**Exit criteria:** Required traditional workflows pass the compatibility matrix.

### Phase 8: 3-D orthogonal views and animation

- [ ] Implement three coordinated orthogonal slice views.
- [ ] Share crosshair and slice state.
- [ ] Implement slice sweeping.
- [ ] Implement plotfile-sequence animation.
- [ ] Implement prefetch with low priority.
- [ ] Cancel obsolete frames.
- [ ] Bound animation cache memory.
- [ ] Add frame timing diagnostics.

**Exit criteria:** Interactive three-plane navigation and bounded-memory animation work reliably.

### Phase 9: Compatibility hardening

- [ ] Complete reference image comparisons.
- [ ] Complete numerical result comparisons.
- [ ] Test very large metadata/box counts.
- [ ] Test slow and high-latency filesystems.
- [ ] Test malformed files and partial reads.
- [ ] Test multiple open datasets.
- [ ] Test long-running memory stability.
- [ ] Test high-DPI displays.
- [ ] Verify Linux packaging.
- [ ] Verify macOS packaging.
- [ ] Write user documentation.

**Exit criteria:** Release candidate meets required compatibility, stability, and memory targets.

### Phase 10: Optional VTK volume rendering

- [-] Add optional VTK CMake target.
- [-] Add bounded `VolumeRequest`.
- [-] Add uniform composite volume result.
- [-] Add VTK adapter.
- [-] Add transfer-function controls.
- [-] Add crop/ROI controls.
- [-] Measure GPU and host memory.
- [-] Experiment with AMR block representations.
- [-] Add progressive-quality rendering.

**Exit criteria:** A bounded ROI can be volume-rendered without compromising the 2-D architecture or memory policy.

### Phase 11: Optional remote service

- [-] Stabilize request/result serialization.
- [-] Implement service process.
- [-] Implement authentication/authorization appropriate to deployment.
- [-] Implement compression.
- [-] Implement request limits.
- [-] Implement remote cancellation.
- [-] Test on HPC storage near compute resources.

**Exit criteria:** The same client can visualize remote data without changing UI logic.

---

## 23. Compatibility Matrix

The agent should maintain test and implementation status in this table during development. The `Required` column is a scope commitment, not a feature inventory. Before Phase 0 exits, every `Yes` entry must cite concrete measured-usage evidence from the compatibility corpus or an explicit human-maintainer scope decision. Missing evidence is a blocker to Phase 0 completion, not permission for an agent to downgrade the commitment. An agent must not change `Yes` to another classification or promote another classification to `Yes` without explicit human-maintainer authorization recorded in the **Plan Change Log**.

| Capability | Required | Requirement evidence or decision | Status | Reference test |
|---|---:|---|---:|---|
| Standalone FArrayBox | Yes | `[!]` Phase 0 evidence required | `[ ]` | |
| Standalone MultiFab | Yes | `[!]` Phase 0 evidence required | `[ ]` | |
| Multilevel plotfile | Yes | `[!]` Phase 0 evidence required | `[ ]` | |
| Scalar component selection | Yes | `[!]` Phase 0 evidence required | `[ ]` | |
| Fine-over-coarse slices | Yes | `[!]` Phase 0 evidence required | `[ ]` | |
| User value range | Yes | `[!]` Phase 0 evidence required | `[ ]` | |
| File/level/region ranges | Yes | `[!]` Phase 0 evidence required | `[ ]` | |
| Palette and color bar | Yes | `[!]` Phase 0 evidence required | `[ ]` | |
| Grid boxes | Yes | `[!]` Phase 0 evidence required | `[ ]` | |
| Point probe | Yes | `[!]` Phase 0 evidence required | `[ ]` | |
| Line plot | Yes | `[!]` Phase 0 evidence required | `[ ]` | |
| Contours | Yes | `[!]` Phase 0 evidence required | `[ ]` | |
| Vector overlay | Yes | `[!]` Phase 0 evidence required | `[ ]` | |
| Orthogonal 3-D slices | Yes | `[!]` Phase 0 evidence required | `[ ]` | |
| Slice animation | Yes | `[!]` Phase 0 evidence required | `[ ]` | |
| Plotfile sequence | Yes | `[!]` Phase 0 evidence required | `[ ]` | |
| Image export | Yes | `[!]` Phase 0 evidence required | `[ ]` | |
| EB body masking | Deferred | Human-maintainer deferral; validate during Phase 0 | `[-]` | |
| VTK volume rendering | Deferred | Decision D-005 | `[-]` | |
| Remote service | Deferred | Phase 11 scope | `[-]` | |
| Profiling visualization | No | Decision D-004 | `[r]` | Removed from scope |

---

## 24. Testing Strategy

### 24.1 Unit tests

Test:

- coordinate transformations,
- physical-to-index conversion,
- box intersections,
- refinement and coarsening,
- coverage masks,
- cache keys,
- eviction,
- pinning,
- request validation,
- palette mapping,
- contour generation,
- derived-field dependencies.

### 24.2 Integration tests

Test complete workflows:

```text
dataset metadata
    -> slice request
    -> selective block reads
    -> AMR composition
    -> image buffer
```

Verify:

- numerical values,
- source levels,
- blocks read,
- bytes read,
- cache occupancy,
- cancellation behavior.

### 24.3 Reference tests

Compare against established Amrvis results:

- probe values,
- line plots,
- min/max values,
- level coverage,
- contours,
- representative images.

Image comparisons should tolerate documented rendering differences such as font rasterization while remaining strict for data colors and geometry.

### 24.4 Performance tests

Measure:

- dataset open time,
- first slice latency,
- neighboring slice latency,
- variable-switch latency,
- cache hit rate,
- bytes read,
- composition throughput,
- animation frame rate,
- peak resident cache bytes.

### 24.5 Required demand-loading tests

> **Human-maintainer-controlled:** Coding agents must not edit, weaken, remove, defer, make optional, or reclassify these tests. Only a human maintainer may change them, with the authorization and exact change recorded in the **Plan Change Log**.

Every release must include tests proving:

1. Opening a plotfile does not load field FABs.
2. A one-cell point request reads only necessary block data.
3. A narrow line request does not load unrelated grids.
4. A single slice does not load the full 3-D hierarchy.
5. A single component request does not retain all components.
6. A bounded volume request does not construct a full-domain finest-resolution volume.
7. Cache memory remains within the configured budget.
8. Cancelled requests do not leak pinned entries.

---

## 25. Performance and Memory Targets

Initial targets should be measured and updated rather than guessed permanently. Establish targets against the mandatory globally serialized I/O baseline first. A separate concurrent profile may be added only after its AMReX I/O path is proven safe.

Record results in this table:

| Workflow | Dataset | I/O mode | Target | Current | Status |
|---|---|---|---:|---:|---:|
| Metadata open | TBD | Globally serialized | TBD | | `[ ]` |
| First 2-D slice | TBD | Globally serialized | TBD | | `[ ]` |
| Adjacent slice | TBD | Globally serialized | TBD | | `[ ]` |
| Variable switch | TBD | Globally serialized | TBD | | `[ ]` |
| Point probe | TBD | Globally serialized | TBD | | `[ ]` |
| Animation frame delivery | TBD | Globally serialized | TBD | | `[ ]` |
| I/O queue wait | TBD | Globally serialized | TBD | | `[ ]` |
| Cache budget overshoot | all | Globally serialized | <= documented temporary overhead | | `[ ]` |

If serialized AMReX I/O cannot meet an interactive or animation throughput target:

1. Preserve correctness, bounded memory, cancellation, and GUI responsiveness.
2. Improve request coalescing, read locality, batching, cache reuse, and stale-work removal before considering concurrency.
3. Reduce or disable speculative prefetch when it competes with visible work.
4. Report the measured serialized limit and queue behavior.
5. Require an explicit human-maintainer scope or target decision rather than assuming unsafe backend parallelism.

Non-negotiable qualitative targets:

- UI remains responsive during I/O.
- Memory does not grow without bound during long navigation sessions.
- Repeated nearby requests demonstrate meaningful cache reuse.
- Request instrumentation can explain unexpected reads.

---

## 26. Error Handling

Use structured error types. Do not call `abort()` for recoverable user or file errors.

Errors should distinguish:

- invalid request,
- unsupported field centering,
- malformed metadata,
- unavailable component,
- out-of-domain request,
- cancelled request,
- read failure,
- memory-budget failure,
- derived-field dependency failure,
- backend capability mismatch.

Qt should display actionable messages while retaining detailed diagnostics in logs.

---

## 27. Logging and Diagnostics

Provide configurable logging categories:

- metadata,
- backend I/O,
- query planner,
- cache,
- composition,
- scheduler,
- Qt controller,
- VTK adapter.

A diagnostics view or command-line option should expose:

- active request count,
- cache bytes,
- pinned bytes,
- cache hit rate,
- bytes read,
- current dataset,
- current ROI,
- grids selected by the last request,
- last request latency.

---

## 28. Documentation Deliverables

- [ ] Architecture overview.
- [ ] Query and result API documentation.
- [ ] Backend implementation guide.
- [ ] Derived-field guide.
- [ ] Memory and cache tuning guide.
- [ ] Qt UI architecture guide.
- [ ] VTK extension guide.
- [ ] User guide.
- [ ] Build and packaging guide.
- [ ] Compatibility matrix.
- [ ] Known differences from legacy Amrvis.

---

## 29. Agent Implementation Rules

The coding agent must follow these rules:

1. Implement in small, reviewable commits.
2. Add or update tests with every behavior change.
3. Do not introduce Qt types into core or backend headers.
4. Do not introduce VTK as a required dependency.
5. Do not perform blocking I/O on the GUI thread.
6. Do not return mutable internal cache-owned `MultiFab` references.
7. Do not hide disk I/O in metadata getters.
8. Instrument bytes read before optimizing read behavior.
9. Add regression tests before fixing subtle AMR composition bugs.
10. Update this plan after completing each phase or making a material design change.
11. Record rejected designs rather than repeatedly reconsidering them without new evidence.
12. Prefer correctness and observable memory behavior over premature GPU optimization.
13. Keep the application usable at the end of each milestone.
14. Do not delete the legacy implementation until replacement behavior is verified.
15. Do not claim feature parity without a compatibility test.

---

## 30. Definition of Done for the Initial Modernized Release

The initial release is complete when:

- It builds with CMake on supported Linux and macOS configurations.
- It contains no runtime dependency on X11, Xt, or Motif.
- It opens supported FAB, MultiFab, and plotfile datasets.
- Dataset opening is metadata-only.
- It selectively reads required FAB components.
- It displays correct 2-D and orthogonal 3-D slices.
- It preserves fine-over-coarse AMR composition.
- It supports component selection, ranges, palettes, probes, grid boxes, line plots, contours, and animation.
- It remains responsive during data access.
- Its raw and computed caches enforce configured memory budgets.
- Demand-loading tests pass.
- Numerical compatibility tests pass.
- Required traditional workflows are documented.
- Profiling visualization code is not part of the new application.
- VTK is not required for the initial release.
- The plan, decision log, and compatibility matrix reflect the final implementation state.

---

## 31. Decision Log

Add entries whenever a material architectural decision is made.

### D-001: Use Qt 6 Widgets

- **Status:** Accepted
- **Decision:** Use Qt 6 Widgets for the desktop application.
- **Reason:** Portable desktop controls, macOS/Linux support, mature event system, and future VTK integration.
- **Consequence:** Qt remains isolated from the data core.

### D-002: Use a CPU 2-D renderer first

- **Status:** Accepted
- **Decision:** Use toolkit-neutral CPU image generation adapted to `QImage`, with overlays drawn using `QPainter`.
- **Reason:** Closest migration path from existing CPU raster generation and lowest initial risk.
- **Consequence:** GPU rendering may be introduced later behind a rendering interface.

### D-003: Replace `AmrData`/`DataServices` with explicit demand-driven queries

- **Status:** Accepted
- **Decision:** Build a new AMR-aware client/service abstraction with typed requests, bounded caches, and selective block reads.
- **Reason:** Preserve Amrvis memory efficiency while avoiding hidden mutable state, varargs dispatch, and GUI coupling.
- **Consequence:** Existing classes may be used as behavioral references but are not the target API.

### D-004: Drop profiling visualization

- **Status:** Accepted
- **Decision:** Do not port profiling, communication, call-trace, or region visualization.
- **Reason:** Low usage and unrelated complexity.
- **Consequence:** The modernization focuses on FAB, MultiFab, and plotfile visualization.

### D-005: Keep VTK optional

- **Status:** Accepted
- **Decision:** Add VTK only as an optional later volume-rendering module.
- **Reason:** VTK is useful for 3-D but unnecessary for the primary 1-D and 2-D viewer.
- **Consequence:** Core and basic packages remain smaller and easier to deploy.

### D-006: Allow the coding agent to maintain this plan

- **Status:** Accepted
- **Decision:** The coding agent may update this file throughout implementation under the rules in Section 2.
- **Reason:** Implementation will reveal additional tasks, risks, and design constraints.
- **Consequence:** Material changes must be recorded and must not silently weaken project requirements.

### D-007: Make the legacy compatibility corpus a gating artifact

- **Status:** Accepted
- **Decision:** Phase 0 produces a versioned, archived compatibility corpus with a manifest, reproducible reference environment, and verified SHA-256 checksums. No later phase may be marked complete before that artifact and its requirement evidence are complete.
- **Reason:** Legacy behavior and build environments can drift or become unavailable, making later compatibility claims impossible to verify.
- **Consequence:** Reference capture is critical-path project work rather than optional preparation.

### D-008: Require evidence for compatibility commitments

- **Status:** Accepted
- **Decision:** Every `Yes` in the compatibility matrix must cite measured usage evidence or an explicit human-maintainer scope decision.
- **Reason:** Copying the complete historical feature inventory would create an unbounded and weakly justified parity commitment.
- **Consequence:** Missing evidence blocks Phase 0 completion, and changes to the `Required` classification require human-maintainer authorization in the Plan Change Log.

### D-009: Design for permanently serialized AMReX I/O

- **Status:** Accepted
- **Decision:** The backend and scheduler must remain functional and responsive with one process-wide AMReX I/O lane. Narrower serialization or concurrent reads are optional modes enabled only after verification.
- **Reason:** AMReX I/O thread safety may not permit useful backend parallelism on supported configurations.
- **Consequence:** Performance baselines use globally serialized I/O, prefetch is subordinate to visible work, and concurrency is not assumed when setting targets.

---

## 32. Rejected Approaches

### R-001: Direct X11-to-Qt translation

- **Status:** Rejected
- **Reason:** It would preserve undesirable coupling between UI, state, data access, and rendering.

### R-002: Use VTK for all visualization from the start

- **Status:** Rejected
- **Reason:** It adds unnecessary complexity and dependency weight for traditional 1-D and 2-D workflows.

### R-003: Load complete MultiFabs into memory

- **Status:** Rejected
- **Reason:** It violates a defining memory-efficiency requirement.

### R-004: Build direct Wayland, Cocoa, and Win32 backends

- **Status:** Rejected
- **Reason:** It replaces one platform-maintenance burden with several.

### R-005: Implement networking before the local query API

- **Status:** Rejected for initial phases
- **Reason:** Transport concerns would obscure and destabilize the core request semantics.

---

## 33. Risk Register

| Risk | Impact | Mitigation | Status |
|---|---|---|---:|
| Legacy Amrvis or its reference environment drifts before behavior is captured | High | Make Phase 0 a completion gate; archive a versioned corpus, environment manifest, and verified checksums | `[ ]` |
| Feature parity scope grows without measured usage evidence | High | Require evidence or an explicit human decision for every `Yes` commitment before Phase 0 exits | `[ ]` |
| Selective AMReX I/O APIs are not thread-safe | High | Default to one process-wide I/O lane; enable narrower serialization or concurrency only after verification | `[ ]` |
| Serialized AMReX I/O cannot meet interaction or animation throughput targets | High | Coalesce and prioritize reads, bound queues, make prefetch idle-only, measure the serialized baseline, and escalate target or scope decisions to a human | `[ ]` |
| Plotfile component layouts vary | High | Explicit metadata index and test corpus | `[ ]` |
| Cache temporary allocations exceed budget | High | Track pinned and transient bytes separately | `[ ]` |
| AMR composition differs from legacy behavior | High | Numerical reference tests and source-level masks | `[ ]` |
| Qt image ownership causes use-after-free | High | Explicit shared buffer ownership and tests | `[ ]` |
| Animation produces request storms | Medium | Cancellation, coalescing, priorities, prefetch limits | `[ ]` |
| Whole-file exact ranges cause excessive I/O | Medium | Metadata min/max and approximate-first mode | `[ ]` |
| VTK later encourages full-volume copies | High | Require bounded `VolumeRequest` in the core contract | `[-]` |
| macOS packaging becomes difficult | Medium | Test packaging early, avoid nonstandard runtime paths | `[ ]` |

---

## 34. Plan Change Log

The agent should append entries; do not rewrite history.

### 2026-07-16

- Created initial modernization plan.
- Selected Qt 6 Widgets with `QImage`/`QPainter`.
- Defined demand-driven AMR data service as the architectural center.
- Removed profiling visualization from scope.
- Reserved VTK for optional future volume rendering.
- Authorized the coding agent to update this plan during implementation.
- Changed the baseline language standard from C++17 to C++20.
- Documented `external/Amrvis/` as the legacy source reference available to coding agents.
- Protected the non-negotiable requirements in Section 4 and required demand-loading tests in Section 24.5 from agent edits; future changes require explicit human-maintainer authorization recorded in this log.
- Made Phase 0 a critical-path gate requiring a versioned, archived compatibility corpus with an environment manifest and verified SHA-256 checksums.
- Required evidence or an explicit human-maintainer decision for every `Yes` commitment in the compatibility matrix.
- Added a globally serialized AMReX I/O fallback design, serialized performance baseline, and prefetch/throughput fallback policy.

---

## 35. Immediate Next Actions

- [ ] Capture the legacy Amrvis source identity, build environment, and reproduction commands before beginning later milestones.
- [ ] Select the compatibility datasets and create the versioned corpus manifest.
- [ ] Archive the initial corpus, publish its SHA-256 checksums, and verify a clean restore.
- [ ] Supply usage evidence or request an explicit human-maintainer decision for every current `Yes` in the compatibility matrix.
- [ ] Create the repository skeleton and top-level CMake configuration.
- [ ] Add this file as `PLAN.md` at the repository root.
- [ ] Implement the metadata types.
- [ ] Build a metadata-only `dataset_inspect` tool.
- [ ] Instrument and prototype one selective plotfile FAB/component read.
- [ ] Record the first measured bytes-read result in this plan.
- [ ] Add the first decision or risk discovered during the prototype.
