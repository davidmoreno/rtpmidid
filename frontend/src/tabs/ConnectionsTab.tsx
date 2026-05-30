import { useMemo, useState } from "preact/hooks";
import { Card } from "../components/Card";
import { ConnectionsTable } from "../components/ConnectionsTable";
import { EndpointPickerDialog } from "../components/EndpointPickerDialog";
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
import { loadDeviceFavoriteIds } from "../deviceFavorites";
import {
  annotateLiveOnly,
  mergeConnectionsWithPersisted,
  type PersistedConnectionRow,
} from "../persistedConnections";

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

type PickerTarget = "a" | "b" | null;

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
  const [picker, setPicker] = useState<PickerTarget>(null);
  const [addSideA, setAddSideA] = useState<string | null>(null);
  const [addSideB, setAddSideB] = useState<string | null>(null);
  const [adding, setAdding] = useState(false);

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

  const favoriteIds = useMemo(() => loadDeviceFavoriteIds(), []);

  /* Merge router rows + pure ALSA aconnect rows. The ALSA rows are dropped
     when they exactly match an existing router edge (peer_device_alsa_seq_t hangs
     off the same ALSA port and the router edge already shows the traffic). */
  const liveAllRows = useMemo(() => {
    const alsaRows = buildAlsaSubscriptionConnections(
      parseAlsaSubscriptions(alsaSubs),
      peers,
    );
    /* Hide alsa rows that are already represented by a router row connecting
       the two backing peers, to avoid duplicate display. */
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

  const startAdd = () => {
    setAddSideA(null);
    setAddSideB(null);
    setAdding(true);
    setPicker("a");
  };

  const cancelAdd = () => {
    setAdding(false);
    setPicker(null);
    setAddSideA(null);
    setAddSideB(null);
  };

  const commitAdd = async () => {
    if (!addSideA || !addSideB) {
      onStatus("Pick both sides before saving.");
      return;
    }
    try {
      await rpc.call("connections.add", { side_a: addSideA, side_b: addSideB });
      cancelAdd();
      await onAfterAction();
      onStatus("");
    } catch (e) {
      onStatus(String(e));
    }
  };

  const removeSaved = async (sideA: string, sideB: string) => {
    try {
      await rpc.call("connections.remove", { side_a: sideA, side_b: sideB });
      await onAfterAction();
      onStatus("");
    } catch (e) {
      onStatus(String(e));
    }
  };

  /* Row-level "+" button: prefer stableId (avoids relying on the daemon
     resolving `peer:N` -> stable id when a row's peer was just torn down). */
  const addRow = async (row: ConnectionRow) => {
    const sideA = row.from.stableId ?? row.from.endpointId;
    const sideB = row.to.stableId ?? row.to.endpointId;
    if (!sideA || !sideB || sideA === sideB) {
      onStatus("Cannot save this connection (no stable identity).");
      return;
    }
    try {
      await rpc.call("connections.add", { side_a: sideA, side_b: sideB });
      await onAfterAction();
      onStatus("");
    } catch (e) {
      onStatus(String(e));
    }
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

  return (
    <div class="space-y-4">
      <RefreshBanner
        refreshIntervalMs={refreshIntervalMs}
        lastRefresh={lastRefresh}
      />
      <Card title="Connections (router + ALSA aconnect)">
        {dbEnabled === true ? (
          <div class="mb-3 flex flex-wrap items-center gap-2">
            <Button type="button" onClick={startAdd}>
              + Add saved pair
            </Button>
            {adding ? (
              <>
                <span class="font-mono text-[11px] ui-text-muted">
                  {addSideA ? `A: ${addSideA}` : "Pick side A…"}
                  {" · "}
                  {addSideB ? `B: ${addSideB}` : "Pick side B…"}
                </span>
                <Button
                  type="button"
                  disabled={!addSideA || !addSideB}
                  onClick={() => void commitAdd()}
                >
                  Save
                </Button>
                <Button type="button" onClick={cancelAdd}>
                  Cancel
                </Button>
              </>
            ) : (
              <span class="font-mono text-[10px] ui-text-subtle">
                Saved pairs persist across restarts. Use the row "+" to save a
                live connection, or pick two endpoints to add a pair that does
                not yet exist (grey names are offline endpoints).
              </span>
            )}
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
        />
      </Card>

      {picker !== null && adding ? (
        <EndpointPickerDialog
          title={picker === "a" ? "Pick side A" : "Pick side B"}
          description="Endpoint is stored as a stable id in the database."
          endpoints={endpoints}
          excludeIds={
            picker === "a" && addSideB
              ? [addSideB]
              : picker === "b" && addSideA
                ? [addSideA]
                : []
          }
          favoriteIds={favoriteIds}
          confirmLabel={picker === "a" && !addSideB ? "Next: side B" : "Select"}
          onClose={() => {
            if (picker === "b" || addSideA) setPicker(null);
            else cancelAdd();
          }}
          onConfirm={(id) => {
            if (picker === "a") {
              setAddSideA(id);
              setPicker("b");
            } else {
              setAddSideB(id);
              setPicker(null);
            }
          }}
        />
      ) : null}
    </div>
  );
}
