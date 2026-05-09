import { Card } from "../components/Card";
import { ConnectionsTable } from "../components/ConnectionsTable";
import { RefreshBanner } from "../components/RefreshBanner";
import type { ConnectionRow } from "../model";

type Props = {
  refreshIntervalMs: number;
  lastRefresh: Date | null;
  connections: ConnectionRow[];
  highlightConnectionRowId: string | null;
  onSelectPeer: (id: number) => void;
};

export function ConnectionsTab({
  refreshIntervalMs,
  lastRefresh,
  connections,
  highlightConnectionRowId,
  onSelectPeer,
}: Props) {
  return (
    <div class="space-y-4">
      <RefreshBanner
        refreshIntervalMs={refreshIntervalMs}
        lastRefresh={lastRefresh}
      />
      <Card title="Connections (router + RTP)">
        <ConnectionsTable
          rows={connections}
          highlightConnectionRowId={highlightConnectionRowId}
          onSelectPeer={onSelectPeer}
        />
      </Card>
    </div>
  );
}
