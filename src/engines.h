#pragma once

#include <QHash>
#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// Everything Depot knows about databases, kept free of processes and windows
// so the GUI, the CLI, and the tests share one source of truth.

struct EngineVersion {
    QString label;         // What people call it: 16, 8.4, 2022
    QString tag;           // The image tag: 16, 8.4, 2022-latest
};

// A second port some engines speak on, beside the main one: ClickHouse
// answers HTTP on its main port and its native protocol on this one.
struct ExtraPort {
    QString key;           // native
    int port = 0;          // Inside the container, and the default on the host
};

struct Engine {
    QString id;            // postgres, mysql, mariadb, redis, mongo, mssql, clickhouse
    QString label;         // What people call it: PostgreSQL, MySQL, ...
    QString repository;    // The image without its tag
    QList<EngineVersion> versions; // Newest supported first; the first is the default
    int port = 0;          // The port inside the container, and the default on the host
    QString dataPath;      // Where the engine keeps its data, mounted on a named volume
    QString user;          // The admin user a client logs in as
    QString defaultPassword; // What Omarchy's own installer sets, for containers Depot didn't create
    QList<ExtraPort> extraPorts = {};

    // The image for a version label; the default when it's empty.
    QString image(const QString &version = {}) const;
    const EngineVersion *version(const QString &label) const;
    // Where this version keeps its data: PostgreSQL moved it in 18.
    QString dataPathFor(const QString &version) const;
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
    QList<QPair<QString, int>> extraPorts = {}; // Key and host port of each extra port it publishes

    bool running() const { return state == "running"; }
    // Every host port, the main one first: what sharing forwards.
    QList<int> ports() const;
};

// One sample of `docker stats` for a container. Sizes are in bytes.
struct Stats {
    double cpu = 0;        // Percent of one core; above 100 when it uses several
    double memory = 0;
    double memoryLimit = 0;
    double netIn = 0;
    double netOut = 0;
    double diskRead = 0;
    double diskWrite = 0;
    int processes = 0;
};

const QList<Engine> &engines();
const Engine *engineById(const QString &id);
const Engine *engineForImage(const QString &image);

// Parses `docker inspect` output, keeping only database containers.
QList<Database> parseInspect(const QByteArray &json);

// Parses `docker stats --no-stream --format '{{json .}}'`, one object per line,
// by container name.
QHash<QString, Stats> parseStats(const QByteArray &jsonLines);

// "9.355MiB", "19.2MB", "126B" to bytes; Docker mixes binary and decimal units.
double parseSize(const QString &text);

// Depot's data volumes from `docker volume ls -q`: only depot-<name> ones.
QSet<QString> parseVolumes(const QByteArray &lines);

// Empty when Depot can keep an engine's old data and still set a new
// password; otherwise why it can't.
QString reuseProblem(const QString &engine);

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
// An empty version means the engine's default.
// Extra host ports pair with the engine's extra ports in order; missing ones
// use the container's port.
QStringList createArgs(const Engine &engine, const QString &name, int port, const QString &password,
                       const QString &version = {}, const QList<int> &extraHostPorts = {});

// The engine ids, for messages and schemas.
QStringList engineIds();

QStringList serveOnArgs(int port);
QStringList serveOffArgs(int port);

// A connection string a client on another machine can paste.
QString connectionUrl(const Database &database, const QString &host);

QString generatePassword(int length = 20);

// The password a new database gets when none is typed: "postgres", the
// usual one for local development, except where the engine refuses it.
QString blankPassword(const QString &engine);
// What to use: the typed password, else the blank default, else a fresh one.
QString passwordFor(const QString &engine, const QString &typed);

// Empty when the name is usable, otherwise why it isn't.
QString nameProblem(const QString &name);

// Empty when a typed password is usable for this engine, otherwise why it
// isn't. An empty password is fine: Depot generates one.
QString passwordProblem(const QString &engine, const QString &password);

QString volumeName(const QString &name);

// The database as plain values, for QML and `depot list --json`.
QVariantMap toVariant(const Database &database, bool exposed, const Tailnet &tailnet);
