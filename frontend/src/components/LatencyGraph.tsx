/**
 * SVG time-series latency graph with log-scale Y axis and threshold dashed lines.
 *
 * Renders inside the existing tooltip (hover on latency bars).
 * Dimensions designed to fit a tooltip panel (~260×110 px viewport).
 */

import type { LatencySample } from "../latencyHistory";
import {
  LATENCY_TIER_MS,
  LATENCY_BAR_MAX_MS,
} from "../latencyScale";

// ── Layout constants ──────────────────────────────────────────────────────

const GRAPH_W = 260;
const GRAPH_H = 100;
const PAD_L = 36; // Y-axis label space
const PAD_R = 6;
const PAD_T = 8;
const PAD_B = 16; // X-axis label space
const PLOT_W = GRAPH_W - PAD_L - PAD_R;
const PLOT_H = GRAPH_H - PAD_T - PAD_B;

/** Log-scale Y transform: maps ms → pixel Y (top of plot = log10(LATENCY_BAR_MAX_MS), bottom = 0). */
function msToY(ms: number): number {
  if (!Number.isFinite(ms) || ms <= 0) return PLOT_H; // bottom
  const minLog = -3; // log10(0.001 ms)
  const maxLog = Math.log10(LATENCY_BAR_MAX_MS); // log10(10000) = 4
  const logMs = Math.log10(ms);
  const t = (logMs - minLog) / (maxLog - minLog);
  return PLOT_H - Math.min(1, Math.max(0, t)) * PLOT_H;
}

/** Short formatted ms for Y-axis labels. */
function shortMs(ms: number): string {
  if (ms < 1) return `${ms * 1000}μs`;
  if (ms >= 1000) return `${ms / 1000}s`;
  return `${ms}ms`;
}

// ── Threshold Y-grid lines (dashed) ────────────────────────────────────────

const GRID_LINES = [
  { ms: LATENCY_TIER_MS.greenBelow, label: "1ms", className: "ui-latency-graph-grid-good" },
  { ms: LATENCY_TIER_MS.yellowBelow, label: "10ms", className: "ui-latency-graph-grid-warn" },
  { ms: LATENCY_TIER_MS.redBelow, label: "100ms", className: "ui-latency-graph-grid-bad" },
];

/** Y-axis tick marks (log-spaced). */
const Y_TICKS = [0.001, 0.01, 0.1, 1, 10, 100, 1000, 5000, 10000];

export function LatencyGraph({ samples }: { samples: LatencySample[] }) {
  if (samples.length < 2) {
    return (
      <div class="text-[10px] ui-text-subtle italic py-1">
        Collecting samples…
      </div>
    );
  }

  const tMin = samples[0].timestamp;
  const tMax = samples[samples.length - 1].timestamp;
  const span = tMax - tMin || 1;

  const xFor = (t: number) =>
    PAD_L + ((t - tMin) / span) * PLOT_W;

  // Build path
  const points = samples.map(
    (s) => `${xFor(s.timestamp).toFixed(1)},${(PAD_T + msToY(s.ms)).toFixed(1)}`,
  );
  const pathD = `M${points.join(" L")}`;

  // Fill under curve for subtle area shading
  const fillD = pathD
    ? `${pathD} L${xFor(tMax).toFixed(1)},${PAD_T + PLOT_H} L${xFor(tMin).toFixed(1)},${PAD_T + PLOT_H} Z`
    : "";

  return (
    <div class="ui-latency-graph-wrap">
      <svg
        viewBox={`0 0 ${GRAPH_W} ${GRAPH_H}`}
        class="ui-latency-graph"
        width={GRAPH_W}
        height={GRAPH_H}
        role="img"
        aria-label="Latency over time"
      >
        {/* Background */}
        <rect
          x={PAD_L}
          y={PAD_T}
          width={PLOT_W}
          height={PLOT_H}
          class="ui-latency-graph-bg"
        />

        {/* Y grid lines and labels */}
        {GRID_LINES.map((g) => {
          const y = PAD_T + msToY(g.ms);
          return (
            <line
              key={`grid-${g.ms}`}
              x1={PAD_L}
              y1={y}
              x2={PAD_L + PLOT_W}
              y2={y}
              class={g.className}
            />
          );
        })}

        {/* Y-axis tick labels */}
        {Y_TICKS.map((ms) => {
          const y = PAD_T + msToY(ms);
          if (y > PAD_T + PLOT_H || y < PAD_T) return null;
          return (
            <text
              key={`yt-${ms}`}
              x={PAD_L - 3}
              y={y + 3}
              text-anchor="end"
              class="ui-latency-graph-ytext"
            >
              {shortMs(ms)}
            </text>
          );
        })}

        {/* X-axis min/max labels */}
        <text
          x={PAD_L}
          y={GRAPH_H - 2}
          text-anchor="start"
          class="ui-latency-graph-xtext"
        >
          -{((Date.now() - tMin) / 1000).toFixed(0)}s
        </text>
        <text
          x={PAD_L + PLOT_W}
          y={GRAPH_H - 2}
          text-anchor="end"
          class="ui-latency-graph-xtext"
        >
          now
        </text>

        {/* Area fill */}
        <path d={fillD} class="ui-latency-graph-area" />

        {/* Data line */}
        <path d={pathD} class="ui-latency-graph-line" fill="none" />

        {/* Latest point dot */}
        {samples.length > 0 && (() => {
          const latest = samples[samples.length - 1];
          return (
            <circle
              cx={xFor(latest.timestamp)}
              cy={PAD_T + msToY(latest.ms)}
              r={2.5}
              class="ui-latency-graph-dot"
            />
          );
        })()}
      </svg>
    </div>
  );
}

/** Returns true if there's enough history data to show a graph. */
export function hasGraphData(samples: LatencySample[]): boolean {
  return samples.length >= 2;
}
