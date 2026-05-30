import { useEffect, useMemo, useRef, useState } from "preact/hooks";
import type { ConnectionEndpointRef, ConnectionRow } from "../model";
import {
  connectionCombinedLatencyMs,
  ConnectionLatencyHoverCell,
} from "./LatencyBar";
import { CONFIRM_SKIP_HINT, runWithConfirm } from "../confirmAction";

type SortKey =
  | "type"
  | "from"
  | "traffic"
  | "lat";

type Snap = {
  recvSum: number;
  sentSum: number;
};

function snapRow(r: ConnectionRow): Snap {
  return {
    recvSum: r.recvSum,
    sentSum: r.sentSum,
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
      case "type":
        return r.type;
      case "from":
        return (r.from.label || "").toLowerCase();
      case "traffic":
        return r.trafficTotal;
      case "lat":
        return numOr(connectionCombinedLatencyMs(r) ?? undefined);
      default:
        return 0;
    }
  };
  return [...rows].sort((a, b) => {
    const va = val(a);
    const vb = val(b);
    if (typeof va === "string" && typeof vb === "string") {
      const c = va < vb ? -dir : va > vb ? dir : 0;
      if (c !== 0) return c;
      /* Stable secondary sort by id keeps row order deterministic between polls. */
      return a.id < b.id ? -1 : a.id > b.id ? 1 : 0;
    }
    const na = Number(va);
    const nb = Number(vb);
    if (na !== nb) return na < nb ? -dir : dir;
    return a.id < b.id ? -1 : a.id > b.id ? 1 : 0;
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
        title="Inbound: packets_recv increased recently (stays lit for one refresh interval)"
        class={`ui-io flex h-7 w-7 shrink-0 items-center justify-center rounded-full font-mono text-[11px] font-bold leading-none ease-out transition-[background-color,border-color,color,box-shadow] duration-[500ms] ${recvActive ? "ui-io-in-on" : ""}`}
      >
        ←
      </span>
      <span
        title="Outbound: packets_sent increased recently (stays lit for one refresh interval)"
        class={`ui-io flex h-7 w-7 shrink-0 items-center justify-center rounded-full font-mono text-[11px] font-bold leading-none ease-out transition-[background-color,border-color,color,box-shadow] duration-[500ms] ${sentActive ? "ui-io-out-on" : ""}`}
      >
        →
      </span>
    </div>
  );
}

function TypeBadge({ type }: { type: ConnectionRow["type"] }) {
  const isAlsa = type === "alsaseq";
  return (
    <span
      class={`inline-flex shrink-0 items-center rounded border px-1.5 py-0.5 font-mono text-[10px] font-black uppercase tracking-wide ${
        isAlsa
          ? "ui-badge-local border-[color:var(--color-badge-local-border)]"
          : "ui-badge-remote border-[color:var(--color-badge-remote-border)]"
      }`}
      title={
        isAlsa
          ? "Pure ALSA-seq aconnect subscription (not routed through midirouter)"
          : "midirouter edge (routed in-process between peers)"
      }
    >
      {isAlsa ? "ALSASEQ" : "MIDIROUTER"}
    </span>
  );
}

function EndpointPill({
  side,
  onOpen,
}: {
  side: ConnectionEndpointRef;
  onOpen?: (endpointId: string) => void;
}) {
  const peerTag =
    side.peerId !== undefined && side.peerId !== 0 ? `#${side.peerId}` : "";
  const inner = (
    <>
      <span class="truncate font-mono text-[12px] font-bold ui-text">
        {side.label || "—"}
      </span>
      {peerTag ? (
        <span class="ui-pill-id ml-1 shrink-0 text-[9px]">{peerTag}</span>
      ) : null}
    </>
  );
  const baseCls =
    "inline-flex max-w-full items-center gap-1 truncate rounded border-2 px-1.5 py-0.5";
  if (side.unavailable || !onOpen) {
    return (
      <span
        class={`${baseCls} border-[color:var(--color-border)] opacity-75`}
        title={
          side.unavailable
            ? "Endpoint not present right now"
            : "Endpoint cannot be focused"
        }
      >
        {inner}
      </span>
    );
  }
  return (
    <button
      type="button"
      class={`${baseCls} ui-chip-link border-[color:var(--color-border)] hover:bg-[color:var(--color-surface-elevated)]`}
      title={`Open in Devices: ${side.endpointId}`}
      onClick={() => onOpen(side.endpointId)}
    >
      {inner}
    </button>
  );
}

function ConnectionCell({
  row,
  onOpen,
}: {
  row: ConnectionRow;
  onOpen?: (endpointId: string) => void;
}) {
  const arrow = row.bidirectional ? "↔" : "→";
  return (
    <div class="flex w-full min-w-0 items-center gap-2">
      <div class="min-w-0 flex-1">
        <EndpointPill side={row.from} onOpen={onOpen} />
      </div>
      <span
        class="shrink-0 font-mono text-base font-black ui-text"
        title={row.bidirectional ? "Bidirectional" : "One-way"}
      >
        {arrow}
      </span>
      <div class="min-w-0 flex-1">
        <EndpointPill side={row.to} onOpen={onOpen} />
      </div>
    </div>
  );
}

/** Star button matching the favourites pattern used in PeersCards:
 *  ★ (highlighted) = saved, click to remove;
 *  ☆ (outline)    = not saved, click to add;
 *  ☆ (dimmed)     = cannot be saved (tooltip explains why).
 */
function DbStarCell({
  row,
  dbEnabled,
  onAdd,
  onRemove,
}: {
  row: ConnectionRow;
  dbEnabled: boolean;
  onAdd?: (row: ConnectionRow) => void;
  onRemove?: (row: ConnectionRow) => void;
}) {
  if (!dbEnabled) return <td class="px-2 py-1.5 align-middle"></td>;

  const saved =
    !!row.canRemoveFromDb &&
    !!row.persistedSideA &&
    !!row.persistedSideB &&
    !!onRemove;
  const canAdd = !saved && !!row.canAddToDb && !!onAdd;
  const disabled = !saved && !canAdd;

  const btnCls =
    "flex h-7 w-7 shrink-0 items-center justify-center rounded-md font-mono text-xl leading-none outline-none transition-all duration-200 ease-out focus-visible:ring-2 focus-visible:ring-[color:var(--color-ring-highlight)]";

  if (disabled) {
    return (
      <td class="px-2 py-1.5 align-middle">
        <span
          class={`${btnCls} cursor-not-allowed ui-text-subtle opacity-50`}
          title={
            row.cannotSaveReason ??
            "This connection cannot be saved (no stable identity)."
          }
          aria-label="Not saveable"
          aria-disabled
        >
          ☆
        </span>
      </td>
    );
  }

  const handler = saved
    ? () => onRemove!(row)
    : () => onAdd!(row);
  const title = saved
    ? `Remove from saved connections. ${CONFIRM_SKIP_HINT}`
    : "Save this connection to the database";
  const aria = saved
    ? "Remove from saved connections"
    : "Save this connection to the database";

  const onStarClick = (ev: MouseEvent) => {
    ev.preventDefault();
    ev.stopPropagation();
    if (saved) {
      runWithConfirm(
        ev,
        `Remove saved connection "${row.from.label}" ${row.direction} "${row.to.label}" from the database?`,
        handler,
      );
    } else {
      handler();
    }
  };

  return (
    <td class="px-2 py-1.5 align-middle">
      <button
        type="button"
        class={`${btnCls} cursor-pointer ui-text-muted hover:scale-110 hover:bg-[color:var(--color-surface-2)] hover:text-[color:var(--color-ring-highlight)] hover:shadow-[0_0_0_1px_color-mix(in_srgb,var(--color-ring-highlight)_35%,transparent)] active:scale-95`}
        aria-label={aria}
        aria-pressed={saved}
        title={title}
        onClick={onStarClick}
      >
        {saved ? (
          <span
            class="text-[color:var(--color-ring-highlight)] drop-shadow-[0_1px_3px_color-mix(in_srgb,var(--color-ring-highlight)_45%,transparent)]"
            aria-hidden
          >
            ★
          </span>
        ) : (
          <span aria-hidden>☆</span>
        )}
      </button>
    </td>
  );
}

type Props = {
  rows: ConnectionRow[];
  /** Pulse ring + scroll after mDNS Connect (matches `ConnectionRow.id`). */
  highlightConnectionRowId?: string | null;
  /** Navigate to Devices tab and highlight a card by endpoint id (alsa:c:p / peer:N / mdns:...). */
  onOpenInDevices?: (endpointId: string) => void;
  dbEnabled?: boolean;
  /** Save this connection in the SQLite db. */
  onAddToDb?: (row: ConnectionRow) => void;
  /** Remove this connection from the SQLite db (uses persistedSideA/B). */
  onRemoveFromDb?: (row: ConnectionRow) => void;
  /** Open editor for a saved connection. */
  onEditSaved?: (row: ConnectionRow) => void;
  /** Enable or disable auto-reconnect for a saved connection. */
  onToggleEnabled?: (row: ConnectionRow, enable: boolean) => void;
  /** Status refresh interval (ms); controls how long the I/O highlight stays lit. */
  refreshIntervalMs: number;
};

export function ConnectionsTable({
  rows,
  highlightConnectionRowId,
  onOpenInDevices,
  dbEnabled = false,
  onAddToDb,
  onRemoveFromDb,
  onEditSaved,
  onToggleEnabled,
  refreshIntervalMs,
}: Props) {
  const [sortKey, setSortKey] = useState<SortKey>("from");
  const [sortAsc, setSortAsc] = useState(true);
  const [, setTick] = useState(0);
  /* expireAt timestamps (performance.now() + refreshIntervalMs) so a single
     increase keeps the I/O circle lit for one full refresh interval - much
     more readable than the previous "lit only between consecutive polls". */
  const recvLitUntilRef = useRef<Map<string, number>>(new Map());
  const sentLitUntilRef = useRef<Map<string, number>>(new Map());
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

  /* Update the pulse-expire maps when a new snapshot arrives. We don't depend
     on the previous snapshot resampling the same row to keep it lit - the
     lit window is anchored to the most recent observed increase. */
  useEffect(() => {
    const nextSnap = new Map<string, Snap>();
    for (const r of rows) nextSnap.set(r.id, snapRow(r));
    const prev = prevMapRef.current;
    prevMapRef.current = nextSnap;
    if (!prev) return;
    const now = performance.now();
    const litWindow = Math.max(250, refreshIntervalMs);
    let changed = false;
    for (const r of rows) {
      const cur = snapRow(r);
      const was = prev.get(r.id);
      if (!was) continue;
      if (cur.recvSum > was.recvSum) {
        recvLitUntilRef.current.set(r.id, now + litWindow);
        changed = true;
      }
      if (cur.sentSum > was.sentSum) {
        sentLitUntilRef.current.set(r.id, now + litWindow);
        changed = true;
      }
    }
    if (changed) setTick((t) => t + 1);
  }, [rows, refreshIntervalMs]);

  /* Ticker drops expired entries so the circle visibly clears even when the
     server has nothing new to send for a while. */
  useEffect(() => {
    let stopped = false;
    const tickInterval = Math.max(150, Math.floor(refreshIntervalMs / 4));
    const handle = window.setInterval(() => {
      if (stopped) return;
      const now = performance.now();
      let changed = false;
      for (const [k, t] of Array.from(recvLitUntilRef.current.entries())) {
        if (t <= now) {
          recvLitUntilRef.current.delete(k);
          changed = true;
        }
      }
      for (const [k, t] of Array.from(sentLitUntilRef.current.entries())) {
        if (t <= now) {
          sentLitUntilRef.current.delete(k);
          changed = true;
        }
      }
      if (changed) setTick((x) => x + 1);
    }, tickInterval);
    return () => {
      stopped = true;
      window.clearInterval(handle);
    };
  }, [refreshIntervalMs]);

  const toggle = (k: SortKey) => {
    if (sortKey === k) setSortAsc(!sortAsc);
    else {
      setSortKey(k);
      /* Text columns default ascending, numeric default descending. */
      setSortAsc(k === "type" || k === "from");
    }
  };

  const now = performance.now();

  return (
    <div class="ui-table-shell overflow-y-visible">
      <table class="ui-table min-w-[52rem] text-left">
        <thead>
          <tr>
            {dbEnabled ? (
              <th
                class="ui-th-sort w-10 text-left font-mono text-xs font-bold uppercase"
                title="Save connections to the database (★ = saved, ☆ = save, dim ☆ = cannot save)"
              >
                ★
              </th>
            ) : null}
            <th class="ui-th-sort text-left font-mono text-xs font-bold uppercase">
              #
            </th>
            <th class="ui-th-sort text-left font-mono text-[10px] font-bold uppercase ui-text-muted">
              I/O
            </th>
            <Th
              label="Type"
              active={sortKey === "type"}
              asc={sortAsc}
              onClick={() => toggle("type")}
            />
            <Th
              label="Connection"
              active={sortKey === "from"}
              asc={sortAsc}
              onClick={() => toggle("from")}
            />
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
            {dbEnabled ? (
              <th class="ui-th-sort text-left font-mono text-xs font-bold uppercase">
                Saved
              </th>
            ) : null}
          </tr>
        </thead>
        <tbody>
          {sorted.map((r, idx) => {
            const recvLit = (recvLitUntilRef.current.get(r.id) ?? 0) > now;
            const sentLit = (sentLitUntilRef.current.get(r.id) ?? 0) > now;
            return (
              <tr
                key={r.id}
                ref={(el) => {
                  if (el) rowRefs.current.set(r.id, el);
                  else rowRefs.current.delete(r.id);
                }}
                class={`ui-tr-zebra ${highlightConnectionRowId === r.id ? "ui-tr-highlight" : ""}`}
              >
                <DbStarCell
                  row={r}
                  dbEnabled={dbEnabled}
                  onAdd={onAddToDb}
                  onRemove={onRemoveFromDb}
                />
                <td class="px-2 py-1.5 font-mono text-sm font-bold tabular-nums ui-text">
                  {idx + 1}
                </td>
                <td class="px-1 py-1 align-middle">
                  <DirectionCircles
                    rowId={r.id}
                    recvActive={recvLit}
                    sentActive={sentLit}
                  />
                </td>
                <td class="whitespace-nowrap px-2 py-1.5 align-middle">
                  <TypeBadge type={r.type} />
                </td>
                <td class="max-w-[40rem] px-2 py-1.5 align-middle">
                  <ConnectionCell row={r} onOpen={onOpenInDevices} />
                </td>
                <td class="px-2 py-1.5 font-mono text-sm font-semibold tabular-nums">
                  {r.trafficTotal}
                </td>
                <td class="relative min-w-[10rem] overflow-visible px-1 py-1 align-top">
                  <ConnectionLatencyHoverCell row={r} />
                </td>
                {dbEnabled ? (
                  <td class="whitespace-nowrap px-2 py-1.5 align-middle">
                    {r.persisted && r.persistedSideA && r.persistedSideB ? (
                      <div class="flex flex-wrap gap-1">
                        {onEditSaved ? (
                          <button
                            type="button"
                            class="rounded border border-[color:var(--color-border)] px-1.5 py-0.5 font-mono text-[10px] ui-text-muted hover:ui-text"
                            onClick={() => onEditSaved(r)}
                          >
                            Edit
                          </button>
                        ) : null}
                        {onToggleEnabled ? (
                          <button
                            type="button"
                            class="rounded border border-[color:var(--color-border)] px-1.5 py-0.5 font-mono text-[10px] ui-text-muted hover:ui-text"
                            title={
                              r.persistedEnabled === false
                                ? "Enable auto-reconnect"
                                : "Disable auto-reconnect"
                            }
                            onClick={() =>
                              onToggleEnabled(
                                r,
                                r.persistedEnabled === false,
                              )
                            }
                          >
                            {r.persistedEnabled === false ? "Enable" : "Disable"}
                          </button>
                        ) : null}
                      </div>
                    ) : null}
                  </td>
                ) : null}
              </tr>
            );
          })}
        </tbody>
      </table>
      <p class="mt-2 font-mono text-[10px] ui-text-subtle">
        <span class="font-bold">★</span> = saved in DB (click to remove,
        confirm unless Shift/Ctrl+click),
        <span class="font-bold"> ☆</span> = click to save,
        <span class="font-bold"> dim ☆</span> = cannot be saved (hover for
        reason). # = row order in this table. I/O: ← / → stay lit for one
        full refresh interval after the corresponding packet counter
        increases. Type: <span class="font-bold">MIDIROUTER</span> = router
        edge, <span class="font-bold">ALSASEQ</span> = pure aconnect link.
        Bidi rows merge A→B and B→A. Click an endpoint to open the matching
        card on the Devices tab.
      </p>
    </div>
  );
}
