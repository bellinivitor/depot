#include "operations.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QProcess>
#include <QTcpServer>
#include <QThread>
#include <functional>

namespace {

struct Result {
    bool ok = false;
    QByteArray out;
    QByteArray err;

    QString error() const { return QString::fromUtf8(err).trimmed(); }
};

Result run(const QString &program, const QStringList &args, const QByteArray &input = {}) {
    QProcess process;
    process.start(program, args);
    if (!process.waitForStarted())
        return {false, {}, QString("%1 is not installed.").arg(program).toUtf8()};
    // SQL goes in on stdin, so passwords stay out of process listings.
    if (!input.isEmpty())
        process.write(input);
    process.closeWriteChannel();
    process.waitForFinished(-1);
    return {process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
            process.readAllStandardOutput(), process.readAllStandardError()};
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

// SQL string literals. PostgreSQL only doubles quotes; MySQL and MariaDB
// also treat backslash as an escape.
QString postgresLiteral(const QString &text) {
    return "'" + QString(text).replace("'", "''") + "'";
}

QString mysqlLiteral(const QString &text) {
    return "'" + QString(text).replace("\\", "\\\\").replace("'", "\\'") + "'";
}

// Polls until the database answers, the container stops, or time runs out.
// Returns an empty string once ready, otherwise why it never was.
QString waitUntilReady(const ops::Tools &tools, const QString &container, const std::function<bool()> &ready,
                       int timeoutMs) {
    QElapsedTimer clock;
    clock.start();
    while (true) {
        if (ready())
            return {};
        // A container that exits won't come up by waiting: say why it stopped.
        const auto state = run(tools.docker, {"inspect", "--format", "{{.State.Status}}", container});
        const QString status = QString::fromUtf8(state.out).trimmed();
        if (state.ok && status != "running" && status != "created" && status != "restarting") {
            const auto logs = run(tools.docker, {"logs", "--tail", "5", container});
            return QString("The container stopped before the database was ready. Its last lines:\n%1")
                .arg(QString::fromUtf8(logs.out + logs.err).trimmed());
        }
        if (clock.elapsed() > timeoutMs)
            return QString("The database didn't accept connections within %1 seconds.").arg(timeoutMs / 1000);
        QThread::msleep(400);
    }
}

// Sets the admin password inside a database that started on old data. Each
// engine is asked over its local socket from inside the container.
QString applyPassword(const ops::Tools &tools, const Engine &engine, const QString &container, const QString &password) {
    const int timeout = 120000;
    auto exec = [&](const QStringList &command, const QByteArray &input = {}, const QStringList &env = {}) {
        QStringList args{"exec"};
        if (!input.isEmpty())
            args << "-i";
        for (const auto &variable : env)
            args << "-e" << variable;
        return run(tools.docker, args << container << command, input);
    };

    if (engine.id == "postgres") {
        if (const QString problem = waitUntilReady(tools, container, [&] {
                return exec({"pg_isready", "-U", "postgres"}).ok;
            }, timeout); !problem.isEmpty())
            return problem;
        // The image trusts connections on the local socket, so no old
        // password is needed.
        const auto altered = exec({"psql", "-U", "postgres", "-d", "postgres", "-v", "ON_ERROR_STOP=1"},
                                  ("ALTER USER postgres WITH PASSWORD " + postgresLiteral(password) + ";\n").toUtf8());
        return altered.ok ? QString() : "Couldn't set the new password: " + altered.error();
    }

    if (engine.id == "mysql" || engine.id == "mariadb") {
        const bool maria = engine.id == "mariadb";
        if (const QString problem = waitUntilReady(tools, container, [&] {
                // ping answers 0 once the server runs, even when it refuses the login.
                return exec({maria ? "mariadb-admin" : "mysqladmin", "ping", "--silent"}).ok;
            }, timeout); !problem.isEmpty())
            return problem;
        const QString literal = mysqlLiteral(password);
        const QByteArray sql = ("ALTER USER IF EXISTS 'root'@'%' IDENTIFIED BY " + literal + ";\n"
                                "ALTER USER IF EXISTS 'root'@'localhost' IDENTIFIED BY " + literal + ";\n"
                                "FLUSH PRIVILEGES;\n").toUtf8();
        // Changing root's password takes root's current one. MariaDB lets
        // root in on the socket without it; otherwise try what it most
        // likely is: the new one again, Omarchy's empty one, Depot's default.
        QList<QStringList> logins;
        if (maria)
            logins << QStringList();
        for (const QString &candidate : QStringList{password, "", blankPassword(engine.id)})
            if (!logins.contains(QStringList{"MYSQL_PWD=" + candidate}))
                logins << QStringList{"MYSQL_PWD=" + candidate};
        for (const auto &login : logins)
            if (exec({maria ? "mariadb" : "mysql", "-uroot"}, sql, login).ok)
                return {};
        return QString("%1 only takes a new root password from someone who knows the current one, and Depot "
                       "doesn't keep the passwords of removed databases. Create it with the old password, or "
                       "start over with fresh data.").arg(engine.label);
    }

    if (engine.id == "clickhouse") {
        // Its image writes the default user's password on every start, so
        // the new one already holds; check it once the server answers.
        QString refused;
        const QString problem = waitUntilReady(tools, container, [&] {
            const auto answer = exec({"clickhouse-client", "--password", password, "-q", "SELECT 1"});
            if (!answer.ok && answer.error().contains("AUTHENTICATION_FAILED"))
                refused = answer.error();
            return answer.ok || !refused.isEmpty();
        }, timeout);
        if (!problem.isEmpty())
            return problem;
        return refused.isEmpty() ? QString() : "ClickHouse refused the new password: " + refused;
    }

    // Redis takes its password as an argument on every start.
    return {};
}

}  // namespace

namespace ops {

Tools Tools::fromEnvironment() {
    Tools tools;
    if (const QString docker = qEnvironmentVariable("DEPOT_DOCKER"); !docker.isEmpty())
        tools.docker = docker;
    if (const QString tailscale = qEnvironmentVariable("DEPOT_TAILSCALE"); !tailscale.isEmpty())
        tools.tailscale = tailscale;
    return tools;
}

const Database *Snapshot::find(const QString &name) const {
    for (const auto &database : databases)
        if (database.name == name)
            return &database;
    return nullptr;
}

bool Snapshot::hasLeftoverData(const QString &name) const {
    return volumes.contains(volumeName(name)) && !find(name);
}

QStringList Snapshot::leftoverNames() const {
    QStringList names;
    for (const auto &volume : volumes) {
        const QString name = volume.mid(QString("depot-").size());
        if (!find(name))
            names << name;
    }
    names.sort();
    return names;
}

Snapshot load(const Tools &tools) {
    Snapshot now;
    now.tools = tools;
    const auto ids = run(tools.docker, {"ps", "-aq", "--no-trunc"});
    if (!ids.ok) {
        now.problem = ids.error();
        if (now.problem.contains("permission denied", Qt::CaseInsensitive))
            now.problem = "Docker refused the connection (permission denied). The user must turn on "
                          "sudoless Docker (omarchy-setup-security-sudoless-docker) and reboot.";
        return now;
    }
    const QStringList list = QString::fromUtf8(ids.out).split('\n', Qt::SkipEmptyParts);
    if (!list.isEmpty())
        now.databases = parseInspect(run(tools.docker, QStringList{"inspect"} + list).out);
    now.volumes = parseVolumes(run(tools.docker, {"volume", "ls", "-q", "--filter", "name=depot-"}).out);
    now.served = parseServedPorts(run(tools.tailscale, {"serve", "status", "--json"}).out);
    now.tailnet = parseTailnet(run(tools.tailscale, {"status", "--json"}).out);
    return now;
}

Created create(const Snapshot &now, const QString &engineId, const QString &name, int port, bool share,
               const QString &typedPassword, const QString &version, ExistingData existing) {
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

    // Old data under this name would keep its old password, whatever this
    // call asks for, so never mount it without being told what to do.
    const bool leftover = now.hasLeftoverData(finalName);
    if (leftover && existing == ExistingData::Refuse) {
        result.leftoverData = true;
        result.error = QString("There is data from an earlier database named %1 in the Docker volume %2.")
                           .arg(finalName, volumeName(finalName));
        return result;
    }
    if (leftover && existing == ExistingData::Reuse) {
        if (const QString problem = reuseProblem(engine->id); !problem.isEmpty()) {
            result.error = problem;
            return result;
        }
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

    if (leftover && existing == ExistingData::Fresh) {
        const auto discarded = run(now.tools.docker, {"volume", "rm", volumeName(finalName)});
        if (!discarded.ok) {
            result.error = "Couldn't delete the old data: " + discarded.error();
            return result;
        }
    }

    const QString password = passwordFor(engine->id, typedPassword);
    // Extra ports take the engine's usual numbers when free, like the main one.
    QList<int> extras;
    for (const auto &extra : engine->extraPorts)
        extras << freePort(extra.port, now, QList<int>{port} + extras);
    const auto created = run(now.tools.docker, createArgs(*engine, finalName, port, password, version, extras));
    if (!created.ok) {
        result.error = created.error();
        return result;
    }

    if (leftover && existing == ExistingData::Reuse) {
        if (const QString problem = applyPassword(now.tools, *engine, finalName, password); !problem.isEmpty()) {
            // A database that answers to a password other than the one Depot
            // shows is worse than none: take the container back down, and
            // leave the data where it was.
            run(now.tools.docker, {"rm", "-f", finalName});
            result.error = problem + QString(" The data is still in %1.").arg(volumeName(finalName));
            return result;
        }
    }

    result.database = {finalName, engine->id, engine->image(version), "running", "127.0.0.1", password, port, true};
    for (int i = 0; i < engine->extraPorts.size(); ++i)
        result.database.extraPorts.append({engine->extraPorts[i].key, extras[i]});
    if (share) {
        for (const int each : result.database.ports()) {
            const auto served = run(now.tools.tailscale, serveOnArgs(each));
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
    const auto result = run(now.tools.docker, {running ? "start" : "stop", name});
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
        const auto result = run(now.tools.tailscale, shared ? serveOnArgs(port) : serveOffArgs(port));
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
            run(now.tools.tailscale, serveOffArgs(port));
    const auto removed = run(now.tools.docker, {"rm", "-f", name});
    if (!removed.ok)
        return removed.error();
    if (deleteData && database->managed) {
        const auto volume = run(now.tools.docker, {"volume", "rm", volumeName(name)});
        if (!volume.ok)
            return volume.error();
    }
    return {};
}

QString discardData(const Snapshot &now, const QString &name) {
    if (now.find(name))
        return QString("%1 still exists. Remove it with its data instead.").arg(name);
    if (!now.volumes.contains(volumeName(name)))
        return QString("There is no data left from a database named %1.").arg(name);
    const auto removed = run(now.tools.docker, {"volume", "rm", volumeName(name)});
    return removed.ok ? QString() : removed.error();
}

QHash<QString, Stats> stats(const QStringList &names, const Tools &tools) {
    if (names.isEmpty())
        return {};
    return parseStats(run(tools.docker, QStringList{"stats", "--no-stream", "--format", "{{json .}}"} + names).out);
}

}  // namespace ops
