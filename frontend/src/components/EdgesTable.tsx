import type { EdgeRow } from "../model";

type Props = { edges: EdgeRow[] };

export function EdgesTable({ edges }: Props) {
  return (
    <div class="overflow-x-auto border-2 border-zinc-900 dark:border-zinc-100">
      <table class="w-full min-w-[32rem] border-collapse text-left font-mono text-xs">
        <thead>
          <tr class="bg-zinc-200 dark:bg-zinc-800">
            <th class="border-b border-zinc-900 px-2 py-2 dark:border-zinc-100">From</th>
            <th class="border-b border-zinc-900 px-2 py-2 dark:border-zinc-100">To</th>
          </tr>
        </thead>
        <tbody>
          {edges.length === 0 ? (
            <tr>
              <td colSpan={2} class="px-2 py-3 text-zinc-500">
                No router edges (empty send_to).
              </td>
            </tr>
          ) : (
            edges.map((e) => (
              <tr
                key={`${e.fromId}-${e.toId}`}
                class="border-b border-zinc-200 odd:bg-white even:bg-zinc-50 dark:border-zinc-700 dark:odd:bg-zinc-950 dark:even:bg-zinc-900/80"
              >
                <td class="px-2 py-1.5">
                  <span class="font-bold tabular-nums">{e.fromId}</span>{" "}
                  <span class="text-zinc-700 dark:text-zinc-300">{e.fromName}</span>
                </td>
                <td class="px-2 py-1.5">
                  <span class="font-bold tabular-nums">{e.toId}</span>{" "}
                  <span class="text-zinc-700 dark:text-zinc-300">{e.toName}</span>
                </td>
              </tr>
            ))
          )}
        </tbody>
      </table>
    </div>
  );
}
