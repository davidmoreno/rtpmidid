import { useEffect, useMemo, useState } from "preact/hooks";
import type { ComponentChildren } from "preact";
import type { RouterPeer } from "../model";
import type { Endpoint } from "../endpoints";
import {
  compareEndpointsForDevicesSort,
  groupForEndpoint,
  type EndpointSortKey,
} from "../endpointPickerUtils";
import { Button } from "./Button";

export function EndpointSelectBadge({ e }: { e: Endpoint }) {
  const g = groupForEndpoint(e);
  const cls =
    g === "local"
      ? "ui-badge-local inline-flex items-center gap-1 rounded px-1 py-0.5 font-mono text-[11px] font-bold"
      : "ui-badge-remote inline-flex items-center gap-1 rounded px-1 py-0.5 font-mono text-[11px] font-bold";
  return (
    <span class={cls}>
      <span class="max-w-[12rem] truncate">{e.label}</span>
      <span class="text-[9px] font-black uppercase opacity-80">{e.kind}</span>
    </span>
  );
}

export type EndpointPickerDialogProps = {
  title: string;
  description?: ComponentChildren;
  endpoints: Endpoint[];
  /** Device identities to omit from the list (e.g. the other side already picked). */
  excludeIds?: string[];
  favoriteIds?: Set<string>;
  sortKey?: EndpointSortKey;
  isPeerConnected?: Map<number, boolean>;
  byPeerId?: Map<number, RouterPeer>;
  confirmLabel?: string;
  /** First click selects; second click on same row confirms (Devices connect flow). */
  confirmOnSecondClick?: boolean;
  onClose: () => void;
  onConfirm: (identity: string) => void;
};

export function EndpointPickerDialog({
  title,
  description,
  endpoints,
  excludeIds = [],
  favoriteIds = new Set<string>(),
  sortKey = "activity",
  isPeerConnected = new Map<number, boolean>(),
  byPeerId = new Map<number, RouterPeer>(),
  confirmLabel = "Select",
  confirmOnSecondClick = false,
  onClose,
  onConfirm,
}: EndpointPickerDialogProps) {
  const [q, setQ] = useState("");
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const exclude = useMemo(() => new Set(excludeIds), [excludeIds]);

  const opts = useMemo(
    () => endpoints.filter((x) => !exclude.has(x.identity)),
    [endpoints, exclude],
  );

  const filtered = useMemo(() => {
    const s = q.trim().toLowerCase();
    const base = !s
      ? opts
      : opts.filter((e) =>
          [e.label, e.sub, e.kind, e.identity].join(" ").toLowerCase().includes(s),
        );
    return [...base].sort((a, b) =>
      compareEndpointsForDevicesSort(
        a,
        b,
        sortKey,
        favoriteIds,
        isPeerConnected,
        byPeerId,
      ),
    );
  }, [opts, q, favoriteIds, sortKey, isPeerConnected, byPeerId]);

  useEffect(() => {
    const onKey = (ev: KeyboardEvent) => {
      if (ev.key === "Escape") {
        ev.preventDefault();
        onClose();
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [onClose]);

  const selectedEp =
    selectedId !== null ? opts.find((x) => x.identity === selectedId) ?? null : null;

  return (
    <div
      role="presentation"
      class="ui-modal-backdrop"
      onClick={() => onClose()}
    >
      <div
        role="dialog"
        aria-modal="true"
        class="ui-modal max-h-[min(90vh,36rem)]"
        onClick={(ev) => ev.stopPropagation()}
      >
        <h2 class="mb-3 font-mono text-sm font-bold uppercase ui-text">{title}</h2>
        {description ? (
          <div class="mb-3 font-mono text-[11px] leading-relaxed ui-text-muted">
            {description}
          </div>
        ) : null}

        <label class="mb-2 block font-mono text-[10px] font-bold uppercase ui-text-muted">
          Search
          <input
            value={q}
            onInput={(e) => setQ((e.target as HTMLInputElement).value)}
            placeholder="Filter by name, kind, id…"
            class="ui-input mt-1 font-mono text-[11px]"
            autoFocus
          />
        </label>

        <div class="max-h-[min(40vh,14rem)] overflow-y-auto rounded-[var(--radius-md)] border border-[color:var(--color-border)] p-2">
          {filtered.length ? (
            <div class="space-y-1">
              {filtered.map((ep) => {
                const g = groupForEndpoint(ep);
                const base =
                  g === "local" ? "ui-endpoint-opt-local" : "ui-endpoint-opt-remote";
                const sel = ep.identity === selectedId;
                return (
                  <button
                    type="button"
                    key={ep.identity}
                    class={`w-full rounded-[var(--radius-sm)] border-2 p-2 text-left font-mono ${base} ${
                      sel ? "ring-2 ring-[color:var(--color-ring-highlight)] ring-inset" : ""
                    }`}
                    onClick={() => {
                      if (confirmOnSecondClick && selectedId === ep.identity) {
                        onConfirm(ep.identity);
                      } else {
                        setSelectedId(ep.identity);
                      }
                    }}
                    title={
                      confirmOnSecondClick && selectedId === ep.identity
                        ? `${ep.identity} — click again to confirm`
                        : ep.identity
                    }
                  >
                    <div class="flex flex-wrap items-center justify-between gap-2">
                      <div class="min-w-0">
                        <div class="truncate text-xs font-black">{ep.label}</div>
                        <div class="truncate text-[10px] font-bold opacity-80">{ep.sub}</div>
                      </div>
                      <div class="shrink-0 text-right">
                        <div class="text-[10px] font-black uppercase">{ep.kind}</div>
                        <div class="text-[10px] opacity-80">{ep.identity}</div>
                      </div>
                    </div>
                  </button>
                );
              })}
            </div>
          ) : (
            <div class="font-mono text-[11px] ui-text-subtle">No matching endpoints.</div>
          )}
        </div>

        {selectedEp ? (
          <p class="mt-2 font-mono text-[10px] ui-text-muted">
            Selected: <EndpointSelectBadge e={selectedEp} />
          </p>
        ) : null}

        <div class="mt-4 flex flex-wrap gap-2">
          <Button type="button" onClick={onClose}>
            Cancel
          </Button>
          <Button
            disabled={selectedId === null}
            onClick={() => {
              if (selectedId !== null) onConfirm(selectedId);
            }}
          >
            {confirmLabel}
          </Button>
        </div>
      </div>
    </div>
  );
}
