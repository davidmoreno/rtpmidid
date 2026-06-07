import { PeersCards } from "../components/PeersCards";
import { RefreshBanner } from "../components/RefreshBanner";
import type { MdnsRemote, RouterPeer } from "../model";
import type { MidiAlsaSeqEntry, MidiRawmidiEntry } from "../midiEnumerate";
import type { RpcClient } from "../rpc";
import type { RegistryDevice } from "../devicesList";

type Props = {
  lastRefresh: Date | null;
  peers: RouterPeer[];
  mdnsRemotes: MdnsRemote[];
  alsaSeq: MidiAlsaSeqEntry[];
  rawmidi: MidiRawmidiEntry[];
  alsaSubs: unknown[];
  registryDevices: RegistryDevice[];
  registryEnabled: boolean;
  connectionsDbEnabled?: boolean;
  highlightEndpointId?: string | null;
  rpc: RpcClient;
  onAfterAction: () => Promise<void> | void;
  onStatus: (msg: string) => void;
};

export function DevicesTab({
  lastRefresh,
  peers,
  mdnsRemotes,
  alsaSeq,
  rawmidi,
  alsaSubs,
  registryDevices,
  registryEnabled,
  connectionsDbEnabled = false,
  highlightEndpointId,
  rpc,
  onAfterAction,
  onStatus,
}: Props) {
  return (
    <div class="space-y-4">
      <RefreshBanner lastRefresh={lastRefresh} />
      <PeersCards
        peers={peers}
        mdnsRemotes={mdnsRemotes}
        alsaSeq={alsaSeq}
        rawmidi={rawmidi}
        alsaSubs={alsaSubs}
        registryDevices={registryDevices}
        registryEnabled={registryEnabled}
        connectionsDbEnabled={connectionsDbEnabled}
        highlightEndpointId={highlightEndpointId}
        rpc={rpc}
        onAfterAction={onAfterAction}
        onStatus={onStatus}
      />
    </div>
  );
}
