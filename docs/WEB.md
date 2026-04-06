# Web control interface

rtpmidid can expose an optional **HTTP** server with:

- **Static files** (dashboard UI) at `http://<bind>:<port>/`
- **WebSocket** control at `ws://<bind>:<port>/ws` using the **same JSON line protocol** as the Unix control socket (see [CONTROL.md](CONTROL.md); authoritative methods are implemented in `src/control_socket.cpp`).

## Configuration

In `[general]` of the ini file or via command line:

| Key / flag       | Meaning |
|------------------|---------|
| `web_port` / `--web-port` | TCP port. **`0` disables** the web server. Default: **8080**. |
| `web_bind` / `--web-bind` | Bind address. Default: **127.0.0.1**. Use `0.0.0.0` only if you intend LAN access. |
| `web_user` / `--web-user` | Login username (optional). |
| `web_password` / `--web-password` | Login password (optional). |

If **both** `web_user` and `web_password` are non-empty, the UI requires a **session cookie**:

1. Open `/login.html`, submit the form (`POST /login` with `username` and `password`).
2. The server sets `HttpOnly` cookie `rtpmidid_session`; the browser sends it on `/` and on the WebSocket upgrade to `/ws`.

If either username or password is **empty**, authentication is **disabled** (convenient for local development only).

`GET /logout` clears the session cookie and drops the server-side session id.

## WebSocket protocol

- Send **one JSON object per text message** (equivalent to one line on the Unix socket). Include **`params`** (`null`, `{}`, or `[]` as appropriate).
- The server replies with **one JSON object per text message** (same shape as the Unix socket response: `result` / `error`, optional `id`).
- On daemon shutdown, clients may receive the same JSON as the Unix control socket:  
  `{"event": "close", "detail": "Shutdown", "code": 0}`  
  Oversized messages may receive a `Message too long` close event (same idea as the Unix socket).

## Static files and dependencies

At **configure** time, CMake downloads **htmx** and **Alpine.js** into the build directory; a build target assembles `web/static/` plus those scripts into `web-rtpmidid/` under the build tree.

- **Development:** the daemon prefers that staged directory (or override with environment variable **`RTPMIDID_WEB_ROOT`** pointing at a directory).
- **Installed package:** data is installed under `share/rtpmidid/web` (see `src/CMakeLists.txt`). The binary also looks for `../share/rtpmidid/web` relative to the executable path.

No npm or Tailwind toolchain is required; the UI CSS is plain hand-written files under `web/static/`.
