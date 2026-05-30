import type { RpcClient } from "./rpc";

/** Legacy `router.create` `{ type, ... }` maps to split RPC methods. */
const ROUTER_CREATE_TYPE_TO_METHOD: Record<string, string> = {
  local_rawmidi_t: "router.create.local_rawmidi",
  peer_device_rtpmidi_client_t: "router.create.network_rtpmidi_client",
  peer_export_rtpmidi_server_t: "router.create.network_rtpmidi_listener",
  peer_device_alsa_seq_t: "router.create.local_alsa_peer",
};

export async function rpcCallRouterCreatePayload(
  rpc: RpcClient,
  payload: Record<string, unknown>,
): Promise<void> {
  const t = payload.type;
  if (typeof t !== "string") {
    throw new Error('router create JSON must include a string "type" field');
  }
  const method = ROUTER_CREATE_TYPE_TO_METHOD[t];
  if (!method) {
    throw new Error(`Unknown router.create type: ${t}`);
  }
  const { type: _drop, ...rest } = payload;
  await rpc.call(method, rest);
}
