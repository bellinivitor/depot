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
    static const QList<Engine> list{
        {"postgres", "PostgreSQL", "postgres:18", 5432, "/var/lib/postgresql", "postgres", ""},
        {"mysql", "MySQL", "mysql:8.4", 3306, "/var/lib/mysql", "root", ""},
        {"mariadb", "MariaDB", "mariadb:11.8", 3306, "/var/lib/mysql", "root", ""},
        {"redis", "Redis", "redis:7", 6379, "/data", "default", ""},
        {"mongo", "MongoDB", "mongo:noble", 27017, "/data/db", "admin", "admin123"},
        {"mssql", "SQL Server", "mcr.microsoft.com/mssql/server:2022-latest", 1433, "/var/opt/mssql", "sa", "@dmin123"},
    };
    return list;
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

    if (repository.endsWith("mssql/server"))
        return engineById("mssql");
    return engineById(repository);
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
        const auto bindings = container.value("HostConfig").toObject().value("PortBindings").toObject()
                                  .value(QString::number(engine->port) + "/tcp").toArray();
        for (const auto &binding : bindings) {
            const auto port = binding.toObject().value("HostPort").toString().toInt();
            if (port > 0) {
                database.port = port;
                database.hostIp = binding.toObject().value("HostIp").toString();
                break;
            }
        }
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

QStringList createArgs(const Engine &engine, const QString &name, int port, const QString &password) {
    QStringList args{"run", "-d", "--restart", "unless-stopped", "--name", name,
                     "-p", QString("127.0.0.1:%1:%2").arg(port).arg(engine.port),
                     "-v", volumeName(name) + ":" + engine.dataPath,
                     "--label", "depot.engine=" + engine.id,
                     "--label", "depot.password=" + password};

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

    return args << engine.image << command;
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

QString nameProblem(const QString &name) {
    if (name.isEmpty())
        return QCoreApplication::translate("Depot", "Give it a name.");
    static const QRegularExpression valid("^[a-zA-Z0-9][a-zA-Z0-9_.-]{0,62}$");
    if (!valid.match(name).hasMatch())
        return QCoreApplication::translate("Depot", "Use letters, digits, dots, dashes, and underscores.");
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
    };
}
