import { useEffect, useMemo, useRef, useState } from "preact/hooks";
import type { ConnectionRow } from "../model";
import { DEFAULT_STATUS_REFRESH_MS } from "../statusRefresh";
import {
  connectionCombinedLatencyMs,
  ConnectionLatencyHoverCell,
} from "./LatencyBar";

type SortKey =
  | "kind"
  | "dir"
  | "n"
  | "traffic"
  | "lat"
  | "summary";

type Snap = {
  traffic: number;
  recvSum: number;
  sentSum: number;
  lat?: number;
  intUntil?: number;
  intSend?: number;
  rtpLast?: number;
  rtpAvg?: number;
};

function snapRow(r: ConnectionRow): Snap {
  return {
    traffic: r.trafficTotal,
    recvSum: r.recvSum,
    sentSum: r.sentSum,
    lat: connectionCombinedLatencyMs(r) ?? undefined,
    intUntil: r.intUntilMax,
    intSend: r.intSendMax,
    rtpLast: r.rtpLastMax,
    rtpAvg: r.rtpAvgMax,
  };
}

function metricMoved(a?: number, b?: number): boolean {
  if (a === undefined && b === undefined) return false;
  if (a === undefined || b === undefined) return true;
  return Math.abs(a - b) > 1e-4;
}

function rowHadActivity(prev: Snap | undefined, cur: Snap): boolean {
  if (!prev) return false;
  if (cur.traffic > prev.traffic) return true;
  if (cur.recvSum > prev.recvSum) return true;
  if (cur.sentSum > prev.sentSum) return true;
  if (metricMoved(prev.lat, cur.lat)) return true;
  if (metricMoved(prev.intUntil, cur.intUntil)) return true;
  if (metricMoved(prev.intSend, cur.intSend)) return true;
  if (metricMoved(prev.rtpLast, cur.rtpLast)) return true;
  if (metricMoved(prev.rtpAvg, cur.rtpAvg)) return true;
  return false;
}

function sortConnections(
  rows: ConnectionRow[],
  key: SortKey,
  asc: boolean,
): ConnectionRow[] {
  const dir = asc ? 1 : -1;
  const numOr = (v: number | undefined): number =>
    v === undefined || Number.isNaN(v) ? -1 : v;
  const val = (r: ConnectionRow): number | string => {
    switch (key) {
      case "kind":
        return r.kindLabel.toLowerCase();
      case "dir":
        return r.direction;
      case "n":
        return r.nParticipants;
      case "traffic":
        return r.trafficTotal;
      case "lat":
        return numOr(connectionCombinedLatencyMs(r) ?? undefined);
      case "summary":
        return r.summary.toLowerCase();
      default:
        return 0;
    }
  };
  return [...rows].sort((a, b) => {
    const va = val(a);
    const vb = val(b);
    if (typeof va === "string" && typeof vb === "string") {
      return va < vb ? -dir : va > vb ? dir : 0;
    }
    const na = Number(va);
    const nb = Number(vb);
    return na < nb ? -dir : na > nb ? dir : 0;
  });
}

function Th({
  label,
  active,
  asc,
  onClick,
}: {
  label: string;
  active: boolean;
  asc: boolean;
  onClick: () => void;
}) {
  return (
    <th class="border-b-2 border-zinc-900 bg-zinc-200 px-2 py-2 text-left dark:border-zinc-100 dark:bg-zinc-800">
      <button
        type="button"
        class={`font-mono text-xs font-bold uppercase tracking-wide hover:underline ${active ? "text-amber-800 dark:text-amber-300" : ""}`}
        onClick={onClick}
      >
        {label}
        {active ? (asc ? " ▲" : " ▼") : ""}
      </button>
    </th>
  );
}

function DirectionCircles({
  rowId,
  recvActive,
  sentActive,
}: {
  rowId: string;
  recvActive: boolean;
  sentActive: boolean;
}) {
  return (
    <div
      class="flex items-center gap-1.5"
      aria-label={`Packet direction indicators for connection ${rowId}`}
    >
      <span
        title="Inbound: packets_recv increased since previous poll (stays lit each poll while increasing)"
        class={`flex h-7 w-7 shrink-0 items-center justify-center rounded-full border-2 font-mono text-[11px] font-bold leading-none ease-out transition-[background-color,border-color,color,box-shadow] duration-[500ms] ${recvActive
          ? "border-emerald-700 bg-emerald-400 text-zinc-900 shadow-[0_0_14px_rgba(52,211,153,0.9)] dark:border-emerald-300 dark:bg-emerald-400"
          : "border-zinc-400 bg-zinc-100 text-zinc-600 dark:border-zinc-600 dark:bg-zinc-800 dark:text-zinc-400"
          }`}
      >
        ←
      </span>
      <span
        title="Outbound: packets_sent increased since previous poll (stays lit each poll while increasing)"
        class={`flex h-7 w-7 shrink-0 items-center justify-center rounded-full border-2 font-mono text-[11px] font-bold leading-none ease-out transition-[background-color,border-color,color,box-shadow] duration-[500ms] ${sentActive
          ? "border-orange-700 bg-orange-400 text-zinc-900 shadow-[0_0_14px_rgba(251,146,60,0.9)] dark:border-orange-300 dark:bg-orange-400"
          : "border-zinc-400 bg-zinc-100 text-zinc-600 dark:border-zinc-600 dark:bg-zinc-800 dark:text-zinc-400"
          }`}
      >
        →
      </span>
    </div>
  );
}

type Props = {
  rows: ConnectionRow[];
  /** Row flash duration; matches poll interval when polling is on. */
  refreshIntervalMs: number;
  onSelectPeer?: (id: number) => void;
};

export function ConnectionsTable({ rows, refreshIntervalMs, onSelectPeer }: Props) {
  const rowFlashMs =
    refreshIntervalMs > 0 ? refreshIntervalMs : DEFAULT_STATUS_REFRESH_MS;

  const [sortKey, setSortKey] = useState<SortKey>("traffic");
  const [sortAsc, setSortAsc] = useState(false);
  const [flashing, setFlashing] = useState<Set<string>>(() => new Set());
  const [recvPulse, setRecvPulse] = useState<Set<string>>(() => new Set());
  const [sentPulse, setSentPulse] = useState<Set<string>>(() => new Set());
  const prevMapRef = useRef<Map<string, Snap> | null>(null);
  const timersRef = useRef<Map<string, number>>(new Map());

  const sorted = useMemo(
    () => sortConnections(rows, sortKey, sortAsc),
    [rows, sortKey, sortAsc],
  );

  useEffect(() => {
    const nextMap = new Map<string, Snap>();
    for (const r of rows) nextMap.set(r.id, snapRow(r));

    const prev = prevMapRef.current;
    prevMapRef.current = nextMap;

    if (!prev) return;

    const hot = new Set<string>();
    const nextRecvPulse = new Set<string>();
    const nextSentPulse = new Set<string>();

    for (const r of rows) {
      const cur = snapRow(r);
      const was = prev.get(r.id);
      if (!was) continue;
      if (rowHadActivity(was, cur)) hot.add(r.id);
      if (cur.recvSum > was.recvSum) nextRecvPulse.add(r.id);
      if (cur.sentSum > was.sentSum) nextSentPulse.add(r.id);
    }

    setRecvPulse(nextRecvPulse);
    setSentPulse(nextSentPulse);

    for (const id of hot) {
      const oldT = timersRef.current.get(id);
      if (oldT !== undefined) window.clearTimeout(oldT);
      setFlashing((s) => new Set(s).add(id));
      const t = window.setTimeout(() => {
        timersRef.current.delete(id);
        setFlashing((s) => {
          const n = new Set(s);
          n.delete(id);
          return n;
        });
      }, rowFlashMs);
      timersRef.current.set(id, t);
    }
  }, [rows, rowFlashMs]);

  useEffect(
    () => () => {
      for (const t of timersRef.current.values()) window.clearTimeout(t);
      timersRef.current.clear();
    },
    [],
  );

  const toggle = (k: SortKey) => {
    if (sortKey === k) setSortAsc(!sortAsc);
    else {
      setSortKey(k);
      setSortAsc(k === "kind" || k === "summary" || k === "dir");
    }
  };

  return (
    <div class="overflow-x-auto overflow-y-visible">
      <table class="w-full min-w-[52rem] border-collapse border-2 border-zinc-900 text-left dark:border-zinc-100">
        <thead>
          <tr>
            <th class="border-b-2 border-zinc-900 bg-zinc-200 px-2 py-2 text-left font-mono text-xs font-bold uppercase dark:border-zinc-100 dark:bg-zinc-800">
              #
            </th>
            <th class="border-b-2 border-zinc-900 bg-zinc-200 px-2 py-2 text-left font-mono text-[10px] font-bold uppercase text-zinc-600 dark:border-zinc-100 dark:bg-zinc-800 dark:text-zinc-400">
              I/O
            </th>
            <Th
              label="Kind"
              active={sortKey === "kind"}
              asc={sortAsc}
              onClick={() => toggle("kind")}
            />
            <Th
              label="Dir"
              active={sortKey === "dir"}
              asc={sortAsc}
              onClick={() => toggle("dir")}
            />
            <Th
              label="#P"
              active={sortKey === "n"}
              asc={sortAsc}
              onClick={() => toggle("n")}
            />
            <Th
              label="Summary"
              active={sortKey === "summary"}
              asc={sortAsc}
              onClick={() => toggle("summary")}
            />
            <th class="border-b-2 border-zinc-900 bg-zinc-200 px-2 py-2 font-mono text-xs font-bold uppercase dark:border-zinc-100 dark:bg-zinc-800">
              Participants
            </th>
            <Th
              label="Traffic Σ"
              active={sortKey === "traffic"}
              asc={sortAsc}
              onClick={() => toggle("traffic")}
            />
            <Th
              label="Latency"
              active={sortKey === "lat"}
              asc={sortAsc}
              onClick={() => toggle("lat")}
            />
          </tr>
        </thead>
        <tbody>
          {sorted.map((r, idx) => (
            <tr
              key={r.id}
              class={`border-b border-zinc-300 odd:bg-white even:bg-zinc-50 dark:border-zinc-700 dark:odd:bg-zinc-950 dark:even:bg-zinc-900/80 ${flashing.has(r.id) ? "conn-row-activity" : ""
                }`}
            >
              <td class="px-2 py-1.5 font-mono text-sm font-bold tabular-nums text-zinc-800 dark:text-zinc-100">
                {idx + 1}
              </td>
              <td class="px-1 py-1 align-middle">
                <DirectionCircles
                  rowId={r.id}
                  recvActive={recvPulse.has(r.id)}
                  sentActive={sentPulse.has(r.id)}
                />
              </td>
              <td class="whitespace-nowrap px-2 py-1.5 font-mono text-xs">
                {r.kindLabel}
              </td>
              <td class="whitespace-nowrap px-2 py-1.5 font-mono text-sm font-bold">
                {r.direction}
              </td>
              <td class="px-2 py-1.5 font-mono text-sm tabular-nums">
                {r.nParticipants}
              </td>
              <td class="max-w-[14rem] truncate px-2 py-1.5 font-mono text-xs">
                {r.summary}
              </td>
              <td class="max-w-[28rem] px-2 py-1.5 font-mono text-[11px] leading-snug text-zinc-800 dark:text-zinc-200">
                <span class="inline-flex flex-wrap items-center gap-1">
                  {r.participantPeers.map((pp, i) => (
                    <span key={pp.id} class="inline-flex items-center gap-1">
                      {i > 0 ? <span class="text-zinc-400">·</span> : null}
                      <button
                        type="button"
                        class="rounded border border-zinc-400 bg-zinc-100 px-1 py-0.5 text-left hover:bg-amber-100 dark:border-zinc-600 dark:bg-zinc-800 dark:hover:bg-zinc-700"
                        onClick={() => onSelectPeer?.(pp.id)}
                      >
                        #{pp.id} {pp.name}
                      </button>
                    </span>
                  ))}
                  {r.participantNote && (
                    <span class="text-zinc-600 dark:text-zinc-400">
                      {r.participantNote}
                    </span>
                  )}
                </span>
              </td>
              <td class="px-2 py-1.5 font-mono text-sm font-semibold tabular-nums">
                {r.trafficTotal}
              </td>
              <td class="relative min-w-[10rem] overflow-visible px-1 py-1 align-top">
                <ConnectionLatencyHoverCell row={r} />
              </td>
            </tr>
          ))}
        </tbody>
      </table>
      <p class="mt-2 font-mono text-[10px] text-zinc-500">
        # = row order in this table. I/O: ← green / → orange mean recv or sent totals
        increased vs the previous poll; they clear on the next poll if there was no
        increase (header selector sets poll rate). Opposite router edges A→B
        and B→A are merged (Dir <span class="font-bold">bidi</span>, both peer
        ids). Router Traffic Σ = sum of packets_sent. RTP: recv/sent on that
        peer. Click a peer chip to open Peers and highlight it.
      </p>
    </div>
  );
}
