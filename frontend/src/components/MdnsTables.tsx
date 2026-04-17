import type { MdnsAnnouncement, MdnsRemote } from "../model";

type Props = {
  status: string;
  announcements: MdnsAnnouncement[];
  remotes: MdnsRemote[];
};

export function MdnsTables({ status, announcements, remotes }: Props) {
  return (
    <div class="grid gap-4 lg:grid-cols-2">
      <div>
        <p class="mb-2 font-mono text-xs font-bold uppercase text-zinc-600 dark:text-zinc-400">
          mDNS status: <span class="text-zinc-900 dark:text-zinc-100">{status}</span>
        </p>
        <h3 class="mb-1 font-mono text-xs font-bold uppercase">Local announcements</h3>
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
              </tr>
            </thead>
            <tbody>
              {announcements.length === 0 ? (
                <tr>
                  <td colSpan={2} class="px-2 py-2 text-zinc-500">
                    None
                  </td>
                </tr>
              ) : (
                announcements.map((a, i) => (
                  <tr
                    key={`a-${i}-${String(a.name)}`}
                    class="border-b border-zinc-200 odd:bg-white even:bg-zinc-50 dark:border-zinc-700 dark:odd:bg-zinc-950 dark:even:bg-zinc-900/80"
                  >
                    <td class="px-2 py-1">{a.name}</td>
                    <td class="px-2 py-1 tabular-nums">{a.port}</td>
                  </tr>
                ))
              )}
            </tbody>
          </table>
        </div>
      </div>
      <div>
        <h3 class="mb-1 font-mono text-xs font-bold uppercase">Discovered remotes</h3>
        <div class="overflow-x-auto border-2 border-zinc-900 dark:border-zinc-100">
          <table class="w-full border-collapse text-left font-mono text-xs">
            <thead>
              <tr class="bg-zinc-200 dark:bg-zinc-800">
                <th class="border-b border-zinc-900 px-2 py-1 dark:border-zinc-100">
                  Name
                </th>
                <th class="border-b border-zinc-900 px-2 py-1 dark:border-zinc-100">
                  Host
                </th>
                <th class="border-b border-zinc-900 px-2 py-1 dark:border-zinc-100">
                  Port
                </th>
              </tr>
            </thead>
            <tbody>
              {remotes.length === 0 ? (
                <tr>
                  <td colSpan={3} class="px-2 py-2 text-zinc-500">
                    None
                  </td>
                </tr>
              ) : (
                remotes.map((r, i) => (
                  <tr
                    key={`r-${i}-${String(r.name)}`}
                    class="border-b border-zinc-200 odd:bg-white even:bg-zinc-50 dark:border-zinc-700 dark:odd:bg-zinc-950 dark:even:bg-zinc-900/80"
                  >
                    <td class="max-w-[12rem] truncate px-2 py-1">{r.name}</td>
                    <td class="max-w-[12rem] truncate px-2 py-1">{r.hostname}</td>
                    <td class="px-2 py-1 tabular-nums">{r.port}</td>
                  </tr>
                ))
              )}
            </tbody>
          </table>
        </div>
      </div>
    </div>
  );
}
