import type { ComponentChildren } from "preact";

type Props = { title: string; children: ComponentChildren; class?: string };

export function Card({ title, children, class: cls = "" }: Props) {
  return (
    <section class={`ui-card ${cls}`}>
      <h2 class="ui-card-head">{title}</h2>
      <div class="ui-card-body">{children}</div>
    </section>
  );
}
