#include "operations.h"

#include <QHostAddress>
#include <QProcess>
#include <QTcpServer>

namespace {

struct Result {
    bool ok = false;
    QByteArray out;
    QByteArray err;

    QString error() const { return QString::fromUtf8(err).trimmed(); }
};

Result run(const QString &program, const QStringList &args) {
    QProcess process;
    process.start(program, args);
    if (!process.waitForStarted())
        return {false, {}, QString("%1 is not installed.").arg(program).toUtf8()};
    process.waitForFinished(-1);
    return {process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
            process.readAllStandardOutput(), process.readAllStandardError()};
}

QString docker() {
    const QString value = qEnvironmentVariable("DEPOT_DOCKER");
    return value.isEmpty() ? "docker" : value;
}

QString tailscale() {
    const QString value = qEnvironmentVariable("DEPOT_TAILSCALE");
    return value.isEmpty() ? "tailscale" : value;
}

int freePort(int start, const ops::Snapshot &now, const QList<int> &alsoTaken = {}) {
    for (int port = start; port < 65535; ++port) {
        bool taken = now.served.contains(port) || alsoTaken.contains(port);
        for (const auto &database : now.databases)
            taken = taken || database.ports().contains(port);
        QTcpServer probe;
        if (!taken && probe.listen(QHostAddress::LocalHost, port))
            return port;
    }
    return 0;
}

}  // namespace

namespace ops {

const Database *Snapshot::find(const QString &name) const {
    for (const auto &database : databases)
        if (database.name == name)
            return &database;
    return nullptr;
}

Snapshot load() {
    Snapshot now;
    const auto ids = run(docker(), {"ps", "-aq", "--no-trunc"});
    if (!ids.ok) {
        now.problem = ids.error();
        if (now.problem.contains("permission denied", Qt::CaseInsensitive))
            now.problem = "Docker refused the connection (permission denied). The user must turn on "
                          "sudoless Docker (omarchy-setup-security-sudoless-docker) and reboot.";
        return now;
    }
    const QStringList list = QString::fromUtf8(ids.out).split('\n', Qt::SkipEmptyParts);
    if (!list.isEmpty())
        now.databases = parseInspect(run(docker(), QStringList{"inspect"} + list).out);
    now.served = parseServedPorts(run(tailscale(), {"serve", "status", "--json"}).out);
    now.tailnet = parseTailnet(run(tailscale(), {"status", "--json"}).out);
    return now;
}

Created create(const Snapshot &now, const QString &engineId, const QString &name, int port, bool share,
               const QString &typedPassword, const QString &version) {
    Created result;
    const Engine *engine = engineById(engineId);
    if (!engine) {
        result.error = QString("Unknown engine. Use one of: %1.").arg(engineIds().join(", "));
        return result;
    }
    if (!engine->version(version)) {
        QStringList known;
        for (const auto &candidate : engine->versions)
            known << candidate.label;
        result.error = QString("%1 doesn't offer version %2. Use one of: %3.").arg(engine->label, version, known.join(", "));
        return result;
    }
    const QString finalName = name.isEmpty() ? engine->id : name;
    if (const QString problem = nameProblem(finalName); !problem.isEmpty()) {
        result.error = problem;
        return result;
    }
    if (now.find(finalName)) {
        result.error = QString("A container named %1 already exists.").arg(finalName);
        return result;
    }
    if (const QString problem = passwordProblem(engine->id, typedPassword); !problem.isEmpty()) {
        result.error = problem;
        return result;
    }
    if (port == 0)
        port = freePort(engine->port, now);
    if (port < 1024 || port > 65535) {
        result.error = "Pick a port between 1024 and 65535.";
        return result;
    }
    if (share && !now.tailnet.running) {
        result.error = "Tailscale isn't connected, so the database can't be shared. Create it without sharing, "
                       "or connect Tailscale first (tailscale up).";
        return result;
    }

    const QString password = passwordFor(engine->id, typedPassword);
    // Extra ports take the engine's usual numbers when free, like the main one.
    QList<int> extras;
    for (const auto &extra : engine->extraPorts)
        extras << freePort(extra.port, now, QList<int>{port} + extras);
    const auto created = run(docker(), createArgs(*engine, finalName, port, password, version, extras));
    if (!created.ok) {
        result.error = created.error();
        return result;
    }
    result.database = {finalName, engine->id, engine->image(version), "running", "127.0.0.1", password, port, true};
    for (int i = 0; i < engine->extraPorts.size(); ++i)
        result.database.extraPorts.append({engine->extraPorts[i].key, extras[i]});
    if (share) {
        for (const int each : result.database.ports()) {
            const auto served = run(tailscale(), serveOnArgs(each));
            if (!served.ok) {
                result.error = "Created and running, but sharing it on the tailnet failed: " + served.error();
                return result;
            }
        }
        result.shared = true;
    }
    return result;
}

QString setRunning(const Snapshot &now, const QString &name, bool running) {
    const Database *database = now.find(name);
    if (!database)
        return QString("No database named %1.").arg(name);
    if (database->running() == running)
        return {};
    const auto result = run(docker(), {running ? "start" : "stop", name});
    return result.ok ? QString() : result.error();
}

QString setShared(const Snapshot &now, const QString &name, bool shared) {
    const Database *database = now.find(name);
    if (!database)
        return QString("No database named %1.").arg(name);
    if (database->port == 0)
        return QString("%1 doesn't publish a port, so there is nothing to share.").arg(name);
    if (now.shared(*database) == shared)
        return {};
    if (shared && !now.tailnet.running)
        return "Tailscale isn't connected. Connect it first (tailscale up).";
    // Every port goes together: ClickHouse's HTTP and native ports alike.
    for (const int port : database->ports()) {
        if (now.served.contains(port) == shared)
            continue;
        const auto result = run(tailscale(), shared ? serveOnArgs(port) : serveOffArgs(port));
        if (!result.ok)
            return result.error();
    }
    return {};
}

QString remove(const Snapshot &now, const QString &name, bool deleteData) {
    const Database *database = now.find(name);
    if (!database)
        return QString("No database named %1.").arg(name);
    // Stop sharing first, so the tailnet never forwards to a port that a
    // later database might reuse.
    for (const int port : database->ports())
        if (now.served.contains(port))
            run(tailscale(), serveOffArgs(port));
    const auto removed = run(docker(), {"rm", "-f", name});
    if (!removed.ok)
        return removed.error();
    if (deleteData && database->managed) {
        const auto volume = run(docker(), {"volume", "rm", volumeName(name)});
        if (!volume.ok)
            return volume.error();
    }
    return {};
}

QHash<QString, Stats> stats(const QStringList &names) {
    if (names.isEmpty())
        return {};
    return parseStats(run(docker(), QStringList{"stats", "--no-stream", "--format", "{{json .}}"} + names).out);
}

}  // namespace ops
