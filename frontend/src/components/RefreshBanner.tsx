type Props = {
  lastRefresh: Date | null;
};

export function RefreshBanner({ lastRefresh }: Props) {
  return (
    <div class="flex flex-wrap items-center justify-between gap-2 font-mono text-xs ui-text-muted">
      <span>
        Live updates via WebSocket — data updates automatically.
      </span>
      {lastRefresh && (
        <span class="tabular-nums">
          Last loaded:{" "}
          {lastRefresh.toLocaleTimeString([], {
            hour: "2-digit",
            minute: "2-digit",
            second: "2-digit",
            hour12: false,
          })}
        </span>
      )}
    </div>
  );
}
