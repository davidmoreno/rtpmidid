/**
 * Hook that pushes latency samples to the browser-side history store
 * on every daemon state update.
 */

import { useEffect } from "preact/hooks";
import { latencyHistory } from "../latencyHistory";
import type { RouterPeer } from "../model";
import { peerCombinedLatencyMs } from "./LatencyBar";

/**
 * Collect latency samples from daemon peers into the browser-side
 * history buffer. Call once near the top of the app tree.
 */
export function useLatencyCollector(peers: RouterPeer[]) {
  useEffect(() => {
    for (const peer of peers) {
      const ms = peerCombinedLatencyMs(peer);
      if (ms === null) continue;
      latencyHistory.push(peer.id, ms);
    }
  }, [peers]);
}
