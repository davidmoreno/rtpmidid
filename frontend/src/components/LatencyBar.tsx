import type { ComponentChildren } from "preact";
import type { ConnectionRow, LatencyTriple, RouterPeer } from "../model";
import { useFixedTooltip } from "./FixedTooltipPortal";
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
      return "bg-zinc-100 dark:bg-zinc-300";
    case "green":
      return "bg-emerald-500 dark:bg-emerald-400";
    case "yellow":
      return "bg-amber-400 dark:bg-amber-300";
    case "red":
      return "bg-red-600 dark:bg-red-500";
    default:
      return "bg-zinc-400";
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
    <div class="relative h-2.5 w-full overflow-hidden border-2 border-zinc-900 bg-zinc-200 dark:border-zinc-100 dark:bg-zinc-800 h-[14px]">
      <div
        class={`absolute bottom-0 left-0 top-0 z-0 transition-[width] duration-200 ${tierFillClass(tier)}`}
        style={{ width: `${pct}%` }}
      />
      <div
        class="pointer-events-none absolute inset-0 z-[1]"
        aria-hidden="true"
      >
        {ticks.map(({ ms: m, pct: p }) => (
          <div
            key={m}
            class="absolute bottom-0 top-0 w-px bg-zinc-900/20 dark:bg-zinc-100/25"
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
  const tt = useFixedTooltip();

  return (
    <div class="min-w-[5rem] font-mono text-[10px]">
      <div
        class="cursor-default py-0.5"
        onMouseEnter={(e) => tt.show(e.currentTarget as HTMLElement, detail)}
        onMouseLeave={tt.scheduleHide}
      >
        <div class="flex items-center gap-1">
          <div class="relative min-h-[14px] flex-1">
            <LatencyTrack ms={combinedMs} compact={compact} />
          </div>
          {showValue && (
            <span class="w-[3.25rem] shrink-0 text-right font-bold tabular-nums text-zinc-800 dark:text-zinc-100">
              {combinedMs === null ? "—" : formatMs(combinedMs)}
            </span>
          )}
        </div>
      </div>
      {tt.portal}
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

  const detail = (
    <div class="space-y-1 text-zinc-800 dark:text-zinc-100">
      <div class="font-bold uppercase text-zinc-500 dark:text-zinc-400">
        Latency (maxima)
      </div>
      <div>
        <span class="text-zinc-500">Int until:</span> {fmt(row.intUntilMax)}
      </div>
      <div>
        <span class="text-zinc-500">Int send:</span> {fmt(row.intSendMax)}
      </div>
      <div>
        <span class="text-zinc-500">RTP last:</span> {fmt(row.rtpLastMax)}
      </div>
      <div>
        <span class="text-zinc-500">RTP avg:</span> {fmt(row.rtpAvgMax)}
      </div>
      <div class="border-t border-zinc-300 pt-1 dark:border-zinc-600">
        <span class="text-zinc-500">Σ:</span>{" "}
        {combined === null ? "—" : formatMs(combined)}
      </div>
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

  const detail = (
    <div class="space-y-1 text-zinc-800 dark:text-zinc-100">
      <div class="font-bold uppercase text-zinc-500 dark:text-zinc-400">
        Latency (last)
      </div>
      <div>
        <span class="text-zinc-500">until→send_midi:</span>{" "}
        {u !== undefined ? formatMs(u) : "—"}
      </div>
      <div>
        <span class="text-zinc-500">send_midi():</span>{" "}
        {sm !== undefined ? formatMs(sm) : "—"}
      </div>
      <div>
        <span class="text-zinc-500">RTP CK:</span>{" "}
        {ck !== undefined ? formatMs(ck) : "—"}
      </div>
      <div class="border-t border-zinc-300 pt-1 dark:border-zinc-600">
        <span class="text-zinc-500">Σ (shown parts):</span>{" "}
        {combined === null ? "—" : formatMs(combined)}
      </div>
    </div>
  );

  return (
    <LatencyHoverBar combinedMs={combined} detail={detail} compact showValue />
  );
}
