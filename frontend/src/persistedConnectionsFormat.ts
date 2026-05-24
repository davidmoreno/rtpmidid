/** Human-readable label for a stable id string stored in the database. */
export function formatStableIdLabel(stableId: string): string {
  const parts = stableId.split(":");
  if (parts.length < 2) return stableId;
  const kind = parts[0];
  const rest = parts.slice(1).map((p) => p.replace(/\|/g, ":")).join(" · ");
  const kindLabels: Record<string, string> = {
    alsa: "ALSA",
    rawmidi: "Raw MIDI",
    rtpmidi: "RTP-MIDI",
    rtpmidi_in: "RTP-MIDI in",
    rtpmidi_server: "RTP-MIDI server",
    alsa_listener: "ALSA listener",
    rtpmidi_multi: "RTP-MIDI multi",
    alsa_multi: "ALSA multi",
  };
  const prefix = kindLabels[kind] ?? kind;
  return `${prefix}: ${rest}`;
}
