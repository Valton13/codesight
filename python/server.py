"""CodeSight backend: gRPC ingest + FastAPI/WebSocket fan-out + GitHub analysis."""

import asyncio
import threading
import time
from concurrent import futures
from contextlib import asynccontextmanager

import grpc
import proto.codesight_pb2 as pb2
import proto.codesight_pb2_grpc as pb2_grpc
import uvicorn
from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from github_analyzer import ROOT, clone_repo, parse_github_url
from graph_builder import GraphBuilder
from pydantic import BaseModel

graph_builder = GraphBuilder()
websocket_connections: set[WebSocket] = set()
app_event_loop: asyncio.AbstractEventLoop | None = None

analysis_lock = threading.Lock()
analysis = {
    "phase": "idle",  # idle | cloning | scanning | ready | error
    "message": "Paste a GitHub URL or watch a local folder.",
    "repo": None,
    "scanned": 0,
    "error": None,
}

_last_graph_broadcast = 0.0


def schedule(msg: dict):
    if app_event_loop is not None:
        asyncio.run_coroutine_threadsafe(broadcast(msg), app_event_loop)


async def broadcast(msg: dict):
    dead = set()
    for ws in list(websocket_connections):
        try:
            await ws.send_json(msg)
        except Exception:
            dead.add(ws)
    websocket_connections.difference_update(dead)


def broadcast_graph(force: bool = False):
    global _last_graph_broadcast
    now = time.monotonic()
    if not force and now - _last_graph_broadcast < 0.25:
        return
    _last_graph_broadcast = now
    schedule({"type": "graph", **graph_builder.get_graph_state()})


def set_status(**kw):
    analysis.update(kw)
    schedule({"type": "status", **analysis})


class CodeSightServicer(pb2_grpc.CodeSightServiceServicer):
    def SendParsedFile(self, request, context):
        try:
            graph_builder.update_from_parsed_file(request)
            broadcast_graph()
            return pb2.AckResponse(success=True, message="OK")
        except Exception as e:
            return pb2.AckResponse(success=False, message=str(e))

    def GetGraphState(self, request, context):
        state = graph_builder.get_graph_state()
        return pb2.GraphState(
            nodes=[
                pb2.Node(id=n["id"], label=n["label"], type=n["type"])
                for n in state["nodes"]
            ],
            edges=[
                pb2.Edge(from_=e["from"], to=e["to"], type=e["type"])
                for e in state["edges"]
            ],
            mermaid_diagram="",
        )


def start_grpc_server():
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
    pb2_grpc.add_CodeSightServiceServicer_to_server(CodeSightServicer(), server)
    server.add_insecure_port("[::]:50051")
    server.start()
    print("gRPC server started on port 50051")
    server.wait_for_termination()


def run_analysis(owner: str, repo: str, branch: str | None):
    import subprocess

    try:
        set_status(
            phase="cloning",
            repo=f"{owner}/{repo}",
            scanned=0,
            error=None,
            message=f"Cloning {owner}/{repo}…",
        )
        graph_builder.reset(repo=f"{owner}/{repo}")
        broadcast_graph(force=True)

        dest = clone_repo(owner, repo, branch)
        graph_builder.repo_root = str(dest)

        binary = ROOT / "build" / "codesight"
        if not binary.exists():
            raise RuntimeError("C++ binary missing. Run: cmake --build build")

        set_status(phase="scanning", message="C++ engine parsing repository…")
        log_path = ROOT / "logs" / "scan.log"
        log_path.parent.mkdir(exist_ok=True)

        scanned = 0
        with open(log_path, "w") as log:
            proc = subprocess.Popen(
                [str(binary), "--scan", str(dest), "300"],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )
            for line in proc.stdout:
                log.write(line)
                line = line.strip()
                if line.startswith("[SCAN]"):
                    scanned += 1
                    if scanned % 10 == 0:
                        set_status(scanned=scanned, message=f"Parsed {scanned} files…")
                elif line.startswith("[SCAN_ERROR]"):
                    raise RuntimeError(line)
            proc.wait(timeout=240)

        if proc.returncode != 0:
            raise RuntimeError(
                f"Scanner exited with code {proc.returncode}. See logs/scan.log"
            )

        set_status(
            phase="ready",
            scanned=scanned,
            message=f"Analyzed {scanned} files from {owner}/{repo}.",
        )
        broadcast_graph(force=True)
    except Exception as e:
        set_status(phase="error", error=str(e), message=str(e))
    finally:
        analysis_lock.release()


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


class AnalyzeRequest(BaseModel):
    url: str


@app.get("/")
async def root():
    return {"message": "CodeSight Backend is running"}


@app.get("/graph")
async def get_graph():
    return graph_builder.get_graph_state()


@app.get("/status")
async def get_status():
    return analysis


@app.post("/analyze/github")
async def analyze_github(req: AnalyzeRequest):
    try:
        owner, repo, branch = parse_github_url(req.url)
    except ValueError as e:
        raise HTTPException(status_code=400, detail=str(e))

    if not analysis_lock.acquire(blocking=False):
        raise HTTPException(status_code=409, detail="An analysis is already running.")

    threading.Thread(
        target=run_analysis, args=(owner, repo, branch), daemon=True
    ).start()
    return {"accepted": True, "repo": f"{owner}/{repo}"}


@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    websocket_connections.add(websocket)
    try:
        await websocket.send_json({"type": "status", **analysis})
        await websocket.send_json({"type": "graph", **graph_builder.get_graph_state()})
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        websocket_connections.discard(websocket)
    except Exception:
        websocket_connections.discard(websocket)


if __name__ == "__main__":
    threading.Thread(target=start_grpc_server, daemon=True).start()
    print("Starting FastAPI server on port 8000")
    uvicorn.run(app, host="0.0.0.0", port=8000)
