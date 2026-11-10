import { useEffect, useState, useRef } from 'react';
import MermaidChart from './components/MermaidChart';

interface GraphState {
  nodes: Array<{ id: string; label: string; type: string }>;
  edges: Array<{ from: string; to: string; type: string }>;
  mermaid_diagram: string;
}

function App() {
  const [graphState, setGraphState] = useState<GraphState | null>(null);
  const [wsStatus, setWsStatus] = useState<'connecting' | 'connected' | 'disconnected'>('connecting');
  const wsRef = useRef<WebSocket | null>(null);

  useEffect(() => {
    const connectWebSocket = () => {
      setWsStatus('connecting');
      const ws = new WebSocket('ws://127.0.0.1:8000/ws');
      wsRef.current = ws;

      ws.onopen = () => {
        setWsStatus('connected');
      };

      ws.onmessage = (event) => {
        try {
          const data = JSON.parse(event.data);
          setGraphState(data);
        } catch (err) {
          console.error('Failed to parse WebSocket message:', err);
        }
      };

      ws.onclose = () => {
        setWsStatus('disconnected');
        // Attempt to reconnect after 3 seconds
        setTimeout(connectWebSocket, 3000);
      };

      ws.onerror = (error) => {
        console.error('WebSocket error:', error);
        ws.close();
      };
    };

    connectWebSocket();

    return () => {
      if (wsRef.current) {
        wsRef.current.close();
      }
    };
  }, []);

  // Calculate basic stats
  const fileCount = graphState?.nodes.filter(n => n.type === 'file').length || 0;
  const functionCount = graphState?.nodes.filter(n => n.type === 'function').length || 0;
  const importCount = graphState?.nodes.filter(n => n.type === 'import').length || 0;

  return (
    <div className="app-container">
      <header className="header">
        <h1>CodeSight</h1>
        <div className={`status-badge ${wsStatus}`}>
          {wsStatus === 'connected' ? '● Live' : '○ Disconnected'}
        </div>
      </header>

      <main className="main-content">
        <aside className="sidebar">
          <div className="stats-card">
            <h3>Project Statistics</h3>
            <div className="stat-item">
              <span className="stat-label">Files</span>
              <span className="stat-value">{fileCount}</span>
            </div>
            <div className="stat-item">
              <span className="stat-label">Functions</span>
              <span className="stat-value">{functionCount}</span>
            </div>
            <div className="stat-item">
              <span className="stat-label">Imports</span>
              <span className="stat-value">{importCount}</span>
            </div>
          </div>

          <div className="info-card">
            <h3>How it works</h3>
            <p>Modify any <code>.py</code> or <code>.js</code> file in the watched directory. The C++ parser will extract the structure and update this diagram in real-time.</p>
          </div>
        </aside>

        <section className="diagram-area">
          {wsStatus === 'connecting' && <div className="loading">Connecting to backend...</div>}
          {wsStatus === 'disconnected' && <div className="loading">Reconnecting...</div>}

          {graphState && graphState.mermaid_diagram && (
            <MermaidChart chart={graphState.mermaid_diagram} />
          )}

          {graphState && !graphState.mermaid_diagram && (
            <div className="empty-state">
              No code structure detected yet. Try modifying a file in the watched directory.
            </div>
          )}
        </section>
      </main>
    </div>
  );
}

export default App;
