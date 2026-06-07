/**
 * Browser-side latency time-series store.
 *
 * Each peer gets a circular buffer of {timestamp, ms} entries
 * pruned to the last 60 seconds. Data never leaves the browser.
 */

const HISTORY_WINDOW_MS = 60_000; // 1 minute
const MAX_SAMPLES = 300;

export interface LatencySample {
  timestamp: number; // Date.now()
  ms: number;
}

interface PeerHistory {
  samples: LatencySample[];
}

class LatencyHistoryStore {
  private peers = new Map<number, PeerHistory>();

  push(peerId: number, ms: number): void {
    const now = Date.now();
    let h = this.peers.get(peerId);
    if (!h) {
      h = { samples: [] };
      this.peers.set(peerId, h);
    }

    h.samples.push({ timestamp: now, ms });
    this.prune(peerId, h);
  }

  get(peerId: number): LatencySample[] {
    const h = this.peers.get(peerId);
    if (!h) return [];
    const cutoff = Date.now() - HISTORY_WINDOW_MS;
    return h.samples.filter((s) => s.timestamp >= cutoff);
  }

  /** Duration of recorded history in ms (0 if no data). */
  spanMs(peerId: number): number {
    const s = this.get(peerId);
    if (s.length < 2) return 0;
    const first = s[0].timestamp;
    const last = s[s.length - 1].timestamp;
    return last - first;
  }

  private prune(peerId: number, h: PeerHistory): void {
    const cutoff = Date.now() - HISTORY_WINDOW_MS;
    h.samples = h.samples.filter((s) => s.timestamp >= cutoff);
    if (h.samples.length > MAX_SAMPLES) {
      h.samples = h.samples.slice(-MAX_SAMPLES);
    }
    if (h.samples.length === 0) {
      this.peers.delete(peerId);
    }
  }
}

export const latencyHistory = new LatencyHistoryStore();
