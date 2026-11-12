import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import DiagramCanvas from './components/DiagramCanvas';
import {
  buildFocusGraph, buildMermaid, DEFAULT_OPTIONS, getLevels, riskBucket,
  CALLABLE_KINDS, THEMES, TYPE_KINDS,
  type AnalysisStatus, type FocusDirection, type GraphNode, type GraphState,
  type Overview, type ViewOptions,
} from './lib/graph';

const API = 'http://127.0.0.1:8000';
const WS = 'ws://127.0.0.1:8000/ws';

const SAMPLES = ['Valton13/codesight', 'fastapi/fastapi', 'pallets/flask', 'expressjs/express'];

interface Toast {
  id: number; msg: string; kind: 'success' | 'error' | 'info';
  action?: () => void; actionLabel?: string;
}
interface LogLine { t: string; msg: string; phase: string }

type Tab = 'overview' | 'view' | 'impact' | 'insights';

const IDLE: AnalysisStatus = {
  phase: 'idle', message: 'Paste a GitHub URL or analyze a local folder.',
  repo: null, scanned: 0, error: null,
};

export default function App() {
  const [graph, setGraph] = useState<GraphState | null>(null);
  const [overview, setOverview] = useState<Overview | null>(null);
  const [status, setStatus] = useState<AnalysisStatus>(IDLE);
  const [opts, setOpts] = useState<ViewOptions>(DEFAULT_OPTIONS);

  const [url, setUrl] = useState('');
  const [localPath, setLocalPath] = useState('');
  const [log, setLog] = useState<LogLine[]>([]);
  const [toasts, setToasts] = useState<Toast[]>([]);
  const [tab, setTab] = useState<Tab>('overview');

  const [drawerId, setDrawerId] = useState<string | null>(null);
  const [sideOpen, setSideOpen] = useState(false);
  const [collapsed, setCollapsed] = useState(false);
  const [immersive, setImmersive] = useState(false);

  const [focusId, setFocusId] = useState<string | null>(null);
  const [focusDepth, setFocusDepth] = useState(3);
  const [direction, setDirection] = useState<FocusDirection>('down');

  const [recent, setRecent] = useState<string[]>(() => {
    try { return JSON.parse(localStorage.getItem('cs-recent') || '[]'); } catch { return []; }
  });

  const inputRef = useRef<HTMLInputElement>(null);
  const prevGraph = useRef<GraphState | null>(null);
  const lastUrl = useRef('');
  const toastId = useRef(0);

  const toast = useCallback((msg: string, kind: Toast['kind'] = 'info',
    action?: () => void, actionLabel?: string) => {
    const id = ++toastId.current;
    setToasts(cur => [...cur, { id, msg, kind, action, actionLabel }]);
    setTimeout(() => setToasts(cur => cur.filter(t => t.id !== id)), 5000);
  }, []);

  useEffect(() => {
    let alive = true;
    let ws: WebSocket | null = null;
    let timer: number | undefined;

    const connect = () => {
      if (!alive) return;
      ws = new WebSocket(WS);
      ws.onmessage = event => {
        try {
          const msg = JSON.parse(event.data);
          if (msg.type === 'graph') {
            setGraph({ nodes: msg.nodes, edges: msg.edges, meta: msg.meta, insights: msg.insights });
          } else if (msg.type === 'overview') {
            setOverview(msg as Overview);
          } else if (msg.type === 'overview_reset') {
            setOverview(null);
          } else if (msg.type === 'status') {
            setStatus(msg);
            setLog(cur => {
              const line = { t: new Date().toLocaleTimeString(), msg: msg.message, phase: msg.phase };
              if (cur[0]?.msg === line.msg) return cur;
              return [line, ...cur].slice(0, 40);
            });
            if (msg.phase === 'ready') toast(`Analysis complete: ${msg.scanned} files`, 'success');
            if (msg.phase === 'error') toast(msg.error || 'Analysis failed', 'error');
          }
        } catch { /* ignore malformed frames */ }
      };
      ws.onclose = () => { if (alive) timer = window.setTimeout(connect, 2500); };
      ws.onerror = () => ws?.close();
    };

    connect();
    return () => { alive = false; clearTimeout(timer); ws?.close(); };
  }, [toast]);

  useEffect(() => {
    if (status.phase === 'cloning') { setFocusId(null); setDrawerId(null); }
  }, [status.phase]);

  useEffect(() => {
    if (graph && focusId && !graph.nodes.some(n => n.id === focusId)) setFocusId(null);
  }, [graph, focusId]);

  const post = useCallback(async (endpoint: string, body: object, label: string) => {
    setStatus(cur => ({ ...cur, phase: 'cloning', message: `Preparing ${label}…`, error: null }));
    try {
      const res = await fetch(`${API}${endpoint}`, {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body),
      });
      const data = await res.json().catch(() => ({}));
      if (!res.ok) {
        setStatus(cur => ({ ...cur, phase: 'error', error: data.detail || `HTTP ${res.status}` }));
        toast(data.detail || `Request failed (${res.status})`, 'error');
        return;
      }
      if (data.repo && endpoint.includes('github')) {
        setRecent(cur => {
          const next = [data.repo, ...cur.filter(x => x !== data.repo)].slice(0, 6);
          localStorage.setItem('cs-recent', JSON.stringify(next));
          return next;
        });
      }
      setSideOpen(false);
    } catch {
      setStatus(cur => ({ ...cur, phase: 'error', error: 'Backend unreachable on port 8000.' }));
      toast('Backend unreachable on port 8000', 'error');
    }
  }, [toast]);

  const analyze = useCallback((target: string) => {
    const value = target.trim();
    if (!value) { toast('Enter a GitHub URL first', 'error'); inputRef.current?.focus(); return; }
    lastUrl.current = value;
    post('/analyze/github', { url: value }, value);
  }, [post, toast]);

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') {
        if (focusId) setFocusId(null);
        else if (drawerId) setDrawerId(null);
        else if (immersive) setImmersive(false);
      }
      if (e.key === '/' && document.activeElement !== inputRef.current) {
        e.preventDefault(); inputRef.current?.focus();
      }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [focusId, drawerId, immersive]);

  const busy = status.phase === 'cloning' || status.phase === 'scanning';

  const levels = useMemo(
    () => (graph && focusId ? getLevels(graph, focusId, direction) : null),
    [graph, focusId, direction],
  );

  const maxLevel = useMemo(
    () => (levels ? Math.max(0, ...levels.values()) : 0), [levels],
  );

  const focusGraph = useMemo(() => {
    if (!graph || !focusId || !levels) return null;
    const depth = focusDepth >= 10 ? Number.POSITIVE_INFINITY : focusDepth;
    return buildFocusGraph(graph, focusId, depth, levels);
  }, [graph, focusId, levels, focusDepth]);

  const displayGraph = focusGraph || graph;

  const effectiveOpts = useMemo(() => {
    const tooBig = (displayGraph?.nodes.length || 0) > 320;
    return {
      ...opts,
      symbolFilter: tooBig && opts.symbolFilter === 'all' ? 'types' as const : opts.symbolFilter,
    };
  }, [opts, displayGraph]);

  const rendered = useMemo(() => {
    if (!displayGraph?.nodes.length) return null;
    return buildMermaid(displayGraph, effectiveOpts,
      focusId && levels ? { rootId: focusId, levels } : null);
  }, [displayGraph, effectiveOpts, focusId, levels]);

  const impact = useMemo(() => {
    if (!focusGraph || !graph) return null;
    const files = focusGraph.nodes.filter(n => n.type === 'file');
    return {
      files: files.length,
      tests: files.filter(n => n.is_test).length,
      deps: focusGraph.nodes.filter(n => n.type === 'external').length,
      routes: focusGraph.nodes.filter(n => n.type === 'route').length,
      symbols: focusGraph.nodes.filter(n =>
        (CALLABLE_KINDS as readonly string[]).includes(n.type) ||
        (TYPE_KINDS as readonly string[]).includes(n.type)).length,
      maxRisk: files.reduce((m, n) => Math.max(m, n.risk || 0), 0),
    };
  }, [focusGraph, graph]);

  const chainByLevel = useMemo(() => {
    if (!focusGraph || !levels) return [];
    const grouped = new Map<number, GraphNode[]>();
    for (const node of focusGraph.nodes) {
      if (node.type !== 'file' && node.type !== 'external') continue;
      const level = levels.get(node.id) ?? 0;
      if (!grouped.has(level)) grouped.set(level, []);
      grouped.get(level)!.push(node);
    }
    return [...grouped.entries()].sort((a, b) => a[0] - b[0]);
  }, [focusGraph, levels]);

  const drawerNode = useMemo(
    () => graph?.nodes.find(n => n.id === drawerId) || null, [graph, drawerId],
  );
  const drawerEdges = useMemo(
    () => (drawerNode && graph
      ? graph.edges.filter(e => e.from === drawerNode.id || e.to === drawerNode.id) : []),
    [graph, drawerNode],
  );
  const drawerChildren = useMemo(
    () => (drawerNode && graph
      ? graph.nodes.filter(n => n.file === drawerNode.id && n.type !== 'route') : []),
    [graph, drawerNode],
  );

  const enterFocus = useCallback((id: string) => {
    setFocusId(id); setDrawerId(id); setFocusDepth(3);
    setOpts(cur => ({ ...cur, direction: 'LR', groupFolders: false }));
    setTab('impact');
  }, []);

  const focusByPath = useCallback((rel: string) => {
    const node = graph?.nodes.find(n => n.type === 'file' &&
      (`${n.sub}/${n.label}` === rel || n.label === rel || n.id.endsWith('/' + rel)));
    if (node) enterFocus(node.id);
    else toast(`File not in graph: ${rel}`, 'info');
  }, [graph, enterFocus, toast]);

  const handleNodeClick = useCallback((id: string) => {
    const node = graph?.nodes.find(n => n.id === id);
    if (!node) return;
    setDrawerId(id);
    if (node.type === 'file') enterFocus(id);
  }, [graph, enterFocus]);

  const set = <K extends keyof ViewOptions>(key: K, value: ViewOptions[K]) =>
    setOpts(cur => ({ ...cur, [key]: value }));

  const clearGraph = () => {
    prevGraph.current = graph;
    setGraph(null); setOverview(null); setFocusId(null); setDrawerId(null);
    toast('Graph cleared', 'info', () => setGraph(prevGraph.current), 'Undo');
  };

  const theme = THEMES[opts.theme];
  const meta = graph?.meta;
  const insights = graph?.insights;
  const focusRoot = graph?.nodes.find(n => n.id === focusId) || null;

  return (
    <div
      className={`app-shell ${collapsed ? 'collapsed' : ''} ${immersive ? 'immersive' : ''}`}
      style={{ ['--accent' as any]: theme.file[1], ['--accent-2' as any]: theme.line }}
    >
      <div className="bg-grid" aria-hidden />
      <div className="bg-glow glow-a" aria-hidden />
      <div className="bg-glow glow-b" aria-hidden />

      <header className="topbar">
        <button className="hamburger" onClick={() => setSideOpen(o => !o)} aria-label="Toggle controls">
          <span /><span /><span />
        </button>

        <div className="brand">
          <svg className="brand-mark" viewBox="0 0 32 32" width="26" height="26" aria-hidden>
            <circle cx="7" cy="8" r="3.4" fill="var(--accent)" />
            <circle cx="25" cy="6" r="2.6" fill="#fbbf24" />
            <circle cx="24" cy="24" r="3.4" fill="#34d399" />
            <circle cx="9" cy="25" r="2.4" fill="#f472b6" />
            <path d="M9 10l13 12M10 8h12M8 11v11M22 9v12" stroke="var(--accent-2)" strokeWidth="1.3" opacity=".7" />
          </svg>
          <span className="brand-name">CodeSight</span>
          <span className="brand-tag">repo observatory</span>
        </div>

        <form className="url-form" onSubmit={e => { e.preventDefault(); analyze(url); }}>
          <input
            ref={inputRef}
            className="url-input"
            placeholder="https://github.com/owner/repo  —  press / to focus"
            value={url}
            onChange={e => setUrl(e.target.value)}
            spellCheck={false}
          />
          <button className="btn primary" type="submit" disabled={busy}>
            {busy ? 'Analyzing…' : 'Analyze'}
          </button>
        </form>

        <button className="icon-btn collapse-btn" onClick={() => setCollapsed(v => !v)}
          title={collapsed ? 'Show panels' : 'Hide panels'}>
          {collapsed ? '›' : '‹'}
        </button>

        <div className={`status-pill phase-${status.phase}`}>
          <span className="dot" />
          {status.phase === 'idle' && 'Idle'}
          {status.phase === 'cloning' && 'Preparing'}
          {status.phase === 'scanning' && `Scanning ${status.scanned}`}
          {status.phase === 'ready' && 'Live'}
          {status.phase === 'error' && 'Error'}
        </div>
      </header>

      {collapsed && !immersive && (
        <button className="side-reopen" onClick={() => setCollapsed(false)}>Panels ›</button>
      )}
      {immersive && (
        <button className="immersive-exit" onClick={() => setImmersive(false)}>Exit wide view</button>
      )}

      <div className="layout">
        {sideOpen && <div className="side-overlay" onClick={() => setSideOpen(false)} />}

        <aside className={`sidebar ${sideOpen ? 'open' : ''}`}>
          <section className="card repo-card">
            <h3>Repository</h3>
            <div className="repo-name">{meta?.repo || status.repo || 'No repository analyzed'}</div>
            <div className="meta-grid">
              <div className="meta-item"><b>{meta?.files ?? 0}</b><span>files</span></div>
              <div className="meta-item"><b>{meta?.types ?? 0}</b><span>types</span></div>
              <div className="meta-item"><b>{meta?.functions ?? 0}</b><span>funcs</span></div>
              <div className="meta-item"><b>{meta?.externals ?? 0}</b><span>deps</span></div>
            </div>
            {busy && <div className="scan-bar"><span /></div>}
          </section>

          <div className="tabs">
            {(['overview', 'view', 'impact', 'insights'] as Tab[]).map(t => (
              <button key={t} className={`tab ${tab === t ? 'active' : ''}`} onClick={() => setTab(t)}>
                {t}
              </button>
            ))}
          </div>

          {tab === 'overview' && (
            <>
              <section className="card">
                <h3>Local folder</h3>
                <form className="inline-form" onSubmit={e => {
                  e.preventDefault();
                  if (localPath.trim()) post('/analyze/local', { path: localPath.trim() }, localPath.trim());
                }}>
                  <input className="text-input" placeholder="/path/to/project"
                    value={localPath} onChange={e => setLocalPath(e.target.value)} spellCheck={false} />
                  <button className="btn ghost" type="submit" disabled={busy}>Scan</button>
                </form>
              </section>

              {overview ? (
                <>
                  <section className="card">
                    <h3>Languages</h3>
                    <div className="bars">
                      {overview.languages.map(l => (
                        <div key={l.name} className="bar-row">
                          <span className="bar-label">{l.name}</span>
                          <div className="bar-track">
                            <div className="bar-fill" style={{ width: `${Math.max(2, l.percent)}%` }} />
                          </div>
                          <span className="bar-value">{l.percent}%</span>
                        </div>
                      ))}
                    </div>
                    <div className="kv-row">
                      <span>{overview.loc.toLocaleString()} LOC</span>
                      <span>{overview.files} files</span>
                      <span>{overview.tests.files} tests</span>
                    </div>
                  </section>

                  <section className="card">
                    <h3>Stack</h3>
                    <div className="chips">
                      {overview.frameworks.length === 0 && <span className="dim">No frameworks detected</span>}
                      {overview.frameworks.map(f => (
                        <span key={f.name} className={`chip static fw-${f.kind}`}>{f.name}</span>
                      ))}
                    </div>
                    <div className="kv-row">
                      <span>{overview.package_managers.join(', ') || 'no manifest'}</span>
                    </div>
                    {overview.monorepo.detected && (
                      <p className="hint">Monorepo: {overview.monorepo.tool}</p>
                    )}
                  </section>

                  <section className="card">
                    <h3>Entry points</h3>
                    <div className="fn-list">
                      {overview.entry_points.length === 0 && <span className="dim">None detected</span>}
                      {overview.entry_points.map(p => (
                        <button key={p} onClick={() => focusByPath(p)}>{p}<span>open</span></button>
                      ))}
                    </div>
                  </section>

                  <section className="card">
                    <h3>Commands</h3>
                    {Object.entries(overview.commands).map(([kind, cmds]) =>
                      cmds.length ? (
                        <div key={kind} className="cmd-group">
                          <span className="cmd-kind">{kind}</span>
                          {cmds.map(c => <code key={c}>{c}</code>)}
                        </div>
                      ) : null,
                    )}
                  </section>

                  <section className="card">
                    <h3>Health</h3>
                    <div className="kv"><span>license</span><code>{overview.license || 'none'}</code></div>
                    <div className="kv"><span>CI</span><code>{overview.ci.length} workflows</code></div>
                    <div className="kv"><span>README</span><code>{overview.docs.readme ? 'yes' : 'no'}</code></div>
                    <div className="kv"><span>docs dir</span><code>{overview.docs.docs_dir ? 'yes' : 'no'}</code></div>
                    <div className="kv"><span>deps</span><code>{overview.dependencies.direct} + {overview.dependencies.dev} dev</code></div>
                  </section>
                </>
              ) : (
                <section className="card">
                  <h3>Overview</h3>
                  <p className="dim">Analyze a repository to see languages, frameworks, entry points and commands.</p>
                  <div className="chips">
                    {SAMPLES.map(s => (
                      <button key={s} className="chip" onClick={() => { setUrl(s); analyze(s); }}>{s}</button>
                    ))}
                  </div>
                </section>
              )}
            </>
          )}

          {tab === 'view' && (
            <section className="card">
              <h3>Graph controls</h3>

              <div className="control-row">
                <span>Symbols</span>
                <select value={opts.symbolFilter}
                  onChange={e => set('symbolFilter', e.target.value as ViewOptions['symbolFilter'])}>
                  <option value="none">Files only</option>
                  <option value="types">+ Types</option>
                  <option value="all">+ Methods</option>
                </select>
              </div>

              <div className="control-row">
                <span>Color by</span>
                <select value={opts.colorMode}
                  onChange={e => set('colorMode', e.target.value as ViewOptions['colorMode'])}>
                  <option value="type">Node type</option>
                  <option value="risk">Risk heatmap</option>
                </select>
              </div>

              <label className="control-row switch-row">
                <span>External deps</span>
                <input type="checkbox" checked={opts.showExternals}
                  onChange={e => set('showExternals', e.target.checked)} />
                <i className="switch" />
              </label>

              <label className="control-row switch-row">
                <span>Folder groups</span>
                <input type="checkbox" checked={opts.groupFolders}
                  onChange={e => set('groupFolders', e.target.checked)} />
                <i className="switch" />
              </label>

              <label className="control-row switch-row">
                <span>Edge labels</span>
                <input type="checkbox" checked={opts.edgeLabels}
                  onChange={e => set('edgeLabels', e.target.checked)} />
                <i className="switch" />
              </label>

              <div className="control-row">
                <span>Layout</span>
                <select value={opts.direction}
                  onChange={e => set('direction', e.target.value as 'TD' | 'LR')}>
                  <option value="TD">Top → down</option>
                  <option value="LR">Left → right</option>
                </select>
              </div>

              <div className="control-row">
                <span>Palette</span>
                <select value={opts.theme}
                  onChange={e => set('theme', e.target.value as ViewOptions['theme'])}>
                  <option value="ink">Deep ink</option>
                  <option value="ember">Ember</option>
                  <option value="forest">Forest</option>
                </select>
              </div>

              <div className="control-row range-row">
                <span>Font {opts.fontSize}px</span>
                <input type="range" min={10} max={22} value={opts.fontSize}
                  onChange={e => set('fontSize', +e.target.value)} />
              </div>
              <div className="control-row range-row">
                <span>Node gap {opts.nodeSpacing}</span>
                <input type="range" min={10} max={90} value={opts.nodeSpacing}
                  onChange={e => set('nodeSpacing', +e.target.value)} />
              </div>
              <div className="control-row range-row">
                <span>Rank gap {opts.rankSpacing}</span>
                <input type="range" min={20} max={140} value={opts.rankSpacing}
                  onChange={e => set('rankSpacing', +e.target.value)} />
              </div>

              <div className="btn-row">
                <button className="btn ghost" onClick={() => setOpts(DEFAULT_OPTIONS)}>Reset</button>
                <button className="btn ghost danger" onClick={clearGraph} disabled={!graph}>Clear</button>
              </div>

              <div className="legend">
                <span className="legend-item"><i style={{ background: theme.file[1] }} />file</span>
                <span className="legend-item"><i style={{ background: theme.sym[1] }} />type</span>
                <span className="legend-item"><i style={{ background: theme.fn[1] }} />callable</span>
                <span className="legend-item"><i style={{ background: theme.ext[1] }} />dependency</span>
              </div>
            </section>
          )}

          {tab === 'impact' && (
            <section className="card">
              <h3>Impact analysis</h3>
              {!focusRoot ? (
                <p className="dim">Click any file node to trace what it depends on and what depends on it.</p>
              ) : (
                <>
                  <div className="repo-name">{focusRoot.label}</div>
                  <div className="seg">
                    {(['down', 'up', 'both'] as FocusDirection[]).map(d => (
                      <button key={d} className={`seg-btn ${direction === d ? 'active' : ''}`}
                        onClick={() => setDirection(d)}>
                        {d === 'down' ? 'Depends on' : d === 'up' ? 'Used by' : 'Both'}
                      </button>
                    ))}
                  </div>

                  <div className="control-row range-row">
                    <span>Depth {focusDepth >= 10 ? 'All' : focusDepth} / max {maxLevel}</span>
                    <input type="range" min={1} max={10} value={focusDepth}
                      disabled={maxLevel === 0}
                      onChange={e => setFocusDepth(+e.target.value)} />
                  </div>

                  {impact && (
                    <div className="impact-grid">
                      <div><b>{impact.files}</b><span>files</span></div>
                      <div><b>{impact.symbols}</b><span>symbols</span></div>
                      <div><b>{impact.deps}</b><span>packages</span></div>
                      <div><b>{impact.tests}</b><span>tests</span></div>
                    </div>
                  )}

                  {impact && (
                    <div className={`risk-badge risk-${riskBucket(impact.maxRisk)}`}>
                      blast radius risk {Math.round(impact.maxRisk * 100)}%
                    </div>
                  )}

                  <div className="chain-list">
                    {chainByLevel.map(([level, nodes]) => (
                      <div key={level} className="chain-level">
                        <div className="chain-level-head">
                          <span>Level {level}</span><b>{nodes.length}</b>
                        </div>
                        <div className="fn-list">
                          {nodes.map(n => (
                            <button key={n.id} onClick={() => n.type === 'file' ? enterFocus(n.id) : setDrawerId(n.id)}>
                              {n.label}<span>{n.type}</span>
                            </button>
                          ))}
                        </div>
                      </div>
                    ))}
                  </div>

                  <button className="btn ghost" onClick={() => setFocusId(null)}>Full graph</button>
                </>
              )}
            </section>
          )}

          {tab === 'insights' && (
            <>
              <section className="card">
                <h3>Key files</h3>
                <div className="fn-list">
                  {insights?.key_files.length ? insights.key_files.map(f => (
                    <button key={f.id} onClick={() => enterFocus(f.id)}>
                      {f.is_entry ? '▶ ' : ''}{f.label}
                      <span>rank {f.score} · in {f.fan_in}</span>
                    </button>
                  )) : <span className="dim">No data yet</span>}
                </div>
              </section>

              <section className="card">
                <h3>Circular dependencies ({insights?.stats.cycles ?? 0})</h3>
                <div className="chain-list">
                  {insights?.cycles.length ? insights.cycles.map((group, i) => (
                    <div key={i} className="chain-level warn">
                      <div className="chain-level-head"><span>cycle {i + 1}</span><b>{group.length}</b></div>
                      <div className="fn-list">
                        {group.map(f => (
                          <button key={f.id} onClick={() => enterFocus(f.id)}>{f.label}</button>
                        ))}
                      </div>
                    </div>
                  )) : <span className="dim">No cycles detected</span>}
                </div>
              </section>

              <section className="card">
                <h3>Complexity hotspots</h3>
                <div className="fn-list">
                  {insights?.hotspots.length ? insights.hotspots.map(h => (
                    <button key={h.id} onClick={() => enterFocus(h.id)}>
                      {h.label}<span>cx {h.complexity} · in {h.fan_in}</span>
                    </button>
                  )) : <span className="dim">No data yet</span>}
                </div>
              </section>

              <section className="card">
                <h3>Orphan files ({insights?.stats.orphans ?? 0})</h3>
                <div className="fn-list">
                  {insights?.orphans.length ? insights.orphans.map(o => (
                    <button key={o.id} onClick={() => enterFocus(o.id)}>{o.label}</button>
                  )) : <span className="dim">None</span>}
                </div>
              </section>
            </>
          )}

          <section className="card">
            <h3>Activity</h3>
            <div className="log">
              {log.length === 0 && <div className="log-line dim">Waiting for events…</div>}
              {log.map((l, i) => (
                <div key={i} className={`log-line lp-${l.phase}`}><span>{l.t}</span>{l.msg}</div>
              ))}
            </div>
          </section>

          {recent.length > 0 && (
            <section className="card">
              <h3>Recent</h3>
              <div className="chips">
                {recent.map(r => (
                  <button key={r} className="chip" onClick={() => { setUrl(r); analyze(r); }}>{r}</button>
                ))}
              </div>
            </section>
          )}
        </aside>

        <main className="stage">
          {status.phase === 'error' && (
            <div className="error-banner">
              <b>Analysis failed.</b> {status.error}
              <button className="btn ghost" onClick={() => analyze(lastUrl.current || url)}>Retry</button>
            </div>
          )}

          {rendered ? (
            <>
              {busy && <div className="streaming-badge">streaming updates…</div>}
              <DiagramCanvas
                code={rendered.code}
                idMap={rendered.idMap}
                theme={opts.theme}
                fontSize={opts.fontSize}
                nodeSpacing={opts.nodeSpacing}
                rankSpacing={opts.rankSpacing}
                immersive={immersive}
                onToggleImmersive={() => setImmersive(v => !v)}
                onNodeClick={handleNodeClick}
                onToast={toast}
              />

              {focusId && focusRoot && (
                <div className="focus-bar" onPointerDown={e => e.stopPropagation()}>
                  <div className="focus-root">
                    <span className="focus-kicker">
                      {direction === 'down' ? 'depends on' : direction === 'up' ? 'used by' : 'both directions'}
                    </span>
                    <b>{focusRoot.label}</b>
                  </div>
                  <div className="seg small">
                    {(['down', 'up', 'both'] as FocusDirection[]).map(d => (
                      <button key={d} className={`seg-btn ${direction === d ? 'active' : ''}`}
                        onClick={() => setDirection(d)}>
                        {d === 'down' ? '↓' : d === 'up' ? '↑' : '↕'}
                      </button>
                    ))}
                  </div>
                  <div className="focus-depth">
                    <span>Depth</span>
                    <input type="range" min={1} max={10} value={focusDepth}
                      disabled={maxLevel === 0}
                      onChange={e => setFocusDepth(+e.target.value)} />
                    <b>{focusDepth >= 10 ? 'All' : focusDepth}</b>
                  </div>
                  {impact && (
                    <div className="focus-stats">
                      <span>{impact.files} files</span>
                      <span>{impact.deps} deps</span>
                      <span>{impact.tests} tests</span>
                    </div>
                  )}
                  <button className="btn ghost" onClick={() => setFocusId(null)}>Full graph</button>
                </div>
              )}
            </>
          ) : (
            <div className="empty-hero">
              {busy ? (
                <div className="loader">
                  <div className="loader-ring" />
                  <h2>{status.phase === 'cloning' ? 'Preparing repository' : 'Parsing with C++ engine'}</h2>
                  <p>{status.message}</p>
                </div>
              ) : (
                <>
                  <h1>Understand any codebase.</h1>
                  <p className="hero-sub">
                    Analyze a GitHub repository or local folder. CodeSight extracts symbols,
                    maps dependencies, ranks key files, detects cycles, and traces the blast
                    radius of any file you click.
                  </p>
                  <div className="samples">
                    {SAMPLES.map(s => (
                      <button key={s} className="sample-chip" onClick={() => { setUrl(s); analyze(s); }}>{s}</button>
                    ))}
                  </div>
                  <div className="cli-hint">
                    <span>or watch a local folder live</span>
                    <code>./build/codesight ./your-project</code>
                  </div>
                </>
              )}
            </div>
          )}
        </main>
      </div>

      {drawerNode && (
        <div className="drawer">
          <div className="drawer-head">
            <span className={`type-badge tb-${drawerNode.type}`}>{drawerNode.type}</span>
            <b>{drawerNode.label}</b>
            <button className="tool-btn" onClick={() => setDrawerId(null)}>✕</button>
          </div>
          <div className="drawer-body">
            {drawerNode.type === 'file' ? (
              <>
                <div className="kv"><span>path</span><code>{drawerNode.sub}/{drawerNode.label}</code></div>
                <div className="kv"><span>lines</span><code>{drawerNode.loc ?? 0}</code></div>
                <div className="kv"><span>fan in / out</span><code>{drawerNode.fan_in ?? 0} / {drawerNode.fan_out ?? 0}</code></div>
                <div className="kv"><span>complexity</span><code>{drawerNode.complexity ?? 0}</code></div>
                <div className="kv"><span>pagerank</span><code>{drawerNode.pagerank ?? 0}</code></div>
                <div className={`risk-badge risk-${riskBucket(drawerNode.risk)}`}>
                  risk {Math.round((drawerNode.risk || 0) * 100)}%
                </div>
                <button className="btn primary trace-btn" onClick={() => enterFocus(drawerNode.id)}>
                  Trace impact
                </button>
                {drawerChildren.length > 0 && (
                  <>
                    <h4>Symbols ({drawerChildren.length})</h4>
                    <div className="fn-list">
                      {drawerChildren.slice(0, 60).map(s => (
                        <button key={s.id} onClick={() => setDrawerId(s.id)}>
                          {s.exported ? '' : '· '}{s.label}
                          <span>{s.kind} :{s.line}</span>
                        </button>
                      ))}
                    </div>
                  </>
                )}
              </>
            ) : (
              <>
                {drawerNode.line != null && (
                  <div className="kv"><span>lines</span><code>{drawerNode.line}–{drawerNode.end_line}</code></div>
                )}
                {drawerNode.complexity != null && (
                  <div className="kv"><span>complexity</span><code>{drawerNode.complexity}</code></div>
                )}
                {drawerNode.exported != null && (
                  <div className="kv"><span>exported</span><code>{drawerNode.exported ? 'yes' : 'no'}</code></div>
                )}
                {drawerNode.file && (
                  <button className="btn ghost trace-btn" onClick={() => enterFocus(drawerNode.file!)}>
                    Trace parent file
                  </button>
                )}
              </>
            )}

            <h4>Connections ({drawerEdges.length})</h4>
            <div className="fn-list">
              {drawerEdges.slice(0, 40).map((e, i) => {
                const otherId = e.from === drawerNode.id ? e.to : e.from;
                const other = graph?.nodes.find(n => n.id === otherId);
                return (
                  <button key={i} onClick={() => other && setDrawerId(other.id)}>
                    <em>{e.type}</em>{other?.label || otherId}
                  </button>
                );
              })}
            </div>
          </div>
        </div>
      )}

      <div className="toasts">
        {toasts.map(t => (
          <div key={t.id} className={`toast toast-${t.kind}`}>
            {t.msg}
            {t.action && (
              <button className="toast-action" onClick={() => {
                t.action!(); setToasts(cur => cur.filter(x => x.id !== t.id));
              }}>{t.actionLabel}</button>
            )}
          </div>
        ))}
      </div>
    </div>
  );
}
