# CodeSight: Live Code Architecture Visualizer

CodeSight is a high-performance, real-time code architecture visualizer. It watches a local codebase, parses files using an Abstract Syntax Tree (AST), and instantly renders a live-updating dependency graph in the browser. 

Built with a hybrid C++/Python architecture to maximize parsing speed and development agility.

![CodeSight UI](screenshot.png)

## Architecture

CodeSight is designed as a distributed microservice system running locally:

1. **C++ Core (Performance Layer):** Uses Linux `inotify` for zero-latency file system event monitoring and Tree-sitter for incremental AST parsing.
2. **gRPC Bridge (IPC Layer):** Transmits structured Protobuf messages from the C++ core to the Python backend with sub-millisecond latency.
3. **Python Backend (Intelligence Layer):** FastAPI server that maintains a dynamic NetworkX dependency graph and broadcasts updates via WebSockets.
4. **React Frontend (Presentation Layer):** Consumes the WebSocket stream and renders live SVG diagrams using Mermaid.js.

```mermaid
graph TD
    A[File System Event] -->|inotify| B(C++ Watcher & Parser)
    B -->|gRPC / Protobuf| C{Python FastAPI Backend}
    C -->|NetworkX Graph| D[Mermaid Diagram Generator]
    D -->|WebSocket| E[React Frontend]
