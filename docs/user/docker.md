# Docker

Run rtpmidid in a container with host networking for mDNS and ALSA device access.

## Quick start

```bash
mkdir -p config
cp default.ini config/default.ini
docker compose up -d
docker compose logs -f
```

## Configuration

Edit `config/default.ini` on the host, then:

```bash
docker compose restart rtpmidid
```

Default RTP-MIDI listen port is **5004**, set in INI:

```ini
[peer]
identity=rtpmidi_multi:name={{hostname}},port=5004
```

Or override with `rtpmidid --port` in the container command.

## Why host network?

`network_mode: host` is required for:

- mDNS / Avahi multicast discovery
- Direct UDP on ports 5004+

## MIDI devices

`/dev/snd` is mounted into the container. Check:

```bash
ls -l /dev/snd/
docker compose exec rtpmidid ls -l /dev/snd/
```

## D-Bus / Avahi

The host D-Bus socket is used for Avahi. If discovery fails:

```bash
ls -l /var/run/dbus/system_bus_socket
# ensure Avahi is running on the host
```

## Port already in use

Change `port=5004` in the `rtpmidi_multi` `[peer]` block in `config/default.ini`
and restart.

## Control socket / CLI inside container

```bash
docker compose exec rtpmidid rtpmidid-cli help
```

## Web UI in Docker

Enable `[web]` in mounted `default.ini`. With `listen=127.0.0.1`, open
`http://127.0.0.1:8089` on the host (host network mode).

## More

Repository `docker-compose.yaml` and image build details. Packaging:
[packaging/README.md](../../packaging/README.md).
