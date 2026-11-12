export type NodeType = 'file' | 'function' | 'external' | 'route';

export interface GraphNode {
    id: string;
    label: string;
    type: NodeType;
    sub?: string;
    file?: string;
    line?: number;
    functions?: number;
}

export interface GraphEdge {
    from: string;
    to: string;
    type: string;
}

export interface GraphMeta {
    repo: string | null;
    files: number;
    functions: number;
    externals: number;
}

export interface GraphState {
    nodes: GraphNode[];
    edges: GraphEdge[];
    meta: GraphMeta;
}

export type Phase = 'idle' | 'cloning' | 'scanning' | 'ready' | 'error';

export interface AnalysisStatus {
    phase: Phase;
    message: string;
    repo: string | null;
    scanned: number;
    error: string | null;
}

export type ThemeKey = 'ink' | 'ember' | 'forest';

export interface ViewOptions {
    showFunctions: boolean;
    showExternals: boolean;
    groupFolders: boolean;
    edgeLabels: boolean;
    direction: 'TD' | 'LR';
    fontSize: number;
    nodeSpacing: number;
    rankSpacing: number;
    theme: ThemeKey;
}

export const DEFAULT_OPTIONS: ViewOptions = {
    showFunctions: false,
    showExternals: true,
    groupFolders: true,
    edgeLabels: false,
    direction: 'TD',
    fontSize: 14,
    nodeSpacing: 34,
    rankSpacing: 56,
    theme: 'ink',
};

export const THEMES: Record<ThemeKey, {
    bg: string; line: string;
    file: [string, string]; fn: [string, string];
    ext: [string, string]; route: [string, string];
}> = {
    ink: {
        bg: '#070b14', line: '#3b82f6',
        file: ['#0e2233', '#22d3ee'], fn: ['#062b22', '#34d399'],
        ext: ['#2b1c07', '#fbbf24'], route: ['#2b0f1d', '#f472b6'],
    },
    ember: {
        bg: '#120a06', line: '#f59e0b',
        file: ['#241505', '#fbbf24'], fn: ['#2a0d0d', '#f87171'],
        ext: ['#06222b', '#22d3ee'], route: ['#2b0f1d', '#f472b6'],
    },
    forest: {
        bg: '#06110c', line: '#10b981',
        file: ['#0a2418', '#34d399'], fn: ['#1a2405', '#a3e635'],
        ext: ['#2b1c07', '#fbbf24'], route: ['#06222b', '#22d3ee'],
    },
};

const esc = (s: string) => s.replace(/\\/g, '\\\\').replace(/"/g, "'").replace(/\n/g, ' ');

export function buildMermaid(graph: GraphState, opts: ViewOptions): {
    code: string;
    idMap: Record<string, string>;
    visible: number;
} {
    const nodes = graph.nodes.filter(n => {
        if (n.type === 'function') return opts.showFunctions;
        if (n.type === 'external') return opts.showExternals;
        return true;
    });
    const ids = new Set(nodes.map(n => n.id));
    const edges = graph.edges.filter(e => ids.has(e.from) && ids.has(e.to));

    const idMap: Record<string, string> = {};
    nodes.forEach((n, i) => { idMap[n.id] = `n${i}`; });

    const t = THEMES[opts.theme];
    const lines: string[] = [`flowchart ${opts.direction}`];
    lines.push(`classDef file fill:${t.file[0]},stroke:${t.file[1]},stroke-width:1.5px,color:#e6edf7`);
    lines.push(`classDef function fill:${t.fn[0]},stroke:${t.fn[1]},stroke-width:1.2px,color:#e6edf7`);
    lines.push(`classDef external fill:${t.ext[0]},stroke:${t.ext[1]},stroke-width:1.2px,color:#e6edf7`);
    lines.push(`classDef route fill:${t.route[0]},stroke:${t.route[1]},stroke-width:1.2px,color:#e6edf7`);

    const emit = (n: GraphNode) => {
        const id = idMap[n.id];
        const label = esc(n.label);
        if (n.type === 'file') lines.push(`${id}["${label}"]`);
        else if (n.type === 'function') lines.push(`${id}(["${label}"])`);
        else if (n.type === 'external') lines.push(`${id}{{"${label}"}}`);
        else lines.push(`${id}>"${label}"]`);
    };

    if (opts.groupFolders) {
        const fileSub = new Map<string, string>();
        graph.nodes.forEach(n => { if (n.type === 'file') fileSub.set(n.id, n.sub || 'root'); });
        const groups = new Map<string, GraphNode[]>();
        nodes.forEach(n => {
            const g = n.type === 'external' ? 'dependencies'
                : n.type === 'file' ? (n.sub || 'root')
                    : (fileSub.get(n.file || '') || 'root');
            if (!groups.has(g)) groups.set(g, []);
            groups.get(g)!.push(n);
        });
        let gi = 0;
        for (const [g, ns] of groups) {
            lines.push(`subgraph g${gi++}["${esc(g)}"]`);
            ns.forEach(emit);
            lines.push('end');
        }
    } else {
        nodes.forEach(emit);
    }

    edges.forEach(e => {
        const label = opts.edgeLabels ? `|${e.type}|` : '';
        lines.push(`${idMap[e.from]} -->${label} ${idMap[e.to]}`);
    });
    nodes.forEach(n => lines.push(`class ${idMap[n.id]} ${n.type}`));

    return { code: lines.join('\n'), idMap, visible: nodes.length };
}
