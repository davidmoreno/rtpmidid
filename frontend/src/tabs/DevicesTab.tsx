import { PeersCards } from "../components/PeersCards";
import { RefreshBanner } from "../components/RefreshBanner";
import type { MdnsRemote, RouterPeer } from "../model";
import type { MidiAlsaSeqEntry, MidiRawmidiEntry } from "../midiEnumerate";
import type { RpcClient } from "../rpc";

type Props = {
  refreshIntervalMs: number;
  lastRefresh: Date | null;
  peers: RouterPeer[];
  mdnsRemotes: MdnsRemote[];
  alsaSeq: MidiAlsaSeqEntry[];
  rawmidi: MidiRawmidiEntry[];
  alsaSubs: unknown[];
  /** Endpoint id to scroll into view + highlight (driven by Connections click). */
  highlightEndpointId?: string | null;
  rpc: RpcClient;
  onAfterAction: () => Promise<void> | void;
  onStatus: (msg: string) => void;
};

export function DevicesTab({
  refreshIntervalMs,
  lastRefresh,
  peers,
  mdnsRemotes,
  alsaSeq,
  rawmidi,
  alsaSubs,
  highlightEndpointId,
  rpc,
  onAfterAction,
  onStatus,
}: Props) {
  return (
    <div class="space-y-4">
      <RefreshBanner
        refreshIntervalMs={refreshIntervalMs}
        lastRefresh={lastRefresh}
      />
      <PeersCards
        peers={peers}
        mdnsRemotes={mdnsRemotes}
        alsaSeq={alsaSeq}
        rawmidi={rawmidi}
        alsaSubs={alsaSubs}
        highlightEndpointId={highlightEndpointId}
        rpc={rpc}
        onAfterAction={onAfterAction}
        onStatus={onStatus}
      />
    </div>
  );
}
