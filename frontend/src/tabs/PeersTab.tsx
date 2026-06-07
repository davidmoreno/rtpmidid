import { Card } from "../components/Card";
import { EdgesTable } from "../components/EdgesTable";
import { PeersTable } from "../components/PeersTable";
import { RefreshBanner } from "../components/RefreshBanner";
import type { EdgeRow, RouterPeer } from "../model";

type Props = {
  lastRefresh: Date | null;
  peers: RouterPeer[];
  edges: EdgeRow[];
  highlightPeerId: number | null;
};

export function PeersTab({
  lastRefresh,
  peers,
  edges,
  highlightPeerId,
}: Props) {
  return (
    <div class="space-y-4">
      <RefreshBanner lastRefresh={lastRefresh} />
      <Card title="Peers">
        <PeersTable peers={peers} highlightPeerId={highlightPeerId} />
      </Card>
      <Card title="Router edges">
        <EdgesTable edges={edges} />
      </Card>
    </div>
  );
}
