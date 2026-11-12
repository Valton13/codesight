export type NodeType =
    | 'file' | 'class' | 'interface' | 'struct' | 'enum' | 'trait' | 'module'
    | 'method' | 'function' | 'constructor' | 'external' | 'route';

export const TYPE_KINDS = ['class', 'interface', 'struct', 'enum', 'trait', 'module'] as const;
export const CALLABLE_KINDS = ['function', 'method', 'constructor'] as const;

export interface GraphNode {
    id: string;
    label: string;
    type: NodeType;
    sub?: string;
    file?: string;
    line?: number;
    end_line?: number;
    exported?: boolean;
    complexity?: number;
    parent_name?: string;
    loc?: number;
    fan_in?: number;
    fan_out?: number;
    pagerank?: number;
    risk?: number;
    is_test?: boolean;
    is_entry?: boolean;
    symbols?: number;
}

export interface GraphEdge { from: string; to: string; type: string }

export interface GraphMeta {
    repo: string | null;
    files: number;
    functions: number;
    types?: number;
    externals: number;
}

export interface Ref { id: string; label: string }

export interface Insights {
    cycles: Ref[][];
    key_files: Array<Ref & { sub?: string; score: number; fan_in: number; risk: number; is_entry: boolean }>;
    hotspots: Array<Ref & { complexity: number; fan_in: number; loc: number; risk: number }>;
    orphans: Ref[];
    stats: { cycles: number; orphans: number; avg_fan_out: number; max_fan_in: number };
}

export interface GraphState {
    nodes: GraphNode[];
    edges: GraphEdge[];
    meta: GraphMeta;
    insights?: Insights;
}

export interface Overview {
    root?: string;
    files: number;
    loc: number;
    languages: Array<{ name: string; files: number; loc: number; percent: number }>;
    frameworks: Array<{ name: string; kind: string }>;
    package_managers: string[];
    entry_points: string[];
    commands: Record<string, string[]>;
    ci: string[];
    license: string | null;
    docs: { readme: boolean; docs_dir: boolean; examples: boolean };
    monorepo: { detected: boolean; tool: string | null; workspaces: string[] };
    tests: { files: number };
    dependencies: { direct: number; dev: number; names: string[] };
}

export type Phase = 'idle' | 'cloning' | 'scanning' | 'ready' | 'error';

export interface AnalysisStatus {
    phase: Phase; message: string; repo: string | null; scanned: number; error: string | null;
}

export type ThemeKey = 'ink' | 'ember' | 'forest';
export type SymbolFilter = 'none' | 'types' | 'all';
export type FocusDirection = 'down' | 'up' | 'both';
export type ColorMode = 'type' | 'risk';

export interface ViewOptions {
    symbolFilter: SymbolFilter;
    showExternals: boolean;
    groupFolders: boolean;
    edgeLabels: boolean;
    colorMode: ColorMode;
    direction: 'TD' | 'LR';
    fontSize: number;
    nodeSpacing: number;
    rankSpacing: number;
    theme: ThemeKey;
}

export const DEFAULT_OPTIONS: ViewOptions = {
    symbolFilter: 'none',
    showExternals: true,
    groupFolders: false,
    edgeLabels: false,
    colorMode: 'type',
    direction: 'LR',
    fontSize: 14,
    nodeSpacing: 40,
    rankSpacing: 70,
    theme: 'ink',
};

interface Palette {
    bg: string; line: string;
    file: [string, string]; fn: [string, string];
    ext: [string, string]; route: [string, string]; sym: [string, string];
}

export const THEMES: Record<ThemeKey, Palette> = {
    ink: {
        bg: '#070b14', line: '#3b82f6',
        file: ['#0e2233', '#22d3ee'], fn: ['#062b22', '#34d399'],
        ext: ['#2b1c07', '#fbbf24'], route: ['#2b0f1d', '#f472b6'],
        sym: ['#1b1436', '#a78bfa'],
    },
    ember: {
        bg: '#120a06', line: '#f59e0b',
        file: ['#241505', '#fbbf24'], fn: ['#2a0d0d', '#f87171'],
        ext: ['#06222b', '#22d3ee'], route: ['#2b0f1d', '#f472b6'],
        sym: ['#241033', '#c084fc'],
    },
    forest: {
        bg: '#06110c', line: '#10b981',
        file: ['#0a2418', '#34d399'], fn: ['#1a2405', '#a3e635'],
        ext: ['#2b1c07', '#fbbf24'], route: ['#06222b', '#22d3ee'],
        sym: ['#101f33', '#818cf8'],
    },
};

export const LEVEL_COLORS = [
    '#22d3ee', '#3b82f6', '#8b5cf6', '#f472b6',
    '#fbbf24', '#34d399', '#f87171', '#a3e635',
];

export const RISK_COLORS = ['#34d399', '#a3e635', '#fbbf24', '#fb923c', '#f87171'];

export function riskBucket(risk = 0): number {
    return Math.max(0, Math.min(4, Math.floor(risk * 5)));
}

export type EdgeKind = 'import' | 'dependency' | 'defines' | 'contains' | 'exposes';

export function edgeKind(edge: GraphEdge, byId: Map<string, GraphNode>): EdgeKind {
    if (edge.type === 'defines') return 'defines';
    if (edge.type === 'contains') return 'contains';
    if (edge.type === 'exposes') return 'exposes';
    return byId.get(edge.to)?.type === 'external' ? 'dependency' : 'import';
}

const esc = (s: string) => s.replace(/\\/g, '\\\\').replace(/"/g, "'").replace(/\n/g, ' ');

export function getLevels(
    graph: GraphState, rootId: string, direction: FocusDirection,
): Map<string, number> {
    const down = new Map<string, string[]>();
    const up = new Map<string, string[]>();

    for (const e of graph.edges) {
        if (e.type !== 'imports') continue;
        if (!down.has(e.from)) down.set(e.from, []);
        down.get(e.from)!.push(e.to);
        if (!up.has(e.to)) up.set(e.to, []);
        up.get(e.to)!.push(e.from);
    }

    const levels = new Map<string, number>([[rootId, 0]]);
    const queue = [rootId];

    while (queue.length) {
        const current = queue.shift()!;
        const level = levels.get(current)!;
        const next: string[] = [];
        if (direction === 'down' || direction === 'both') next.push(...(down.get(current) || []));
        if (direction === 'up' || direction === 'both') next.push(...(up.get(current) || []));
        for (const n of next) {
            if (!levels.has(n)) {
                levels.set(n, level + 1);
                queue.push(n);
            }
        }
    }
    return levels;
}

export function buildFocusGraph(
    graph: GraphState, rootId: string, depth: number, levels: Map<string, number>,
): GraphState {
    const keep = new Set<string>();
    for (const [id, level] of levels) if (level <= depth) keep.add(id);
    keep.add(rootId);

    const nodes = graph.nodes.filter(n => {
        if (keep.has(n.id)) return true;
        if (n.file && keep.has(n.file) && n.type !== 'external') return true;
        return false;
    });
    const ids = new Set(nodes.map(n => n.id));

    const edges = graph.edges.filter(e => {
        if (!ids.has(e.from) || !ids.has(e.to)) return false;
        if (e.type === 'imports') return levels.has(e.from) && levels.has(e.to);
        return true;
    });

    return { ...graph, nodes, edges };
}

export function buildMermaid(
    graph: GraphState,
    opts: ViewOptions,
    focus?: { rootId: string; levels: Map<string, number> } | null,
): { code: string; idMap: Record<string, string>; visible: number } {
    const byId = new Map(graph.nodes.map(n => [n.id, n]));

    const nodes = graph.nodes.filter(n => {
        if ((CALLABLE_KINDS as readonly string[]).includes(n.type)) return opts.symbolFilter === 'all';
        if ((TYPE_KINDS as readonly string[]).includes(n.type)) return opts.symbolFilter !== 'none';
        if (n.type === 'external') return opts.showExternals;
        return true;
    });

    const ids = new Set(nodes.map(n => n.id));
    const edges = graph.edges.filter(e => ids.has(e.from) && ids.has(e.to));

    const idMap: Record<string, string> = {};
    nodes.forEach((n, i) => { idMap[n.id] = `n${i}`; });

    // Mermaid reserves keywords such as `call`, `click`, `end`, `style`, `default`.
    // Namespacing every class name guarantees a node kind can never collide with one.
    const C = {
        file: 'cs_file',
        sym: 'cs_sym',
        callable: 'cs_callable',
        external: 'cs_external',
        route: 'cs_route',
        root: 'cs_root',
        risk: (i: number) => `cs_risk${i}`,
        lvl: (i: number) => `cs_lvl${i}`,
    };

    const t = THEMES[opts.theme];
    const lines: string[] = [`flowchart ${opts.direction}`];

    lines.push(`classDef ${C.file} fill:${t.file[0]},stroke:${t.file[1]},stroke-width:1.6px,color:#e6edf7`);
    lines.push(`classDef ${C.sym} fill:${t.sym[0]},stroke:${t.sym[1]},stroke-width:1.5px,color:#e6edf7`);
    lines.push(`classDef ${C.callable} fill:${t.fn[0]},stroke:${t.fn[1]},stroke-width:1.2px,color:#e6edf7`);
    lines.push(`classDef ${C.external} fill:${t.ext[0]},stroke:${t.ext[1]},stroke-width:1.5px,color:#e6edf7`);
    lines.push(`classDef ${C.route} fill:${t.route[0]},stroke:${t.route[1]},stroke-width:1.2px,color:#e6edf7`);

    RISK_COLORS.forEach((color, i) => {
        lines.push(`classDef ${C.risk(i)} fill:#0b1220,stroke:${color},stroke-width:${1.4 + i * 0.4}px,color:#e6edf7`);
    });

    if (focus) {
        lines.push(`classDef ${C.root} fill:${t.file[0]},stroke:${LEVEL_COLORS[0]},stroke-width:3px,color:#ffffff`);
        LEVEL_COLORS.forEach((color, i) => {
            lines.push(`classDef ${C.lvl(i)} fill:#0b1220,stroke:${color},stroke-width:2px,color:#e6edf7`);
        });
    }

    const classOf = (n: GraphNode): string => {
        if (focus && n.type === 'file') {
            const level = focus.levels.get(n.id) ?? 0;
            return level === 0 ? C.root : C.lvl(level % LEVEL_COLORS.length);
        }
        if (opts.colorMode === 'risk' && n.type === 'file') return C.risk(riskBucket(n.risk));
        if ((TYPE_KINDS as readonly string[]).includes(n.type)) return C.sym;
        if ((CALLABLE_KINDS as readonly string[]).includes(n.type)) return C.callable;
        if (n.type === 'external') return C.external;
        if (n.type === 'route') return C.route;
        return C.file;
    };

    const labelOf = (n: GraphNode): string => {
        let label = n.label;
        if (focus) {
            const level = focus.levels.get(n.id);
            if (level != null) label = `L${level} · ${label}`;
        }
        if (n.type === 'file') {
            if (n.is_entry) label = `▶ ${label}`;
            if (n.is_test) label = `✓ ${label}`;
        } else if (n.exported === false) {
            label = `· ${label}`;
        }
        return label;
    };

    const emit = (n: GraphNode) => {
        const id = idMap[n.id];
        const label = esc(labelOf(n));
        switch (n.type) {
            case 'class': case 'trait': lines.push(`${id}("${label}")`); break;
            case 'interface': case 'method': case 'function': case 'constructor':
                lines.push(`${id}(["${label}"])`); break;
            case 'struct': case 'module': lines.push(`${id}[["${label}"]]`); break;
            case 'enum': lines.push(`${id}{{"${label}"}}`); break;
            case 'external': lines.push(`${id}(("${label}"))`); break;
            case 'route': lines.push(`${id}>"${label}"]`); break;
            default: lines.push(`${id}["${label}"]`);
        }
    };

    if (opts.groupFolders) {
        const fileSub = new Map<string, string>();
        graph.nodes.forEach(n => { if (n.type === 'file') fileSub.set(n.id, n.sub || 'root'); });
        const groups = new Map<string, GraphNode[]>();
        nodes.forEach(n => {
            const group = n.type === 'external' ? 'dependencies'
                : n.type === 'file' ? (n.sub || 'root')
                    : fileSub.get(n.file || '') || 'root';
            if (!groups.has(group)) groups.set(group, []);
            groups.get(group)!.push(n);
        });
        let gi = 0;
        for (const [group, groupNodes] of groups) {
            lines.push(`subgraph g${gi++}["${esc(group)}"]`);
            groupNodes.forEach(emit);
            lines.push('end');
        }
    } else {
        nodes.forEach(emit);
    }

    const styleLines: string[] = [];
    edges.forEach((edge, index) => {
        const kind = edgeKind(edge, byId);
        const arrow = kind === 'dependency' || kind === 'defines' || kind === 'contains' ? '-.->'
            : kind === 'exposes' ? '==>' : '-->';
        const label = opts.edgeLabels ? `|${kind === 'dependency' ? 'depends on' : kind}|` : '';
        lines.push(`${idMap[edge.from]} ${arrow}${label} ${idMap[edge.to]}`);

        const style = kind === 'import' ? `stroke:${t.line},stroke-width:2.2px`
            : kind === 'dependency' ? `stroke:${t.ext[1]},stroke-width:1.7px,stroke-dasharray:6 4`
                : kind === 'contains' ? `stroke:${t.sym[1]},stroke-width:1.2px,stroke-dasharray:3 3`
                    : kind === 'defines' ? `stroke:${t.fn[1]},stroke-width:1.2px,stroke-dasharray:2 4`
                        : `stroke:${t.route[1]},stroke-width:1.8px,stroke-dasharray:10 4`;
        styleLines.push(`linkStyle ${index} ${style}`);
    });

    lines.push(...styleLines);
    nodes.forEach(n => lines.push(`class ${idMap[n.id]} ${classOf(n)}`));

    return { code: lines.join('\n'), idMap, visible: nodes.length };
}
