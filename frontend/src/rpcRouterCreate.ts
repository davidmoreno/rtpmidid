import type { RpcClient } from "./rpc";
import { identityFromForm, serializeIdentity } from "./deviceIdentity";

/** Wire `type` keys from Actions tab JSON → identity type prefix. */
const WIRE_TYPE_TO_PREFIX: Record<string, string> = {
  local_rawmidi_t: "rawmidi",
  peer_device_rtpmidi_client_t: "rtpmidi_client",
  peer_export_rtpmidi_server_t: "rtpmidi_server",
  peer_device_alsa_seq_t: "alsa_seq",
};

function legacyPayloadToValues(
  wireType: string,
  payload: Record<string, unknown>,
): Record<string, string> {
  switch (wireType) {
    case "local_rawmidi_t":
      return {
        device: String(payload.device ?? ""),
        name: String(payload.name ?? ""),
      };
    case "peer_device_rtpmidi_client_t":
      return {
        hostname: String(payload.hostname ?? ""),
        port: String(payload.port ?? "5004"),
        service: String(payload.name ?? payload.service ?? ""),
      };
    case "peer_export_rtpmidi_server_t":
      return {
        name: String(payload.name ?? ""),
        port: String(payload.udp_port ?? payload.port ?? "5004"),
      };
    case "peer_device_alsa_seq_t":
      return {
        client: String(payload.alsa_client ?? payload.client ?? ""),
        port: String(payload.alsa_port ?? payload.port ?? ""),
        name: String(payload.name ?? ""),
      };
    default:
      return {};
  }
}

/** Create a peer via unified `router.create { identity }`. */
export async function rpcCallRouterCreatePayload(
  rpc: RpcClient,
  payload: Record<string, unknown>,
): Promise<void> {
  if (typeof payload.identity === "string" && payload.identity.trim()) {
    await rpc.call("router.create", { identity: payload.identity.trim() });
    return;
  }

  const t = payload.type;
  if (typeof t !== "string") {
    throw new Error(
      'router create JSON must include an "identity" string or legacy "type" field',
    );
  }
  const prefix = WIRE_TYPE_TO_PREFIX[t];
  if (!prefix) {
    throw new Error(`Unknown router.create type: ${t}`);
  }
  const { type: _drop, ...rest } = payload;
  const values = legacyPayloadToValues(t, rest);
  const parsed = identityFromForm(prefix, values);
  if (!parsed) {
    throw new Error("Incomplete fields for peer identity");
  }
  await rpc.call("router.create", {
    identity: serializeIdentity(parsed),
  });
}
