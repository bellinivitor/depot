#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

#include "engines.h"

// What the command line and the MCP server do, run synchronously: each call
// waits for docker or tailscale and reports what went wrong in a sentence.
// The window has its own asynchronous path in Backend, since it must never
// block.
namespace ops {

// The machine as it is now: its database containers, which ports the
// tailnet forwards, and this node on the tailnet.
struct Snapshot {
    QString problem;        // Set when Docker can't be reached; the rest is empty then
    QList<Database> databases;
    QSet<int> served;
    Tailnet tailnet;

    const Database *find(const QString &name) const;
    bool shared(const Database &database) const { return database.port > 0 && served.contains(database.port); }
    // The name other devices reach this machine by, or its tailnet address.
    QString host() const { return !tailnet.dnsName.isEmpty() ? tailnet.dnsName : tailnet.ip; }
};

Snapshot load();

struct Created {
    QString error;          // Empty on success
    Database database;
    bool shared = false;
};

// Port 0 picks the engine's default, or the next free one after it.
Created create(const Snapshot &now, const QString &engine, const QString &name, int port, bool share);

// Each returns an empty string on success, otherwise what went wrong.
QString setRunning(const Snapshot &now, const QString &name, bool running);
QString setShared(const Snapshot &now, const QString &name, bool shared);
QString remove(const Snapshot &now, const QString &name, bool deleteData);

// One `docker stats` sample for the named containers that are running.
QHash<QString, Stats> stats(const QStringList &names);

}  // namespace ops
