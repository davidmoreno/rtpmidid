import { useMemo, useState } from "preact/hooks";
import { Card } from "../components/Card";
import { ConnectionsTable } from "../components/ConnectionsTable";
import { ConnectionEditorDialog } from "../components/ConnectionEditorDialog";
import { RefreshBanner } from "../components/RefreshBanner";
import { Button } from "../components/Button";
import {
  buildAlsaSubscriptionConnections,
  parseAlsaSubscriptions,
  type ConnectionRow,
  type MdnsRemote,
  type RouterPeer,
} from "../model";
import type { MidiAlsaSeqEntry, MidiRawmidiEntry } from "../midiEnumerate";
import type { RpcClient } from "../rpc";
import { buildEndpoints } from "../endpoints";
import {
  annotateLiveOnly,
  mergeConnectionsWithPersisted,
  type PersistedConnectionRow,
} from "../persistedConnections";
import type { ConnectionDirection } from "../deviceIdentity";

type Props = {
  refreshIntervalMs: number;
  lastRefresh: Date | null;
  liveConnections: ConnectionRow[];
  savedConnections: PersistedConnectionRow[];
  alsaSubs: unknown[];
  dbEnabled: boolean;
  highlightConnectionRowId: string | null;
  onOpenInDevices: (endpointId: string) => void;
  rpc: RpcClient;
  peers: RouterPeer[];
  mdnsRemotes: MdnsRemote[];
  alsaSeq: MidiAlsaSeqEntry[];
  rawmidi: MidiRawmidiEntry[];
  onAfterAction: () => Promise<void> | void;
  onStatus: (msg: string) => void;
};

type EditorMode =
  | { kind: "add" }
  | {
      kind: "edit";
      sideA: string;
      sideB: string;
      direction: ConnectionDirection;
      enabled: boolean;
    };

export function ConnectionsTab({
  refreshIntervalMs,
  lastRefresh,
  liveConnections,
  savedConnections,
  alsaSubs,
  dbEnabled,
  highlightConnectionRowId,
  onOpenInDevices,
  rpc,
  peers,
  mdnsRemotes,
  alsaSeq,
  rawmidi,
  onAfterAction,
  onStatus,
}: Props) {
  const [editor, setEditor] = useState<EditorMode | null>(null);

  const endpoints = useMemo(
    () =>
      buildEndpoints({
        alsaSeq,
        rawmidi,
        mdnsRemotes,
        peers,
      }),
    [alsaSeq, rawmidi, mdnsRemotes, peers],
  );

  const liveAllRows = useMemo(() => {
    const alsaRows = buildAlsaSubscriptionConnections(
      parseAlsaSubscriptions(alsaSubs),
      peers,
    );
    const routerPeerPairs = new Set<string>();
    for (const r of liveConnections) {
      if (r.from.peerId !== undefined && r.to.peerId !== undefined) {
        const a = r.from.peerId;
        const b = r.to.peerId;
        routerPeerPairs.add(a < b ? `${a}:${b}` : `${b}:${a}`);
      }
    }
    const filteredAlsa = alsaRows.filter((r) => {
      const ap = r.from.peerId;
      const bp = r.to.peerId;
      if (ap === undefined || bp === undefined) return true;
      const k = ap < bp ? `${ap}:${bp}` : `${bp}:${ap}`;
      return !routerPeerPairs.has(k);
    });
    return [...liveConnections, ...filteredAlsa];
  }, [liveConnections, alsaSubs, peers]);

  const rows = useMemo(
    () =>
      dbEnabled
        ? mergeConnectionsWithPersisted(liveAllRows, savedConnections, peers)
        : annotateLiveOnly(liveAllRows),
    [dbEnabled, liveAllRows, savedConnections, peers],
  );

  const saveConnection = async (payload: {
    side_a: string;
    side_b: string;
    direction: ConnectionDirection;
    enabled: number;
  }) => {
    await rpc.call("connections.save", payload);
    setEditor(null);
    await onAfterAction();
    onStatus("");
  };

  const removeSaved = async (sideA: string, sideB: string) => {
    await rpc.call("connections.remove", { side_a: sideA, side_b: sideB });
    await onAfterAction();
    onStatus("");
  };

  const addRow = async (row: ConnectionRow) => {
    const sideA = row.from.stableId ?? row.from.endpointId;
    const sideB = row.to.stableId ?? row.to.endpointId;
    if (!sideA || !sideB || sideA === sideB) {
      onStatus("Cannot save this connection (no stable identity).");
      return;
    }
    const bidi = row.bidirectional ?? row.direction === "↔";
    await saveConnection({
      side_a: sideA,
      side_b: sideB,
      direction: bidi ? "both" : "a2b",
      enabled: 1,
    });
  };

  const removeRow = async (row: ConnectionRow) => {
    const sideA = row.persistedSideA ?? row.from.stableId;
    const sideB = row.persistedSideB ?? row.to.stableId;
    if (!sideA || !sideB) {
      onStatus("Cannot remove this connection (no persisted side ids).");
      return;
    }
    await removeSaved(sideA, sideB);
  };

  const toggleEnabled = async (row: ConnectionRow, enable: boolean) => {
    const sideA = row.persistedSideA ?? row.from.stableId;
    const sideB = row.persistedSideB ?? row.to.stableId;
    if (!sideA || !sideB) return;
    await rpc.call(enable ? "connections.enable" : "connections.disable", {
      side_a: sideA,
      side_b: sideB,
    });
    await onAfterAction();
    onStatus("");
  };

  const openEdit = (row: ConnectionRow) => {
    const sideA = row.persistedSideA ?? row.from.stableId ?? row.from.endpointId;
    const sideB = row.persistedSideB ?? row.to.stableId ?? row.to.endpointId;
    if (!sideA || !sideB) return;
    const saved = savedConnections.find(
      (s) =>
        (s.side_a === sideA && s.side_b === sideB) ||
        (s.side_a === sideB && s.side_b === sideA),
    );
    let direction: ConnectionDirection = saved?.direction ?? "both";
    if (saved && saved.side_a === sideB && saved.side_b === sideA) {
      if (direction === "a2b") direction = "b2a";
      else if (direction === "b2a") direction = "a2b";
    }
    setEditor({
      kind: "edit",
      sideA,
      sideB,
      direction,
      enabled: saved?.enabled !== false,
    });
  };

  return (
    <div class="space-y-4">
      <RefreshBanner
        refreshIntervalMs={refreshIntervalMs}
        lastRefresh={lastRefresh}
      />
      <Card title="Connections (router + ALSA aconnect)">
        {dbEnabled === true ? (
          <div class="mb-3 flex flex-wrap items-center gap-2">
            <Button type="button" onClick={() => setEditor({ kind: "add" })}>
              + Add saved connection
            </Button>
            <span class="font-mono text-[10px] ui-text-subtle">
              Saved connections auto-reconnect with the direction and matching
              rules you set. Click ★ on a live row to save quickly, or use the
              editor for direction and partial matching.
            </span>
          </div>
        ) : (
          <p class="mb-3 font-mono text-[11px] ui-text-subtle">
            Enable <code class="ui-text">[database] path=…</code> in the daemon INI to
            save connection pairs here.
          </p>
        )}
        <ConnectionsTable
          rows={rows}
          highlightConnectionRowId={highlightConnectionRowId}
          onOpenInDevices={onOpenInDevices}
          dbEnabled={dbEnabled}
          refreshIntervalMs={refreshIntervalMs}
          onAddToDb={dbEnabled ? (r) => void addRow(r) : undefined}
          onRemoveFromDb={dbEnabled ? (r) => void removeRow(r) : undefined}
          onEditSaved={dbEnabled ? (r) => openEdit(r) : undefined}
          onToggleEnabled={
            dbEnabled
              ? (r, enable) => void toggleEnabled(r, enable)
              : undefined
          }
        />
      </Card>

      {editor !== null ? (
        <ConnectionEditorDialog
          title={editor.kind === "add" ? "Add saved connection" : "Edit saved connection"}
          endpoints={endpoints}
          initial={
            editor.kind === "add"
              ? {
                  sideA: "",
                  sideB: "",
                  direction: "both",
                  enabled: true,
                }
              : {
                  sideA: editor.sideA,
                  sideB: editor.sideB,
                  direction: editor.direction,
                  enabled: editor.enabled,
                }
          }
          onClose={() => setEditor(null)}
          onSave={saveConnection}
        />
      ) : null}
    </div>
  );
}
