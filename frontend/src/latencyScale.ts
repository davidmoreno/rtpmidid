/**
 * Single source of truth for latency bar scale, tick positions, and tier colors.
 * Adjust numbers here only; components map tiers to CSS classes.
 */

/** Upper bound of the bar scale (ms). */
export const LATENCY_BAR_MAX_MS = 10_000;

/**
 * Color tier by absolute latency (ms), not bar position.
 * white: sub-ms (<1ms), green <10ms, yellow <100ms, red ≥100ms.
 */
export type LatencyColorTier = "white" | "green" | "yellow" | "red";

/** Tier thresholds (ms). Edit these to change color breakpoints. */
export const LATENCY_TIER_MS = {
  /** Below this → white (excellent) */
  greenBelow: 1,
  /** Below this → green */
  yellowBelow: 10,
  /** Below this → yellow */
  redBelow: 100,
} as const;

/**
 * Piecewise linear bar mapping (approx. four visual regions):
 * 0–25%: 0→1 ms, 25–50%: 1→10 ms, 50–75%: 10→100 ms, 75–100%: log10 100→10000 ms.
 */
export function msToBarPosition(ms: number): number {
  if (!Number.isFinite(ms) || ms <= 0) return 0;
  const m = Math.min(ms, LATENCY_BAR_MAX_MS);
  if (m <= 1) return (m / 1) * 25;
  if (m <= 10) return 25 + ((m - 1) / 9) * 25;
  if (m <= 100) return 50 + ((m - 10) / 90) * 25;
  const lo = Math.log10(100);
  const hi = Math.log10(LATENCY_BAR_MAX_MS);
  const t = (Math.log10(Math.max(m, 100)) - lo) / (hi - lo);
  return 75 + Math.min(1, Math.max(0, t)) * 25;
}

export function latencyColorTier(ms: number | undefined | null): LatencyColorTier {
  if (ms === undefined || ms === null || !Number.isFinite(ms) || ms < 0) return "white";
  if (ms < LATENCY_TIER_MS.greenBelow) return "white";
  if (ms < LATENCY_TIER_MS.yellowBelow) return "green";
  if (ms < LATENCY_TIER_MS.redBelow) return "yellow";
  return "red";
}

/** Milestones (ms) for mini tick marks on the bar (subset may be shown in narrow cells). */
export const LATENCY_TICK_MS = [0.001, 0.01, 0.1, 1, 10, 100, 1_000, 10_000] as const;

export function tickPositionsPct(): { ms: number; pct: number }[] {
  return LATENCY_TICK_MS.map((ms) => ({ ms, pct: msToBarPosition(ms) }));
}

export function formatMs(ms: number | undefined | null): string {
  if (ms === undefined || ms === null || !Number.isFinite(ms)) return "—";
  if (ms < 0.01) return `${ms.toFixed(4)} ms`;
  if (ms < 1) return `${ms.toFixed(3)} ms`;
  if (ms < 10) return `${ms.toFixed(2)} ms`;
  if (ms < 100) return `${ms.toFixed(1)} ms`;
  return `${ms.toFixed(0)} ms`;
}
