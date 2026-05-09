import { useEffect, useMemo, useRef, useState } from "preact/hooks";
import type { ConnectionRow } from "../model";
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
    <th class="ui-th-sort">
      <button
        type="button"
        class={`font-mono text-xs font-bold uppercase tracking-wide hover:underline ${active ? "ui-text-sort-active" : "ui-text"}`}
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
        class={`ui-io flex h-7 w-7 shrink-0 items-center justify-center rounded-full font-mono text-[11px] font-bold leading-none ease-out transition-[background-color,border-color,color,box-shadow] duration-[500ms] ${recvActive ? "ui-io-in-on" : ""}`}
      >
        ←
      </span>
      <span
        title="Outbound: packets_sent increased since previous poll (stays lit each poll while increasing)"
        class={`ui-io flex h-7 w-7 shrink-0 items-center justify-center rounded-full font-mono text-[11px] font-bold leading-none ease-out transition-[background-color,border-color,color,box-shadow] duration-[500ms] ${sentActive ? "ui-io-out-on" : ""}`}
      >
        →
      </span>
    </div>
  );
}

type Props = {
  rows: ConnectionRow[];
  /** Pulse ring + scroll after mDNS Connect (matches `ConnectionRow.id`). */
  highlightConnectionRowId?: string | null;
  onSelectPeer?: (id: number) => void;
};

export function ConnectionsTable({
  rows,
  highlightConnectionRowId,
  onSelectPeer,
}: Props) {
  const [sortKey, setSortKey] = useState<SortKey>("traffic");
  const [sortAsc, setSortAsc] = useState(false);
  const [recvPulse, setRecvPulse] = useState<Set<string>>(() => new Set());
  const [sentPulse, setSentPulse] = useState<Set<string>>(() => new Set());
  const prevMapRef = useRef<Map<string, Snap> | null>(null);
  const rowRefs = useRef<Map<string, HTMLTableRowElement>>(new Map());

  const sorted = useMemo(
    () => sortConnections(rows, sortKey, sortAsc),
    [rows, sortKey, sortAsc],
  );

  useEffect(() => {
    if (
      highlightConnectionRowId === undefined ||
      highlightConnectionRowId === null
    )
      return;
    const el = rowRefs.current.get(highlightConnectionRowId);
    el?.scrollIntoView({ block: "nearest", behavior: "smooth" });
  }, [highlightConnectionRowId, sorted]);

  useEffect(() => {
    const nextMap = new Map<string, Snap>();
    for (const r of rows) nextMap.set(r.id, snapRow(r));

    const prev = prevMapRef.current;
    prevMapRef.current = nextMap;

    if (!prev) return;

    const nextRecvPulse = new Set<string>();
    const nextSentPulse = new Set<string>();

    for (const r of rows) {
      const cur = snapRow(r);
      const was = prev.get(r.id);
      if (!was) continue;
      if (cur.recvSum > was.recvSum) nextRecvPulse.add(r.id);
      if (cur.sentSum > was.sentSum) nextSentPulse.add(r.id);
    }

    setRecvPulse(nextRecvPulse);
    setSentPulse(nextSentPulse);
  }, [rows]);

  const toggle = (k: SortKey) => {
    if (sortKey === k) setSortAsc(!sortAsc);
    else {
      setSortKey(k);
      setSortAsc(k === "kind" || k === "summary" || k === "dir");
    }
  };

  return (
    <div class="ui-table-shell overflow-y-visible">
      <table class="ui-table min-w-[52rem] text-left">
        <thead>
          <tr>
            <th class="ui-th-sort text-left font-mono text-xs font-bold uppercase">
              #
            </th>
            <th class="ui-th-sort text-left font-mono text-[10px] font-bold uppercase ui-text-muted">
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
            <th class="ui-th-sort font-mono text-xs font-bold uppercase">
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
              ref={(el) => {
                if (el) rowRefs.current.set(r.id, el);
                else rowRefs.current.delete(r.id);
              }}
              class={`ui-tr-zebra ${highlightConnectionRowId === r.id ? "ui-tr-highlight" : ""
                }`}
            >
              <td class="px-2 py-1.5 font-mono text-sm font-bold tabular-nums ui-text">
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
              <td class="max-w-[28rem] px-2 py-1.5 font-mono text-[11px] leading-snug ui-text">
                <span class="inline-flex flex-wrap items-center gap-1">
                  {r.participantPeers.map((pp, i) => (
                    <span key={pp.id} class="inline-flex items-center gap-1">
                      {i > 0 ? <span class="ui-text-subtle">·</span> : null}
                      <button
                        type="button"
                        class="ui-chip-link font-mono text-[11px]"
                        onClick={() => onSelectPeer?.(pp.id)}
                      >
                        #{pp.id} {pp.name}
                      </button>
                    </span>
                  ))}
                  {r.participantNote && (
                    <span class="ui-text-muted">
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
      <p class="mt-2 font-mono text-[10px] ui-text-subtle">
        # = row order in this table. I/O: ← green / → orange mean recv or sent totals
        increased vs the previous poll; they clear on the next poll if there was no
        increase (header selector sets poll rate). Opposite router edges A→B
        and B→A are merged (Dir <span class="font-bold">↔</span>, one row).
        Router Traffic Σ = sum of packets_sent. RTP: recv/sent on that
        peer. Click a peer chip to open Peers and highlight it.
      </p>
    </div>
  );
}
