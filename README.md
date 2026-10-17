# MIRC Client

Native desktop client for **MIRC (Modern Internet Relay Chat)**, written in C with GTK 3. It runs on Linux and Windows.

MIRC works like IRC: anyone can host a chat server on their own IP or domain. On top of that it adds accounts, direct messages and cosmetics such as name colours, gradients, avatars, banners and profile themes. One account works on every MIRC server that trusts the same session server.

---

## Features

- Several servers at once, shown in a server rail on the left
- Channels and direct messages, with unread and mention badges
- Message history that loads more as you scroll up
- Edit and delete messages (right-click a message, or press **↑** in an empty input field)
- `/me` actions, typing indicators and online presence
- Member list grouped by role (owner ♛, operator @) and online status
- Profile cards with banner, avatar, bio, and *Message* / *Kick* / *Ban* buttons
- Cosmetics rendered natively: solid or gradient name colours, round avatars, themed profile cards
- Desktop notifications for mentions and DMs
- TLS with certificate pinning (trust on first use) for self-signed server certificates
- Slash commands for everything (see below)
- Profile changes made on the account website are picked up automatically

---

## How login works

```
1. Client ── username + password ──▶ Session server       → login token (stored locally)
2. Client ── TCP/TLS ──────────────▶ Chat server          → "hello"
3. Client ── "token for host:port" ▶ Session server       → one-time server token (60 s)
4. Client ── auth {server token} ──▶ Chat server
5.                                   Chat server ── verify ──▶ Session server
6. Client ◀────────────── welcome ── Chat server
```

- Your password is only ever sent to the **session server**.
- Chat servers never see your password or your login token. They only receive a single-use token, valid for 60 seconds and only for their own address.
- A token issued for one server cannot be used on another.

Accounts are created and managed on the session server's **website** (`/register`, `/account`), not in the client.

---

## Installation

### Linux: Debian / Ubuntu package

If you have a prebuilt `.deb`, install it like this:

```bash
sudo apt install ./mirc-client_0.1.0_amd64.deb
```

MIRC then shows up in your application menu, or you can run `mirc` from a terminal.

### Linux: build from source

```bash
# Dependencies (Debian/Ubuntu)
sudo apt install build-essential pkg-config \
    libgtk-3-dev libsoup-3.0-dev libjson-glib-dev glib-networking

# Fedora:  sudo dnf install gcc make pkgconf gtk3-devel libsoup3-devel json-glib-devel glib-networking
# Arch:    sudo pacman -S base-devel gtk3 libsoup3 json-glib glib-networking

cd client
make                 # builds ./mirc
./mirc               # run without installing

sudo make install    # installs to /usr/local (binary, .desktop entry, icon)
sudo make uninstall  # removes it again

make deb             # builds build/deb/mirc-client_<version>_<arch>.deb
```

Use `make install PREFIX=/usr` to install to a different prefix.

> **`glib-networking` is required for TLS.** Without it, connections to port 6697 fail with an error saying TLS support is not available.

### Windows

The Windows build uses **MSYS2** and produces a regular installer (`Setup.exe`).

1. Install [MSYS2](https://www.msys2.org).
2. Open the **"MSYS2 MINGW64"** shell. The other shells (MSYS, UCRT64) will not work with this script.
3. Run:
   ```bash
   cd /c/path/to/mirc/client
   ./packaging/windows/build-windows.sh
   ```
4. The result is `build/windows/MIRC-Setup-0.1.0.exe`.

The script installs all build dependencies with `pacman`, compiles the client, collects the required DLLs and bundles the following:
- GTK runtime and icon theme
- TLS module (glib-networking)
- Image loaders for PNG, JPEG, GIF and WebP
- A dark default theme

It then builds an NSIS installer that creates Start menu and desktop shortcuts and an uninstaller.

---

## Getting started

1. **Create an account** on your session server's website, e.g. `http://localhost:8440/register`. The login dialog also has an *"Account erstellen (Webseite)"* button that opens this page.
2. **Start MIRC** and log in with your username, password and the session server address.
3. **Add a server** with the **+** button in the server rail:

   | Field | Example | Note |
      |---|---|---|
   | Host or IP | `chat.example.org` or `203.0.113.5` | |
   | Port | `6697` | `6680` for plaintext |
   | TLS | on | recommended |
   | Display name | optional | otherwise taken from the server |

4. **Confirm the certificate.** Servers with a self-signed certificate show their SHA-256 fingerprint on the first connection. Compare it with the one the server operator sees in the chat server log (`TLS-Fingerabdruck`), then click *Trust and connect*. MIRC remembers it and warns you loudly if it ever changes.

Servers with a certificate from a public CA, such as Let's Encrypt, connect without asking.

---

## Using the client

| Where | Action |
|---|---|
| Server rail (left) | Click to switch servers. A badge shows mentions, a dot shows unread messages. |
| **⋯** next to the server name | Connect or disconnect, join a channel, browse channels, edit or remove the server |
| **+** next to *Channels* / *Direct messages* | Join or create a channel, start a DM |
| Right-click a channel or DM | Leave the channel or hide the DM |
| Click an avatar or name | Open the profile card |
| Right-click a message | Copy, edit or delete it |
| **↑** in an empty input field | Edit your last message, **Esc** to cancel |
| *Mitglieder* button | Toggle the member list |
| **⚙** (bottom left) | Open the account website, reload profile, about, log out |

### Commands

| Command | Description |
|---|---|
| `/join #channel` | Join a channel. If it doesn't exist yet it is created and you become its owner. |
| `/part [reason]` | Leave the current channel |
| `/msg <user> [text]` | Open a DM, optionally sending a message right away (aliases: `/query`, `/dm`) |
| `/me <text>` | Send an action |
| `/topic <text>` | Set the channel topic (operators) |
| `/kick <user> [reason]` | Remove a user from the channel (operators) |
| `/ban <user> [reason]` | Ban a user from the channel (operators) |
| `/unban <user>` | Lift a ban (operators) |
| `/op <user>`, `/deop <user>` | Grant or revoke operator status (channel owner) |
| `/delchannel` | Delete the channel (owner or server admin) |
| `/whois <user>` | Show profile, online status and shared channels |
| `/list` | Browse all channels on the server |
| `/clear` | Clear the current view (local only) |
| `/reconnect` | Reconnect to the current server |
| `/help` | List all commands |

To send a message that starts with `/`, type `//` instead, e.g. `//shrug`.

### Profile and cosmetics

Display name, bio, name colour or gradient, avatar, banner and theme are all edited on the **account website**. Use **⚙ → Profil & Cosmetics bearbeiten** to open it.

The client picks up changes automatically:
- when the MIRC window regains focus, and
- every 45 seconds at the latest.

It then pushes your new look to every server you're connected to.

---

## Configuration and data

Settings are stored in an INI file:

| OS | Location |
|---|---|
| Linux | `~/.config/mirc/client.ini` (or `$XDG_CONFIG_HOME/mirc/client.ini`) |
| Windows | `%LOCALAPPDATA%\mirc\client.ini` |

It contains:

- `[session]`: session server URL, last username and the **login token**. The file is created readable only by your user, so keep it private.
- `[server <id>]`: your saved servers
- `[pins]`: trusted certificate fingerprints per `host:port`
- `[ui]`: last opened server and channel

**Logging out** (⚙ → Abmelden) deletes the token and invalidates it on the session server. You can also end client sessions remotely from the website under *Sicherheit → Aktive Sitzungen*.

To reset a certificate you trusted by mistake, delete its line under `[pins]`.

---

## Troubleshooting

| Problem | Fix |
|---|---|
| *"Server nutzt einen anderen Sessionserver"* | The chat server trusts a different session server URL than the one you logged in with. The URLs must match exactly, including `http`/`https` and port. |
| *"Token wurde für eine andere Serveradresse ausgestellt"* | The chat server doesn't list the address you used in its `MIRC_ADDRESSES`. Use the server's official hostname, or ask the operator to add the address. |
| *"Zertifikat hat sich geändert!"* | The server presented a different certificate than last time. Only trust it if the operator confirms the new fingerprint. |
| TLS connection fails immediately | Install `glib-networking` (Linux). On Windows, rebuild with the provided script so the TLS module is bundled. |
| Avatars don't load | The session server's `MIRC_PUBLIC_URL` must be reachable from your machine. |
| Logged out on start | Your login token expired after 30 days or was revoked on the website. Log in again. |

Run `G_MESSAGES_DEBUG=all ./mirc` to see debug output in the terminal.

---

## Source layout

```
client/
├── src/
│   ├── main.c        entry point, login / session start
│   ├── conn.c        chat server connection: TCP/TLS, JSON lines, protocol handling
│   ├── session.c     session server HTTP API (libsoup), image cache, profile refresh
│   ├── ui.c          main window: server rail, sidebar, messages, members, input
│   ├── dialogs.c     login, add server, certificate, channel list, profile card
│   ├── commands.c    slash commands
│   ├── avatar.c      round avatar widget (Cairo)
│   ├── util.c        JSON helpers, profiles, messages, colours, Pango markup
│   ├── config.c      INI settings
│   └── mirc.h        shared types and declarations
├── packaging/
│   ├── linux/        .desktop entry, build-deb.sh
│   ├── windows/      build-windows.sh (MSYS2), NSIS installer, icon, resources
│   └── mirc.svg      application icon
└── Makefile
```

**Dependencies:** GTK 3 (≥ 3.22), libsoup 3, json-glib, GLib/GIO with glib-networking. The code is C11 (`gnu11`).

See [`../PROTOCOL.md`](../PROTOCOL.md) for the wire protocol.

---

## Known limitations

- MIRC servers only. Classic IRC servers are not supported.
- No file attachments, reactions or threads yet.
- No end-to-end encryption. Server operators can read DMs on their own server, as with IRC and Discord.
- Profile editing happens on the website rather than inside the client.
- The UI text is currently German.

## License

MIT