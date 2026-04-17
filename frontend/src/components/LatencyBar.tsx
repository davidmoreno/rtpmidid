import type { LatencyTriple } from "../model";

type Props = {
  label: string;
  triple?: LatencyTriple;
  /** Value at 100% bar width (ms) */
  capMs: number;
  which?: "last" | "average";
};

export function LatencyBar({ label, triple, capMs, which = "last" }: Props) {
  const v = triple?.[which];
  const ms = typeof v === "number" && !Number.isNaN(v) ? v : null;
  const pct =
    ms === null || capMs <= 0 ? 0 : Math.min(100, Math.max(0, (ms / capMs) * 100));
  const text = ms === null ? "—" : `${ms.toFixed(2)} ms`;

  return (
    <div class="mb-2 font-mono text-xs">
      <div class="mb-0.5 flex justify-between gap-2">
        <span class="text-zinc-600 dark:text-zinc-400">{label}</span>
        <span class="shrink-0 font-bold tabular-nums">{text}</span>
      </div>
      <div class="h-2.5 border-2 border-zinc-900 bg-zinc-200 dark:border-zinc-100 dark:bg-zinc-800">
        <div
          class="h-full bg-amber-500 transition-[width] duration-300 dark:bg-amber-400"
          style={{ width: `${pct}%` }}
        />
      </div>
      {triple?.average !== undefined && which === "last" && (
        <div class="mt-0.5 text-[10px] text-zinc-500">
          avg {triple.average.toFixed(2)} ms · σ{" "}
          {(triple.stddev ?? 0).toFixed(2)} ms
        </div>
      )}
    </div>
  );
}
