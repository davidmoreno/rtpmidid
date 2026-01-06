# Docker Usage Guide for rtpmidid

This guide explains how to use rtpmidid with Docker and Docker Compose.

## Prerequisites

- Docker and Docker Compose installed
- Access to D-Bus system socket (usually requires running as root or being in appropriate groups)
- ALSA MIDI devices accessible (if using physical MIDI hardware)

## Quick Start

1. **Create configuration directory and copy default.ini:**
   ```bash
   mkdir -p config
   cp default.ini config/default.ini
   ```

2. **Edit configuration (optional):**
   ```bash
   nano config/default.ini
   ```

3. **Build and start the container:**
   ```bash
   docker-compose up -d
   ```

4. **View logs:**
   ```bash
   docker-compose logs -f rtpmidid
   ```

## Configuration

### Customizing default.ini

The `default.ini` file is mounted from `./config/default.ini` into the container. You can:

1. Edit `config/default.ini` directly on the host
2. Restart the container to apply changes:
   ```bash
   docker-compose restart rtpmidid
   ```

### Key Configuration Options

- **Port 5004**: Default RTP MIDI port (configured in `[rtpmidi_announce]` section)
- **UDP Port Range**: The application can use ports 5004-5100+ for connections
- **ALSA Name**: Set in `[general]` section as `alsa_name`
- **Control Socket**: Configured in `[general]` section as `control`

## Network Configuration

The docker-compose.yaml uses `network_mode: host` which is required for:

- **mDNS/Avahi**: Multicast DNS discovery requires host network access
- **UDP Ports**: Direct access to UDP ports 5004-5100+ without port mapping complexity
- **Service Discovery**: Allows rtpmidid to discover and announce RTP MIDI services on the local network

### Port Usage

- **5004**: Default RTP MIDI control port
- **5005**: Default RTP MIDI data port (control + 1)
- **5004-5100+**: Range used for additional connections as configured

## D-Bus Integration

The container connects to the host's D-Bus system socket for Avahi integration:

- **Path**: `/var/run/dbus/system_bus_socket`
- **Purpose**: Allows rtpmidid to use Avahi for mDNS service discovery and announcement
- **Access**: May require running Docker with appropriate permissions or using `sudo`

## MIDI Device Access

Physical MIDI devices are accessed via `/dev/snd`:

- **Mount**: `/dev/snd:/dev/snd`
- **Permissions**: Container runs as `rtpmidid` user in `audio` group
- **Devices**: All ALSA MIDI devices on the host are accessible

### Using Host MIDI Devices

If you have MIDI devices connected to the host, they will be available in the container. The container can:
- Export local ALSA MIDI ports to the network
- Import network RTP MIDI devices as ALSA ports
- Bridge between local and network MIDI devices

## Running Without Docker Compose

If you prefer to use `docker run` directly:

```bash
docker build -t rtpmidid .
docker run -d \
  --name rtpmidid \
  --network host \
  --volume $(pwd)/config/default.ini:/etc/rtpmidid/default.ini:ro \
  --volume /dev/snd:/dev/snd:rw \
  --volume /var/run/dbus/system_bus_socket:/var/run/dbus/system_bus_socket:ro \
  --cap-add NET_ADMIN \
  --cap-add NET_RAW \
  --cap-add SYS_NICE \
  --restart unless-stopped \
  rtpmidid
```

## Troubleshooting

### D-Bus Connection Issues

If you see errors about D-Bus:
```bash
# Check if D-Bus socket exists
ls -l /var/run/dbus/system_bus_socket

# May need to run with sudo or adjust permissions
sudo docker-compose up -d
```

### mDNS Not Working

If mDNS discovery isn't working:
1. Ensure `network_mode: host` is set
2. Check that Avahi daemon is running on the host
3. Verify firewall allows UDP multicast traffic

### MIDI Devices Not Found

If MIDI devices aren't accessible:
1. Check device permissions: `ls -l /dev/snd/`
2. Verify the container has access: `docker-compose exec rtpmidid ls -l /dev/snd/`
3. Ensure devices are in the `audio` group

### Port Already in Use

If port 5004 is already in use:
1. Edit `config/default.ini` to use a different port
2. Update the `[rtpmidi_announce]` section
3. Restart the container

## Stopping and Cleaning Up

```bash
# Stop the container
docker-compose down

# Remove volumes (removes runtime data)
docker-compose down -v

# Remove the image
docker rmi rtpmidid
```

## Advanced Usage

### Custom Build Arguments

You can customize the build:

```yaml
build:
  context: .
  dockerfile: Dockerfile
  args:
    - CMAKE_BUILD_TYPE=Debug
```

### Running with Custom Arguments

Override the default command:

```bash
docker-compose run --rm rtpmidid /usr/bin/rtpmidid --help
```

### Accessing the Control Socket

The control socket is available at `/var/run/rtpmidid/control.sock` inside the container. To use the CLI:

```bash
# From inside the container
docker-compose exec rtpmidid /usr/bin/rtpmidid-cli help

# Or mount the socket to host
# Add to volumes: - ./sockets:/var/run/rtpmidid
```

## Security Considerations

- The container runs as non-root user `rtpmidid`
- Uses `network_mode: host` which shares the host's network stack
- Requires `NET_ADMIN`, `NET_RAW`, and `SYS_NICE` capabilities
- Consider firewall rules on the host to restrict access if needed

## Support

For issues and questions:
- GitHub: https://github.com/davidmoreno/rtpmidid
- Check logs: `docker-compose logs rtpmidid`
