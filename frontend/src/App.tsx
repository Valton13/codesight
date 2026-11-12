import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import DiagramCanvas from './components/DiagramCanvas';
import {
  buildMermaid, DEFAULT_OPTIONS, THEMES,
  type AnalysisStatus, type GraphState, type ViewOptions,
} from './lib/graph';

const API = 'http://127.0.0.1:8000';
const WS = 'ws://127.0.0.1:8000/ws';

const SAMPLES = [
  'Valton13/codesight',
  'fastapi/fastapi',
  'pallets/flask',
  'expressjs/express',
];

interface Toast { id: number; msg: string; kind: 'success' | 'error' | 'info'; action?: () => void; actionLabel?: string }
interface LogLine { t: string; msg: string; phase: string }

const IDLE: AnalysisStatus = {
  phase: 'idle', message: 'Paste a GitHub URL or watch a local folder.',
  repo: null, scanned: 0, error: null,
};

export default function App() {
  const [graph, setGraph] = useState<GraphState | null>(null);
  const [status, setStatus] = useState<AnalysisStatus>(IDLE);
  const [opts, setOpts] = useState<ViewOptions>(DEFAULT_OPTIONS);
  const [url, setUrl] = useState('');
  const [log, setLog] = useState<LogLine[]>([]);
  const [toasts, setToasts] = useState<Toast[]>([]);
  const [drawerId, setDrawerId] = useState<string | null>(null);
  const [sideOpen, setSideOpen] = useState(false);
  const [recent, setRecent] = useState<string[]>(() => {
    try { return JSON.parse(localStorage.getItem('cs-recent') || '[]'); } catch { return []; }
  });

  const inputRef = useRef<HTMLInputElement>(null);
  const prevGraph = useRef<GraphState | null>(null);
  const lastUrl = useRef('');
  const toastId = useRef(0);

  const toast = useCallback((msg: string, kind: Toast['kind'] = 'info', action?: () => void, actionLabel?: string) => {
    const id = ++toastId.current;
    setToasts(ts => [...ts, { id, msg, kind, action, actionLabel }]);
    setTimeout(() => setToasts(ts => ts.filter(t => t.id !== id)), 5000);
  }, []);

  // WebSocket lifecycle with guarded reconnect.
  useEffect(() => {
    let alive = true;
    let ws: WebSocket | null = null;
    let timer: number | undefined;
    const connect = () => {
      if (!alive) return;
      ws = new WebSocket(WS);
      ws.onmessage = ev => {
        try {
          const msg = JSON.parse(ev.data);
          if (msg.type === 'graph') {
            setGraph({ nodes: msg.nodes, edges: msg.edges, meta: msg.meta });
          } else if (msg.type === 'status') {
            setStatus(msg);
            setLog(l => {
              const line = { t: new Date().toLocaleTimeString(), msg: msg.message, phase: msg.phase };
              if (l[0]?.msg === line.msg) return l;
              return [line, ...l].slice(0, 40);
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

  const analyze = useCallback(async (target: string) => {
    const value = target.trim();
    if (!value) { toast('Enter a GitHub URL first', 'error'); inputRef.current?.focus(); return; }
    lastUrl.current = value;
    setStatus(s => ({ ...s, phase: 'cloning', message: `Contacting GitHub for ${value}…`, error: null }));
    try {
      const res = await fetch(`${API}/analyze/github`, {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ url: value }),
      });
      const data = await res.json().catch(() => ({}));
      if (!res.ok) {
        setStatus(s => ({ ...s, phase: 'error', error: data.detail || `HTTP ${res.status}` }));
        toast(data.detail || `Request failed (${res.status})`, 'error');
        return;
      }
      setRecent(r => {
        const next = [data.repo, ...r.filter(x => x !== data.repo)].slice(0, 6);
        localStorage.setItem('cs-recent', JSON.stringify(next));
        return next;
      });
      setSideOpen(false);
    } catch {
      setStatus(s => ({ ...s, phase: 'error', error: 'Backend unreachable. Is server.py running?' }));
      toast('Backend unreachable on port 8000', 'error');
    }
  }, [toast]);

  // Keyboard shortcuts.
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') setDrawerId(null);
      if (e.key === '/' && document.activeElement !== inputRef.current) {
        e.preventDefault(); inputRef.current?.focus();
      }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, []);

  const busy = status.phase === 'cloning' || status.phase === 'scanning';
  const effectiveOpts = useMemo(() => {
    const tooBig = (graph?.nodes.length || 0) > 260;
    return { ...opts, showFunctions: opts.showFunctions && !tooBig };
  }, [opts, graph]);

  const rendered = useMemo(
    () => (graph && graph.nodes.length ? buildMermaid(graph, effectiveOpts) : null),
    [graph, effectiveOpts],
  );

  const drawerNode = useMemo(
    () => graph?.nodes.find(n => n.id === drawerId) || null,
    [graph, drawerId],
  );
  const drawerFunctions = useMemo(
    () => (drawerNode?.type === 'file'
      ? graph!.nodes.filter(n => n.type === 'function' && n.file === drawerNode.id)
      : []),
    [graph, drawerNode],
  );
  const drawerEdges = useMemo(
    () => (drawerNode ? graph!.edges.filter(e => e.from === drawerNode.id || e.to === drawerNode.id) : []),
    [graph, drawerNode],
  );

  const set = <K extends keyof ViewOptions>(k: K, v: ViewOptions[K]) =>
    setOpts(o => ({ ...o, [k]: v }));

  const clearGraph = () => {
    prevGraph.current = graph;
    setGraph(null);
    toast('Graph cleared', 'info', () => setGraph(prevGraph.current), 'Undo');
  };

  const theme = THEMES[opts.theme];
  const meta = graph?.meta;

  return (
    <div className="app-shell" style={{ ['--accent' as any]: theme.file[1], ['--accent-2' as any]: theme.line }}>
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
          <svg viewBox="0 0 16 16" width="15" height="15" className="gh-icon" aria-hidden>
            <path fill="currentColor" d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27s1.36.09 2 .27c1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.01 8.01 0 0 0 16 8c0-4.42-3.58-8-8-8Z" />
          </svg>
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

        <div className={`status-pill phase-${status.phase}`}>
          <span className="dot" />
          {status.phase === 'idle' && 'Idle'}
          {status.phase === 'cloning' && 'Cloning'}
          {status.phase === 'scanning' && `Scanning ${status.scanned}`}
          {status.phase === 'ready' && 'Live'}
          {status.phase === 'error' && 'Error'}
        </div>
      </header>

      <div className="layout">
        {sideOpen && <div className="side-overlay" onClick={() => setSideOpen(false)} />}
        <aside className={`sidebar ${sideOpen ? 'open' : ''}`}>
          <section className="card repo-card">
            <h3>Repository</h3>
            <div className="repo-name">{meta?.repo || status.repo || 'No repository analyzed'}</div>
            <div className="meta-grid">
              <div className="meta-item"><b>{meta?.files ?? 0}</b><span>files</span></div>
              <div className="meta-item"><b>{meta?.functions ?? 0}</b><span>functions</span></div>
              <div className="meta-item"><b>{meta?.externals ?? 0}</b><span>deps</span></div>
              <div className="meta-item"><b>{graph?.edges.length ?? 0}</b><span>edges</span></div>
            </div>
            <div className="pipeline">
              {(['cloning', 'scanning', 'ready'] as const).map((p, i) => {
                const order = { idle: -1, cloning: 0, scanning: 1, ready: 2, error: -1 }[status.phase];
                const cls = order > i ? 'done' : order === i ? 'active' : '';
                return (
                  <div key={p} className={`pipe-step ${cls}`}>
                    <i />{['clone', 'parse', 'graph'][i]}
                  </div>
                );
              })}
            </div>
            {busy && <div className="scan-bar"><span /></div>}
          </section>

          <section className="card">
            <h3>View controls</h3>
            <label className="control-row switch-row">
              <span>Function nodes</span>
              <input type="checkbox" checked={opts.showFunctions}
                onChange={e => set('showFunctions', e.target.checked)} />
              <i className="switch" />
            </label>
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
              <select value={opts.direction} onChange={e => set('direction', e.target.value as 'TD' | 'LR')}>
                <option value="TD">Top → down</option>
                <option value="LR">Left → right</option>
              </select>
            </div>
            <div className="control-row">
              <span>Palette</span>
              <select value={opts.theme} onChange={e => set('theme', e.target.value as ViewOptions['theme'])}>
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
              <button className="btn ghost" onClick={() => setOpts(DEFAULT_OPTIONS)}>Reset view</button>
              <button className="btn ghost danger" onClick={clearGraph} disabled={!graph}>Clear</button>
            </div>
            {(graph?.nodes.length || 0) > 260 && opts.showFunctions && (
              <p className="hint">Function nodes auto-hidden above 260 nodes to keep the graph readable.</p>
            )}
          </section>

          <section className="card">
            <h3>Legend</h3>
            <div className="legend">
              <span className="legend-item"><i style={{ background: theme.file[1] }} />file</span>
              <span className="legend-item"><i style={{ background: theme.fn[1] }} />function</span>
              <span className="legend-item"><i style={{ background: theme.ext[1] }} />dependency</span>
              <span className="legend-item"><i style={{ background: theme.route[1] }} />api route</span>
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

          <section className="card">
            <h3>Activity</h3>
            <div className="log">
              {log.length === 0 && <div className="log-line dim">Waiting for events…</div>}
              {log.map((l, i) => (
                <div key={i} className={`log-line lp-${l.phase}`}>
                  <span>{l.t}</span>{l.msg}
                </div>
              ))}
            </div>
          </section>
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
                onNodeClick={setDrawerId}
                onToast={toast}
              />
            </>
          ) : (
            <div className="empty-hero">
              {busy ? (
                <div className="loader">
                  <div className="loader-ring" />
                  <h2>{status.phase === 'cloning' ? 'Cloning repository' : 'Parsing with C++ engine'}</h2>
                  <p>{status.message}</p>
                  <div className="loader-pipeline">
                    <span className={status.phase === 'cloning' ? 'on' : 'done'}>git clone</span>
                    <em>→</em>
                    <span className={status.phase === 'scanning' ? 'on' : ''}>tree-sitter</span>
                    <em>→</em>
                    <span>gRPC</span>
                    <em>→</em>
                    <span>graph</span>
                  </div>
                </div>
              ) : (
                <>
                  <h1>See any repository's architecture.</h1>
                  <p className="hero-sub">
                    Paste a GitHub URL above. CodeSight clones it, parses every file with a
                    C++ tree-sitter engine, and streams a live dependency graph to this canvas.
                  </p>
                  <div className="samples">
                    {SAMPLES.map(s => (
                      <button key={s} className="sample-chip" onClick={() => { setUrl(s); analyze(s); }}>
                        {s}
                      </button>
                    ))}
                  </div>
                  <div className="cli-hint">
                    <span>or watch a local folder</span>
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
            {drawerNode.sub && <div className="kv"><span>path</span><code>{drawerNode.sub}/{drawerNode.label}</code></div>}
            {drawerNode.line != null && <div className="kv"><span>line</span><code>{drawerNode.line}</code></div>}
            {drawerNode.type === 'file' && (
              <>
                <div className="kv"><span>functions</span><code>{drawerFunctions.length}</code></div>
                {drawerFunctions.length > 0 && (
                  <div className="fn-list">
                    {drawerFunctions.map(f => (
                      <button key={f.id} onClick={() => setDrawerId(f.id)}>
                        {f.label}<span>:{f.line}</span>
                      </button>
                    ))}
                  </div>
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
            {t.action && <button className="toast-action" onClick={() => { t.action!(); setToasts(ts => ts.filter(x => x.id !== t.id)); }}>{t.actionLabel}</button>}
          </div>
        ))}
      </div>
    </div>
  );
}
