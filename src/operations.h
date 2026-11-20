#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

#include "engines.h"

// What Depot does to databases, run synchronously: each call waits for docker
// or tailscale and reports what went wrong in a sentence. The command line
// and the MCP server call these directly; the window calls create() on a
// worker thread, so it never blocks.
namespace ops {

// The programs Depot drives. Tests and screenshots point them at fakes.
struct Tools {
    QString docker = "docker";
    QString tailscale = "tailscale";

    // $DEPOT_DOCKER and $DEPOT_TAILSCALE when set.
    static Tools fromEnvironment();
};

// The machine as it is now: its database containers, Depot's data volumes,
// which ports the tailnet forwards, and this node on the tailnet.
struct Snapshot {
    Tools tools;
    QString problem;        // Set when Docker can't be reached; the rest is empty then
    QList<Database> databases;
    QSet<QString> volumes;  // depot-<name> volumes, used or not
    QSet<int> served;
    Tailnet tailnet;

    const Database *find(const QString &name) const;
    bool shared(const Database &database) const { return database.port > 0 && served.contains(database.port); }
    // The name other devices reach this machine by, or its tailnet address.
    QString host() const { return !tailnet.dnsName.isEmpty() ? tailnet.dnsName : tailnet.ip; }
    // Data that outlived its container: a removed database whose data was kept.
    bool hasLeftoverData(const QString &name) const;
    QStringList leftoverNames() const;
};

Snapshot load(const Tools &tools = Tools::fromEnvironment());

// What create() does when a removed database left data under the same name.
// The official images read their password only when they set up an empty
// data directory, so mounting old data silently keeps the old password.
enum class ExistingData {
    Refuse,                 // Stop and say so: the caller must choose
    Reuse,                  // Keep the data and set the new password inside the database
    Fresh,                  // Delete the old data and start over
};

struct Created {
    QString error;          // Empty on success
    bool leftoverData = false; // Refused because old data is there; the caller should ask
    Database database;
    bool shared = false;
};

// Port 0 picks the engine's default, or the next free one after it; an empty
// password is "postgres" (generated for SQL Server).
Created create(const Snapshot &now, const QString &engine, const QString &name, int port, bool share,
               const QString &password = {}, const QString &version = {},
               ExistingData existing = ExistingData::Refuse);

// Each returns an empty string on success, otherwise what went wrong.
QString setRunning(const Snapshot &now, const QString &name, bool running);
QString setShared(const Snapshot &now, const QString &name, bool shared);
QString remove(const Snapshot &now, const QString &name, bool deleteData);
// Deletes the data a removed database left behind. Refuses while a container
// with that name exists.
QString discardData(const Snapshot &now, const QString &name);

// One `docker stats` sample for the named containers that are running.
QHash<QString, Stats> stats(const QStringList &names, const Tools &tools = Tools::fromEnvironment());

}  // namespace ops
