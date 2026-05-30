import { useMemo } from "preact/hooks";
import {
  EndpointSelectBadge,
} from "./EndpointPickerDialog";
import type { Endpoint } from "../endpoints";
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
  /** Full-width header opens the endpoint picker (connection editor). */
  onPickEndpoint?: () => void;
  /** Live endpoint for a richer header badge when available. */
  endpoint?: Endpoint | null;
};

function toggleFieldBracket(fields: IdentityField[], key: string): IdentityField[] {
  return fields.map((f) =>
    f.key === key ? { ...f, bracketed: !f.bracketed } : f,
  );
}

function QueryFieldToggles({
  parsed,
  onChange,
}: {
  parsed: ParsedIdentity;
  onChange: (serialized: string) => void;
}) {
  return (
    <>
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
              class={`ui-btn-plain border px-2 py-0.5 font-mono text-[10px] ${active
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
    </>
  );
}

function SideHeaderButton({
  label,
  value,
  endpoint,
  onPickEndpoint,
  solo,
}: {
  label: string;
  value: string;
  endpoint?: Endpoint | null;
  onPickEndpoint: () => void;
  /** Only the header row (no field toggles below). */
  solo?: boolean;
}) {
  const summary = value ? formatIdentityLabel(value) : "";
  const chosen = !!value.trim();
  return (
    <button
      type="button"
      class={`ui-side-picker-header ${solo ? "ui-side-picker-header--solo" : ""
        } ${chosen ? "ui-side-picker-header--chosen" : "ui-side-picker-header--empty"}`}
      onClick={onPickEndpoint}
      title={
        chosen
          ? `Change side ${label} endpoint (click)`
          : "Choose an endpoint"
      }
    >
      <span class="flex min-w-0 flex-wrap items-center gap-2">
        <span class="ui-side-picker-header__label">Side {label}</span>
        <span class="ui-side-picker-header__hint" aria-hidden>
          Change
        </span>
      </span>
      {endpoint ? (
        <EndpointSelectBadge e={endpoint} />
      ) : chosen ? (
        <span class="min-w-0 truncate text-[11px] font-bold ui-text">{summary}</span>
      ) : (
        <span class="text-[11px] font-bold ui-text-muted">Choose endpoint…</span>
      )}
    </button>
  );
}

export function StoredQueryEditor({
  label,
  value,
  onChange,
  onPickEndpoint,
  endpoint = null,
}: Props) {
  const parsed = useMemo(() => parseIdentity(value), [value]);

  if (onPickEndpoint) {
    if (!value.trim()) {
      return (
        <div class="ui-panel-bordered mb-3 overflow-hidden">
          <SideHeaderButton
            label={label}
            value={value}
            endpoint={endpoint}
            onPickEndpoint={onPickEndpoint}
            solo
          />
        </div>
      );
    }

    if (!parsed) {
      return (
        <div class="ui-panel-bordered mb-3 overflow-hidden">
          <SideHeaderButton
            label={label}
            value={value}
            endpoint={endpoint}
            onPickEndpoint={onPickEndpoint}
          />
          <div class="border-t border-[color:var(--color-border)] px-2 py-2">
            <p class="font-mono text-[11px] ui-text-subtle">{value}</p>
          </div>
        </div>
      );
    }

    return (
      <div class="ui-panel-bordered mb-3 overflow-hidden">
        <SideHeaderButton
          label={label}
          value={value}
          endpoint={endpoint}
          onPickEndpoint={onPickEndpoint}
        />
        <div class="border-t border-[color:var(--color-border)] px-2 py-2">
          <QueryFieldToggles parsed={parsed} onChange={onChange} />
        </div>
      </div>
    );
  }

  if (!parsed) {
    return (
      <div class="ui-panel-bordered p-2">
        <div class="mb-1 font-mono text-[10px] font-bold uppercase ui-text-muted">
          {label}
        </div>
        <p class="font-mono text-[11px] ui-text-subtle">{value || "—"}</p>
      </div>
    );
  }

  const summary = formatIdentityLabel(value);

  return (
    <div class="ui-panel-bordered p-2">
      <div class="mb-1 flex flex-wrap items-center gap-2">
        <span class="font-mono text-[10px] font-bold uppercase ui-text-muted">
          {label}
        </span>
        <span class="font-mono text-[11px] ui-text">{summary}</span>
      </div>
      <QueryFieldToggles parsed={parsed} onChange={onChange} />
    </div>
  );
}
