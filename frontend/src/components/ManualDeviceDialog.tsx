import { useMemo, useState } from "preact/hooks";
import { Button } from "./Button";
import {
  DEVICE_TYPE_DEFS,
  identityFromForm,
  identityToFormValues,
  serializeIdentity,
} from "../deviceIdentity";

type Props = {
  title?: string;
  initialIdentity?: string;
  initialName?: string;
  onClose: () => void;
  onSave: (
    identity: string,
    displayName: string,
    previousIdentity?: string,
  ) => Promise<void>;
};

export function ManualDeviceDialog({
  title = "Add device",
  initialIdentity,
  initialName = "",
  onClose,
  onSave,
}: Props) {
  const seed = useMemo(
    () => (initialIdentity ? identityToFormValues(initialIdentity) : null),
    [initialIdentity],
  );
  const [typePrefix, setTypePrefix] = useState(
    () => seed?.typePrefix ?? DEVICE_TYPE_DEFS[0].typePrefix,
  );
  const [values, setValues] = useState<Record<string, string>>(
    () => seed?.values ?? {},
  );
  const [displayName, setDisplayName] = useState(initialName);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState("");

  const def = DEVICE_TYPE_DEFS.find((d) => d.typePrefix === typePrefix)!;
  const editing = !!initialIdentity;

  const commit = async () => {
    const parsed = identityFromForm(typePrefix, values);
    if (!parsed) {
      setErr("Fill in the required fields.");
      return;
    }
    setBusy(true);
    setErr("");
    try {
      const identity = serializeIdentity(parsed);
      await onSave(identity, displayName.trim(), initialIdentity);
      onClose();
    } catch (e) {
      setErr(String(e));
    } finally {
      setBusy(false);
    }
  };

  return (
    <div
      class="fixed inset-0 z-50 flex items-center justify-center bg-black/40 p-4"
      onClick={(e) => {
        if (e.target === e.currentTarget) onClose();
      }}
    >
      <div
        class="ui-card max-h-[90vh] w-full max-w-md overflow-y-auto p-4 shadow-lg"
        role="dialog"
        aria-labelledby="manual-device-title"
      >
        <h2
          id="manual-device-title"
          class="mb-3 font-mono text-sm font-black uppercase ui-text"
        >
          {title}
        </h2>
        <p class="mb-3 font-mono text-[11px] ui-text-muted">
          {editing
            ? "Update the stored identity or display name. Changing the identity replaces the registry entry."
            : "Register a device you expect to appear later, or pin a specific endpoint."}
        </p>

        <label class="mb-3 block">
          <span class="mb-1 block font-mono text-[10px] font-bold uppercase ui-text-muted">
            Device type
          </span>
          <select
            class="ui-input w-full font-mono text-sm"
            value={typePrefix}
            disabled={editing}
            onChange={(e) => {
              setTypePrefix((e.target as HTMLSelectElement).value);
              setValues({});
            }}
          >
            {DEVICE_TYPE_DEFS.map((d) => (
              <option key={d.typePrefix} value={d.typePrefix}>
                {d.label}
              </option>
            ))}
          </select>
        </label>

        {def.fields.map((f) => (
          <label key={f.key} class="mb-3 block">
            <span class="mb-1 block font-mono text-[10px] font-bold uppercase ui-text-muted">
              {f.label}
              {f.optional ? " (optional)" : ""}
            </span>
            <input
              type="text"
              class="ui-input w-full font-mono text-sm"
              placeholder={f.placeholder}
              value={values[f.key] ?? ""}
              onInput={(e) =>
                setValues((prev) => ({
                  ...prev,
                  [f.key]: (e.target as HTMLInputElement).value,
                }))
              }
            />
          </label>
        ))}

        <label class="mb-3 block">
          <span class="mb-1 block font-mono text-[10px] font-bold uppercase ui-text-muted">
            Display name (optional)
          </span>
          <input
            type="text"
            class="ui-input w-full font-mono text-sm"
            placeholder="Friendly label in the list"
            value={displayName}
            onInput={(e) =>
              setDisplayName((e.target as HTMLInputElement).value)
            }
          />
        </label>

        {err ? (
          <p class="mb-3 font-mono text-[11px] text-red-600">{err}</p>
        ) : null}

        <div class="flex flex-wrap gap-2">
          <Button type="button" disabled={busy} onClick={() => void commit()}>
            Save device
          </Button>
          <Button type="button" onClick={onClose}>
            Cancel
          </Button>
        </div>
      </div>
    </div>
  );
}
