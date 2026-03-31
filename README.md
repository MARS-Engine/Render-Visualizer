# Render Visualizer

Render Visualizer is a node-based visual scripting playground built on top of MARS. It is focused on experimenting with reflection-driven node authoring, editor tooling, and a small runtime that can compile a graph into an executable frame pipeline.

The project is intentionally small and iteration-friendly. Nodes are described as normal C++ structs, pins are discovered through static reflection, and the editor uses that metadata to draw the graph, expose an inspector, and build a runtime execution stack.

## 🚧 Status

This project is still in an active prototyping phase. The core editor loop is already there, but the app is still growing and some systems are deliberately lightweight while the architecture settles.

## What This App Is

Render Visualizer is being built as a node editor powered by static reflection, with the long-term goal of authoring complete graphics render pipelines from start to finish entirely through nodes.

That includes the full range of rendering workflow we care about, from traditional passes all the way to raytracing. The idea is not just to make a generic scripting graph, but to make a graph-driven renderer authoring environment where rendering features, resources, passes, and execution flow can all be assembled visually.

This direction is not purely theoretical. We already prototyped this idea in the past, including a raytracing-capable version, using AI heavily for rapid experimentation. That prototype proved that the approach works. This codebase is the rewrite from scratch, with a stronger focus on code quality, maintainability, and cleaner architecture.

Instead of hardcoding every node, pin, inspector widget, and runtime binding by hand, this project uses static reflection to infer a lot of that structure automatically:

- `[[=rv::input]]` and `[[=rv::output]]` define data pins.
- `[[=rv::execute]]` marks the member function that the runtime should call.
- `[[=rv::node_pure()]]` marks nodes that should not get execution pins.

That metadata then flows through the whole app, from the editor all the way into the runtime executor.

## Core Systems

### Blackboard Canvas 🧭

The blackboard is the central node-editing surface. It is responsible for:

- Drawing the background grid
- Laying out nodes and pins
- Rendering links as cubic beziers
- Converting graph-space positions into screen-space positions
- Maintaining the camera offset used when panning the graph

This system is what makes nodes feel like editor objects instead of raw data records. It also caches node sizes so hit-testing and drawing use the same layout information.

### Reflection and Node Metadata 🧠

The reflection layer is the backbone of the project. A node type is just a reflected C++ struct with annotated fields and an execute function.

From that, the app derives:

- Display name
- Input and output pin lists
- Pin colours and type hashes
- Whether the node is pure or execution-driven
- Runtime member access for pin values
- The member function that should be invoked at runtime
- Inspector widgets for editable inputs

This keeps the editor and runtime talking about the same node shape, instead of maintaining separate definitions for authoring and execution.

### Node Registry 📚

The registry is the catalog of node types that the editor knows how to spawn. When a node type is registered, the editor can:

- Show it in the right-click context menu
- Create an instance for it in the graph
- Ask it for pin metadata
- Ask it for runtime metadata

The registry is intentionally simple right now, but it is the bridge between "this type exists in code" and "this node can appear in the editor."

### Graph Builder 🏗️

The graph builder owns the editable graph state. Each graph node stores:

- A unique node id
- Position and cached size
- A reflected instance of the node's C++ type
- The node's link data
- Selection state
- Runtime metadata used for compilation

It is also the system that turns editor state into runtime state. When execution starts, the graph builder walks the graph from the built-in `Start` node and compiles it into:

- An aligned `frame_stack`
- One stack object per reachable non-Start node
- A linear execution schedule
- Precomputed pin-copy operations
- A vector of opaque execute-member handles

Unconnected inputs are seeded from the current editor instance, which means inspector edits naturally feed into the runtime build.

### UI State Manager 🖱️

The UI state manager is the interaction layer for the blackboard. It listens to the MARS window event system and translates mouse input into editor behavior:

- Node selection
- Title-bar dragging
- Pin linking
- Context-menu spawning
- Middle-mouse camera panning

It is deliberately separate from rendering, which keeps the drawing code focused on visuals and the state manager focused on interaction rules.

### Side Panels and Inspector 🪟

The app currently has a minimal shell around the canvas:

- Left drawer: reserved for future tools
- Right drawer: overview and inspector panels

The overview panel already controls runtime execution with `Start` and `Stop`. The inspector panel shows the currently selected node's non-execution input pins and renders them through MARS ImGui reflection helpers.

That means editing a node input in the inspector updates the underlying node instance directly, and the runtime can rebuild from that edited state.

### Frame Stack and Frame Executor ⚙️

The runtime side is intentionally explicit.

The `frame_stack` owns the actual runtime node objects in aligned storage. When a graph is compiled, each reachable runtime node is copy-constructed into that stack from the editor-side instance.

The `frame_executor` then runs every frame while execution is active:

1. For the next scheduled node, copy connected input values from upstream node outputs
2. Look up the node object in the frame stack
3. Invoke the reflected execute member on that object

Pure nodes are scheduled immediately before the execution node that consumes them, so they behave like data dependencies in the compiled frame.

## Execution Model

The current execution model is intentionally small and strict:

- Execution starts from a unique built-in `Start` node
- Execution links form a single forward path
- Execution outputs can only target one next execution input
- Data outputs can fan out to multiple consumers
- Each input pin can only have one source
- Pure dependencies are expanded before their consumer during compilation

If the graph becomes invalid for runtime compilation, execution does not start and the builder reports an error instead of guessing.

## Platform Support

This app is built on top of MARS, so its practical platform story currently follows the same direction: Windows and Linux are the intended targets, while macOS is not a focus at the moment.

| Platform   | Supported | Notes |
|------------|-----------|-------|
| Windows 11 | ✅ | Primary development target |
| Linux      | ✅ | Expected to work alongside the MARS stack |
| macOS      | ❌ | Not currently targeted |

## Dependencies

Most of the heavy lifting comes from the local MARS engine dependency and the libraries it already integrates.

| Dependency | Role | Notes |
|---|---|---|
| MARS | Engine / platform layer | Provides rendering, events, math, reflection helpers, and ImGui integration |
| Dear ImGui | Editor UI | Used for the blackboard, drawers, inspector, and context menus |
| SDL3 | Windowing and input | Routed through the MARS window abstraction |
| Clang P2996 reflection fork | Static reflection | Required for the reflection-heavy node and runtime systems |

## Current Direction

The project already has the full loop of:

- Define a node in C++
- Register it
- Spawn it in the editor
- Inspect and edit its inputs
- Link it into a graph
- Compile it into a runtime frame
- Execute it every frame

The next layer is mostly about depth and polish: richer graph validation, more editor tools, better inspection, and a broader runtime model.
