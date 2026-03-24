Render Visualizer
=======================================

| System | purpose |
|---|---|
| Node Reflection | Turn annotated C++ node types into runtime node metadata (pins, hooks, editor callbacks). |
| Node Graph | Hold the editor's nodes, links, functions, and helper queries. |
| Blackboard | Global, reflected runtime state (time, camera, inputs) accessible to nodes. |
| Graph Builder | Validate the graph and produce a `graph_execution_plan` (build steps + function plans). |
| Graph Execution Plan | The compiled output of the builder: root node, VM stack, build steps, function plans. |
| Graph Executor | Consumes the execution plan to build resources and drive the VM each frame. |
| VM Stack & Slots | Compiled slot layout for passing values between nodes and functions. |
| Execution / Runtime | Owns the builder, plan, and executor; coordinates the build and render loop. |
| Frames & Call Stack | Per-function storage and simple call-stack semantics for function calls. |
| Execution Context | Safe, typed helpers for node hooks to read/write data and call functions. |
| Persistence & Editor Integration | Save/load graphs and let nodes provide editor UI and dynamic pins. |

Node Reflection
- Why it matters: You write a C++ struct or annotated type and reflection makes it a usable node without a ton of glue code.
- Look at: [node_registration.hpp](include/render_visualizer/node_registration.hpp)

Node Graph
- Why it matters: This is the editor's source-of-truth, everything you see in the UI is represented here.
- Look at: [node_graph.hpp](include/render_visualizer/node_graph.hpp)

Blackboard
- Why it matters: Useful global state (frame time, camera) lives here and is easily visible to nodes.
- Look at: [graph_blackboard.hpp](include/render_visualizer/graph_blackboard.hpp)

Graph Builder / Compiler
- Why it matters: Ensures the graph is valid, computes the order to build resources, and compiles function layouts. Its sole output is a `graph_execution_plan` stored on the runtime.
+ Look at: [runtime_plan_builder.cpp](src/runtime/runtime_plan_builder.cpp) and [runtime_slot_compiler.cpp](src/runtime/runtime_slot_compiler.cpp)

Graph Execution Plan
- Why it matters: The explicit boundary between the builder and executor. Holds everything the executor needs: `root_node_id`, the `vm_stack`, `build_steps`, and `function_plans`. Clearing it (`plan = {}`) is all that is needed to reset compiled state.
- Look at: `graph_execution_plan` in [stack.hpp](include/render_visualizer/runtime/stack.hpp)

VM Stack & Slots
- Why it matters: The runtime talks to nodes by slot index (fast, compact), changes here affect how values flow between nodes.
- Look at: [stack.hpp](include/render_visualizer/runtime/stack.hpp)

Graph Executor
- Why it matters: Reads the execution plan produced by the builder, executes build steps to create GPU resources, and runs setup/render function chains each frame via the VM.
+ Look at: [runtime_vm_execution.cpp](src/runtime/runtime_vm_execution.cpp)

Execution / Runtime
- Why it matters: Top-level owner of the builder, plan, and executor. Exposes `tick()`, `record_pre_swapchain()`, and the accessors that node hooks call to read/write slot values.
+ Look at: [runtime_facade.cpp](src/runtime/runtime_facade.cpp) and [runtime.hpp](include/render_visualizer/runtime.hpp)

Frames & Call Stack
- Why it matters: Frames hold per-function local state and back the simple function-call model used by nodes.
+ Look at: [runtime_stack.cpp](src/runtime/runtime_stack.cpp) and [stack.hpp](include/render_visualizer/runtime/stack.hpp)

Execution Context & Services
- Why it matters: Node hooks use this to read inputs, push outputs, access blackboard values, and call functions safely.
- Look at: [execution_context.hpp](include/render_visualizer/execution_context.hpp)

Persistence & Editor Integration
- Why it matters: Save/load is simple, and node reflection enables editor UI hooks and dynamic pin behavior.
+ Look at: [graph_persistence.hpp](include/render_visualizer/graph_persistence.hpp) and the editor-related callbacks in [node_registration.hpp](include/render_visualizer/node_registration.hpp)
