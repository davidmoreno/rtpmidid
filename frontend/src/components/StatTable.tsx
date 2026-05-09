type Row = { k: string; v: string };

export function StatTable({ rows }: { rows: Row[] }) {
  return (
    <table class="ui-stat-table">
      <tbody>
        {rows.map((r) => (
          <tr key={r.k}>
            <th>{r.k}</th>
            <td>{r.v}</td>
          </tr>
        ))}
      </tbody>
    </table>
  );
}
