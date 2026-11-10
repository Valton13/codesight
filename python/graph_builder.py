import re

import networkx as nx
import proto.codesight_pb2 as pb2


class GraphBuilder:
    def __init__(self):
        self.graph = nx.DiGraph()
        self.file_to_functions: dict[str, set[str]] = {}
        self.file_to_imports: dict[str, set[str]] = {}

    def update_from_parsed_file(self, parsed_file: pb2.ParsedFile):
        filepath = parsed_file.filepath

        # Use just the filename for the label to keep it clean and avoid path security issues
        filename = filepath.split("/")[-1]
        self.graph.add_node(filepath, type="file", label=filename)

        if filepath not in self.file_to_functions:
            self.file_to_functions[filepath] = set()

        old_functions = self.file_to_functions[filepath].copy()
        for func_name in old_functions:
            func_id = f"{filepath}::{func_name}"
            if self.graph.has_node(func_id):
                self.graph.remove_node(func_id)

        self.file_to_functions[filepath] = set()
        for func in parsed_file.functions:
            func_id = f"{filepath}::{func.name}"
            self.graph.add_node(func_id, type="function", label=func.name)
            self.graph.add_edge(filepath, func_id, type="defines")
            self.file_to_functions[filepath].add(func.name)

        if filepath not in self.file_to_imports:
            self.file_to_imports[filepath] = set()

        old_imports = self.file_to_imports[filepath].copy()
        for imp_module in old_imports:
            self.graph.remove_edges_from(
                [
                    (filepath, target)
                    for target in list(self.graph.successors(filepath))
                    if self.graph.edges[filepath, target].get("type") == "imports"
                ]
            )

        self.file_to_imports[filepath] = set()
        for imp in parsed_file.imports:
            imp_id = f"import::{imp.module}"
            self.graph.add_node(imp_id, type="import", label=imp.module)
            self.graph.add_edge(filepath, imp_id, type="imports")
            self.file_to_imports[filepath].add(imp.module)

        for route in parsed_file.api_routes:
            route_id = f"{filepath}::{route.method}::{route.path}"
            self.graph.add_node(
                route_id, type="api_route", label=f"{route.method} {route.path}"
            )
            self.graph.add_edge(filepath, route_id, type="defines")

    def get_mermaid_diagram(self) -> str:
        lines = ["graph TD"]

        for node_id, data in self.graph.nodes(data=True):
            node_type = data.get("type", "unknown")
            label = data.get("label", node_id)

            # Safely escape quotes and newlines for Mermaid
            safe_label = str(label).replace('"', '\\"').replace("\n", " ")
            safe_id = self._escape_id(node_id)

            if node_type == "file":
                lines.append(f'    {safe_id}["📄 {safe_label}"]')
            elif node_type == "function":
                lines.append(f'    {safe_id}(["⚙️ {safe_label}"])')
            elif node_type == "import":
                lines.append(f'    {safe_id}{{"📦 {safe_label}"}}')
            elif node_type == "api_route":
                lines.append(f'    {safe_id}>"🌐 {safe_label}"]')

        for source, target, data in self.graph.edges(data=True):
            edge_type = data.get("type", "")
            source_id = self._escape_id(source)
            target_id = self._escape_id(target)

            if edge_type == "imports":
                lines.append(f"    {source_id} -->|imports| {target_id}")
            elif edge_type == "defines":
                lines.append(f"    {source_id} -->|defines| {target_id}")
            elif edge_type == "calls":
                lines.append(f"    {source_id} -->|calls| {target_id}")

        return "\n".join(lines)

    def _escape_id(self, node_id: str) -> str:
        # Replace ALL non-alphanumeric characters with underscores.
        # This guarantees a valid Mermaid ID and prevents Firefox file:// errors.
        return re.sub(r"[^a-zA-Z0-9]", "_", node_id)

    def get_graph_state(self) -> dict:
        nodes = []
        for node_id, data in self.graph.nodes(data=True):
            nodes.append(
                {
                    "id": node_id,
                    "label": data.get("label", node_id),
                    "type": data.get("type", "unknown"),
                }
            )

        edges = []
        for source, target, data in self.graph.edges(data=True):
            edges.append(
                {"from": source, "to": target, "type": data.get("type", "unknown")}
            )

        return {
            "nodes": nodes,
            "edges": edges,
            "mermaid_diagram": self.get_mermaid_diagram(),
        }
