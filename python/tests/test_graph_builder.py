from types import SimpleNamespace as NS

from graph_builder import GraphBuilder


def sym(name, kind, line=1, parent="", exported=True, complexity=1):
    return NS(
        name=name,
        kind=kind,
        line_number=line,
        end_line=line,
        parent=parent,
        exported=exported,
        complexity=complexity,
    )


def imp(module, line=1):
    return NS(module=module, alias="", line_number=line)


def route(method, path, handler="", line=1):
    return NS(method=method, path=path, handler=handler, line_number=line)


def call(caller, callee, line=1):
    return NS(caller=caller, callee=callee, line_number=line)


def fn(name, line=1):
    return NS(name=name, line_number=line)


def add_file(gb, root, rel, symbols=(), imports=(), routes=(), calls=(), functions=()):
    path = root / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(["x"] * 5) + "\n", encoding="utf-8")
    gb.update_from_parsed_file(
        NS(
            filepath=str(path),
            functions=list(functions),
            imports=list(imports),
            api_routes=list(routes),
            symbols=list(symbols),
            calls=list(calls),
        )
    )
    return str(path)


def edges_of(state, kind):
    return [e for e in state["edges"] if e["type"] == kind]


def test_internal_import_and_cross_file_call(tmp_path):
    gb = GraphBuilder()
    gb.reset(repo="demo", repo_root=str(tmp_path))
    a = add_file(
        gb,
        tmp_path,
        "a.py",
        symbols=[sym("run", "function", 2)],
        imports=[imp("from b import helper")],
        calls=[call("run", "helper", 3)],
        functions=[fn("run", 2)],
    )
    b = add_file(
        gb,
        tmp_path,
        "b.py",
        symbols=[sym("helper", "function", 1)],
        functions=[fn("helper", 1)],
    )
    state = gb.get_graph_state()

    assert any(e["from"] == a and e["to"] == b for e in edges_of(state, "imports"))
    calls = edges_of(state, "calls")
    assert calls, "expected at least one resolved call edge"
    assert all(e["confidence"] in ("high", "medium") for e in calls)
    assert state["meta"]["calls"] >= 1


def test_self_method_call(tmp_path):
    gb = GraphBuilder()
    gb.reset(repo="demo", repo_root=str(tmp_path))
    add_file(
        gb,
        tmp_path,
        "c.py",
        symbols=[
            sym("Svc", "class", 1),
            sym("run", "method", 2, parent="Svc"),
            sym("helper", "method", 4, parent="Svc"),
        ],
        calls=[call("Svc.run", "self.helper", 3)],
    )
    state = gb.get_graph_state()
    calls = edges_of(state, "calls")
    assert calls
    assert calls[0]["confidence"] == "high"


def test_external_dependency_node(tmp_path):
    gb = GraphBuilder()
    gb.reset(repo="demo", repo_root=str(tmp_path))
    add_file(gb, tmp_path, "d.py", imports=[imp("import os")])
    state = gb.get_graph_state()
    assert any(n["type"] == "external" and n["label"] == "os" for n in state["nodes"])


def test_route_node_and_exposes_edge(tmp_path):
    gb = GraphBuilder()
    gb.reset(repo="demo", repo_root=str(tmp_path))
    path = add_file(
        gb, tmp_path, "api.py", routes=[route("GET", "/users", "list_users")]
    )
    state = gb.get_graph_state()
    routes = [n for n in state["nodes"] if n["type"] == "route"]
    assert routes and routes[0]["label"] == "GET /users"
    assert any(e["from"] == path and e["type"] == "exposes" for e in state["edges"])


def test_cycle_detection(tmp_path):
    gb = GraphBuilder()
    gb.reset(repo="demo", repo_root=str(tmp_path))
    add_file(gb, tmp_path, "x.py", imports=[imp("import y")])
    add_file(gb, tmp_path, "y.py", imports=[imp("import x")])
    state = gb.get_graph_state()
    assert state["insights"]["stats"]["cycles"] >= 1
    assert state["insights"]["cycles"]


def test_metrics_and_orphans(tmp_path):
    gb = GraphBuilder()
    gb.reset(repo="demo", repo_root=str(tmp_path))
    add_file(gb, tmp_path, "lonely.py", symbols=[sym("solo", "function", 1)])
    state = gb.get_graph_state()
    files = [n for n in state["nodes"] if n["type"] == "file"]
    assert files
    for n in files:
        assert "fan_in" in n and "fan_out" in n
        assert "pagerank" in n and "risk" in n
        assert n["loc"] > 0
    assert state["insights"]["stats"]["orphans"] >= 1
