# Depot

Create local databases in Docker and share them with your other devices over
[Tailscale](https://tailscale.com), from one keyboard-driven window built the
way [Omarchy](https://omarchy.org)'s own apps are.

![Depot](screenshot.png)

Pick PostgreSQL, MySQL, MariaDB, Redis, MongoDB, or SQL Server, give it a
name, and Depot starts it with a generated password and its data in a Docker
volume. The port listens on `127.0.0.1` only; flip **Tailnet** and Depot runs
`tailscale serve --tcp` so your laptop, and nothing on the local network,
reaches it at `your-machine:5432`. The connection details sit at the bottom of
the window, one click from the clipboard: always for this machine, for the
projects running here, and for the tailnet too while it is shared.

Each database shows what it costs the machine as it runs: CPU and memory
with the last two minutes drawn as a line, plus network and disk traffic, from
`docker stats`. The header adds them up across everything running.

The window wears the current Omarchy theme, background, surfaces, text and
accent, light or dark, and switches with it live, in the same monospace font
as the rest of the desktop.

Databases made by Omarchy's *Install > Development > Docker DB* show up too,
with a warning before you share one that has no password.

## Install

Depot needs Qt 6, Docker, and Tailscale, all of which Omarchy ships.

```sh
git clone https://github.com/bellinivitor/depot
cd depot
bin/install
```

It then shows up in the app launcher (`Super + Space`).

Two one-time permissions:

- **Docker.** Omarchy keeps Docker behind a password prompt, and Depot needs
  to talk to it directly. Turn on sudoless Docker under *Setup > Security >
  Sudoless Docker* (or `omarchy-setup-security-sudoless-docker`) and reboot.
  Depot offers the button when it can't reach Docker. Note that the docker
  group is root-equivalent.
- **Tailscale.** Let your user change serve settings without sudo:
  `sudo tailscale set --operator=$USER`.

## Shortcuts

| Key | Action |
| --- | --- |
| `Tab` / `Shift+Tab` | Move between the list and every button |
| `↑` `↓` / `J` `K` | Select a database |
| `1`–`9` | Jump to a database |
| `Enter` | From the list, go to its buttons |
| `N` | New database |
| `Space` | Start or stop |
| `T` | Share on the tailnet, or stop sharing |
| `C` | Copy the connection string (the tailnet one while shared) |
| `L` | Copy the connection string for this machine |
| `Y` | Copy the password |
| `P` | Show or hide the password |
| `D` / `Delete` | Remove (asks first, and keeps the data unless you say otherwise) |
| `R` | Refresh |
| `?` | Show shortcuts |
| `Q` | Quit |

## Command line

Everything the window does works without one, for scripts and agents. Each
command exits 0 or 1 with errors on stderr; `list` and `create` take `--json`.

```sh
depot list --json
depot create postgres app            # prints the tailnet connection string
depot create mysql shop --port 3307 --no-expose
depot url app --local
depot start app | depot stop app
depot expose app | depot unexpose app
depot remove app --keep-data
depot skill install                  # teaches Claude Code to drive Depot
```

## For AI agents (MCP)

`depot mcp` is a [Model Context Protocol](https://modelcontextprotocol.io)
server on stdio, so Claude Code, Codex, OpenCode, and other agents can manage
the databases themselves. Register it once:

```sh
claude mcp add --scope user depot -- depot mcp     # Claude Code
codex mcp add depot -- depot mcp                   # Codex
```

Or, in any client that takes a JSON config:

```json
{ "mcpServers": { "depot": { "command": "depot", "args": ["mcp"] } } }
```

| Tool | What it does |
| --- | --- |
| `list_databases` | Every database container, its state, port, and sharing; no passwords |
| `get_connection` | Host, port, user, password, and URL, from this machine or the tailnet |
| `get_usage` | CPU, memory, network, and disk right now |
| `create_database` | A new database with a generated password; shared only when asked |
| `start_database` / `stop_database` | Start or stop one |
| `share_database` | Share it on the tailnet, or stop |
| `remove_database` | Remove it; the data stays unless `delete_data` is set |

The defaults lean safe for an agent: nothing is shared on the tailnet unless
asked, removal keeps the data volume, and tools are annotated so clients can
ask before the destructive one.

## Development

```sh
bin/build    # build/depot
bin/test     # Qt Test, offscreen, against fake docker and tailscale CLIs
bin/install  # makepkg -fsi
```

`DEPOT_DOCKER` and `DEPOT_TAILSCALE` point Depot at other binaries, such as
the fakes in `tests/fakes/`.

## License

MIT. The theme and translator code follow
[Omacut](https://github.com/omacom-io/omacut) and
[Monologue](https://github.com/omacom/monologue) by David Heinemeier Hansson.
