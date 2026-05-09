import { useEffect, useState } from "preact/hooks";
import { Button } from "../components/Button";
import { Card } from "../components/Card";
import type { RpcClient } from "../rpc";
import { rpcCallRouterCreatePayload } from "../rpcRouterCreate";

type Props = {
  rpc: RpcClient;
  onRefresh: () => Promise<void>;
  onStatus: (msg: string) => void;
};

export function ActionsTab({ rpc, onRefresh, onStatus }: Props) {
  const [authUser, setAuthUser] = useState("");
  const [authPass, setAuthPass] = useState("");
  const [connectHost, setConnectHost] = useState("");
  const [connectPort, setConnectPort] = useState("5004");
  const [connectName, setConnectName] = useState("");
  const [createJson, setCreateJson] = useState(
    '{"type":"network_rtpmidi_client_t","name":"Remote","hostname":"127.0.0.1","port":5004}',
  );
  const [fromId, setFromId] = useState("");
  const [toId, setToId] = useState("");

  useEffect(() => {
    rpc.setAuth(authUser, authPass);
  }, [authUser, authPass, rpc]);

  return (
    <div class="grid max-w-3xl gap-4">
      <Card title="Auth (reload after change)">
        <p class="mb-2 font-mono text-xs ui-text-muted">
          If the daemon has username/password set, enter them here and reload the
          page.
        </p>
        <label class="mb-2 block font-mono text-xs ui-text">
          Username
          <input
            class="ui-input mt-1 text-xs"
            value={authUser}
            onInput={(e) => setAuthUser((e.target as HTMLInputElement).value)}
          />
        </label>
        <label class="mb-2 block font-mono text-xs ui-text">
          Password
          <input
            type="password"
            class="ui-input mt-1 text-xs"
            value={authPass}
            onInput={(e) => setAuthPass((e.target as HTMLInputElement).value)}
          />
        </label>
      </Card>
      <Card title="Connect (local_alsa_listener)">
        <p class="mb-2 font-mono text-xs ui-text-muted">
          Same as CLI: optional name, hostname, port.
        </p>
        <input
          placeholder="name (optional)"
          class="ui-input mb-2 text-xs"
          value={connectName}
          onInput={(e) => setConnectName((e.target as HTMLInputElement).value)}
        />
        <input
          placeholder="hostname"
          class="ui-input mb-2 text-xs"
          value={connectHost}
          onInput={(e) => setConnectHost((e.target as HTMLInputElement).value)}
        />
        <input
          placeholder="port"
          class="ui-input mb-2 text-xs"
          value={connectPort}
          onInput={(e) => setConnectPort((e.target as HTMLInputElement).value)}
        />
        <Button
          onClick={async () => {
            try {
              const params: Record<string, string> = {
                hostname: connectHost.trim(),
              };
              if (connectPort.trim() !== "") {
                params.port = connectPort.trim();
              }
              if (connectName.trim() !== "") {
                params.name = connectName.trim();
              }
              await rpc.call("connect", params);
              await onRefresh();
            } catch (e) {
              onStatus(String(e));
            }
          }}
        >
          connect
        </Button>
      </Card>
      <Card title="router.create (typed JSON)">
        <p class="mb-2 font-mono text-xs ui-text-muted">
          Use legacy shape{" "}
          <code class="ui-code">{`{"type":"local_rawmidi_t",...}`}</code> (type keys
          from <code class="ui-code">router.create.list</code>), or full JSON-RPC{" "}
          <code class="ui-code">{`{"method":"…","params":{…}}`}</code>.
        </p>
        <textarea
          class="ui-textarea mb-2 h-32"
          value={createJson}
          onInput={(e) =>
            setCreateJson((e.target as HTMLTextAreaElement).value)
          }
        />
        <Button
          onClick={async () => {
            try {
              const o = JSON.parse(createJson) as Record<string, unknown>;
              if (typeof o.method === "string") {
                await rpc.call(o.method, (o.params as object) ?? {});
              } else {
                await rpcCallRouterCreatePayload(rpc, o);
              }
              await onRefresh();
            } catch (e) {
              onStatus(String(e));
            }
          }}
        >
          send
        </Button>
      </Card>
      <Card title="router.connect">
        <input
          placeholder="from peer id"
          class="ui-input mb-2 text-xs"
          value={fromId}
          onInput={(e) => setFromId((e.target as HTMLInputElement).value)}
        />
        <input
          placeholder="to peer id"
          class="ui-input mb-2 text-xs"
          value={toId}
          onInput={(e) => setToId((e.target as HTMLInputElement).value)}
        />
        <Button
          onClick={async () => {
            try {
              await rpc.call("router.connect", {
                from: Number(fromId),
                to: Number(toId),
              });
              await onRefresh();
            } catch (e) {
              onStatus(String(e));
            }
          }}
        >
          connect peers
        </Button>
      </Card>
    </div>
  );
}
