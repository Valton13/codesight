"""Dependency + symbol graph with metrics, cycles, key-file ranking and risk scoring."""

import posixpath
import re
from dataclasses import dataclass, field
from pathlib import Path

SOURCE_EXT = (
    r"(?:py|js|jsx|mjs|cjs|ts|tsx|mts|cts|c|h|cpp|cc|cxx|hpp|hh|hxx|"
    r"java|cs|go|rs|rb|php)"
)

TYPE_KINDS = {"class", "interface", "struct", "enum", "trait", "module"}
CALLABLE_KINDS = {"function", "method", "constructor"}

ENTRY_NAMES = {
    "main.py",
    "app.py",
    "server.py",
    "manage.py",
    "asgi.py",
    "wsgi.py",
    "__main__.py",
    "index.js",
    "index.ts",
    "server.js",
    "server.ts",
    "app.js",
    "app.ts",
    "main.ts",
    "main.go",
    "main.c",
    "main.cpp",
    "main.rs",
    "lib.rs",
    "Main.java",
    "Application.java",
    "Program.cs",
    "index.php",
    "artisan",
}

TEST_RE = re.compile(
    r"(^|/)(tests?|__tests__|spec)(/|$)|(^|/)test_[^/]+$|_test\.[a-z]+$|"
    r"\.(test|spec)\.[a-z]+$|Test\.java$|Tests\.cs$|_test\.go$",
    re.IGNORECASE,
)


@dataclass
class Symbol:
    name: str
    kind: str
    line: int
    end_line: int
    parent: str
    exported: bool
    complexity: int


@dataclass
class FileRecord:
    path: str
    functions: list[tuple[str, int]] = field(default_factory=list)
    imports: list[str] = field(default_factory=list)
    routes: list[tuple[str, str, str]] = field(default_factory=list)
    symbols: list[Symbol] = field(default_factory=list)
    loc: int = 0


def _count_lines(path: str) -> int:
    try:
        p = Path(path)
        if p.stat().st_size > 1_000_000:
            return 0
        return p.read_text(encoding="utf-8", errors="ignore").count("\n") + 1
    except OSError:
        return 0


def _pagerank(
    nodes: list[str],
    edges: list[tuple[str, str]],
    damping: float = 0.85,
    iterations: int = 24,
) -> dict[str, float]:
    n = len(nodes)
    if n == 0:
        return {}
    rank = {node: 1.0 / n for node in nodes}
    out: dict[str, list[str]] = {node: [] for node in nodes}
    for a, b in edges:
        if a in out and b in rank:
            out[a].append(b)

    for _ in range(iterations):
        dangling = sum(rank[node] for node in nodes if not out[node])
        next_rank = {node: (1 - damping) / n + damping * dangling / n for node in nodes}
        for node in nodes:
            targets = out[node]
            if targets:
                share = damping * rank[node] / len(targets)
                for t in targets:
                    next_rank[t] += share
        rank = next_rank
    return rank


def _cycles(nodes: list[str], edges: list[tuple[str, str]]) -> list[list[str]]:
    """Tarjan SCC; returns components with more than one node."""
    adj: dict[str, list[str]] = {n: [] for n in nodes}
    for a, b in edges:
        if a in adj and b in adj:
            adj[a].append(b)

    index: dict[str, int] = {}
    low: dict[str, int] = {}
    stack: list[str] = []
    on_stack = set()
    result: list[list[str]] = []
    counter = [0]

    def strongconnect(v: str):
        work = [(v, 0)]
        while work:
            node, i = work[-1]
            if i == 0:
                index[node] = counter[0]
                low[node] = counter[0]
                counter[0] += 1
                stack.append(node)
                on_stack.add(node)
            recursed = False
            neighbours = adj.get(node, [])
            while i < len(neighbours):
                w = neighbours[i]
                i += 1
                if w not in index:
                    work[-1] = (node, i)
                    work.append((w, 0))
                    recursed = True
                    break
                elif w in on_stack:
                    low[node] = min(low[node], index[w])
            if recursed:
                continue
            work.pop()
            if work:
                parent = work[-1][0]
                low[parent] = min(low[parent], low[node])
            if low[node] == index[node]:
                component = []
                while True:
                    w = stack.pop()
                    on_stack.discard(w)
                    component.append(w)
                    if w == node:
                        break
                if len(component) > 1:
                    result.append(component)

    for node in nodes:
        if node not in index:
            strongconnect(node)
    return result


class GraphBuilder:
    def __init__(self):
        self.files: dict[str, FileRecord] = {}
        self.repo: str | None = None
        self.repo_root: str | None = None

    def reset(self, repo: str | None = None, repo_root: str | None = None):
        self.files.clear()
        self.repo = repo
        self.repo_root = repo_root

    def update_from_parsed_file(self, parsed_file):
        rec = FileRecord(path=parsed_file.filepath)
        rec.functions = [(f.name, f.line_number) for f in parsed_file.functions]
        rec.imports = [i.module for i in parsed_file.imports]
        rec.routes = [(r.method, r.path, r.handler) for r in parsed_file.api_routes]
        rec.symbols = [
            Symbol(
                s.name,
                s.kind,
                s.line_number,
                s.end_line,
                s.parent,
                s.exported,
                s.complexity,
            )
            for s in getattr(parsed_file, "symbols", [])
        ]
        rec.loc = _count_lines(rec.path)
        self.files[rec.path] = rec

    def imports_by_file(self) -> dict[str, list[str]]:
        return {self._rel(p): rec.imports for p, rec in self.files.items()}

    def _rel(self, path: str) -> str:
        if self.repo_root and path.startswith(self.repo_root):
            return path[len(self.repo_root) :].lstrip("/")
        return path

    @staticmethod
    def _import_modules(raw: str) -> list[str]:
        raw = raw.strip()
        if len(raw) >= 2 and raw[0] in "\"<'" and raw[-1] in "\">'":
            raw = raw[1:-1].strip()

        m = re.match(r"^([\w./\-]+)\." + SOURCE_EXT + r"$", raw)
        if m:
            return [m.group(1)]

        mods: list[str] = []
        m = re.match(r"from\s+([\w.\-/]+)\s+import", raw)
        if m:
            mods.append(m.group(1))
        elif raw.startswith("import "):
            for part in raw[7:].split(","):
                part = part.strip().split(" as ")[0].strip()
                if part:
                    mods.append(part)
        else:
            m = re.search(r"from\s+['\"]([^'\"]+)['\"]", raw)
            if m:
                mods.append(m.group(1))
            else:
                m = re.match(r"import\s+['\"]([^'\"]+)['\"]", raw)
                if m:
                    mods.append(m.group(1))

        if (
            not mods
            and raw
            and re.match(r"^[@\w./:\-]+$", raw)
            and not raw.startswith(("import", "from", "use", "using"))
        ):
            mods.append(raw)
        return mods

    @staticmethod
    def _external_root(mod: str) -> str | None:
        if mod.startswith("."):
            return None
        head = re.split(r"[./]", mod)[0]
        return head or None

    def get_graph_state(self) -> dict:
        nodes: list[dict] = []
        edges: list[dict] = []
        seen_edges = set()

        def add_edge(a: str, b: str, kind: str):
            key = (a, b, kind)
            if a != b and key not in seen_edges:
                seen_edges.add(key)
                edges.append({"from": a, "to": b, "type": kind})

        index: dict[str, str] = {}
        for path in self.files:
            rel = self._rel(path)
            stem = re.sub(r"\." + SOURCE_EXT + r"$", "", rel)
            index[stem] = path
            index.setdefault(posixpath.basename(stem), path)

        def resolve(mod: str, file_rel: str) -> str | None:
            candidates = []
            if mod.startswith("."):
                base = posixpath.dirname(file_rel)
                candidates.append(
                    posixpath.normpath(posixpath.join(base, mod.lstrip("./")))
                )
            else:
                candidates.append(mod.replace(".", "/"))
                candidates.append(mod.replace("::", "/"))
                candidates.append(mod.lstrip("./"))
            for c in candidates:
                if c in index:
                    return index[c]
                if c + "/__init__" in index:
                    return index[c + "/__init__"]
                for stem, path in index.items():
                    if stem.endswith("/" + c):
                        return path
            return None

        # ---- pass 1: file/symbol/external nodes and non-import edges ----
        externals: dict[str, str] = {}
        pending_imports: list[tuple[str, str]] = []
        symbol_ids: dict[tuple[str, str], str] = {}

        for path, rec in self.files.items():
            rel = self._rel(path)
            nodes.append(
                {
                    "id": path,
                    "label": posixpath.basename(rel),
                    "sub": posixpath.dirname(rel) or "root",
                    "type": "file",
                    "loc": rec.loc,
                    "is_test": bool(TEST_RE.search(rel)),
                    "is_entry": posixpath.basename(rel) in ENTRY_NAMES,
                    "complexity": sum(s.complexity for s in rec.symbols),
                    "symbols": len(rec.symbols),
                }
            )

            for sym in rec.symbols:
                sid = f"{path}::{sym.parent + '.' if sym.parent else ''}{sym.name}@{sym.line}"
                symbol_ids[
                    (path, f"{sym.parent}.{sym.name}" if sym.parent else sym.name)
                ] = sid
                nodes.append(
                    {
                        "id": sid,
                        "label": sym.name,
                        "type": sym.kind,
                        "file": path,
                        "line": sym.line,
                        "end_line": sym.end_line,
                        "exported": sym.exported,
                        "complexity": sym.complexity,
                        "parent_name": sym.parent,
                    }
                )

            for method, rpath, handler in rec.routes:
                rid = f"{path}::{method}::{rpath}"
                nodes.append(
                    {
                        "id": rid,
                        "label": f"{method} {rpath}",
                        "type": "route",
                        "file": path,
                    }
                )
                add_edge(path, rid, "exposes")

            for raw in rec.imports:
                for mod in self._import_modules(raw):
                    pending_imports.append((path, mod))

        # symbol containment edges
        for node in nodes:
            if node["type"] in TYPE_KINDS or node["type"] in CALLABLE_KINDS:
                parent_name = node.get("parent_name")
                if parent_name:
                    pid = symbol_ids.get((node["file"], parent_name))
                    if pid:
                        add_edge(pid, node["id"], "contains")
                    else:
                        add_edge(node["file"], node["id"], "defines")
                else:
                    add_edge(node["file"], node["id"], "defines")

        # ---- pass 2: import edges ----
        for path, mod in pending_imports:
            rel = self._rel(path)
            target = resolve(mod, rel)
            if target and target in self.files:
                add_edge(path, target, "imports")
            else:
                root = self._external_root(mod)
                if not root:
                    continue
                eid = f"ext::{root}"
                if eid not in externals:
                    externals[eid] = root
                    nodes.append({"id": eid, "label": root, "type": "external"})
                add_edge(path, eid, "imports")

        # ---- metrics on the file-level import graph ----
        file_ids = list(self.files.keys())
        file_edges = [
            (e["from"], e["to"])
            for e in edges
            if e["type"] == "imports"
            and e["from"] in self.files
            and e["to"] in self.files
        ]

        fan_in = {f: 0 for f in file_ids}
        fan_out = {f: 0 for f in file_ids}
        for a, b in file_edges:
            fan_out[a] += 1
            fan_in[b] += 1

        ranks = _pagerank(file_ids, file_edges)
        max_rank = max(ranks.values()) if ranks else 1.0
        max_fan_in = max(fan_in.values()) if fan_in else 1
        max_complexity = (
            max(
                (n.get("complexity", 0) for n in nodes if n["type"] == "file"),
                default=1,
            )
            or 1
        )

        for node in nodes:
            if node["type"] != "file":
                continue
            fid = node["id"]
            node["fan_in"] = fan_in[fid]
            node["fan_out"] = fan_out[fid]
            node["pagerank"] = round(ranks.get(fid, 0.0) / max_rank, 4)
            complexity_norm = node.get("complexity", 0) / max_complexity
            fan_norm = fan_in[fid] / max_fan_in if max_fan_in else 0
            test_penalty = 0.0 if node["is_test"] else 0.15
            node["risk"] = round(
                min(1.0, 0.45 * complexity_norm + 0.4 * fan_norm + test_penalty), 3
            )

        cycle_groups = _cycles(file_ids, file_edges)
        key_files = sorted(
            (n for n in nodes if n["type"] == "file" and not n["is_test"]),
            key=lambda n: (
                n["pagerank"] * 0.55
                + (n["fan_in"] / max_fan_in if max_fan_in else 0) * 0.3
                + (0.15 if n["is_entry"] else 0)
            ),
            reverse=True,
        )[:10]
        hotspots = sorted(
            (n for n in nodes if n["type"] == "file"),
            key=lambda n: n.get("complexity", 0) * (n["fan_in"] + 1),
            reverse=True,
        )[:10]
        connected = {a for a, _ in file_edges} | {b for _, b in file_edges}
        orphans = [
            n["id"] for n in nodes if n["type"] == "file" and n["id"] not in connected
        ]

        return {
            "nodes": nodes,
            "edges": edges,
            "meta": {
                "repo": self.repo,
                "files": len(self.files),
                "functions": sum(1 for n in nodes if n["type"] in CALLABLE_KINDS),
                "types": sum(1 for n in nodes if n["type"] in TYPE_KINDS),
                "externals": len(externals),
            },
            "insights": {
                "cycles": [
                    [
                        {"id": fid, "label": posixpath.basename(self._rel(fid))}
                        for fid in group
                    ]
                    for group in cycle_groups[:12]
                ],
                "key_files": [
                    {
                        "id": n["id"],
                        "label": n["label"],
                        "sub": n["sub"],
                        "score": round(n["pagerank"], 3),
                        "fan_in": n["fan_in"],
                        "risk": n["risk"],
                        "is_entry": n["is_entry"],
                    }
                    for n in key_files
                ],
                "hotspots": [
                    {
                        "id": n["id"],
                        "label": n["label"],
                        "complexity": n.get("complexity", 0),
                        "fan_in": n["fan_in"],
                        "loc": n.get("loc", 0),
                        "risk": n["risk"],
                    }
                    for n in hotspots
                ],
                "orphans": [
                    {"id": fid, "label": posixpath.basename(self._rel(fid))}
                    for fid in orphans[:20]
                ],
                "stats": {
                    "cycles": len(cycle_groups),
                    "orphans": len(orphans),
                    "avg_fan_out": round(sum(fan_out.values()) / len(file_ids), 2)
                    if file_ids
                    else 0,
                    "max_fan_in": max_fan_in,
                },
            },
        }
