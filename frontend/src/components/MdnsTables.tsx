import {
  groupMdnsAnnouncements,
  groupMdnsRemotes,
  type MdnsAnnouncement,
  type MdnsAnnouncementGroup,
  type MdnsRemote,
  type MdnsRemoteGroup,
} from "../model";
import { useFixedTooltip } from "./FixedTooltipPortal";

type Props = {
  status: string;
  announcements: MdnsAnnouncement[];
  remotes: MdnsRemote[];
};

/** Host:port or [ipv6]:port for copy/paste. */
function connectEndpoint(ip: string, port: number | string): string {
  const p = String(port);
  const trimmed = ip.trim();
  if (!trimmed) return "";
  if (trimmed.includes(":") && !trimmed.startsWith("[")) {
    return `[${trimmed}]:${p}`;
  }
  return `${trimmed}:${p}`;
}

function rawDetailLines(raw: Record<string, unknown> | undefined): string[] {
  if (!raw) return [];
  const skip = new Set(["name", "hostname", "ip", "port"]);
  const out: string[] = [];
  for (const [k, v] of Object.entries(raw)) {
    if (skip.has(k)) continue;
    if (v === null || v === undefined) continue;
    out.push(`${k}: ${typeof v === "object" ? JSON.stringify(v) : String(v)}`);
  }
  return out.sort();
}

function announcementTipContent(a: MdnsAnnouncementGroup) {
  return (
    <div class="space-y-1 text-zinc-800 dark:text-zinc-100">
      <div class="font-bold uppercase text-zinc-500 dark:text-zinc-400">
        Announcement
      </div>
      <div>
        <span class="text-zinc-500">Name:</span> {a.name}
      </div>
      <div>
        <span class="text-zinc-500">Port:</span> {a.port}
      </div>
      <div>
        <span class="text-zinc-500">Merged rows:</span> <strong>{a.count}</strong>
      </div>
    </div>
  );
}

function remoteTipContent(g: MdnsRemoteGroup) {
  return (
    <div class="space-y-1 text-zinc-800 dark:text-zinc-100">
      <div class="font-bold uppercase text-zinc-500 dark:text-zinc-400">Service</div>
      <div>
        <span class="text-zinc-500">Name:</span> {g.name}
      </div>
      <div>
        <span class="text-zinc-500">UDP port:</span> {g.port}
      </div>

      <div class="mt-2 font-bold uppercase text-emerald-800 dark:text-emerald-300">
        Connect (IP)
      </div>
      {g.ips.length > 0 ? (
        <ul class="mb-2 space-y-1 font-mono text-[11px]">
          {g.ips.map((ip) => (
            <li key={ip} class="break-all">
              <strong>{connectEndpoint(ip, g.port)}</strong>
            </li>
          ))}
        </ul>
      ) : (
        <p class="mb-2 text-[10px] text-amber-800 dark:text-amber-300">
          No resolved IP in status JSON (needs rtpmidid with mDNS IP field). Fallback:{" "}
          <span class="font-mono font-bold">
            {g.addresses[0]
              ? `${g.addresses[0]}:${g.port}`
              : `(hostname):${g.port}`}
          </span>
        </p>
      )}

      <div class="font-bold text-zinc-500 dark:text-zinc-400">mDNS hostnames</div>
      <ul class="mb-2 list-inside list-disc space-y-0.5 text-[10px]">
        {g.addresses.length ? (
          g.addresses.map((h) => (
            <li key={h} class="break-all">
              {h}
            </li>
          ))
        ) : (
          <li class="text-zinc-500">—</li>
        )}
      </ul>

      <div class="mb-1 font-bold text-zinc-500 dark:text-zinc-400">Resolver rows</div>
      <ul class="space-y-2 border-t border-zinc-200 pt-1 dark:border-zinc-700">
        {g.instances.map((inst, i) => (
          <li key={`${inst.ip}-${inst.hostname}-${i}`} class="break-all text-[10px]">
            {inst.ip.trim() ? (
              <div>
                <span class="text-zinc-500">IP:</span>{" "}
                <strong class="font-mono">
                  {connectEndpoint(inst.ip.trim(), inst.port)}
                </strong>
              </div>
            ) : null}
            <div>
              <span class="text-zinc-500">Host:</span>{" "}
              <span class="font-mono">{inst.hostname || "—"}</span>
            </div>
            {rawDetailLines(inst.raw).length > 0 && (
              <pre class="mt-0.5 whitespace-pre-wrap pl-2 text-[9px] text-zinc-600 dark:text-zinc-400">
                {rawDetailLines(inst.raw).join("\n")}
              </pre>
            )}
          </li>
        ))}
      </ul>
    </div>
  );
}

export function MdnsTables({ status, announcements, remotes }: Props) {
  const annGroups = groupMdnsAnnouncements(announcements);
  const remoteGroups = groupMdnsRemotes(remotes);
  const tt = useFixedTooltip();

  return (
    <div class="grid gap-4 lg:grid-cols-2">
      <div>
        <p class="mb-2 font-mono text-xs font-bold uppercase text-zinc-600 dark:text-zinc-400">
          mDNS status:{" "}
          <span class="text-zinc-900 dark:text-zinc-100">{status}</span>
        </p>
        <h3 class="mb-1 font-mono text-xs font-bold uppercase">
          Local announcements
        </h3>
        <p class="mb-1 font-mono text-[10px] text-zinc-500">
          Grouped by name and port. Hover a row for merged count and details
          (fixed layer, not clipped).
        </p>
        <div class="overflow-x-auto border-2 border-zinc-900 dark:border-zinc-100">
          <table class="w-full border-collapse text-left font-mono text-xs">
            <thead>
              <tr class="bg-zinc-200 dark:bg-zinc-800">
                <th class="border-b border-zinc-900 px-2 py-1 dark:border-zinc-100">
                  Name
                </th>
                <th class="border-b border-zinc-900 px-2 py-1 dark:border-zinc-100">
                  Port
                </th>
                <th class="border-b border-zinc-900 px-2 py-1 dark:border-zinc-100">
                  #
                </th>
              </tr>
            </thead>
            <tbody>
              {annGroups.length === 0 ? (
                <tr>
                  <td colSpan={3} class="px-2 py-2 text-zinc-500">
                    None
                  </td>
                </tr>
              ) : (
                annGroups.map((a) => (
                  <tr
                    key={`${a.name}-${String(a.port)}`}
                    class="cursor-default border-b border-zinc-200 odd:bg-white even:bg-zinc-50 dark:border-zinc-700 dark:odd:bg-zinc-950 dark:even:bg-zinc-900/80"
                    onMouseEnter={(e) =>
                      tt.show(e.currentTarget as HTMLElement, announcementTipContent(a))
                    }
                    onMouseLeave={tt.scheduleHide}
                  >
                    <td class="max-w-[16rem] truncate px-2 py-1">{a.name}</td>
                    <td class="px-2 py-1 tabular-nums">{a.port}</td>
                    <td class="px-2 py-1 tabular-nums">{a.count}</td>
                  </tr>
                ))
              )}
            </tbody>
          </table>
        </div>
      </div>
      <div>
        <h3 class="mb-1 font-mono text-xs font-bold uppercase">
          Discovered remotes
        </h3>
        <p class="mb-1 font-mono text-[10px] text-zinc-500">
          Grouped by service name and port. Hover for resolved IPs (how to
          connect), mDNS hostnames, and raw fields.
        </p>
        <div class="overflow-x-auto border-2 border-zinc-900 dark:border-zinc-100">
          <table class="w-full border-collapse text-left font-mono text-xs">
            <thead>
              <tr class="bg-zinc-200 dark:bg-zinc-800">
                <th class="border-b border-zinc-900 px-2 py-1 dark:border-zinc-100">
                  Name
                </th>
                <th class="border-b border-zinc-900 px-2 py-1 dark:border-zinc-100">
                  Port
                </th>
                <th class="border-b border-zinc-900 px-2 py-1 dark:border-zinc-100">
                  IPs
                </th>
              </tr>
            </thead>
            <tbody>
              {remoteGroups.length === 0 ? (
                <tr>
                  <td colSpan={3} class="px-2 py-2 text-zinc-500">
                    None
                  </td>
                </tr>
              ) : (
                remoteGroups.map((g) => (
                  <tr
                    key={`${g.name}-${String(g.port)}`}
                    class="cursor-default border-b border-zinc-200 odd:bg-white even:bg-zinc-50 dark:border-zinc-700 dark:odd:bg-zinc-950 dark:even:bg-zinc-900/80"
                    onMouseEnter={(e) =>
                      tt.show(e.currentTarget as HTMLElement, remoteTipContent(g))
                    }
                    onMouseLeave={tt.scheduleHide}
                  >
                    <td class="max-w-[14rem] truncate px-2 py-1">{g.name}</td>
                    <td class="px-2 py-1 tabular-nums">{g.port}</td>
                    <td class="px-2 py-1 tabular-nums">
                      {g.ips.length > 0 ? g.ips.length : g.instances.length}
                    </td>
                  </tr>
                ))
              )}
            </tbody>
          </table>
        </div>
      </div>
      {tt.portal}
    </div>
  );
}
