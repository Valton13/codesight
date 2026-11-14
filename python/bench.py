"""Reproducible performance measurement for CodeSight.

Runs the C++ parser in --bench mode, then measures Python graph construction,
insight computation, overview fingerprinting, and payload size. Writes
docs/metrics.json. Only report numbers produced by this script.
"""

import json
import subprocess
import sys
import time
from pathlib import Path
from types import SimpleNamespace as NS

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))

from graph_builder import GraphBuilder
from repo_insights import analyze_repo


def parse_bench_line(stdout: str) -> dict:
    for line in stdout.splitlines():
        if line.startswith("[BENCH]"):
            out = {}
            for tok in line[len("[BENCH]") :].split():
                k, _, v = tok.partition("=")
                out[k] = float(v) if "." in v else int(v)
            return out
    raise RuntimeError("no [BENCH] line in scanner output:\n" + stdout)


def to_record(rec: dict) -> NS:
    return NS(
        filepath=rec["filepath"],
        functions=[NS(**x) for x in rec["functions"]],
        imports=[NS(**x) for x in rec["imports"]],
        api_routes=[NS(**x) for x in rec["api_routes"]],
        symbols=[NS(**x) for x in rec["symbols"]],
        calls=[NS(**x) for x in rec["calls"]],
    )


def main() -> None:
    target = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "test_project").resolve()
    max_files = int(sys.argv[2]) if len(sys.argv) > 2 else 300
    dump = ROOT / "logs" / "bench_dump.json"

    binary = ROOT / "build" / "codesight"
    if not binary.exists():
        raise SystemExit("build/codesight missing; run cmake --build build first")

    proc = subprocess.run(
        [str(binary), "--bench", str(target), str(max_files), str(dump)],
        capture_output=True,
        text=True,
        check=True,
    )
    cpp = parse_bench_line(proc.stdout)

    records = [to_record(r) for r in json.loads(dump.read_text())]

    gb = GraphBuilder()
    gb.reset(repo=target.name, repo_root=str(target))

    t0 = time.perf_counter()
    for rec in records:
        gb.update_from_parsed_file(rec)
    ingest_ms = (time.perf_counter() - t0) * 1000

    t0 = time.perf_counter()
    state = gb.get_graph_state()
    graph_ms = (time.perf_counter() - t0) * 1000

    t0 = time.perf_counter()
    overview = analyze_repo(target, gb.imports_by_file())
    overview_ms = (time.perf_counter() - t0) * 1000

    payload = json.dumps(state)
    payload_kb = len(payload.encode("utf-8")) / 1024

    metrics = {
        "target": str(target),
        "cpp": cpp,
        "python": {
            "ingest_ms": round(ingest_ms, 3),
            "graph_build_ms": round(graph_ms, 3),
            "overview_ms": round(overview_ms, 3),
            "graph_payload_kb": round(payload_kb, 1),
            "nodes": len(state["nodes"]),
            "edges": len(state["edges"]),
            "calls_resolved": state["meta"].get("calls", 0),
            "routes": sum(1 for n in state["nodes"] if n["type"] == "route"),
        },
    }

    out = ROOT / "docs" / "metrics.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(metrics, indent=2) + "\n")

    print(json.dumps(metrics, indent=2))
    print(f"\nwrote {out}")


if __name__ == "__main__":
    main()
