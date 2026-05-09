import type { ComponentChildren } from "preact";

type Props = {
  children: ComponentChildren;
  onClick?: () => void;
  type?: "button" | "submit";
  disabled?: boolean;
};

export function Button({ children, onClick, type = "button", disabled }: Props) {
  return (
    <button
      type={type}
      disabled={disabled}
      onClick={onClick}
      class="ui-btn"
    >
      {children}
    </button>
  );
}
