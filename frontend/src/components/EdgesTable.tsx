import type { EdgeRow } from "../model";

type Props = { edges: EdgeRow[] };

export function EdgesTable({ edges }: Props) {
  return (
    <div class="ui-table-shell">
      <table class="ui-table min-w-[32rem] text-left font-mono text-xs">
        <thead>
          <tr>
            <th class="ui-th px-2 py-2">From</th>
            <th class="ui-th px-2 py-2">To</th>
          </tr>
        </thead>
        <tbody>
          {edges.length === 0 ? (
            <tr>
              <td colSpan={2} class="px-2 py-3 ui-text-subtle">
                No router edges (empty send_to).
              </td>
            </tr>
          ) : (
            edges.map((e) => (
              <tr
                key={`${e.fromId}-${e.toId}`}
                class="ui-tr-zebra"
              >
                <td class="px-2 py-1.5">
                  <span class="font-bold tabular-nums">{e.fromId}</span>{" "}
                  <span class="ui-text-muted">{e.fromName}</span>
                </td>
                <td class="px-2 py-1.5">
                  <span class="font-bold tabular-nums">{e.toId}</span>{" "}
                  <span class="ui-text-muted">{e.toName}</span>
                </td>
              </tr>
            ))
          )}
        </tbody>
      </table>
    </div>
  );
}
