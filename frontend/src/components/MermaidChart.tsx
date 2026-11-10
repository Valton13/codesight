import { useEffect, useState } from 'react';
import mermaid from 'mermaid';

mermaid.initialize({
    startOnLoad: false,
    theme: 'dark',
    securityLevel: 'loose',
    flowchart: {
        useMaxWidth: true,
        htmlLabels: true,
        curve: 'basis'
    }
});

interface MermaidChartProps {
    chart: string;
}

export default function MermaidChart({ chart }: MermaidChartProps) {
    const [svgContent, setSvgContent] = useState<string>('');
    const [error, setError] = useState<string>('');

    useEffect(() => {
        const renderChart = async () => {
            if (!chart) {
                console.log('Mermaid: No chart data provided');
                return;
            }

            console.log('Mermaid: Rendering chart, length:', chart.length);

            try {
                setError('');
                const id = `mermaid-${Date.now()}`;
                const { svg } = await mermaid.render(id, chart);
                console.log('Mermaid: SVG generated successfully, length:', svg.length);
                setSvgContent(svg);
            } catch (err: any) {
                console.error('Mermaid rendering error:', err);
                setError(`Failed to render: ${err.message || 'Unknown error'}`);
            }
        };

        renderChart();
    }, [chart]);

    if (error) {
        return (
            <div className="error-message">
                <strong>Mermaid Error:</strong> {error}
                <br /><br />
                <details>
                    <summary>Click to see the raw Mermaid string</summary>
                    <pre style={{ textAlign: 'left', fontSize: '0.8rem', marginTop: '10px' }}>
                        {chart}
                    </pre>
                </details>
            </div>
        );
    }

    if (!svgContent && chart) {
        return <div className="loading">Generating diagram...</div>;
    }

    if (!svgContent && !chart) {
        return <div className="empty-state">No diagram data yet. Modify a file to see the architecture.</div>;
    }

    return (
        <div
            className="mermaid-container"
            dangerouslySetInnerHTML={{ __html: svgContent }}
        />
    );
}
