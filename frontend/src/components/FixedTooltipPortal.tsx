import type { ComponentChildren } from "preact";
import { useCallback, useEffect, useRef, useState } from "preact/hooks";
import { createPortal } from "preact/compat";

const HIDE_DELAY_MS = 180;

export type FixedTooltipState = {
  anchor: DOMRect;
  content: ComponentChildren;
} | null;

type Props = {
  tip: FixedTooltipState;
  onPanelEnter: () => void;
  onPanelLeave: () => void;
};

/**
 * Renders tooltip content in `document.body` with `position: fixed` so it is
 * not clipped by `overflow-*` on table/card ancestors.
 */
export function FixedTooltipPortal({ tip, onPanelEnter, onPanelLeave }: Props) {
  if (tip == null || typeof document === "undefined") return null;

  const vw = window.innerWidth;
  const vh = window.innerHeight;
  const maxW = Math.min(28 * 16, vw - 16);
  const gap = 6;
  let left = tip.anchor.left;
  let top = tip.anchor.bottom + gap;
  const minPanel = 100;
  if (top + minPanel > vh - 8) {
    top = Math.max(8, tip.anchor.top - minPanel - gap);
  }

  if (left + maxW > vw - 8) {
    left = vw - 8 - maxW;
  }
  if (left < 8) {
    left = 8;
  }

  const maxH = Math.max(120, vh - top - 8);

  return createPortal(
    <div
      role="tooltip"
      data-fixed-tooltip="1"
      class="ui-tooltip-panel fixed"
      style={{
        left,
        top,
        maxWidth: maxW,
        maxHeight: maxH,
        zIndex: 2147483000,
        pointerEvents: "auto",
      }}
      onMouseEnter={onPanelEnter}
      onMouseLeave={onPanelLeave}
    >
      {tip.content}
    </div>,
    document.body,
  );
}

export function useFixedTooltip() {
  const [tip, setTip] = useState<FixedTooltipState>(null);
  const hideTimer = useRef<number | undefined>(undefined);

  const cancelHide = useCallback(() => {
    if (hideTimer.current !== undefined) {
      window.clearTimeout(hideTimer.current);
      hideTimer.current = undefined;
    }
  }, []);

  const scheduleHide = useCallback(() => {
    cancelHide();
    hideTimer.current = window.setTimeout(() => {
      setTip(null);
      hideTimer.current = undefined;
    }, HIDE_DELAY_MS);
  }, [cancelHide]);

  const show = useCallback(
    (anchorEl: Element, content: ComponentChildren) => {
      cancelHide();
      setTip({ anchor: anchorEl.getBoundingClientRect(), content });
    },
    [cancelHide],
  );

  useEffect(() => {
    if (tip == null) return undefined;
    const close = (ev: Event) => {
      const el = ev.target as HTMLElement | null;
      if (el?.closest?.("[data-fixed-tooltip]")) return;
      scheduleHide();
    };
    window.addEventListener("scroll", close, true);
    window.addEventListener("resize", close);
    return () => {
      window.removeEventListener("scroll", close, true);
      window.removeEventListener("resize", close);
    };
  }, [tip, scheduleHide]);

  useEffect(
    () => () => {
      if (hideTimer.current !== undefined) {
        window.clearTimeout(hideTimer.current);
      }
    },
    [],
  );

  return {
    tip,
    show,
    scheduleHide,
    cancelHide,
    portal: (
      <FixedTooltipPortal
        tip={tip}
        onPanelEnter={cancelHide}
        onPanelLeave={scheduleHide}
      />
    ),
  };
}
