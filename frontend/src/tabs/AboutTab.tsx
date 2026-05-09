import { Card } from "../components/Card";
import { RefreshBanner } from "../components/RefreshBanner";
import { StatTable } from "../components/StatTable";

type Props = {
  refreshIntervalMs: number;
  lastRefresh: Date | null;
  statsRows: { k: string; v: string }[];
};

export function AboutTab({
  refreshIntervalMs,
  lastRefresh,
  statsRows,
}: Props) {
  return (
    <div class="space-y-4">
      <RefreshBanner
        refreshIntervalMs={refreshIntervalMs}
        lastRefresh={lastRefresh}
      />
      <Card title="About this UI">
        <p class="mb-3 font-mono text-xs leading-relaxed ui-text-muted">
          Web dashboard for <strong class="ui-text">rtpmidid</strong>: live
          connections, router peers, RTP view, and mDNS discovery. Commands are sent
          over JSON-RPC on a WebSocket to the daemon.
        </p>
        <StatTable rows={statsRows} />
        <p class="mt-3 font-mono text-xs ui-text-muted">
          Latency bars use a shared piecewise scale and color tiers in{" "}
          <code class="ui-code">latencyScale.ts</code>. Each row shows one bar for
          the <strong class="ui-text">sum</strong> of available samples; hover the bar
          for a per-metric breakdown.
        </p>
      </Card>
    </div>
  );
}
