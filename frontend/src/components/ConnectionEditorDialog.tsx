import { useMemo, useState } from "preact/hooks";
import { Button } from "./Button";
import { EndpointPickerDialog } from "./EndpointPickerDialog";
import { StoredQueryEditor } from "./StoredQueryEditor";
import type { Endpoint } from "../endpoints";
import {
  directionArrow,
  directionLabel,
  type ConnectionDirection,
} from "../deviceIdentity";
import { loadDeviceFavoriteIds } from "../deviceFavorites";
import { useEscapeKey } from "../useEscapeKey";

export type ConnectionEditorInitial = {
  sideA: string;
  sideB: string;
  direction: ConnectionDirection;
};

type Props = {
  title: string;
  endpoints: Endpoint[];
  initial: ConnectionEditorInitial;
  onClose: () => void;
  onSave: (payload: {
    side_a: string;
    side_b: string;
    direction: ConnectionDirection;
    enabled: number;
  }) => Promise<void>;
};

type PickTarget = "a" | "b" | null;

function endpointForIdentity(endpoints: Endpoint[], identity: string): Endpoint | null {
  if (!identity.trim()) return null;
  return endpoints.find((e) => e.identity === identity) ?? null;
}

export function ConnectionEditorDialog({
  title,
  endpoints,
  initial,
  onClose,
  onSave,
}: Props) {
  const [sideA, setSideA] = useState(initial.sideA);
  const [sideB, setSideB] = useState(initial.sideB);
  const [direction, setDirection] = useState<ConnectionDirection>(initial.direction);
  const [picker, setPicker] = useState<PickTarget>(null);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState("");

  const favoriteIds = useMemo(() => loadDeviceFavoriteIds(), []);
  const preview = directionLabel(direction, sideA, sideB);

  useEscapeKey(() => {
    if (!busy) onClose();
  });

  const pickEndpoint = (id: string) => {
    if (picker === "a") setSideA(id);
    else if (picker === "b") setSideB(id);
    setPicker(null);
  };

  const commit = async () => {
    if (!sideA.trim() || !sideB.trim()) {
      setErr("Both sides are required.");
      return;
    }
    if (sideA === sideB) {
      setErr("Both sides must be different.");
      return;
    }
    setBusy(true);
    setErr("");
    try {
      await onSave({
        side_a: sideA,
        side_b: sideB,
        direction,
        enabled: 1,
      });
      onClose();
    } catch (e) {
      setErr(String(e));
    } finally {
      setBusy(false);
    }
  };

  return (
    <>
      <div
        class="ui-modal-backdrop"
        onClick={(e) => {
          if (e.target === e.currentTarget) onClose();
        }}
      >
        <div
          class="ui-modal max-h-[90vh] max-w-lg"
          role="dialog"
          aria-labelledby="conn-editor-title"
          onClick={(e) => e.stopPropagation()}
        >
          <h2
            id="conn-editor-title"
            class="mb-3 font-mono text-sm font-black uppercase ui-text"
          >
            {title}
          </h2>

          <StoredQueryEditor
            label="A"
            value={sideA}
            onChange={setSideA}
            onPickEndpoint={() => setPicker("a")}
            endpoint={endpointForIdentity(endpoints, sideA)}
          />

          <div class="mb-3">
            <span class="mb-1 block font-mono text-[10px] font-bold uppercase ui-text-muted">
              Direction
            </span>
            <div class="flex flex-wrap gap-2">
              {(
                [
                  ["a2b", "A → B"],
                  ["b2a", "B → A"],
                  ["both", "Both ways"],
                ] as const
              ).map(([dir, lbl]) => (
                <button
                  key={dir}
                  type="button"
                  class={`ui-btn-plain border px-3 py-1 font-mono text-[11px] font-bold ${
                    direction === dir
                      ? "border-[color:var(--color-ring-highlight)] ui-text"
                      : "border-[color:var(--color-border)] ui-text-muted"
                  }`}
                  onClick={() => setDirection(dir)}
                >
                  {lbl} {directionArrow(dir)}
                </button>
              ))}
            </div>
          </div>

          <StoredQueryEditor
            label="B"
            value={sideB}
            onChange={setSideB}
            onPickEndpoint={() => setPicker("b")}
            endpoint={endpointForIdentity(endpoints, sideB)}
          />

          <p class="ui-panel-bordered mb-3 px-2 py-1.5 font-mono text-[11px] ui-text-muted">
            Preview: {preview}
          </p>

          {err ? (
            <p class="mb-3 font-mono text-[11px] text-red-600">{err}</p>
          ) : null}

          <div class="flex flex-wrap gap-2">
            <Button type="button" disabled={busy} onClick={() => void commit()}>
              Save connection
            </Button>
            <Button type="button" onClick={onClose}>
              Cancel
            </Button>
          </div>
        </div>
      </div>

      {picker ? (
        <EndpointPickerDialog
          title={picker === "a" ? "Side A endpoint" : "Side B endpoint"}
          description="The endpoint identity is stored as a device query."
          endpoints={endpoints}
          excludeIds={
            picker === "a" && sideB
              ? [sideB]
              : picker === "b" && sideA
                ? [sideA]
                : []
          }
          favoriteIds={favoriteIds}
          confirmLabel="Use this endpoint"
          onClose={() => setPicker(null)}
          onConfirm={pickEndpoint}
        />
      ) : null}
    </>
  );
}
