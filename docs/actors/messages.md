# The message protocol

Every message is **self-owning**: crossing a queue is a copy or a move, never a
borrow. There is deliberately **no global message catalog** — each message type
lives next to the subsystem that owns its semantics, and each actor declares
exactly the control messages it accepts as a `std::variant`.

## The data plane: `data_message_t`

Exactly two data-plane message kinds exist (spec:
`actor-message-protocol`), both carried by one fixed-size struct:

```cpp
struct data_message_t {
  enum class kind_t : uint8_t { midi_received, midi_to_wire };
  kind_t kind; peer_id_t from, to; midi_payload_t payload;
  static data_message_t midi_received(peer_id_t from, midi_payload_t &&);
  static data_message_t midi_to_wire(peer_id_t to, peer_id_t from, midi_payload_t &&);
};
```

- `midi_received{from, payload}` — peer → router (an actor hosting several ids
  sends with its own id).
- `midi_to_wire{to, from, payload}` — router → peer; `to` selects the port
  inside a multi-id actor (e.g. ALSA ports).

`midi_payload_t` owns its bytes: **1536 bytes of inline storage** (covers
MTU-sized RTP-MIDI packets) so the hot path never allocates, plus a **bounded
heap escape pool** for oversized payloads (rawmidi sysex floods). When the
escape pool is exhausted the payload is dropped, counted and logged
rate-limited.

Data-plane messages travel the **data lane**; everything else travels the
**control lane**.

## The control plane: per-actor variants

Each actor declares the control messages it accepts:

```cpp
// mdns_actor.hpp — accepts only mdns/status/reply messages. No MIDI, no routing.
using mdns_control_t =
    std::variant<stop_t, mdns_status_req_t, mdns_announce_t, mdns_unannounce_t,
                 mdns_remove_t, peer_ids_result_t, ack_t>;

class mdns_actor_t : public actor_t<std::monostate, mdns_control_t> {
  using control_messages = mdns_control_t;
  void on_control(mdns_control_t &&msg) override; // visits only its own types
};
```

The actor's mailbox control lane element type **is** that variant, so:

- the mailbox can only ever hold — and the actor only ever sees — its own
  messages;
- the typed `post_control<M>` `static_assert`s membership in the target's
  variant (a **compile error** to post an unaccepted message to a known
  mailbox);
- `on_control` visitors are small and total — an unhandled alternative is
  visibly a bug.

### Where the messages live

| Header | Messages | Owner actor |
|---|---|---|
| `message_core.hpp` | ids, `hdr_t`, the acceptance trait, `mailbox_handle_t`, lifecycle (`stop`, `stopped`, `actor_died`, `reap_actor`, `peer_event`, `control_payload`), generic replies (`ack_t`, `peer_ids_result_t`), `supervisor_control_t`, `reply_control_t`, `make_*` helpers | every actor |
| `data_message.hpp` | `midi_payload_t`, `data_message_t` | data plane |
| `router_messages.hpp` | `register_peer_t`, `spawn_peer_t`, `connect_t`, `status_req_t`, ... + `router_control_t`, `router_mailbox_t` | router |
| `peer_messages.hpp` | `registered_t`, `peer_status_req_t`, `peer_command_t`, ... + `peer_control_t`, `peer_mailbox_t` | peers |
| `worker_messages.hpp` | `worker_job_t`, `dns_resolved_t` + `worker_control_t`, `worker_mailbox_t` | worker |
| `network_messages.hpp` | `udp_datagram_t`, `udp_peer_gone_t` | network listener/peers |
| `mdns_messages.hpp` | `mdns_*` + `mdns_control_t`, `mdns_mailbox_t` | mdns |
| `alsa_messages.hpp` | `alsa_create_port_t`, `alsa_remove_port_t`, `alsa_port_event_t` + `alsa_control_t`, `alsa_mailbox_t` | alsa |
| `control_messages.hpp` | `connection_control_t`, `listener_control_t`, `control_listener_control_t` + the mailbox aliases | control socket / listeners |

There is **no `messages.hpp`** and no union of every message type. Cross-actor
posting through the erased `mailbox_handle_t` matches by type identity against
the target's own variant (see [mailbox.md](mailbox.md)).

## Adding a new message type

1. Add the struct next to the subsystem that owns its semantics (its actor's
   message header).
2. Add it to that subsystem's `*_control_t` variant (and to any *other* actor's
   variant that must accept it — e.g. a reply type goes into the requester's
   variant too).
3. If a mailbox alias must exist, define it next to the variant.
4. Handle it in the owning actor's `on_control` visitor.

Nothing else — no central file to update.

## Request/response: correlation ids, `reply_to`, deadlines

```cpp
struct hdr_t { uint64_t corr = 0; };          // the correlation id
```

Requests that need an answer carry the requester's mailbox handle so the
responder can answer directly:

```cpp
struct status_req_t { hdr_t hdr; mailbox_handle_t reply_to; };
```

Canonical flow (requester-driven):

1. The requester posts a request with `reply_to = its own mailbox`.
2. The responder answers by posting the typed reply **directly to
   `reply_to`** (the type-erased handle).
3. The requester waits with a **requester-side deadline** — either with the
   actor's selective wait (`wait_for`, see [lifecycle.md](lifecycle.md)) or a
   `pending_request_table_t` for always-busy actors.
4. **Responders never know about deadlines.** On timeout the requester
   produces a local error outcome. Late responses with unknown correlation
   ids are **silently discarded** (spec-defined).

The router is the hub for replies to *other* actors: it answers heads and
scatters with the requester's `reply_to`, so peers answer the requester
directly and the router keeps no gather state.

## Lifecycle messages every actor accepts/posts

- `stop_t` — graceful stop; handled by the actor wrapper (never dispatched to
  `on_control`).
- `stopped_t` — posted by the actor wrapper after the loop exits (join is
  safe); carries the actor's id for the router's remove choreography.
- `actor_died_t` — posted on fatal errors (e.g. `on_start` failure).
- `reap_actor_t` — router → supervisor: delegate joining a wedged thread to
  the reaper.

`make_stop()`, `make_stopped()`, `make_actor_died()`, `make_peer_event()`,
`make_control_payload()` are the concise builders (they return the concrete
struct; the posting side wraps it into its own variant).
