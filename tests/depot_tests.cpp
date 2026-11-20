#include <QColor>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "engines.h"
#include "mcp.h"
#include "theme.h"
#include "translator.h"

// Inspect output for one Depot database, one made by omarchy-install-docker-dbs,
// and one container that isn't a database at all.
static const char *inspectJson = R"([
  {"Id": "a", "Name": "/app", "Config": {"Image": "postgres:18",
     "Labels": {"depot.engine": "postgres", "depot.password": "s3cretPass"}},
   "State": {"Status": "running"},
   "HostConfig": {"PortBindings": {"5432/tcp": [{"HostIp": "127.0.0.1", "HostPort": "5433"}]}}},
  {"Id": "b", "Name": "/mysql8", "Config": {"Image": "docker.io/library/mysql:8.4", "Labels": {}},
   "State": {"Status": "exited"},
   "HostConfig": {"PortBindings": {"3306/tcp": [{"HostIp": "", "HostPort": "3306"}]}}},
  {"Id": "c", "Name": "/web", "Config": {"Image": "nginx:latest", "Labels": {}},
   "State": {"Status": "running"}, "HostConfig": {"PortBindings": {}}}
])";

class DepotTests : public QObject {
    Q_OBJECT

private slots:
    void recognizesEnginesByImage()
    {
        QCOMPARE(engineForImage("postgres:16")->id, QString("postgres"));
        QCOMPARE(engineForImage("docker.io/library/redis")->id, QString("redis"));
        QCOMPARE(engineForImage("mcr.microsoft.com/mssql/server:2022-CU12-ubuntu-22.04")->id, QString("mssql"));
        QVERIFY(engineForImage("localhost:5000/mongo:7") == nullptr);
        QVERIFY(engineForImage("nginx") == nullptr);
    }

    void parsesDatabasesFromInspect()
    {
        const auto databases = parseInspect(inspectJson);
        QCOMPARE(databases.size(), 2);

        QCOMPARE(databases[0].name, QString("app"));
        QVERIFY(databases[0].managed);
        QCOMPARE(databases[0].password, QString("s3cretPass"));
        QCOMPARE(databases[0].port, 5433);
        QVERIFY(databases[0].running());

        // Omarchy's own MySQL: no Depot labels, no password, and still a port
        // while stopped.
        QCOMPARE(databases[1].name, QString("mysql8"));
        QVERIFY(!databases[1].managed);
        QCOMPARE(databases[1].password, QString());
        QCOMPARE(databases[1].port, 3306);
        QVERIFY(!databases[1].running());
        QVERIFY(toVariant(databases[1], false, {}).value("publishedEverywhere").toBool());
        QVERIFY(!toVariant(databases[0], false, {}).value("publishedEverywhere").toBool());
    }

    void parsesTailscale()
    {
        QCOMPARE(parseServedPorts(R"({"TCP": {"5432": {"TCPForward": "127.0.0.1:5432"}, "443": {"HTTPS": true}}})"),
                 QSet<int>{5432});
        QCOMPARE(parseServedPorts("{}"), QSet<int>{});

        const auto tailnet = parseTailnet(R"({"BackendState": "Running", "Self": {"HostName": "box",
            "DNSName": "box.tail1.ts.net.", "TailscaleIPs": ["fd7a::1", "100.64.0.1"]}})");
        QVERIFY(tailnet.running);
        QCOMPARE(tailnet.dnsName, QString("box.tail1.ts.net"));
        QCOMPARE(tailnet.hostName, QString("box"));
        QCOMPARE(tailnet.ip, QString("100.64.0.1"));
        QVERIFY(!parseTailnet(R"({"BackendState": "Stopped"})").running);
    }

    void createsOnLoopbackOnly()
    {
        const auto args = createArgs(*engineById("postgres"), "app", 5440, "pw");
        QVERIFY(args.contains("127.0.0.1:5440:5432"));
        QVERIFY(args.contains("depot-app:/var/lib/postgresql"));
        QVERIFY(args.contains("depot.engine=postgres"));
        QVERIFY(args.contains("POSTGRES_PASSWORD=pw"));
        QCOMPARE(args.last(), QString("postgres:18"));

        const auto redis = createArgs(*engineById("redis"), "cache", 6379, "pw");
        QCOMPARE(redis.mid(redis.indexOf("redis:7")), (QStringList{"redis:7", "redis-server", "--requirepass", "pw", "--appendonly", "yes"}));

        QCOMPARE(serveOnArgs(5432), (QStringList{"serve", "--bg", "--tcp", "5432", "tcp://127.0.0.1:5432"}));
        QCOMPARE(serveOffArgs(5432), (QStringList{"serve", "--tcp", "5432", "off"}));
    }

    void buildsConnectionStrings()
    {
        Database db{"app", "postgres", "postgres:18", "running", "127.0.0.1", "pw", 5433, true};
        QCOMPARE(connectionUrl(db, "box"), QString("postgresql://postgres:pw@box:5433/postgres"));
        db.password.clear();
        QCOMPARE(connectionUrl(db, "box"), QString("postgresql://postgres@box:5433/postgres"));

        Database redis{"cache", "redis", "redis:7", "running", "127.0.0.1", "pw", 6379, true};
        QCOMPARE(connectionUrl(redis, "box"), QString("redis://:pw@box:6379"));
        Database mongo{"docs", "mongo", "mongo", "running", "127.0.0.1", "admin123", 27017, false};
        QCOMPARE(connectionUrl(mongo, "box"), QString("mongodb://admin:admin123@box:27017"));
        Database mssql{"sql", "mssql", "", "running", "127.0.0.1", "@dmin123", 1433, false};
        QCOMPARE(connectionUrl(mssql, "box"), QString("Server=box,1433;User Id=sa;Password=@dmin123;TrustServerCertificate=True"));
    }

    void parsesStats()
    {
        QCOMPARE(parseSize("126B"), 126.0);
        QCOMPARE(parseSize("1.56kB"), 1560.0);
        QCOMPARE(parseSize("9.5MiB"), 9.5 * 1024 * 1024);
        QCOMPARE(parseSize("2GiB"), 2.0 * 1024 * 1024 * 1024);
        QCOMPARE(parseSize("garbage"), 0.0);

        const auto stats = parseStats(
            R"({"BlockIO":"19.2MB / 0B","CPUPerc":"112.50%","MemUsage":"9.5MiB / 2GiB","Name":"cache","NetIO":"1.56kB / 126B","PIDs":"6"})"
            "\n\n");
        QCOMPARE(stats.size(), 1);
        const Stats cache = stats.value("cache");
        QCOMPARE(cache.cpu, 112.5);
        QCOMPARE(cache.memory, 9.5 * 1024 * 1024);
        QCOMPARE(cache.memoryLimit, 2.0 * 1024 * 1024 * 1024);
        QCOMPARE(cache.netIn, 1560.0);
        QCOMPARE(cache.netOut, 126.0);
        QCOMPARE(cache.diskRead, 19200000.0);
        QCOMPARE(cache.diskWrite, 0.0);
        QCOMPARE(cache.processes, 6);
    }

    void generatesStrongPasswords()
    {
        const QRegularExpression shape("^[A-Za-z0-9]{20}$");
        QSet<QString> seen;
        for (int i = 0; i < 50; ++i) {
            const QString password = generatePassword();
            QVERIFY(shape.match(password).hasMatch());
            QVERIFY(password.contains(QRegularExpression("[A-Z]")) && password.contains(QRegularExpression("[a-z]"))
                    && password.contains(QRegularExpression("[0-9]")));
            seen.insert(password);
        }
        QCOMPARE(seen.size(), 50);
    }

    void validatesNames()
    {
        QVERIFY(nameProblem("app").isEmpty());
        QVERIFY(nameProblem("my_app.v2-test").isEmpty());
        QVERIFY(!nameProblem("").isEmpty());
        QVERIFY(!nameProblem("-app").isEmpty());
        QVERIFY(!nameProblem("has space").isEmpty());
    }

    void backendCreatesSharesAndRemoves()
    {
        QTemporaryDir state;
        qputenv("FAKE_STATE", state.path().toUtf8());
        Backend backend(FAKES "/docker", FAKES "/tailscale", nullptr, 0);
        backend.refresh();
        QTRY_VERIFY(backend.loaded());
        QCOMPARE(backend.tailnetName(), QString("workstation.tail1234.ts.net"));
        QVERIFY(backend.databases().isEmpty());

        const int port = backend.suggestPort("postgres");
        QVERIFY(port >= 5432);
        QCOMPARE(backend.suggestName("postgres"), QString("postgres"));

        QSignalSpy created(&backend, &Backend::created);
        backend.create("postgres", "app", port, true);
        // It shows up at once, while Docker still works on it.
        QCOMPARE(backend.databases().value(0).toMap().value("state").toString(), QString("creating"));
        QTRY_COMPARE(created.size(), 1);
        QTRY_VERIFY(!backend.databases().isEmpty() && backend.databases().value(0).toMap().value("exposed").toBool());

        auto app = backend.databases().value(0).toMap();
        QCOMPARE(app.value("name").toString(), QString("app"));
        QCOMPARE(app.value("password").toString().size(), 20);
        QVERIFY(app.value("url").toString().startsWith("postgresql://postgres:"));
        QVERIFY(app.value("url").toString().endsWith(QString("@workstation.tail1234.ts.net:%1/postgres").arg(port)));
        QVERIFY(!backend.nameProblem("app").isEmpty());
        QCOMPARE(backend.suggestName("postgres"), QString("postgres"));
        QVERIFY(!backend.portProblem(port).isEmpty());

        backend.setRunning("app", false);
        QTRY_VERIFY(!backend.databases().value(0).toMap().value("running").toBool()
                    && !backend.databases().value(0).toMap().value("busy").toBool());

        backend.setExposed("app", false);
        QTRY_VERIFY(!backend.databases().value(0).toMap().value("exposed").toBool()
                    && !backend.databases().value(0).toMap().value("busy").toBool());
        backend.setExposed("app", true);
        QTRY_VERIFY(backend.databases().value(0).toMap().value("exposed").toBool());

        backend.remove("app", true);
        QTRY_VERIFY(backend.databases().isEmpty());
        QFile volumes(state.path() + "/removed-volumes");
        QVERIFY(volumes.open(QIODevice::ReadOnly));
        QCOMPARE(volumes.readAll(), QByteArray("depot-app\n"));
        // Removing stopped the sharing too.
        QFile serve(state.path() + "/serve.json");
        QVERIFY(serve.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(serve.readAll()).object().size(), 0);
    }

    void backendSamplesUsage()
    {
        QTemporaryDir state;
        qputenv("FAKE_STATE", state.path().toUtf8());
        Backend backend(FAKES "/docker", FAKES "/tailscale", nullptr, 200);
        backend.create("redis", "cache", backend.suggestPort("redis"), false);
        QTRY_VERIFY(backend.databases().size() == 1 && backend.names() == QStringList{"cache"});

        // A few samples build up a history, and the totals add them up.
        QTRY_VERIFY_WITH_TIMEOUT(backend.databases().value(0).toMap().value("stats").toMap()
                                     .value("cpuHistory").toList().size() >= 3, 15000);
        const auto stats = backend.databases().value(0).toMap().value("stats").toMap();
        QVERIFY(stats.value("memory").toDouble() > 0);
        QVERIFY(stats.value("memoryText").toString().contains("MiB"));
        QVERIFY(stats.value("processes").toInt() > 0);
        QCOMPARE(backend.totals().value("running").toInt(), 1);
        QVERIFY(backend.totals().value("sampled").toBool());

        // A stopped database has no stats.
        backend.setRunning("cache", false);
        QTRY_VERIFY(!backend.databases().value(0).toMap().contains("stats"));
        QCOMPARE(backend.totals().value("running").toInt(), 0);
    }

    void mcpNegotiatesAndListsTools()
    {
        const auto init = mcp::handle(QJsonDocument::fromJson(
            R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{}}})").object()).toObject();
        QCOMPARE(init.value("id").toInt(), 1);
        QCOMPARE(init.value("result").toObject().value("protocolVersion").toString(), QString("2025-03-26"));
        QVERIFY(init.value("result").toObject().value("capabilities").toObject().contains("tools"));

        // An unknown version gets the newest this server speaks.
        const auto future = mcp::handle(QJsonDocument::fromJson(
            R"({"jsonrpc":"2.0","id":2,"method":"initialize","params":{"protocolVersion":"2099-01-01"}})").object()).toObject();
        QCOMPARE(future.value("result").toObject().value("protocolVersion").toString(), mcp::supportedVersions().first());

        // Notifications get no reply.
        QVERIFY(mcp::handle(QJsonDocument::fromJson(R"({"jsonrpc":"2.0","method":"notifications/initialized"})").object()).isNull());

        const auto tools = mcp::tools();
        QCOMPARE(tools.size(), 8);
        for (const auto &value : tools) {
            const auto tool = value.toObject();
            QVERIFY(!tool.value("description").toString().isEmpty());
            QCOMPARE(tool.value("inputSchema").toObject().value("type").toString(), QString("object"));
            const auto hints = tool.value("annotations").toObject();
            // Only removal destroys anything, and every reader says so.
            QCOMPARE(hints.value("destructiveHint").toBool(), tool.value("name").toString() == "remove_database");
            if (tool.value("name").toString().startsWith("list") || tool.value("name").toString().startsWith("get"))
                QVERIFY(hints.value("readOnlyHint").toBool());
        }

        const auto unknown = mcp::handle(QJsonDocument::fromJson(R"({"jsonrpc":"2.0","id":3,"method":"nope"})").object()).toObject();
        QCOMPARE(unknown.value("error").toObject().value("code").toInt(), -32601);
    }

    void mcpManagesDatabases()
    {
        QTemporaryDir state;
        qputenv("FAKE_STATE", state.path().toUtf8());
        qputenv("DEPOT_DOCKER", FAKES "/docker");
        qputenv("DEPOT_TAILSCALE", FAKES "/tailscale");
        auto call = [](const QString &tool, const QJsonObject &args) {
            return mcp::handle({{"jsonrpc", "2.0"}, {"id", 9}, {"method", "tools/call"},
                                {"params", QJsonObject{{"name", tool}, {"arguments", args}}}})
                .toObject().value("result").toObject();
        };

        // Created without sharing unless asked.
        auto created = call("create_database", {{"engine", "redis"}, {"name", "cache"}});
        QVERIFY(!created.value("isError").toBool());
        const auto content = created.value("structuredContent").toObject();
        QVERIFY(!content.value("database").toObject().value("shared_on_tailnet").toBool());
        QCOMPARE(content.value("connection").toObject().value("password").toString().size(), 20);

        // The listing never carries passwords.
        auto listed = call("list_databases", {});
        QVERIFY(!QJsonDocument(listed).toJson().contains(content.value("connection").toObject().value("password").toString().toUtf8()));
        QCOMPARE(listed.value("structuredContent").toObject().value("databases").toArray().size(), 1);

        QVERIFY(call("get_connection", {{"name", "cache"}, {"from", "tailnet"}}).value("isError").toBool());
        QVERIFY(!call("share_database", {{"name", "cache"}, {"shared", true}}).value("isError").toBool());
        QCOMPARE(call("get_connection", {{"name", "cache"}}).value("structuredContent").toObject().value("host").toString(),
                 QString("workstation.tail1234.ts.net"));

        QVERIFY(call("stop_database", {{"name", "missing"}}).value("isError").toBool());
        QVERIFY(!call("stop_database", {{"name", "cache"}}).value("isError").toBool());
        QCOMPARE(call("get_usage", {{"name", "cache"}}).value("structuredContent").toObject().value("running").toBool(), false);

        // Removing keeps the data unless asked.
        QVERIFY(!call("remove_database", {{"name", "cache"}}).value("isError").toBool());
        QVERIFY(!QFile::exists(state.path() + "/removed-volumes"));
        QCOMPARE(call("list_databases", {}).value("structuredContent").toObject().value("databases").toArray().size(), 0);

        qunsetenv("DEPOT_DOCKER");
        qunsetenv("DEPOT_TAILSCALE");
    }

    void backendReportsDockerPermission()
    {
        QTemporaryDir state;
        qputenv("FAKE_STATE", state.path().toUtf8());
        qputenv("FAKE_DOCKER_DENIED", "1");
        Backend backend(FAKES "/docker", FAKES "/tailscale", nullptr, 0);
        backend.refresh();
        QTRY_VERIFY(backend.loaded());
        qunsetenv("FAKE_DOCKER_DENIED");
        QVERIFY(backend.dockerNeedsPermission());
        QVERIFY(backend.dockerProblem().contains("sudoless Docker"));
    }

    void backendReportsMissingTools()
    {
        Backend backend("/nonexistent/docker", "/nonexistent/tailscale", nullptr, 0);
        backend.refresh();
        QTRY_VERIFY(backend.loaded());
        QVERIFY(backend.dockerProblem().contains("not installed"));
        QVERIFY(!backend.tailscaleProblem().isEmpty());
    }

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        QFile colors(dir.path() + "/theme/colors.toml");
        QVERIFY(colors.open(QIODevice::WriteOnly));
        colors.write("accent = \"#112233\"\n");
        colors.close();

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeReadsThePalette()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml",
              "mode = \"dark\"\naccent = \"#7aa2f7\"\nbackground = \"#1a1b26\"\nlighter_background = \"#24283b\"\n");
        Theme theme(dir.path());
        QVERIFY(theme.dark());
        QCOMPARE(theme.accent(), QString("#7aa2f7"));
        QCOMPARE(theme.background(), QString("#1a1b26"));
        QCOMPARE(theme.surface(), QString("#24283b"));
        // Keys the theme leaves out are filled in, never empty.
        QVERIFY(QColor(theme.selection()).isValid());
        QVERIFY(QColor(theme.textDim()).isValid());
        QVERIFY(QColor(theme.green()).isValid());
        QCOMPARE(theme.tint("#ff0000", 0.5), QString("#80ff0000"));

        write(dir.path() + "/theme/colors.toml", "mode = \"light\"\nbackground = \"#eff1f5\"\n");
        QTRY_VERIFY(!theme.dark());
        QCOMPARE(theme.background(), QString("#eff1f5"));
    }

    void themeFollowsHyprlandRounding()
    {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        auto write = [](const QString &path, const QByteArray &data) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        QVERIFY(write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n"));
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(write(response, "{\"int\":0}"));

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        QVERIFY(write(response, "{\"int\":6}"));
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", R"({"%n clips": {"one": "%n clip", "other": "%n clips"}})");
        write(dir.path() + "/ru.json", R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 22), QString("%n клипа"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
        QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
    }

private:
    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }
};

QTEST_MAIN(DepotTests)
#include "depot_tests.moc"
