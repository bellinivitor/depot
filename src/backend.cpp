#include "backend.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QHostAddress>
#include <QLocale>
#include <QProcess>
#include <QStandardPaths>
#include <QTcpServer>

static QString program(const QString &given, const char *variable, const QString &fallback) {
    if (!given.isEmpty())
        return given;
    const QString overridden = qEnvironmentVariable(variable);
    return overridden.isEmpty() ? fallback : overridden;
}

Backend::Backend(const QString &docker, const QString &tailscale, QObject *parent, int refreshInterval)
    : QObject(parent),
      m_docker(program(docker, "DEPOT_DOCKER", "docker")),
      m_tailscale(program(tailscale, "DEPOT_TAILSCALE", "tailscale")) {
    // Containers and serve config also change from the terminal, so keep
    // looking rather than trusting only what this window did.
    connect(&m_timer, &QTimer::timeout, this, &Backend::refresh);
    // Stats sample back to back: `docker stats --no-stream` itself takes a
    // second to measure, so a fixed timer would pile calls up.
    m_statsTimer.setSingleShot(true);
    connect(&m_statsTimer, &QTimer::timeout, this, &Backend::sampleStats);
    if (refreshInterval > 0) {
        m_timer.start(refreshInterval);
        m_statsTimer.setInterval(qMin(refreshInterval, 1000));
        refresh();
    }
}

void Backend::run(const QString &program, const QStringList &args, Done done) {
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [process, done](int code, QProcess::ExitStatus status) {
        done(status == QProcess::NormalExit && code == 0, process->readAllStandardOutput(),
             process->readAllStandardError());
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, program, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        done(false, {}, tr("%1 is not installed.").arg(program).toUtf8());
        process->deleteLater();
    });
    process->start(program, args);
}

void Backend::readDockerProblem(const QByteArray &err) {
    const QString text = QString::fromUtf8(err).trimmed();
    m_dockerNeedsPermission = text.contains("permission denied", Qt::CaseInsensitive);
    if (m_dockerNeedsPermission)
        m_dockerProblem = tr("Depot can't reach Docker. Omarchy keeps Docker behind a password prompt, and Depot "
                             "needs to talk to it directly: turn on sudoless Docker, then reboot.");
    else if (text.contains("Cannot connect to the Docker daemon"))
        m_dockerProblem = tr("Docker isn't running. Start it with: sudo systemctl start docker");
    else
        m_dockerProblem = text.isEmpty() ? tr("Docker failed without saying why.") : text;
}

void Backend::refresh() {
    if (m_refreshSteps > 0)
        return;
    m_refreshSteps = 3;
    m_nextDatabases.clear();
    m_nextServed.clear();
    m_nextTailnet = {};
    m_nextTailscaleProblem.clear();

    docker({"ps", "-aq", "--no-trunc"}, [this](bool ok, const QByteArray &out, const QByteArray &err) {
        if (!ok) {
            readDockerProblem(err);
            finishRefreshStep();
            return;
        }
        const QStringList ids = QString::fromUtf8(out).split('\n', Qt::SkipEmptyParts);
        if (ids.isEmpty()) {
            m_dockerProblem.clear();
            m_dockerNeedsPermission = false;
            finishRefreshStep();
            return;
        }
        docker(QStringList{"inspect"} + ids, [this](bool ok, const QByteArray &out, const QByteArray &err) {
            // A container removed between ps and inspect makes inspect fail
            // but still print the rest, so read what came back either way.
            m_nextDatabases = parseInspect(out);
            if (ok || !m_nextDatabases.isEmpty()) {
                m_dockerProblem.clear();
                m_dockerNeedsPermission = false;
            } else {
                readDockerProblem(err);
            }
            finishRefreshStep();
        });
    });

    tailscale({"status", "--json"}, [this](bool, const QByteArray &out, const QByteArray &err) {
        m_nextTailnet = parseTailnet(out);
        if (!m_nextTailnet.running && m_nextTailscaleProblem.isEmpty()) {
            const QString text = QString::fromUtf8(err).trimmed();
            m_nextTailscaleProblem = !text.isEmpty() && m_nextTailnet.state.isEmpty()
                ? text
                : tr("Tailscale isn't connected. Run: tailscale up");
        }
        finishRefreshStep();
    });

    tailscale({"serve", "status", "--json"}, [this](bool ok, const QByteArray &out, const QByteArray &) {
        if (ok)
            m_nextServed = parseServedPorts(out);
        finishRefreshStep();
    });
}

void Backend::publish() {
    QStringList names;
    for (const auto &item : databases())
        names.append(item.toMap().value("name").toString());
    if (names != m_names) {
        m_names = names;
        emit namesChanged();
    }
    emit changed();
}

void Backend::sampleStats() {
    QStringList running;
    for (const auto &database : m_databases)
        if (database.running())
            running.append(database.name);
    if (m_sampling)
        return;
    if (running.isEmpty()) {
        m_stats.clear();
        if (m_statsTimer.interval() > 0)
            m_statsTimer.start();
        return;
    }
    m_sampling = true;
    docker(QStringList{"stats", "--no-stream", "--format", "{{json .}}"} + running,
           [this](bool, const QByteArray &out, const QByteArray &) {
        m_sampling = false;
        // A container that stopped between samples simply drops out.
        m_stats = parseStats(out);
        for (auto it = m_stats.begin(); it != m_stats.end(); ++it) {
            auto &cpu = m_cpuHistory[it.key()];
            auto &memory = m_memoryHistory[it.key()];
            cpu.append(it->cpu);
            memory.append(it->memory);
            while (cpu.size() > historyLength)
                cpu.removeFirst();
            while (memory.size() > historyLength)
                memory.removeFirst();
        }
        publish();
        if (m_statsTimer.interval() > 0)
            m_statsTimer.start();
    });
}

void Backend::finishRefreshStep() {
    if (--m_refreshSteps > 0)
        return;
    // Without Docker there is nothing trustworthy to show, so show nothing
    // rather than a stale list.
    m_databases = m_dockerProblem.isEmpty() ? m_nextDatabases : QList<Database>();
    m_served = m_nextServed;
    m_tailnet = m_nextTailnet;
    m_tailscaleProblem = m_nextTailscaleProblem;
    const bool first = !m_loaded;
    m_loaded = true;
    publish();
    if (first && m_statsTimer.interval() > 0)
        sampleStats();
}

QVariantList Backend::databases() const {
    QVariantList list;
    for (const auto &database : m_databases) {
        auto item = toVariant(database, database.port > 0 && m_served.contains(database.port), m_tailnet);
        item["busy"] = m_busy.contains(database.name);
        if (database.running() && m_stats.contains(database.name)) {
            const Stats &stats = m_stats[database.name];
            const QLocale locale;
            auto size = [&](double bytes) { return locale.formattedDataSize(qint64(bytes), 1, QLocale::DataSizeIecFormat); };
            QVariantList cpu, memory;
            for (double value : m_cpuHistory.value(database.name))
                cpu.append(value);
            for (double value : m_memoryHistory.value(database.name))
                memory.append(value);
            item["stats"] = QVariantMap{
                {"cpu", stats.cpu},
                {"cpuText", locale.toString(stats.cpu, 'f', stats.cpu < 10 ? 1 : 0) + "%"},
                {"memory", stats.memory},
                {"memoryText", size(stats.memory)},
                {"memoryLimitText", size(stats.memoryLimit)},
                {"memoryShare", stats.memoryLimit > 0 ? stats.memory / stats.memoryLimit : 0.0},
                {"netInText", size(stats.netIn)},
                {"netOutText", size(stats.netOut)},
                {"diskReadText", size(stats.diskRead)},
                {"diskWriteText", size(stats.diskWrite)},
                {"processes", stats.processes},
                {"cpuHistory", cpu},
                {"memoryHistory", memory},
            };
        }
        list.append(item);
    }
    // Databases still downloading or starting show up right away, so the
    // window answers the moment someone presses Create.
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
        if (find(it.key()))
            continue;
        const Engine *engine = engineById(it.value());
        list.append(QVariantMap{{"name", it.key()}, {"engine", it.value()},
                                {"engineLabel", engine ? engine->label : it.value()},
                                {"image", m_pendingImages.value(it.key())}, {"state", "creating"},
                                {"running", false}, {"busy", true}, {"port", 0}, {"exposed", false},
                                {"managed", true}});
    }
    std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("name").toString() < b.toMap().value("name").toString();
    });
    return list;
}

QVariantMap Backend::totals() const {
    int running = 0, shared = 0;
    double cpu = 0, memory = 0;
    for (const auto &database : m_databases) {
        running += database.running();
        shared += database.port > 0 && m_served.contains(database.port);
        if (database.running() && m_stats.contains(database.name)) {
            cpu += m_stats[database.name].cpu;
            memory += m_stats[database.name].memory;
        }
    }
    const QLocale locale;
    return {
        {"count", m_databases.size()},
        {"running", running},
        {"shared", shared},
        {"sampled", !m_stats.isEmpty()},
        {"cpuText", locale.toString(cpu, 'f', cpu < 10 ? 1 : 0) + "%"},
        {"memoryText", locale.formattedDataSize(qint64(memory), 1, QLocale::DataSizeIecFormat)},
    };
}

QVariantList Backend::engineList() const {
    QVariantList list;
    for (const auto &engine : engines()) {
        QVariantList versions;
        for (const auto &version : engine.versions)
            versions.append(version.label);
        list.append(QVariantMap{{"id", engine.id}, {"label", engine.label}, {"image", engine.image()},
                                {"versions", versions},
                                {"port", engine.port}});
    }
    return list;
}

const Database *Backend::find(const QString &name) const {
    for (const auto &database : m_databases)
        if (database.name == name)
            return &database;
    return nullptr;
}

void Backend::serveAll(QList<int> ports, bool on, Done done) {
    if (ports.isEmpty()) {
        done(true, {}, {});
        return;
    }
    const int port = ports.takeFirst();
    tailscale(on ? serveOnArgs(port) : serveOffArgs(port),
              [this, ports, on, done](bool ok, const QByteArray &out, const QByteArray &err) {
        if (!ok)
            done(false, out, err);
        else
            serveAll(ports, on, done);
    });
}

bool Backend::portTaken(int port) const {
    for (const auto &database : m_databases)
        if (database.ports().contains(port))
            return true;
    if (m_served.contains(port))
        return true;
    // Something outside Docker may hold it too.
    QTcpServer probe;
    return !probe.listen(QHostAddress::LocalHost, port);
}

int Backend::suggestPort(const QString &engine) const {
    const Engine *found = engineById(engine);
    int port = found ? found->port : 5432;
    while (port < 65535 && portTaken(port))
        ++port;
    return port;
}

QString Backend::suggestName(const QString &engine) const {
    for (int i = 1;; ++i) {
        const QString name = i == 1 ? engine : QString("%1-%2").arg(engine).arg(i);
        if (!find(name) && !m_pending.contains(name))
            return name;
    }
}

QString Backend::nameProblem(const QString &name) const {
    const QString problem = ::nameProblem(name);
    if (!problem.isEmpty())
        return problem;
    if (find(name) || m_pending.contains(name))
        return tr("A container with that name already exists.");
    return {};
}

QString Backend::portProblem(int port) const {
    if (port < 1024 || port > 65535)
        return tr("Pick a port between 1024 and 65535.");
    if (portTaken(port))
        return tr("Port %1 is already in use.").arg(port);
    return {};
}

void Backend::create(const QString &engineId, const QString &name, int port, bool expose, const QString &password,
                     const QString &version) {
    const Engine *engine = engineById(engineId);
    if (!engine || !engine->version(version) || !nameProblem(name).isEmpty() || !portProblem(port).isEmpty()
        || !::passwordProblem(engineId, password).isEmpty())
        return;

    m_pending.insert(name, engineId);
    publish();

    m_pendingImages.insert(name, engine->image(version));
    // Extra ports take the engine's usual numbers when free, like the main one.
    QList<int> extras;
    for (const auto &extra : engine->extraPorts) {
        int candidate = extra.port;
        while (candidate < 65535 && (candidate == port || extras.contains(candidate) || portTaken(candidate)))
            ++candidate;
        extras << candidate;
    }
    docker(createArgs(*engine, name, port, passwordFor(engineId, password), version, extras),
           [this, name, port, extras, expose](bool ok, const QByteArray &, const QByteArray &err) {
        m_pending.remove(name);
        m_pendingImages.remove(name);
        if (!ok) {
            emit notice(tr("Couldn't create %1: %2").arg(name, QString::fromUtf8(err).trimmed()), true);
            refresh();
            return;
        }
        emit created(name);
        if (!expose) {
            refresh();
            return;
        }
        serveAll(QList<int>{port} + extras, true, [this, name](bool ok, const QByteArray &, const QByteArray &err) {
            if (!ok)
                emit notice(tr("%1 is running, but sharing it on the tailnet failed: %2")
                                .arg(name, QString::fromUtf8(err).trimmed()), true);
            refresh();
        });
    });
}

void Backend::setRunning(const QString &name, bool running) {
    const Database *database = find(name);
    if (!database || m_busy.contains(name) || database->running() == running)
        return;
    m_busy.insert(name);
    publish();
    docker({running ? "start" : "stop", name}, [this, name](bool ok, const QByteArray &, const QByteArray &err) {
        m_busy.remove(name);
        if (!ok)
            emit notice(QString::fromUtf8(err).trimmed(), true);
        refresh();
    });
}

void Backend::setExposed(const QString &name, bool exposed) {
    const Database *database = find(name);
    if (!database || database->port == 0 || m_busy.contains(name)
        || m_served.contains(database->port) == exposed)
        return;
    m_busy.insert(name);
    publish();
    QList<int> ports;
    for (const int port : database->ports())
        if (m_served.contains(port) != exposed)
            ports << port;
    serveAll(ports, exposed, [this, name](bool ok, const QByteArray &, const QByteArray &err) {
        m_busy.remove(name);
        if (!ok) {
            QString text = QString::fromUtf8(err).trimmed();
            if (text.contains("Access denied", Qt::CaseInsensitive))
                text = tr("Tailscale refused. Let your user manage it once with: sudo tailscale set --operator=$USER");
            emit notice(text, true);
        }
        refresh();
    });
}

void Backend::remove(const QString &name, bool deleteData) {
    const Database *database = find(name);
    if (!database || m_busy.contains(name))
        return;
    m_busy.insert(name);
    publish();

    QList<int> served;
    for (const int port : database->ports())
        if (m_served.contains(port))
            served << port;
    const bool managed = database->managed;
    auto removeContainer = [this, name, deleteData, managed] {
        docker({"rm", "-f", name}, [this, name, deleteData, managed](bool ok, const QByteArray &, const QByteArray &err) {
            if (!ok || !deleteData || !managed) {
                m_busy.remove(name);
                if (!ok)
                    emit notice(QString::fromUtf8(err).trimmed(), true);
                refresh();
                return;
            }
            docker({"volume", "rm", volumeName(name)}, [this, name](bool ok, const QByteArray &, const QByteArray &err) {
                m_busy.remove(name);
                if (!ok)
                    emit notice(QString::fromUtf8(err).trimmed(), true);
                refresh();
            });
        });
    };
    // Stop sharing first, so the tailnet never forwards to a port that a
    // later database might reuse.
    serveAll(served, false, [removeContainer](bool, const QByteArray &, const QByteArray &) { removeContainer(); });
}

void Backend::copy(const QString &text) {
    if (auto *clipboard = QGuiApplication::clipboard())
        clipboard->setText(text);
}

void Backend::openDockerSetup() {
    const QString launcher = QStandardPaths::findExecutable("omarchy-launch-floating-terminal-with-presentation");
    if (launcher.isEmpty() || !QProcess::startDetached(launcher, {"omarchy-setup-security-sudoless-docker"}))
        emit notice(tr("Couldn't open the setup. Run omarchy-setup-security-sudoless-docker in a terminal."), true);
}
