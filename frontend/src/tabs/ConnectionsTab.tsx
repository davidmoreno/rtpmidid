import { useMemo, useState } from "preact/hooks";
import { Card } from "../components/Card";
import { ConnectionsTable } from "../components/ConnectionsTable";
import { EndpointPickerDialog } from "../components/EndpointPickerDialog";
import { RefreshBanner } from "../components/RefreshBanner";
import { Button } from "../components/Button";
import type { ConnectionRow, MdnsRemote, RouterPeer } from "../model";
import type { MidiAlsaSeqEntry, MidiRawmidiEntry } from "../midiEnumerate";
import type { RpcClient } from "../rpc";
import { buildEndpoints } from "../endpoints";
import { loadDeviceFavoriteIds } from "../deviceFavorites";
import {
  mergeConnectionsWithPersisted,
  type PersistedConnectionRow,
} from "../persistedConnections";

type Props = {
  refreshIntervalMs: number;
  lastRefresh: Date | null;
  liveConnections: ConnectionRow[];
  savedConnections: PersistedConnectionRow[];
  dbEnabled: boolean;
  highlightConnectionRowId: string | null;
  onSelectPeer: (id: number) => void;
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
  dbEnabled,
  highlightConnectionRowId,
  onSelectPeer,
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

  const rows = useMemo(
    () =>
      dbEnabled
        ? mergeConnectionsWithPersisted(liveConnections, savedConnections, peers)
        : liveConnections,
    [dbEnabled, liveConnections, savedConnections, peers],
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

  return (
    <div class="space-y-4">
      <RefreshBanner
        refreshIntervalMs={refreshIntervalMs}
        lastRefresh={lastRefresh}
      />
      <Card title="Connections (router + RTP)">
        {dbEnabled === true ? (
          <div class="mb-3 flex flex-wrap items-center gap-2">
            <Button type="button" onClick={startAdd} title="Add saved connection">
              + Add
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
                Saved pairs persist across restarts; grey names are offline endpoints.
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
          onSelectPeer={onSelectPeer}
          dbEnabled={dbEnabled}
          onRemoveSaved={
            dbEnabled
              ? (sideA, sideB) => {
                  void removeSaved(sideA, sideB);
                }
              : undefined
          }
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
