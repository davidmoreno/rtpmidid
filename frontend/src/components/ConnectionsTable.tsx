import { useEffect, useMemo, useRef, useState } from "preact/hooks";
import type { ConnectionRow } from "../model";

type SortKey =
  | "kind"
  | "n"
  | "traffic"
  | "iu"
  | "is"
  | "rtpl"
  | "rtpa"
  | "summary";

type Snap = {
  traffic: number;
  intUntil?: number;
  intSend?: number;
  rtpLast?: number;
  rtpAvg?: number;
};

function snapRow(r: ConnectionRow): Snap {
  return {
    traffic: r.trafficTotal,
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
      case "n":
        return r.nParticipants;
      case "traffic":
        return r.trafficTotal;
      case "iu":
        return numOr(r.intUntilMax);
      case "is":
        return numOr(r.intSendMax);
      case "rtpl":
        return numOr(r.rtpLastMax);
      case "rtpa":
        return numOr(r.rtpAvgMax);
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

function fmtMax(v: number | undefined, digits: number): string {
  if (v === undefined || Number.isNaN(v)) return "—";
  return v.toFixed(digits);
}

type Props = { rows: ConnectionRow[] };

export function ConnectionsTable({ rows }: Props) {
  const [sortKey, setSortKey] = useState<SortKey>("traffic");
  const [sortAsc, setSortAsc] = useState(false);
  const [flashing, setFlashing] = useState<Set<string>>(() => new Set());
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
    for (const r of rows) {
      const cur = snapRow(r);
      const was = prev.get(r.id);
      if (!was) continue;
      if (rowHadActivity(was, cur)) hot.add(r.id);
    }

    if (!hot.size) return;

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
      }, 1000);
      timersRef.current.set(id, t);
    }
  }, [rows]);

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
      setSortAsc(k === "kind" || k === "summary");
    }
  };

  return (
    <div class="overflow-x-auto">
      <table class="w-full min-w-[52rem] border-collapse border-2 border-zinc-900 text-left dark:border-zinc-100">
        <thead>
          <tr>
            <Th
              label="Kind"
              active={sortKey === "kind"}
              asc={sortAsc}
              onClick={() => toggle("kind")}
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
              label="Int until max"
              active={sortKey === "iu"}
              asc={sortAsc}
              onClick={() => toggle("iu")}
            />
            <Th
              label="Int send max"
              active={sortKey === "is"}
              asc={sortAsc}
              onClick={() => toggle("is")}
            />
            <Th
              label="RTP last max"
              active={sortKey === "rtpl"}
              asc={sortAsc}
              onClick={() => toggle("rtpl")}
            />
            <Th
              label="RTP avg max"
              active={sortKey === "rtpa"}
              asc={sortAsc}
              onClick={() => toggle("rtpa")}
            />
          </tr>
        </thead>
        <tbody>
          {sorted.map((r) => (
            <tr
              key={r.id}
              class={`border-b border-zinc-300 odd:bg-white even:bg-zinc-50 dark:border-zinc-700 dark:odd:bg-zinc-950 dark:even:bg-zinc-900/80 ${
                flashing.has(r.id) ? "conn-row-activity" : ""
              }`}
            >
              <td class="whitespace-nowrap px-2 py-1.5 font-mono text-xs">
                {r.kindLabel}
              </td>
              <td class="px-2 py-1.5 font-mono text-sm tabular-nums">
                {r.nParticipants}
              </td>
              <td class="max-w-[14rem] truncate px-2 py-1.5 font-mono text-xs">
                {r.summary}
              </td>
              <td class="max-w-[32rem] px-2 py-1.5 font-mono text-[11px] leading-snug text-zinc-800 dark:text-zinc-200">
                {r.participants}
              </td>
              <td class="px-2 py-1.5 font-mono text-sm font-semibold tabular-nums">
                {r.trafficTotal}
              </td>
              <td class="whitespace-nowrap px-2 py-1.5 font-mono text-xs tabular-nums">
                {fmtMax(r.intUntilMax, 3)}
              </td>
              <td class="whitespace-nowrap px-2 py-1.5 font-mono text-xs tabular-nums">
                {fmtMax(r.intSendMax, 3)}
              </td>
              <td class="whitespace-nowrap px-2 py-1.5 font-mono text-xs tabular-nums">
                {fmtMax(r.rtpLastMax, 2)}
              </td>
              <td class="whitespace-nowrap px-2 py-1.5 font-mono text-xs tabular-nums">
                {fmtMax(r.rtpAvgMax, 2)}
              </td>
            </tr>
          ))}
        </tbody>
      </table>
      <p class="mt-2 font-mono text-[10px] text-zinc-500">
        #P = logical participants. Opposite router edges A→B and B→A are merged
        (↔). Latency columns show the maximum across involved peers / RTP
        sub-peers. Rows flash briefly when packet counters or latency maxima
        change. Sort default: traffic.
      </p>
    </div>
  );
}
