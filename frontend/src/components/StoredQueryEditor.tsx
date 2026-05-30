import { useMemo } from "preact/hooks";
import {
  fieldLabel,
  formatIdentityLabel,
  parseIdentity,
  serializeIdentity,
  type IdentityField,
  type ParsedIdentity,
} from "../deviceIdentity";

type Props = {
  label: string;
  value: string;
  onChange: (serialized: string) => void;
  /** When set, seed fields from this full identity (all active). */
  seedFromIdentity?: string;
};

function toggleFieldBracket(fields: IdentityField[], key: string): IdentityField[] {
  return fields.map((f) =>
    f.key === key ? { ...f, bracketed: !f.bracketed } : f,
  );
}

export function StoredQueryEditor({ label, value, onChange }: Props) {
  const parsed = useMemo(() => parseIdentity(value), [value]);

  if (!parsed) {
    return (
      <div class="rounded border border-[color:var(--color-border)] p-2">
        <div class="mb-1 font-mono text-[10px] font-bold uppercase ui-text-muted">
          {label}
        </div>
        <p class="font-mono text-[11px] ui-text-subtle">{value || "—"}</p>
      </div>
    );
  }

  const summary = formatIdentityLabel(value);

  return (
    <div class="rounded border border-[color:var(--color-border)] p-2">
      <div class="mb-1 flex flex-wrap items-center gap-2">
        <span class="font-mono text-[10px] font-bold uppercase ui-text-muted">
          {label}
        </span>
        <span class="font-mono text-[11px] ui-text">{summary}</span>
      </div>
      <div class="flex flex-wrap gap-1.5">
        {parsed.fields.map((f) => {
          const active = !f.bracketed;
          return (
            <button
              key={f.key}
              type="button"
              title={
                active
                  ? "Click to remember but not match (bracketed)"
                  : "Click to use for matching"
              }
              class={`rounded border px-2 py-0.5 font-mono text-[10px] transition-colors ${
                active
                  ? "border-[color:var(--color-ring-highlight)] bg-[color:var(--color-surface-elevated)] font-bold ui-text"
                  : "border-dashed border-[color:var(--color-border)] opacity-60 ui-text-muted"
              }`}
              onClick={() => {
                const next: ParsedIdentity = {
                  typePrefix: parsed.typePrefix,
                  fields: toggleFieldBracket(parsed.fields, f.key),
                };
                onChange(serializeIdentity(next));
              }}
            >
              {f.bracketed ? `[${fieldLabel(f.key)}=${f.value}]` : `${fieldLabel(f.key)}=${f.value}`}
            </button>
          );
        })}
      </div>
      <p class="mt-1.5 font-mono text-[9px] ui-text-subtle">
        Solid = used for matching. Dashed [brackets] = remembered only.
      </p>
    </div>
  );
}
