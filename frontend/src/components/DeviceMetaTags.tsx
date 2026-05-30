import { formatLastSeen } from "../devicesList";
import type { DeviceStatusTag } from "../mergeDeviceList";

type Props = {
  sourceTag: string | null;
  statusTag: DeviceStatusTag;
  lastSeen?: number;
  /** When offline-only, show last seen inline. */
  showLastSeen?: boolean;
};

function StatusTag({ status }: { status: DeviceStatusTag }) {
  const online = status === "online";
  return (
    <span
      class={`inline-flex rounded px-1.5 py-0.5 font-mono text-[9px] font-black uppercase ${
        online
          ? "bg-emerald-500/20 text-emerald-700 dark:text-emerald-300"
          : "bg-zinc-500/15 ui-text-muted"
      }`}
    >
      {online ? "Online" : "Offline"}
    </span>
  );
}

function SourceTag({ label }: { label: string }) {
  return (
    <span class="inline-flex rounded border border-[color:var(--color-border)] px-1.5 py-0.5 font-mono text-[9px] font-black uppercase ui-text-muted">
      {label}
    </span>
  );
}

export function DeviceMetaTags({
  sourceTag,
  statusTag,
  lastSeen,
  showLastSeen = false,
}: Props) {
  return (
    <>
      <StatusTag status={statusTag} />
      {sourceTag ? <SourceTag label={sourceTag} /> : null}
      {showLastSeen && lastSeen ? (
        <span
          class="font-mono text-[9px] ui-text-subtle"
          title="Last time this device was seen online"
        >
          seen {formatLastSeen(lastSeen)}
        </span>
      ) : null}
    </>
  );
}
