#include "mcp.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QLocale>
#include <cstdio>
#include <iostream>
#include <string>

#include "operations.h"

namespace {

const char *instructions =
    "Depot manages development databases running in Docker on this machine (PostgreSQL, MySQL, MariaDB, "
    "Redis, MongoDB, SQL Server). Databases created here get a generated password and keep their data in a "
    "Docker volume named depot-<name>. Their port listens on 127.0.0.1 only; sharing one on the tailnet "
    "(Tailscale Serve) lets the user's other devices reach it, never the local network or the internet.\n"
    "Use list_databases first. get_connection returns credentials; only fetch them when you need to connect. "
    "Ask the user before remove_database, and before sharing a database that has no password.";

QJsonObject schema(const QJsonObject &properties, const QStringList &required = {}) {
    QJsonObject result{{"type", "object"}, {"properties", properties}, {"additionalProperties", false}};
    if (!required.isEmpty())
        result["required"] = QJsonArray::fromStringList(required);
    return result;
}

QJsonObject nameProperty(const QString &what) {
    return {{"type", "string"}, {"description", what}};
}

QJsonObject annotations(const QString &title, bool readOnly, bool destructive, bool idempotent) {
    return {{"title", title}, {"readOnlyHint", readOnly}, {"destructiveHint", destructive},
            {"idempotentHint", idempotent}, {"openWorldHint", false}};
}

QJsonObject tool(const QString &name, const QString &title, const QString &description,
                 const QJsonObject &inputSchema, const QJsonObject &hints) {
    return {{"name", name}, {"title", title}, {"description", description},
            {"inputSchema", inputSchema}, {"annotations", hints}};
}

// A tool's answer: the data as JSON text for every client, and as
// structured content for clients that read it.
QJsonObject result(const QJsonObject &data) {
    return {{"content", QJsonArray{QJsonObject{{"type", "text"},
                                               {"text", QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Indented))}}}},
            {"structuredContent", data},
            {"isError", false}};
}

// A failure the agent can act on goes back as a tool result, not a protocol
// error, so the model sees the sentence.
QJsonObject failure(const QString &message) {
    return {{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", message}}}}, {"isError", true}};
}

QString size(double bytes) {
    return QLocale::c().formattedDataSize(qint64(bytes), 1, QLocale::DataSizeIecFormat);
}

QJsonObject describe(const Database &database, const ops::Snapshot &now) {
    const Engine *engine = engineById(database.engine);
    return {
        {"name", database.name},
        {"engine", database.engine},
        {"engine_label", engine ? engine->label : database.engine},
        {"image", database.image},
        {"state", database.state},
        {"running", database.running()},
        {"port", database.port},
        {"shared_on_tailnet", now.shared(database)},
        {"created_by_depot", database.managed},
        {"has_password", !database.password.isEmpty()},
        {"published_on_all_interfaces", database.port > 0 && (database.hostIp.isEmpty() || database.hostIp == "0.0.0.0")},
    };
}

QJsonObject connection(const Database &database, const QString &host, const QString &from) {
    const Engine *engine = engineById(database.engine);
    return {
        {"name", database.name},
        {"engine", database.engine},
        {"reachable_from", from},
        {"host", host},
        {"port", database.port},
        {"user", engine ? engine->user : QString()},
        {"password", database.password},
        {"url", connectionUrl(database, host)},
    };
}

QJsonObject callTool(const QString &name, const QJsonObject &args) {
    const auto now = ops::load();
    if (!now.problem.isEmpty())
        return failure(now.problem);
    const QString target = args.value("name").toString();

    if (name == "list_databases") {
        QJsonArray list;
        for (const auto &database : now.databases)
            list.append(describe(database, now));
        return result({{"databases", list},
                       {"tailnet", QJsonObject{{"connected", now.tailnet.running}, {"host", now.host()},
                                               {"ip", now.tailnet.ip}}}});
    }

    if (name == "create_database") {
        const bool share = args.value("share_on_tailnet").toBool(false);
        const auto created = ops::create(now, args.value("engine").toString(), target, args.value("port").toInt(0), share);
        if (!created.error.isEmpty() && created.database.name.isEmpty())
            return failure(created.error);
        QJsonObject data{{"database", describe(created.database, now)},
                         {"connection", connection(created.database, "127.0.0.1", "this_machine")}};
        QJsonObject shown = data["database"].toObject();
        shown["shared_on_tailnet"] = created.shared;
        data["database"] = shown;
        if (created.shared)
            data["tailnet_connection"] = connection(created.database, now.host(), "tailnet");
        if (!created.error.isEmpty())
            data["warning"] = created.error;
        data["note"] = "The container is starting; the database accepts connections a few seconds after this, "
                       "longer the first time an image downloads.";
        return result(data);
    }

    if (target.isEmpty())
        return failure("Give the database's name.");
    const Database *database = now.find(target);
    if (!database)
        return failure(QString("No database named %1. Call list_databases to see them.").arg(target));

    if (name == "get_connection") {
        QString from = args.value("from").toString();
        if (from.isEmpty())
            from = now.shared(*database) ? "tailnet" : "this_machine";
        if (from == "tailnet") {
            if (!now.shared(*database))
                return failure(QString("%1 isn't shared on the tailnet. Share it with share_database, or ask for "
                                       "from=this_machine.").arg(target));
            return result(connection(*database, now.host(), "tailnet"));
        }
        return result(connection(*database, "127.0.0.1", "this_machine"));
    }

    if (name == "get_usage") {
        if (!database->running())
            return result({{"name", target}, {"running", false}});
        const auto sample = ops::stats({target}).value(target);
        return result({
            {"name", target},
            {"running", true},
            {"cpu_percent", sample.cpu},
            {"memory_bytes", sample.memory},
            {"memory", size(sample.memory)},
            {"memory_limit", size(sample.memoryLimit)},
            {"network_received", size(sample.netIn)},
            {"network_sent", size(sample.netOut)},
            {"disk_read", size(sample.diskRead)},
            {"disk_written", size(sample.diskWrite)},
            {"processes", sample.processes},
        });
    }

    auto done = [&](const QString &error) {
        if (!error.isEmpty())
            return failure(error);
        const auto after = ops::load();
        const Database *updated = after.find(target);
        return result(updated ? describe(*updated, after) : QJsonObject{{"name", target}, {"removed", true}});
    };

    if (name == "start_database")
        return done(ops::setRunning(now, target, true));
    if (name == "stop_database")
        return done(ops::setRunning(now, target, false));
    if (name == "share_database") {
        if (!args.contains("shared"))
            return failure("Say whether to share it: shared=true or shared=false.");
        return done(ops::setShared(now, target, args.value("shared").toBool()));
    }
    if (name == "remove_database")
        return done(ops::remove(now, target, args.value("delete_data").toBool(false)));

    return failure(QString("Unknown tool %1.").arg(name));
}

QJsonObject reply(const QJsonValue &id, const QJsonObject &resultObject) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", resultObject}};
}

QJsonObject error(const QJsonValue &id, int code, const QString &message) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", QJsonObject{{"code", code}, {"message", message}}}};
}

void write(const QJsonValue &value) {
    const QByteArray line = value.isArray() ? QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)
                                            : QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
    std::fwrite(line.constData(), 1, size_t(line.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

}  // namespace

namespace mcp {

QStringList supportedVersions() {
    return {"2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"};
}

QJsonArray tools() {
    const QJsonObject engine{{"type", "string"},
                             {"enum", QJsonArray{"postgres", "mysql", "mariadb", "redis", "mongo", "mssql"}},
                             {"description", "postgres (PostgreSQL 18), mysql (MySQL 8.4), mariadb (MariaDB 11.8), "
                                             "redis (Redis 7), mongo (MongoDB), or mssql (SQL Server 2022)."}};
    return {
        tool("list_databases", "List databases",
             "Lists every database container on this machine, whether Depot created it or not: engine, state, "
             "host port, and whether it is shared on the tailnet. Also says how this machine is named on the "
             "tailnet. Never includes passwords.",
             schema({}), annotations("List databases", true, false, true)),
        tool("get_connection", "Get connection details",
             "Returns host, port, user, password, and a connection URL for one database. from=this_machine gives "
             "127.0.0.1; from=tailnet gives this machine's tailnet name, for the user's other devices, and only "
             "works while the database is shared. Defaults to tailnet when shared, this_machine otherwise.",
             schema({{"name", nameProperty("The database's name, as list_databases shows it.")},
                     {"from", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"this_machine", "tailnet"}},
                                          {"description", "Where the client connects from."}}}},
                    {"name"}),
             annotations("Get connection details", true, false, true)),
        tool("get_usage", "Get hardware usage",
             "Samples what a running database costs this machine right now: CPU percent (of one core; above 100 "
             "when it uses several), memory, network traffic, disk traffic, and process count. Takes about a "
             "second.",
             schema({{"name", nameProperty("The database's name.")}}, {"name"}),
             annotations("Get hardware usage", true, false, true)),
        tool("create_database", "Create a database",
             "Creates and starts a new database with a generated password and its data in the Docker volume "
             "depot-<name>. It listens on 127.0.0.1 only. Set share_on_tailnet to also let the user's other "
             "devices reach it. Returns the connection details, password included. The first database of an "
             "engine downloads its image, which can take minutes.",
             schema({{"engine", engine},
                     {"name", QJsonObject{{"type", "string"},
                                          {"description", "Letters, digits, dots, dashes, and underscores. Defaults to the engine's id."}}},
                     {"port", QJsonObject{{"type", "integer"}, {"minimum", 1024}, {"maximum", 65535},
                                          {"description", "Host port. Defaults to the engine's usual port, or the next free one."}}},
                     {"share_on_tailnet", QJsonObject{{"type", "boolean"}, {"default", false},
                                                      {"description", "Share it on the tailnet right away."}}}},
                    {"engine"}),
             annotations("Create a database", false, false, false)),
        tool("start_database", "Start a database", "Starts a stopped database. Does nothing if it already runs.",
             schema({{"name", nameProperty("The database's name.")}}, {"name"}),
             annotations("Start a database", false, false, true)),
        tool("stop_database", "Stop a database",
             "Stops a running database; its data stays. Open connections drop. Does nothing if it is already stopped.",
             schema({{"name", nameProperty("The database's name.")}}, {"name"}),
             annotations("Stop a database", false, false, true)),
        tool("share_database", "Share on the tailnet",
             "Shares a database with the user's other devices on their tailnet (shared=true), or stops sharing "
             "it (shared=false). Uses Tailscale Serve, so the local network and the internet still can't reach "
             "it. Check has_password first: a database without one is open to every device on the tailnet.",
             schema({{"name", nameProperty("The database's name.")},
                     {"shared", QJsonObject{{"type", "boolean"}, {"description", "true to share, false to stop sharing."}}}},
                    {"name", "shared"}),
             annotations("Share on the tailnet", false, false, true)),
        tool("remove_database", "Remove a database",
             "Removes a database's container and stops sharing it. Its data stays in the Docker volume unless "
             "delete_data is true, which deletes every table and row for good. Ask the user first.",
             schema({{"name", nameProperty("The database's name.")},
                     {"delete_data", QJsonObject{{"type", "boolean"}, {"default", false},
                                                 {"description", "Also delete the data volume. Cannot be undone."}}}},
                    {"name"}),
             annotations("Remove a database", false, true, false)),
    };
}

QJsonValue handle(const QJsonObject &message) {
    const QJsonValue id = message.value("id");
    const bool notification = !message.contains("id");
    const QString method = message.value("method").toString();

    if (message.value("jsonrpc").toString() != "2.0" || method.isEmpty())
        return notification ? QJsonValue() : QJsonValue(error(id, -32600, "Invalid request"));
    if (notification)
        return {};  // notifications/initialized, notifications/cancelled, ...

    const QJsonObject params = message.value("params").toObject();

    if (method == "initialize") {
        const QString asked = params.value("protocolVersion").toString();
        const QString version = supportedVersions().contains(asked) ? asked : supportedVersions().first();
        return reply(id, {{"protocolVersion", version},
                          {"capabilities", QJsonObject{{"tools", QJsonObject{{"listChanged", false}}}}},
                          {"serverInfo", QJsonObject{{"name", "depot"}, {"title", "Depot"},
                                                     {"version", QCoreApplication::applicationVersion()}}},
                          {"instructions", instructions}});
    }
    if (method == "ping")
        return reply(id, {});
    if (method == "tools/list")
        return reply(id, {{"tools", tools()}});
    if (method == "tools/call") {
        const QString name = params.value("name").toString();
        bool known = false;
        for (const auto &definition : tools())
            known = known || definition.toObject().value("name").toString() == name;
        if (!known)
            return error(id, -32602, QString("Unknown tool: %1").arg(name));
        return reply(id, callTool(name, params.value("arguments").toObject()));
    }
    return error(id, -32601, QString("Method not found: %1").arg(method));
}

int serve() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.find_first_not_of(" \t\r") == std::string::npos)
            continue;
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(QByteArray::fromStdString(line), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            write(error(QJsonValue::Null, -32700, "Parse error"));
            continue;
        }
        // Older clients may batch several messages in one array.
        if (document.isArray()) {
            QJsonArray replies;
            for (const auto &item : document.array())
                if (const auto answer = handle(item.toObject()); !answer.isUndefined() && !answer.isNull())
                    replies.append(answer);
            if (!replies.isEmpty())
                write(replies);
            continue;
        }
        const auto answer = handle(document.object());
        if (!answer.isUndefined() && !answer.isNull())
            write(answer);
    }
    return 0;
}

}  // namespace mcp
