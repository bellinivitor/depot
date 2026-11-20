#include "engines.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QUrl>
#include <cmath>
#include <tuple>

const QList<Engine> &engines() {
    // Images and defaults match omarchy-install-docker-dbs, so databases made
    // there and here behave alike.
    // The defaults match omarchy-install-docker-dbs, so databases made there
    // and here behave alike; the others are the versions still maintained
    // upstream, each checked to exist on its registry.
    static const QList<Engine> list{
        {"postgres", "PostgreSQL", "postgres",
         {{"18", "18"}, {"17", "17"}, {"16", "16"}, {"15", "15"}, {"14", "14"}, {"13", "13"}},
         5432, "/var/lib/postgresql", "postgres", ""},
        {"mysql", "MySQL", "mysql", {{"8.4", "8.4"}, {"9", "9"}, {"8.0", "8.0"}},
         3306, "/var/lib/mysql", "root", ""},
        {"mariadb", "MariaDB", "mariadb",
         {{"11.8", "11.8"}, {"12", "12"}, {"11.4", "11.4"}, {"10.11", "10.11"}, {"10.6", "10.6"}},
         3306, "/var/lib/mysql", "root", ""},
        {"redis", "Redis", "redis", {{"7", "7"}, {"8", "8"}, {"6.2", "6.2"}},
         6379, "/data", "default", ""},
        {"mongo", "MongoDB", "mongo", {{"8.2", "8.2"}, {"8.0", "8.0"}, {"7.0", "7.0"}, {"6.0", "6.0"}},
         27017, "/data/db", "admin", "admin123"},
        {"mssql", "SQL Server", "mcr.microsoft.com/mssql/server",
         {{"2022", "2022-latest"}, {"2025", "2025-latest"}, {"2019", "2019-latest"}, {"2017", "2017-latest"}},
         1433, "/var/opt/mssql", "sa", "@dmin123"},
        // HTTP on 8123 is what most drivers and DBeaver use; the native
        // protocol on 9000 is what clickhouse-client speaks.
        {"clickhouse", "ClickHouse", "clickhouse/clickhouse-server",
         {{"25.8", "25.8"}, {"25.10", "25.10"}, {"25.9", "25.9"}, {"25.3", "25.3"}, {"24.8", "24.8"}},
         8123, "/var/lib/clickhouse", "default", "", {{"native", 9000}}},
    };
    return list;
}

const EngineVersion *Engine::version(const QString &label) const {
    if (label.isEmpty())
        return versions.isEmpty() ? nullptr : &versions.first();
    for (const auto &candidate : versions)
        if (candidate.label == label)
            return &candidate;
    return nullptr;
}

QString Engine::image(const QString &label) const {
    const EngineVersion *chosen = version(label);
    return repository + ":" + (chosen ? chosen->tag : QString("latest"));
}

QString Engine::dataPathFor(const QString &label) const {
    // Up to 17 the image keeps its data, and declares its volume, one level
    // down. Mounting the parent would leave the data in an anonymous volume
    // that removing the container orphans.
    if (id == "postgres") {
        const EngineVersion *chosen = version(label);
        if (chosen && chosen->label.toInt() < 18)
            return "/var/lib/postgresql/data";
    }
    return dataPath;
}

QStringList engineIds() {
    QStringList ids;
    for (const auto &engine : engines())
        ids << engine.id;
    return ids;
}

QList<int> Database::ports() const {
    QList<int> all;
    if (port > 0)
        all << port;
    for (const auto &extra : extraPorts)
        if (extra.second > 0)
            all << extra.second;
    return all;
}

const Engine *engineById(const QString &id) {
    for (const auto &engine : engines())
        if (engine.id == id)
            return &engine;
    return nullptr;
}

const Engine *engineForImage(const QString &image) {
    // Drop the tag and Docker Hub's implied prefixes, so "postgres:16",
    // "docker.io/library/postgres", and "postgres" all read as PostgreSQL.
    QString repository = image.section('@', 0, 0);
    const int colon = repository.lastIndexOf(':');
    if (colon > repository.lastIndexOf('/'))
        repository.truncate(colon);
    for (const QString prefix : {"docker.io/library/", "docker.io/", "library/"})
        if (repository.startsWith(prefix))
            repository.remove(0, prefix.size());

    for (const auto &engine : engines())
        if (repository == engine.repository || repository == engine.id)
            return &engine;
    return nullptr;
}

QList<Database> parseInspect(const QByteArray &json) {
    QList<Database> result;
    const auto containers = QJsonDocument::fromJson(json).array();
    for (const auto &value : containers) {
        const auto container = value.toObject();
        const auto config = container.value("Config").toObject();
        const auto labels = config.value("Labels").toObject();

        const Engine *engine = engineById(labels.value("depot.engine").toString());
        const bool managed = engine != nullptr;
        if (!engine)
            engine = engineForImage(config.value("Image").toString());
        if (!engine)
            continue;

        Database database;
        database.name = container.value("Name").toString().mid(1);
        database.engine = engine->id;
        database.image = config.value("Image").toString();
        database.state = container.value("State").toObject().value("Status").toString();
        database.managed = managed;
        database.password = managed ? labels.value("depot.password").toString() : engine->defaultPassword;

        // Port bindings live in HostConfig, so a stopped container still
        // reports the port it will come back on.
        const auto portBindings = container.value("HostConfig").toObject().value("PortBindings").toObject();
        auto hostPort = [&](int containerPort, QString *hostIp) {
            for (const auto &binding : portBindings.value(QString::number(containerPort) + "/tcp").toArray()) {
                const auto port = binding.toObject().value("HostPort").toString().toInt();
                if (port > 0) {
                    if (hostIp)
                        *hostIp = binding.toObject().value("HostIp").toString();
                    return port;
                }
            }
            return 0;
        };
        database.port = hostPort(engine->port, &database.hostIp);
        for (const auto &extra : engine->extraPorts)
            if (const int port = hostPort(extra.port, nullptr); port > 0)
                database.extraPorts.append({extra.key, port});
        result.append(database);
    }
    std::sort(result.begin(), result.end(), [](const Database &a, const Database &b) { return a.name < b.name; });
    return result;
}

double parseSize(const QString &text) {
    static const QRegularExpression shape(R"(^\s*([0-9.]+)\s*([kKMGTP]?i?B)\s*$)");
    const auto match = shape.match(text);
    if (!match.hasMatch())
        return 0;
    const QString unit = match.captured(2);
    const bool binary = unit.contains('i');
    const double base = binary ? 1024 : 1000;
    const int power = QString("BKMGTP").indexOf(unit.at(0).toUpper());
    return match.captured(1).toDouble() * std::pow(base, qMax(0, power));
}

QHash<QString, Stats> parseStats(const QByteArray &jsonLines) {
    QHash<QString, Stats> result;
    for (const auto &line : jsonLines.split('\n')) {
        const auto object = QJsonDocument::fromJson(line).object();
        const QString name = object.value("Name").toString();
        if (name.isEmpty())
            continue;
        auto pair = [&](const char *key) {
            const auto parts = object.value(key).toString().split('/');
            return std::pair{parseSize(parts.value(0)), parseSize(parts.value(1))};
        };
        Stats stats;
        QString cpu = object.value("CPUPerc").toString();
        stats.cpu = cpu.remove('%').toDouble();
        std::tie(stats.memory, stats.memoryLimit) = pair("MemUsage");
        std::tie(stats.netIn, stats.netOut) = pair("NetIO");
        std::tie(stats.diskRead, stats.diskWrite) = pair("BlockIO");
        stats.processes = object.value("PIDs").toString().toInt();
        result.insert(name, stats);
    }
    return result;
}

QSet<int> parseServedPorts(const QByteArray &json) {
    QSet<int> ports;
    const auto tcp = QJsonDocument::fromJson(json).object().value("TCP").toObject();
    for (auto it = tcp.begin(); it != tcp.end(); ++it)
        if (it.value().toObject().contains("TCPForward"))
            ports.insert(it.key().toInt());
    return ports;
}

Tailnet parseTailnet(const QByteArray &statusJson) {
    Tailnet tailnet;
    const auto root = QJsonDocument::fromJson(statusJson).object();
    const auto self = root.value("Self").toObject();
    tailnet.state = root.value("BackendState").toString();
    tailnet.running = tailnet.state == "Running";
    tailnet.dnsName = self.value("DNSName").toString();
    if (tailnet.dnsName.endsWith('.'))
        tailnet.dnsName.chop(1);
    tailnet.hostName = tailnet.dnsName.section('.', 0, 0);
    if (tailnet.hostName.isEmpty())
        tailnet.hostName = self.value("HostName").toString();
    for (const auto &ip : self.value("TailscaleIPs").toArray())
        if (ip.toString().contains('.')) {
            tailnet.ip = ip.toString();
            break;
        }
    return tailnet;
}

QStringList createArgs(const Engine &engine, const QString &name, int port, const QString &password,
                       const QString &version, const QList<int> &extraHostPorts) {
    QStringList args{"run", "-d", "--restart", "unless-stopped", "--name", name,
                     "-p", QString("127.0.0.1:%1:%2").arg(port).arg(engine.port)};
    for (int i = 0; i < engine.extraPorts.size(); ++i)
        args << "-p" << QString("127.0.0.1:%1:%2").arg(extraHostPorts.value(i, engine.extraPorts[i].port))
                                                   .arg(engine.extraPorts[i].port);
    args << "-v" << volumeName(name) + ":" + engine.dataPathFor(version)
         << "--label" << "depot.engine=" + engine.id
         << "--label" << "depot.password=" + password;

    QStringList command;
    if (engine.id == "postgres")
        args << "-e" << "POSTGRES_PASSWORD=" + password;
    else if (engine.id == "mysql")
        args << "-e" << "MYSQL_ROOT_PASSWORD=" + password;
    else if (engine.id == "mariadb")
        args << "-e" << "MARIADB_ROOT_PASSWORD=" + password;
    else if (engine.id == "mongo")
        args << "-e" << "MONGO_INITDB_ROOT_USERNAME=" + engine.user << "-e" << "MONGO_INITDB_ROOT_PASSWORD=" + password;
    else if (engine.id == "mssql")
        args << "-e" << "ACCEPT_EULA=Y" << "-e" << "MSSQL_PID=Developer" << "-e" << "MSSQL_SA_PASSWORD=" + password;
    else if (engine.id == "redis")
        command << "redis-server" << "--requirepass" << password << "--appendonly" << "yes";
    else if (engine.id == "clickhouse")
        // The password goes to the default user; ClickHouse asks for the
        // higher open-file limit in its own docs.
        args << "-e" << "CLICKHOUSE_PASSWORD=" + password << "-e" << "CLICKHOUSE_DEFAULT_ACCESS_MANAGEMENT=1"
             << "--ulimit" << "nofile=262144:262144";

    return args << engine.image(version) << command;
}

// Targets 127.0.0.1 rather than localhost: Docker publishes on IPv4 only, and
// localhost may resolve to ::1 first.
QStringList serveOnArgs(int port) {
    return {"serve", "--bg", "--tcp", QString::number(port), QString("tcp://127.0.0.1:%1").arg(port)};
}

QStringList serveOffArgs(int port) {
    return {"serve", "--tcp", QString::number(port), "off"};
}

QString connectionUrl(const Database &database, const QString &host) {
    const Engine *engine = engineById(database.engine);
    if (!engine || database.port == 0)
        return {};
    const QString address = QString("%1:%2").arg(host).arg(database.port);
    const QString password = QString::fromUtf8(QUrl::toPercentEncoding(database.password));
    const QString credentials = database.password.isEmpty() ? engine->user : engine->user + ":" + password;

    if (engine->id == "postgres")
        return QString("postgresql://%1@%2/postgres").arg(credentials, address);
    if (engine->id == "mysql" || engine->id == "mariadb")
        return QString("mysql://%1@%2").arg(credentials, address);
    if (engine->id == "redis")
        return database.password.isEmpty() ? "redis://" + address : QString("redis://:%1@%2").arg(password, address);
    if (engine->id == "mongo")
        return QString("mongodb://%1@%2").arg(credentials, address);
    if (engine->id == "clickhouse")
        return database.password.isEmpty() ? QString("http://%1@%2").arg(engine->user, address)
                                            : QString("http://%1:%2@%3").arg(engine->user, password, address);
    if (engine->id == "mssql")
        return QString("Server=%1,%2;User Id=sa;Password=%3;TrustServerCertificate=True")
            .arg(host).arg(database.port).arg(database.password);
    return {};
}

QString generatePassword(int length) {
    // Letters and digits only: no quoting trouble in URLs, shells, or Docker
    // labels, and SQL Server's complexity rule accepts upper, lower, and digit.
    static const QString alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    auto *random = QRandomGenerator::system();
    while (true) {
        QString password;
        for (int i = 0; i < length; ++i)
            password += alphabet.at(random->bounded(alphabet.size()));
        const bool upper = password.contains(QRegularExpression("[A-Z]"));
        const bool lower = password.contains(QRegularExpression("[a-z]"));
        const bool digit = password.contains(QRegularExpression("[0-9]"));
        if (upper && lower && digit)
            return password;
    }
}

QString blankPassword(const QString &engine) {
    // SQL Server won't start with a password of one kind of character.
    return engine == "mssql" ? QString() : QString("postgres");
}

QString passwordFor(const QString &engine, const QString &typed) {
    if (!typed.isEmpty())
        return typed;
    const QString blank = blankPassword(engine);
    return blank.isEmpty() ? generatePassword() : blank;
}

QString nameProblem(const QString &name) {
    if (name.isEmpty())
        return QCoreApplication::translate("Depot", "Give it a name.");
    static const QRegularExpression valid("^[a-zA-Z0-9][a-zA-Z0-9_.-]{0,62}$");
    if (!valid.match(name).hasMatch())
        return QCoreApplication::translate("Depot", "Use letters, digits, dots, dashes, and underscores.");
    return {};
}

QString passwordProblem(const QString &engine, const QString &password) {
    if (password.isEmpty())
        return {};
    // Shared on a tailnet, a short password is a door left ajar.
    if (password.size() < 8)
        return QCoreApplication::translate("Depot", "Use at least 8 characters.");
    if (password.contains(QRegularExpression("\\s")))
        return QCoreApplication::translate("Depot", "Leave out spaces.");
    if (engine == "mssql") {
        // SQL Server refuses to start without three of the four kinds.
        int kinds = 0;
        for (const char *kind : {"[A-Z]", "[a-z]", "[0-9]", "[^A-Za-z0-9]"})
            kinds += password.contains(QRegularExpression(kind));
        if (kinds < 3)
            return QCoreApplication::translate("Depot", "SQL Server needs three of: uppercase, lowercase, digits, symbols.");
    }
    return {};
}

QString volumeName(const QString &name) {
    return "depot-" + name;
}

QVariantMap toVariant(const Database &database, bool exposed, const Tailnet &tailnet) {
    const Engine *engine = engineById(database.engine);
    const QString host = !tailnet.dnsName.isEmpty() ? tailnet.dnsName : tailnet.ip;
    const bool everywhere = database.port > 0 && (database.hostIp.isEmpty() || database.hostIp == "0.0.0.0"
                                                  || database.hostIp == "::");
    return {
        {"name", database.name},
        {"engine", database.engine},
        {"engineLabel", engine ? engine->label : database.engine},
        {"image", database.image},
        {"state", database.state},
        {"running", database.running()},
        {"port", database.port},
        {"publishedEverywhere", everywhere},
        {"managed", database.managed},
        {"user", engine ? engine->user : QString()},
        {"password", database.password},
        {"exposed", exposed},
        {"host", host},
        {"ip", tailnet.ip},
        {"url", host.isEmpty() ? QString() : connectionUrl(database, host)},
        {"localUrl", connectionUrl(database, "127.0.0.1")},
        {"extraPorts", [&] {
             QVariantList list;
             for (const auto &extra : database.extraPorts)
                 list.append(QVariantMap{{"key", extra.first}, {"port", extra.second}});
             return list;
         }()},
    };
}
