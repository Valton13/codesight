"""Incremental dependency graph for CodeSight.

Stores per-file parse records and derives a clean node/edge model:
files, functions, API routes, resolved internal imports, and aggregated
external dependencies. The frontend renders this model with live filters.
"""

import posixpath
import re
from dataclasses import dataclass, field


@dataclass
class FileRecord:
    path: str
    functions: list[tuple[str, int]] = field(default_factory=list)
    imports: list[str] = field(default_factory=list)
    routes: list[tuple[str, str, str]] = field(default_factory=list)


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
        self.files[rec.path] = rec

    # ---------- helpers ----------

    def _rel(self, path: str) -> str:
        if self.repo_root and path.startswith(self.repo_root):
            return path[len(self.repo_root) :].lstrip("/")
        return path

    @staticmethod
    def _import_modules(raw: str) -> list[str]:
        """Extract module specifiers from Python/JS import statements."""
        raw = raw.strip()
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
        return mods

    @staticmethod
    def _external_root(mod: str) -> str | None:
        if mod.startswith("."):
            return None  # unresolved relative import: not an external package
        head = re.split(r"[./]", mod)[0]
        return head or None

    # ---------- graph derivation ----------

    def get_graph_state(self) -> dict:
        nodes: list[dict] = []
        edges: list[dict] = []
        seen_edges = set()

        def add_edge(a: str, b: str, kind: str):
            key = (a, b, kind)
            if a != b and key not in seen_edges:
                seen_edges.add(key)
                edges.append({"from": a, "to": b, "type": kind})

        # Index for resolving internal imports: stem -> real path.
        index: dict[str, str] = {}
        for path in self.files:
            rel = self._rel(path)
            stem = re.sub(r"\.(py|js|jsx|ts|tsx|go|cpp|c|h|hpp)$", "", rel)
            index[stem] = path
            index.setdefault(posixpath.basename(stem), path)

        def resolve(mod: str, file_rel: str) -> str | None:
            candidates = []
            if mod.startswith("."):
                base = posixpath.dirname(file_rel)
                cleaned = mod.lstrip("./")
                candidates.append(posixpath.normpath(posixpath.join(base, cleaned)))
            else:
                candidates.append(mod.replace(".", "/"))
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

        externals: dict[str, str] = {}

        for path, rec in self.files.items():
            rel = self._rel(path)
            nodes.append(
                {
                    "id": path,
                    "label": posixpath.basename(rel),
                    "sub": posixpath.dirname(rel) or "root",
                    "type": "file",
                    "functions": len(rec.functions),
                }
            )

            for name, line in rec.functions:
                fid = f"{path}::{name}"
                nodes.append(
                    {
                        "id": fid,
                        "label": name,
                        "type": "function",
                        "file": path,
                        "line": line,
                    }
                )
                add_edge(path, fid, "defines")

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
                            nodes.append(
                                {
                                    "id": eid,
                                    "label": root,
                                    "type": "external",
                                }
                            )
                        add_edge(path, eid, "imports")

        return {
            "nodes": nodes,
            "edges": edges,
            "meta": {
                "repo": self.repo,
                "files": len(self.files),
                "functions": sum(len(r.functions) for r in self.files.values()),
                "externals": len(externals),
            },
        }
