// The command-line side follows Hype's src/cli.cpp: every command prints
// plain text, or JSON with --json, and exits 0 or 1 with errors on stderr.

#include "cli.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>

#include "engines.h"
#include "mcp.h"
#include "operations.h"

namespace {

QTextStream out(stdout);
QTextStream err(stderr);

int fail(const QString &message) {
    err << "depot: " << message.trimmed() << Qt::endl;
    return 1;
}

int done(const QString &error) {
    return error.isEmpty() ? 0 : fail(error);
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
              "  mcp                          Run the MCP server on stdio, for AI agents\n"
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

    if (command == "mcp")
        return mcp::serve();

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

    const auto now = ops::load();
    if (!now.problem.isEmpty())
        return fail(now.problem);

    if (command == "list") {
        if (json) {
            QJsonArray array;
            for (const auto &database : now.databases)
                array.append(QJsonObject::fromVariantMap(toVariant(database, now.shared(database), now.tailnet)));
            out << QJsonDocument(array).toJson(QJsonDocument::Indented);
            return 0;
        }
        for (const auto &database : now.databases)
            out << QString("%1\t%2\t%3\t%4\t%5\n")
                       .arg(database.name, engineById(database.engine)->label, database.state)
                       .arg(database.port)
                       .arg(now.shared(database) ? "tailnet" : "local");
        return 0;
    }

    if (command == "create") {
        const bool share = args.removeAll("--no-expose") == 0;
        int port = 0;
        const int portFlag = args.indexOf("--port");
        if (portFlag >= 0) {
            port = args.value(portFlag + 1).toInt();
            if (port == 0)
                return fail("pick a port between 1024 and 65535");
            args.remove(portFlag, qMin(2, int(args.size() - portFlag)));
        }
        const auto created = ops::create(now, args.value(0), args.value(1), port, share);
        if (created.database.name.isEmpty())
            return fail(created.error);
        const auto item = toVariant(created.database, created.shared, now.tailnet);
        if (json)
            out << QJsonDocument(QJsonObject::fromVariantMap(item)).toJson(QJsonDocument::Indented);
        else
            out << item.value(created.shared ? "url" : "localUrl").toString() << Qt::endl;
        return created.error.isEmpty() ? 0 : fail(created.error);
    }

    const QString name = args.value(0);
    if (name.isEmpty())
        return fail("name a database: depot " + command + " <name>");
    const Database *database = now.find(name);
    if (!database)
        return fail("no database named " + name);

    if (command == "start" || command == "stop")
        return done(ops::setRunning(now, name, command == "start"));
    if (command == "expose" || command == "unexpose")
        return done(ops::setShared(now, name, command == "expose"));
    if (command == "remove")
        return done(ops::remove(now, name, !args.contains("--keep-data")));

    if (command == "url") {
        const QString host = args.contains("--local") ? "127.0.0.1" : now.host();
        if (host.isEmpty())
            return fail("Tailscale isn't connected; use --local for this machine");
        out << connectionUrl(*database, host) << Qt::endl;
        return 0;
    }

    usage(err);
    return 1;
}
