import asyncio
import threading
from concurrent import futures
from contextlib import asynccontextmanager

import grpc
import proto.codesight_pb2 as pb2
import proto.codesight_pb2_grpc as pb2_grpc
import uvicorn
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from graph_builder import GraphBuilder

graph_builder = GraphBuilder()
websocket_connections: set[WebSocket] = set()
app_event_loop = None


@asynccontextmanager
async def lifespan(app: FastAPI):
    global app_event_loop
    app_event_loop = asyncio.get_running_loop()
    yield
    websocket_connections.clear()


app = FastAPI(title="CodeSight Backend", lifespan=lifespan)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


class CodeSightServicer(pb2_grpc.CodeSightServiceServicer):
    def SendParsedFile(self, request, context):
        try:
            graph_builder.update_from_parsed_file(request)

            print(f"Received parsed file: {request.filepath}")
            print(f"  Functions: {len(request.functions)}")
            print(f"  Imports: {len(request.imports)}")
            print(f"  API Routes: {len(request.api_routes)}")

            if app_event_loop:
                asyncio.run_coroutine_threadsafe(
                    broadcast_graph_update(), app_event_loop
                )
            else:
                print("Warning: Event loop not yet available, skipping broadcast")

            return pb2.AckResponse(success=True, message="OK")
        except Exception as e:
            print(f"Error processing parsed file: {e}")
            return pb2.AckResponse(success=False, message=str(e))

    def GetGraphState(self, request, context):
        state = graph_builder.get_graph_state()

        nodes = [
            pb2.Node(id=n["id"], label=n["label"], type=n["type"])
            for n in state["nodes"]
        ]
        edges = [
            pb2.Edge(from_=e["from"], to=e["to"], type=e["type"])
            for e in state["edges"]
        ]

        return pb2.GraphState(
            nodes=nodes, edges=edges, mermaid_diagram=state["mermaid_diagram"]
        )


async def broadcast_graph_update():
    if not websocket_connections:
        print("DEBUG: No WebSocket connections to broadcast to.")
        return

    state = graph_builder.get_graph_state()
    print(
        f"DEBUG: Broadcasting graph state. Diagram length: {len(state['mermaid_diagram'])} chars"
    )
    print(f"DEBUG: Diagram preview: {state['mermaid_diagram'][:100]}...")

    disconnected = set()
    for websocket in websocket_connections:
        try:
            await websocket.send_json(state)
        except Exception as e:
            print(f"Error sending to WebSocket: {e}")
            disconnected.add(websocket)

    websocket_connections -= disconnected


def start_grpc_server():
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
    pb2_grpc.add_CodeSightServiceServicer_to_server(CodeSightServicer(), server)
    server.add_insecure_port("[::]:50051")
    server.start()
    print("gRPC server started on port 50051")
    server.wait_for_termination()


@app.get("/")
async def root():
    return {"message": "CodeSight Backend is running"}


@app.get("/graph")
async def get_graph():
    return graph_builder.get_graph_state()


@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    websocket_connections.add(websocket)
    print("DEBUG: New WebSocket connection established.")

    try:
        state = graph_builder.get_graph_state()
        await websocket.send_json(state)

        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        websocket_connections.remove(websocket)
        print("DEBUG: WebSocket disconnected.")
    except Exception as e:
        print(f"WebSocket error: {e}")
        if websocket in websocket_connections:
            websocket_connections.remove(websocket)


if __name__ == "__main__":
    grpc_thread = threading.Thread(target=start_grpc_server, daemon=True)
    grpc_thread.start()

    print("Starting FastAPI server on port 8000")
    uvicorn.run(app, host="0.0.0.0", port=8000)
