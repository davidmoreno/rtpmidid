/** Human-readable label for a stable id string stored in the database. */
export function formatStableIdLabel(stableId: string): string {
  const parts = stableId.split(":");
  if (parts.length < 2) return stableId;
  const kind = parts[0];
  const rest = parts.slice(1).map((p) => p.replace(/\|/g, ":")).join(" · ");
  const kindLabels: Record<string, string> = {
    alsa: "ALSA",
    alsa_local: "ALSA local",
    rawmidi: "Raw MIDI",
    rawmidi_named: "Raw MIDI",
    rtpmidi: "RTP-MIDI",
    rtpmidi_client_named: "RTP-MIDI client",
    rtpmidi_in: "RTP-MIDI in",
    rtpmidi_in_named: "RTP-MIDI in",
    rtpmidi_server: "RTP-MIDI server",
    alsa_listener: "ALSA listener",
    alsa_listener_named: "ALSA listener",
    rtpmidi_multi: "RTP-MIDI multi",
    alsa_multi: "ALSA multi",
  };
  const prefix = kindLabels[kind] ?? kind;
  return `${prefix}: ${rest}`;
}
