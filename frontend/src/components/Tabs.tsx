import type { ComponentChildren } from "preact";

export type TabDef = { id: string; label: string; content: ComponentChildren };

type Props = {
  tabs: TabDef[];
  active: string;
  onChange: (id: string) => void;
};

export function Tabs({ tabs, active, onChange }: Props) {
  return (
    <div>
      <div class="ui-tab-bar">
        {tabs.map((t) => (
          <button
            type="button"
            key={t.id}
            class={t.id === active ? "ui-tab ui-tab-active" : "ui-tab"}
            onClick={() => onChange(t.id)}
          >
            {t.label}
          </button>
        ))}
      </div>
      {tabs.map((t) => (
        <div key={t.id} class={t.id === active ? "" : "hidden"}>
          {t.content}
        </div>
      ))}
    </div>
  );
}
