/** Binary MIDI WebSocket for `monitor.start` sessions (`/ws/monitor?uuid=`). */

export function monitorWebSocketUrl(uuid: string): string {
  const { protocol, host } = window.location;
  const wsProto = protocol === "https:" ? "wss:" : "ws:";
  return `${wsProto}//${host}/ws/monitor?uuid=${encodeURIComponent(uuid)}`;
}
