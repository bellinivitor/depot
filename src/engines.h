#pragma once

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// Everything Depot knows about databases, kept free of processes and windows
// so the GUI, the CLI, and the tests share one source of truth.

struct Engine {
    QString id;            // postgres, mysql, mariadb, redis, mongo, mssql
    QString label;         // What people call it: PostgreSQL, MySQL, ...
    QString image;         // The image Depot creates new databases from
    int port = 0;          // The port inside the container, and the default on the host
    QString dataPath;      // Where the engine keeps its data, mounted on a named volume
    QString user;          // The admin user a client logs in as
    QString defaultPassword; // What Omarchy's own installer sets, for containers Depot didn't create
};

struct Database {
    QString name;
    QString engine;        // Engine id
    QString image;
    QString state;         // Docker's state: running, exited, created, restarting, ...
    QString hostIp;        // Address the port is published on; empty or 0.0.0.0 means every interface
    QString password;
    int port = 0;          // Port on the host, 0 when the container publishes none
    bool managed = false;  // Created by Depot, so it has a password and a data volume

    bool running() const { return state == "running"; }
};

const QList<Engine> &engines();
const Engine *engineById(const QString &id);
const Engine *engineForImage(const QString &image);

// Parses `docker inspect` output, keeping only database containers.
QList<Database> parseInspect(const QByteArray &json);

// The TCP ports `tailscale serve status --json` forwards.
QSet<int> parseServedPorts(const QByteArray &json);

struct Tailnet {
    bool running = false;
    QString dnsName;       // omarchy.tail1234.ts.net
    QString hostName;      // omarchy
    QString ip;            // 100.x.y.z
    QString state;         // Running, Stopped, NeedsLogin, ...
};
Tailnet parseTailnet(const QByteArray &statusJson);

// Arguments for `docker run` that create a database: published on 127.0.0.1
// only, so nothing reaches it except through Tailscale Serve.
QStringList createArgs(const Engine &engine, const QString &name, int port, const QString &password);

QStringList serveOnArgs(int port);
QStringList serveOffArgs(int port);

// A connection string a client on another machine can paste.
QString connectionUrl(const Database &database, const QString &host);

QString generatePassword(int length = 20);

// Empty when the name is usable, otherwise why it isn't.
QString nameProblem(const QString &name);

QString volumeName(const QString &name);

// The database as plain values, for QML and `depot list --json`.
QVariantMap toVariant(const Database &database, bool exposed, const Tailnet &tailnet);
