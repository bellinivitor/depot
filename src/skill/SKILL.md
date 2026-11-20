---
name: depot
description: Create, start, stop, share, and remove local development databases (PostgreSQL, MySQL, MariaDB, Redis, MongoDB, SQL Server) in Docker with the `depot` command, and share them with other devices over Tailscale. Use when someone needs a local database, its connection string, or wants another machine on their tailnet to reach one.
---

# Depot

`depot` manages database containers on this machine. Every database it creates
gets a generated password, keeps its data in the Docker volume `depot-<name>`,
listens only on 127.0.0.1, and is shared on the tailnet through
`tailscale serve --tcp`, never on the local network.

Every command exits 0 or 1, with errors on stderr. Add `--json` to `list` and
`create` for machine-readable output.

```sh
depot list --json                    # every database container, with URLs
depot create postgres app            # new PostgreSQL "app", shared on the tailnet; prints its URL
depot create mysql shop --port 3307  # pick the host port
depot create redis cache --no-expose # this machine only
depot url app                        # connection string for other tailnet devices
depot url app --local                # connection string for this machine
depot start app | depot stop app
depot expose app | depot unexpose app
depot remove app                     # removes the container AND its data
depot remove app --keep-data         # keeps the depot-app volume
```

Engines: postgres, mysql, mariadb, redis, mongo, mssql.

Removing deletes data for good: confirm with the person first, and prefer
`--keep-data` when unsure. Containers created by `omarchy-install-docker-dbs`
show up too; they have no password (except MongoDB and SQL Server's defaults),
so warn before exposing them.

If a command says permission denied on the Docker socket, the user must turn
on sudoless Docker (`omarchy-setup-security-sudoless-docker`) and reboot; do
not work around it with sudo.
