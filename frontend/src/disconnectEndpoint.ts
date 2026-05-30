import type { RpcClient } from "./rpc";

export type DisconnectEndpointOptions = {
  fromIdentity: string;
  toIdentity: string;
  fromPeerId?: number;
  toPeerId?: number;
  fromLabel?: string;
  toLabel?: string;
  /** When true, also drop the pair from the persisted connections DB. */
  connectionsDbEnabled?: boolean;
};

/**
 * Disconnect two endpoints: prefer router peer ids when known, else identity RPC.
 * Optionally removes the remembered (★) connection row from the database.
 */
export async function disconnectEndpoints(
  rpc: RpcClient,
  onStatus: (msg: string) => void,
  onAfterAction: () => Promise<void> | void,
  opts: DisconnectEndpointOptions,
): Promise<void> {
  const {
    fromIdentity,
    toIdentity,
    fromPeerId,
    toPeerId,
    fromLabel,
    toLabel,
    connectionsDbEnabled,
  } = opts;

  if (!fromIdentity.trim() || !toIdentity.trim()) {
    onStatus("Disconnect failed: missing endpoint identity");
    return;
  }

  try {
    onStatus("");
    const canRouter =
      fromPeerId !== undefined &&
      toPeerId !== undefined &&
      Number.isFinite(fromPeerId) &&
      Number.isFinite(toPeerId);

    if (canRouter) {
      await rpc.call("router.disconnect", { from: fromPeerId, to: toPeerId });
      await rpc.call("router.disconnect", { from: toPeerId, to: fromPeerId });
    } else {
      await rpc.call("endpoint.disconnect", {
        from: fromIdentity,
        to: toIdentity,
      });
    }

    if (connectionsDbEnabled) {
      try {
        await rpc.call("connections.remove", {
          side_a: fromIdentity,
          side_b: toIdentity,
        });
      } catch {
        /* Pair may use different stored query strings; router disconnect may have removed it. */
      }
    }

    await onAfterAction();
    const a = fromLabel?.trim() || fromIdentity;
    const b = toLabel?.trim() || toIdentity;
    onStatus(`Disconnected ${a} ↔ ${b}`);
  } catch (e) {
    onStatus(`Disconnect failed: ${e}`);
  }
}
