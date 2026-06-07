import { Card } from "../components/Card";
import { MdnsTables } from "../components/MdnsTables";
import { RefreshBanner } from "../components/RefreshBanner";
import type { MdnsAnnouncement, MdnsRemote } from "../model";
import type { WireLocalChoice } from "../midiEnumerate";
import type { RpcClient } from "../rpc";

type Props = {
  lastRefresh: Date | null;
  status: string;
  announcements: MdnsAnnouncement[];
  remotes: MdnsRemote[];
  rpc: RpcClient;
  onWireMdnsToLocal?: (args: {
    serviceName: string;
    target: string;
    port: number | string;
    local: WireLocalChoice;
  }) => Promise<void>;
};

export function MdnsTab({
  lastRefresh,
  status,
  announcements,
  remotes,
  rpc,
  onWireMdnsToLocal,
}: Props) {
  return (
    <div class="space-y-4">
      <RefreshBanner lastRefresh={lastRefresh} />
      <Card title="mDNS">
        <MdnsTables
          status={status}
          announcements={announcements}
          remotes={remotes}
          rpc={rpc}
          onWireMdnsToLocal={onWireMdnsToLocal}
        />
      </Card>
    </div>
  );
}
