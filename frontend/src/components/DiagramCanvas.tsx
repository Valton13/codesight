import { useCallback, useEffect, useLayoutEffect, useRef, useState } from 'react';
import mermaid from 'mermaid';
import { THEMES, type ThemeKey } from '../lib/graph';

interface Props {
    code: string;
    idMap: Record<string, string>;
    theme: ThemeKey;
    fontSize: number;
    nodeSpacing: number;
    rankSpacing: number;
    onNodeClick: (originalId: string) => void;
    onToast: (msg: string, kind?: 'success' | 'error' | 'info') => void;
}

export default function DiagramCanvas({
    code, idMap, theme, fontSize, nodeSpacing, rankSpacing, onNodeClick, onToast,
}: Props) {
    const canvasRef = useRef<HTMLDivElement>(null);
    const innerRef = useRef<HTMLDivElement>(null);
    const [svg, setSvg] = useState('');
    const [renderError, setRenderError] = useState('');
    const [transform, setTransform] = useState({ x: 40, y: 40, scale: 1 });
    const drag = useRef({ active: false, moved: 0, px: 0, py: 0 });
    const queue = useRef<Promise<void>>(Promise.resolve());
    const configKey = `${theme}|${fontSize}|${nodeSpacing}|${rankSpacing}`;

    // Sequential render queue: never run two mermaid renders concurrently.
    useEffect(() => {
        let cancelled = false;
        queue.current = queue.current.then(async () => {
            if (cancelled || !code) return;
            const t = THEMES[theme];
            mermaid.initialize({
                startOnLoad: false,
                securityLevel: 'loose',
                theme: 'base',
                fontFamily: '"IBM Plex Mono", ui-monospace, monospace',
                themeVariables: {
                    background: t.bg,
                    primaryColor: t.file[0],
                    primaryTextColor: '#e6edf7',
                    primaryBorderColor: t.file[1],
                    lineColor: t.line,
                    fontSize: `${fontSize}px`,
                    clusterBkg: 'rgba(255,255,255,0.03)',
                    clusterBorder: 'rgba(148,163,184,0.25)',
                    edgeLabelBackground: t.bg,
                },
                flowchart: {
                    useMaxWidth: false,
                    htmlLabels: true,
                    curve: 'basis',
                    nodeSpacing,
                    rankSpacing,
                },
            });
            try {
                const { svg: out } = await mermaid.render(`cs-${Date.now()}`, code);
                if (!cancelled) { setSvg(out); setRenderError(''); }
            } catch (e: any) {
                if (!cancelled) setRenderError(e?.message || 'Diagram render failed');
            }
        });
        return () => { cancelled = true; };
    }, [code, configKey, theme, fontSize, nodeSpacing, rankSpacing]);

    const fit = useCallback(() => {
        const canvas = canvasRef.current;
        const svgEl = innerRef.current?.querySelector('svg');
        if (!canvas || !svgEl) return;
        const vb = (svgEl as SVGSVGElement).viewBox?.baseVal;
        const w = vb?.width || svgEl.getBoundingClientRect().width || 800;
        const h = vb?.height || svgEl.getBoundingClientRect().height || 600;
        const cw = canvas.clientWidth, ch = canvas.clientHeight;
        const scale = Math.max(0.15, Math.min((cw - 90) / w, (ch - 90) / h, 1.5));
        setTransform({ x: (cw - w * scale) / 2, y: (ch - h * scale) / 2, scale });
    }, []);

    useLayoutEffect(() => { if (svg) fit(); }, [svg, fit]);

    // Wheel zoom around the cursor (non-passive so preventDefault works).
    useEffect(() => {
        const el = canvasRef.current;
        if (!el) return;
        const onWheel = (e: WheelEvent) => {
            e.preventDefault();
            const rect = el.getBoundingClientRect();
            const mx = e.clientX - rect.left, my = e.clientY - rect.top;
            setTransform(tr => {
                const ns = Math.max(0.15, Math.min(4, tr.scale * Math.exp(-e.deltaY * 0.0012)));
                return { scale: ns, x: mx - (mx - tr.x) * (ns / tr.scale), y: my - (my - tr.y) * (ns / tr.scale) };
            });
        };
        el.addEventListener('wheel', onWheel, { passive: false });
        return () => el.removeEventListener('wheel', onWheel);
    }, []);

    // Node click delegation: map mermaid's generated ids back to graph ids.
    useEffect(() => {
        const root = innerRef.current;
        if (!root || !svg) return;
        const reverse: Record<string, string> = {};
        Object.entries(idMap).forEach(([orig, short]) => { reverse[short] = orig; });
        const handler = (e: Event) => {
            if (drag.current.moved > 5) return; // ignore clicks that were pans
            const g = (e.target as Element).closest('g.node');
            if (!g) return;
            const m = (g.getAttribute('id') || '').match(/^flowchart-(n\d+)-\d+$/);
            if (m && reverse[m[1]]) onNodeClick(reverse[m[1]]);
        };
        root.addEventListener('click', handler);
        return () => root.removeEventListener('click', handler);
    }, [svg, idMap, onNodeClick]);

    const onPointerDown = (e: React.PointerEvent) => {
        drag.current = { active: true, moved: 0, px: e.clientX, py: e.clientY };
        (e.currentTarget as Element).setPointerCapture(e.pointerId);
    };
    const onPointerMove = (e: React.PointerEvent) => {
        if (!drag.current.active) return;
        const dx = e.clientX - drag.current.px, dy = e.clientY - drag.current.py;
        drag.current.moved += Math.abs(dx) + Math.abs(dy);
        drag.current.px = e.clientX; drag.current.py = e.clientY;
        setTransform(tr => ({ ...tr, x: tr.x + dx, y: tr.y + dy }));
    };
    const onPointerUp = () => { drag.current.active = false; };

    const zoomBy = (f: number) => {
        const el = canvasRef.current;
        if (!el) return;
        const mx = el.clientWidth / 2, my = el.clientHeight / 2;
        setTransform(tr => {
            const ns = Math.max(0.15, Math.min(4, tr.scale * f));
            return { scale: ns, x: mx - (mx - tr.x) * (ns / tr.scale), y: my - (my - tr.y) * (ns / tr.scale) };
        });
    };

    const currentSvg = () => innerRef.current?.querySelector('svg') as SVGSVGElement | null;

    const exportSVG = () => {
        const el = currentSvg();
        if (!el) return;
        const data = new XMLSerializer().serializeToString(el);
        const url = URL.createObjectURL(new Blob([data], { type: 'image/svg+xml;charset=utf-8' }));
        const a = document.createElement('a');
        a.href = url; a.download = `codesight-${Date.now()}.svg`; a.click();
        URL.revokeObjectURL(url);
        onToast('SVG exported', 'success');
    };

    const exportPNG = () => {
        const el = currentSvg();
        if (!el) return;
        const data = new XMLSerializer().serializeToString(el);
        const url = URL.createObjectURL(new Blob([data], { type: 'image/svg+xml;charset=utf-8' }));
        const img = new Image();
        img.onload = () => {
            const vb = el.viewBox?.baseVal;
            const w = (vb?.width || img.width) * 2, h = (vb?.height || img.height) * 2;
            const canvas = document.createElement('canvas');
            canvas.width = w; canvas.height = h;
            const ctx = canvas.getContext('2d');
            if (ctx) {
                ctx.fillStyle = THEMES[theme].bg;
                ctx.fillRect(0, 0, w, h);
                ctx.drawImage(img, 0, 0, w, h);
                const a = document.createElement('a');
                a.href = canvas.toDataURL('image/png');
                a.download = `codesight-${Date.now()}.png`; a.click();
                onToast('PNG exported', 'success');
            }
            URL.revokeObjectURL(url);
        };
        img.onerror = () => { URL.revokeObjectURL(url); onToast('PNG export failed', 'error'); };
        img.src = url;
    };

    const copyCode = async () => {
        try {
            await navigator.clipboard.writeText(code);
            onToast('Mermaid source copied', 'success');
        } catch {
            onToast('Clipboard unavailable', 'error');
        }
    };

    if (renderError) {
        return <div className="canvas-message error">Render error: {renderError}</div>;
    }

    return (
        <div
            ref={canvasRef}
            className="canvas"
            onPointerDown={onPointerDown}
            onPointerMove={onPointerMove}
            onPointerUp={onPointerUp}
            onPointerCancel={onPointerUp}
        >
            <div className="canvas-tools" onPointerDown={e => e.stopPropagation()}>
                <button className="tool-btn" title="Zoom in" onClick={() => zoomBy(1.25)}>+</button>
                <button className="tool-btn" title="Zoom out" onClick={() => zoomBy(0.8)}>−</button>
                <button className="tool-btn" title="Fit to view" onClick={fit}>Fit</button>
                <span className="zoom-label">{Math.round(transform.scale * 100)}%</span>
                <span className="tool-sep" />
                <button className="tool-btn" onClick={exportSVG}>SVG</button>
                <button className="tool-btn" onClick={exportPNG}>PNG</button>
                <button className="tool-btn" onClick={copyCode}>Copy</button>
            </div>
            <div
                ref={innerRef}
                className="canvas-inner diagram-svg"
                style={{ transform: `translate(${transform.x}px, ${transform.y}px) scale(${transform.scale})` }}
                dangerouslySetInnerHTML={{ __html: svg }}
            />
            {!svg && <div className="canvas-message">Rendering diagram…</div>}
        </div>
    );
}
