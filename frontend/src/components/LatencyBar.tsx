import type { ComponentChildren } from "preact";
import { useEffect, useRef } from "preact/hooks";
import type { ConnectionRow, LatencyTriple, RouterPeer } from "../model";
import { latencyHistory } from "../latencyHistory";
import { useFixedTooltip } from "./FixedTooltipPortal";
import { LatencyGraph, hasGraphData } from "./LatencyGraph";
import {
  formatMs,
  latencyColorTier,
  LATENCY_TICK_MS,
  msToBarPosition,
  tickPositionsPct,
  type LatencyColorTier,
} from "../latencyScale";

function tierFillClass(t: LatencyColorTier): string {
  switch (t) {
    case "white":
      return "ui-latency-tier-0";
    case "green":
      return "ui-latency-tier-1";
    case "yellow":
      return "ui-latency-tier-2";
    case "red":
      return "ui-latency-tier-3";
    default:
      return "ui-latency-tier-0";
  }
}

function tickPositionsForStrip(compact: boolean): { ms: number; pct: number }[] {
  return compact
    ? tickPositionsPct().filter((x) => [1, 10, 100, 1000, 10_000].includes(x.ms))
    : tickPositionsPct().filter((x) =>
      LATENCY_TICK_MS.includes(x.ms as (typeof LATENCY_TICK_MS)[number]),
    );
}

/** Bar track: fill + subtle ticks drawn inside the track (no native title). */
export function LatencyTrack({
  ms,
  compact = false,
}: {
  ms: number | null;
  compact?: boolean;
}) {
  const pct = ms === null ? 0 : msToBarPosition(ms);
  const tier = latencyColorTier(ms);
  const ticks = tickPositionsForStrip(compact);

  return (
    <div class="ui-latency-track relative h-[14px]">
      <div
        class={`ui-latency-fill absolute bottom-0 left-0 top-0 z-0 transition-[width] duration-200 ${tierFillClass(tier)}`}
        style={{ width: `${pct}%` }}
      />
      <div
        class="pointer-events-none absolute inset-0 z-[1]"
        aria-hidden="true"
      >
        {ticks.map(({ ms: m, pct: p }) => (
          <div
            key={m}
            class="ui-latency-tick absolute bottom-0 top-0 w-px"
            style={{ left: `${p}%` }}
          />
        ))}
      </div>
    </div>
  );
}

type HoverBarProps = {
  combinedMs: number | null;
  /** Shown in popover on hover (HTML, immediate via CSS :hover) */
  detail: ComponentChildren;
  compact?: boolean;
  showValue?: boolean;
};

/**
 * Single latency bar (combined ms). Hover shows `detail` in a popover (no title attr).
 */
export function LatencyHoverBar({
  combinedMs,
  detail,
  compact = false,
  showValue = true,
}: HoverBarProps) {
  const anchorRef = useRef<HTMLDivElement | null>(null);
  const { tip, show, refresh, scheduleHide, portal } = useFixedTooltip();

  useEffect(() => {
    if (tip === null || anchorRef.current === null) return;
    refresh(anchorRef.current, detail);
  }, [combinedMs, detail, tip, refresh]);

  return (
    <div class="min-w-[5rem] font-mono text-[10px]">
      <div
        ref={anchorRef}
        class="cursor-default py-0.5"
        onMouseEnter={(e) => show(e.currentTarget as HTMLElement, detail)}
        onMouseLeave={scheduleHide}
      >
        <div class="flex items-center gap-1">
          <div class="relative min-h-[14px] flex-1">
            <LatencyTrack ms={combinedMs} compact={compact} />
          </div>
          {showValue && (
            <span class="ui-latency-value w-[3.25rem] shrink-0 text-right font-bold tabular-nums">
              {combinedMs === null ? "—" : formatMs(combinedMs)}
            </span>
          )}
        </div>
      </div>
      {portal}
    </div>
  );
}

function lastMs(t?: LatencyTriple): number | undefined {
  const v = t?.last;
  return typeof v === "number" && !Number.isNaN(v) ? v : undefined;
}

/** Sum of defined `last` latencies (until, send_midi, RTP CK). */
export function peerCombinedLatencyMs(peer: {
  internal?: { until?: LatencyTriple; sendMidi?: LatencyTriple };
  network?: LatencyTriple;
}): number | null {
  let s = 0;
  let n = 0;
  const u = lastMs(peer.internal?.until);
  const sm = lastMs(peer.internal?.sendMidi);
  const ck = lastMs(peer.network);
  if (u !== undefined) {
    s += u;
    n++;
  }
  if (sm !== undefined) {
    s += sm;
    n++;
  }
  if (ck !== undefined) {
    s += ck;
    n++;
  }
  return n ? s : null;
}

function addLatencyPart(sum: number, count: number, v?: number): [number, number] {
  if (v === undefined || Number.isNaN(v)) return [sum, count];
  return [sum + v, count + 1];
}

export function connectionCombinedLatencyMs(row: ConnectionRow): number | null {
  let sum = 0;
  let n = 0;
  [sum, n] = addLatencyPart(sum, n, row.intUntilMax);
  [sum, n] = addLatencyPart(sum, n, row.intSendMax);
  [sum, n] = addLatencyPart(sum, n, row.rtpLastMax);
  [sum, n] = addLatencyPart(sum, n, row.rtpAvgMax);
  return n ? sum : null;
}

export function ConnectionLatencyHoverCell({ row }: { row: ConnectionRow }) {
  const combined = connectionCombinedLatencyMs(row);
  const fmt = (v: number | undefined) =>
    v !== undefined && !Number.isNaN(v) ? formatMs(v) : "—";

  // Collect graph samples from all participant peers (computed inline — cheap O(samples))
  const graphSamples = (() => {
    const all: { timestamp: number; ms: number }[] = [];
    for (const pid of row.participantRouterIds) {
      const s = latencyHistory.get(pid);
      for (const x of s) all.push(x);
    }
    all.sort((a, b) => a.timestamp - b.timestamp);
    return all;
  })();

  const detail = (
    <div class="space-y-1 ui-text">
      <div class="font-bold uppercase ui-text-subtle">
        Latency (maxima)
      </div>
      <div>
        <span class="ui-text-subtle">Int until:</span> {fmt(row.intUntilMax)}
      </div>
      <div>
        <span class="ui-text-subtle">Int send:</span> {fmt(row.intSendMax)}
      </div>
      <div>
        <span class="ui-text-subtle">RTP last:</span> {fmt(row.rtpLastMax)}
      </div>
      <div>
        <span class="ui-text-subtle">RTP avg:</span> {fmt(row.rtpAvgMax)}
      </div>
      <div class="border-t border-[color:var(--color-border-muted)] pt-1">
        <span class="ui-text-subtle">Σ:</span>{" "}
        {combined === null ? "—" : formatMs(combined)}
      </div>
      {hasGraphData(graphSamples) && (
        <div class="border-t border-[color:var(--color-border-muted)] pt-1 mt-1">
          <LatencyGraph samples={graphSamples} />
        </div>
      )}
    </div>
  );

  return (
    <LatencyHoverBar combinedMs={combined} detail={detail} compact showValue />
  );
}

export function PeerLatencyHoverCell({ peer }: { peer: RouterPeer }) {
  const combined = peerCombinedLatencyMs(peer);
  const u = lastMs(peer.internal?.until);
  const sm = lastMs(peer.internal?.sendMidi);
  const ck = lastMs(peer.network);

  // Read graph samples from browser-side history (computed inline — cheap O(samples))
  const graphSamples = latencyHistory.get(peer.id);

  const detail = (
    <div class="space-y-1 ui-text">
      <div class="font-bold uppercase ui-text-subtle">
        Latency (last)
      </div>
      <div>
        <span class="ui-text-subtle">until→send_midi:</span>{" "}
        {u !== undefined ? formatMs(u) : "—"}
      </div>
      <div>
        <span class="ui-text-subtle">send_midi():</span>{" "}
        {sm !== undefined ? formatMs(sm) : "—"}
      </div>
      <div>
        <span class="ui-text-subtle">RTP CK:</span>{" "}
        {ck !== undefined ? formatMs(ck) : "—"}
      </div>
      <div class="border-t border-[color:var(--color-border-muted)] pt-1">
        <span class="ui-text-subtle">Σ (shown parts):</span>{" "}
        {combined === null ? "—" : formatMs(combined)}
      </div>
      {hasGraphData(graphSamples) && (
        <div class="border-t border-[color:var(--color-border-muted)] pt-1 mt-1">
          <LatencyGraph samples={graphSamples} />
        </div>
      )}
    </div>
  );

  return (
    <LatencyHoverBar combinedMs={combined} detail={detail} compact showValue />
  );
}
