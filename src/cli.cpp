// The command-line side follows Hype's src/cli.cpp: every command prints
// plain text, or JSON with --json, and exits 0 or 1 with errors on stderr.

#include "cli.h"

#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QTcpServer>
#include <QTextStream>

#include "engines.h"

namespace {

QTextStream out(stdout);
QTextStream err(stderr);

struct Result {
    bool ok = false;
    QByteArray out;
    QByteArray err;
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

int fail(const QString &message) {
    err << "depot: " << message.trimmed() << Qt::endl;
    return 1;
}

bool loadDatabases(QList<Database> &databases, QString &problem) {
    const auto ids = run(docker(), {"ps", "-aq", "--no-trunc"});
    if (!ids.ok) {
        problem = QString::fromUtf8(ids.err);
        return false;
    }
    const QStringList list = QString::fromUtf8(ids.out).split('\n', Qt::SkipEmptyParts);
    if (list.isEmpty())
        return true;
    const auto inspected = run(docker(), QStringList{"inspect"} + list);
    databases = parseInspect(inspected.out);
    return true;
}

const Database *find(const QList<Database> &databases, const QString &name) {
    for (const auto &database : databases)
        if (database.name == name)
            return &database;
    return nullptr;
}

int portFor(const Engine &engine, const QList<Database> &databases, const QSet<int> &served) {
    for (int port = engine.port; port < 65535; ++port) {
        bool taken = served.contains(port);
        for (const auto &database : databases)
            taken = taken || database.port == port;
        QTcpServer probe;
        if (!taken && probe.listen(QHostAddress::LocalHost, port))
            return port;
    }
    return 0;
}

void usage(QTextStream &stream) {
    stream << "Usage: depot [command]\n\n"
              "  open                         Open the window (the default)\n"
              "  list [--json]                List the databases on this machine\n"
              "  create <engine> [name] [--port N] [--no-expose] [--json]\n"
              "                               Create a database with a fresh password and share it\n"
              "                               on the tailnet. Engines: postgres, mysql, mariadb,\n"
              "                               redis, mongo, mssql\n"
              "  start <name>                 Start a database\n"
              "  stop <name>                  Stop a database\n"
              "  expose <name>                Share a database on the tailnet\n"
              "  unexpose <name>              Stop sharing it\n"
              "  url <name> [--local]         Print its connection string\n"
              "  remove <name> [--keep-data]  Remove a database, and its data unless kept\n"
              "  skill install                Install the agent skill for Depot\n";
}

}  // namespace

int runCli(const QStringList &arguments) {
    QStringList args = arguments.mid(1);
    if (args.isEmpty() || args.first() == "open")
        return -1;

    const bool json = args.removeAll("--json") > 0;
    const QString command = args.takeFirst();

    if (command == "help" || command == "--help" || command == "-h") {
        usage(out);
        return 0;
    }

    if (command == "skill") {
        if (args.value(0) != "install")
            return fail("usage: depot skill install");
        QFile source(":/skill/SKILL.md");
        if (!source.open(QIODevice::ReadOnly))
            return fail("the bundled skill is missing");
        const QString directory = QDir::homePath() + "/.claude/skills/depot";
        QDir().mkpath(directory);
        QSaveFile target(directory + "/SKILL.md");
        if (!target.open(QIODevice::WriteOnly) || target.write(source.readAll()) < 0 || !target.commit())
            return fail("couldn't write " + target.fileName());
        out << "Installed " << target.fileName() << Qt::endl;
        return 0;
    }

    QList<Database> databases;
    QString problem;
    if (!loadDatabases(databases, problem))
        return fail(problem);

    const auto served = parseServedPorts(run(tailscale(), {"serve", "status", "--json"}).out);
    const auto tailnet = parseTailnet(run(tailscale(), {"status", "--json"}).out);

    if (command == "list") {
        if (json) {
            QJsonArray array;
            for (const auto &database : databases)
                array.append(QJsonObject::fromVariantMap(toVariant(database, served.contains(database.port), tailnet)));
            out << QJsonDocument(array).toJson(QJsonDocument::Indented);
            return 0;
        }
        for (const auto &database : databases)
            out << QString("%1\t%2\t%3\t%4\t%5\n")
                       .arg(database.name, engineById(database.engine)->label, database.state)
                       .arg(database.port)
                       .arg(served.contains(database.port) ? "tailnet" : "local");
        return 0;
    }

    if (command == "create") {
        const Engine *engine = engineById(args.value(0));
        if (!engine)
            return fail("unknown engine. Use one of: postgres, mysql, mariadb, redis, mongo, mssql");
        const bool expose = args.removeAll("--no-expose") == 0;
        int port = 0;
        const int portFlag = args.indexOf("--port");
        if (portFlag >= 0) {
            port = args.value(portFlag + 1).toInt();
            args.remove(portFlag, qMin(2, int(args.size() - portFlag)));
        }
        const QString name = args.value(1, engine->id);
        const QString nameIssue = nameProblem(name);
        if (!nameIssue.isEmpty())
            return fail(nameIssue);
        if (find(databases, name))
            return fail("a container named " + name + " already exists");
        if (port == 0)
            port = portFor(*engine, databases, served);
        if (port < 1024 || port > 65535)
            return fail("pick a port between 1024 and 65535");

        const QString password = generatePassword();
        const auto created = run(docker(), createArgs(*engine, name, port, password));
        if (!created.ok)
            return fail(QString::fromUtf8(created.err));
        if (expose) {
            const auto shared = run(tailscale(), serveOnArgs(port));
            if (!shared.ok)
                return fail("created, but sharing on the tailnet failed: " + QString::fromUtf8(shared.err));
        }

        Database database{name, engine->id, engine->image, "running", "127.0.0.1", password, port, true};
        const auto item = toVariant(database, expose, tailnet);
        if (json)
            out << QJsonDocument(QJsonObject::fromVariantMap(item)).toJson(QJsonDocument::Indented);
        else
            out << item.value(expose ? "url" : "localUrl").toString() << Qt::endl;
        return 0;
    }

    const QString name = args.value(0);
    const Database *database = find(databases, name);
    if (name.isEmpty())
        return fail("name a database: depot " + command + " <name>");
    if (!database)
        return fail("no database named " + name);

    if (command == "start" || command == "stop") {
        const auto result = run(docker(), {command, name});
        return result.ok ? 0 : fail(QString::fromUtf8(result.err));
    }

    if (command == "expose" || command == "unexpose") {
        if (database->port == 0)
            return fail(name + " doesn't publish a port");
        const auto result = run(tailscale(), command == "expose" ? serveOnArgs(database->port) : serveOffArgs(database->port));
        return result.ok ? 0 : fail(QString::fromUtf8(result.err));
    }

    if (command == "url") {
        const QString host = args.contains("--local") ? "127.0.0.1"
                           : !tailnet.dnsName.isEmpty() ? tailnet.dnsName : tailnet.ip;
        if (host.isEmpty())
            return fail("Tailscale isn't connected; use --local for this machine");
        out << connectionUrl(*database, host) << Qt::endl;
        return 0;
    }

    if (command == "remove") {
        if (served.contains(database->port))
            run(tailscale(), serveOffArgs(database->port));
        const auto removed = run(docker(), {"rm", "-f", name});
        if (!removed.ok)
            return fail(QString::fromUtf8(removed.err));
        if (database->managed && !args.contains("--keep-data")) {
            const auto volume = run(docker(), {"volume", "rm", volumeName(name)});
            if (!volume.ok)
                return fail(QString::fromUtf8(volume.err));
        }
        return 0;
    }

    usage(err);
    return 1;
}
