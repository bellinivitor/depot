#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariantList>
#include <functional>

#include "engines.h"

// The databases on this machine and what the window can do with them. Every
// Docker and Tailscale call runs as an asynchronous process, so a slow image
// download never freezes the window.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList databases READ databases NOTIFY changed)
    // Names change far less often than the data, and a list keyed on them
    // keeps its delegates (and keyboard focus) while stats tick.
    Q_PROPERTY(QStringList names READ names NOTIFY namesChanged)
    Q_PROPERTY(QVariantMap totals READ totals NOTIFY changed)
    // Removed databases whose data was kept: creating one of these names
    // again has to choose between that data and a fresh start.
    Q_PROPERTY(QStringList leftoverNames READ leftoverNames NOTIFY changed)
    Q_PROPERTY(QVariantList engineList READ engineList CONSTANT)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(QString dockerProblem READ dockerProblem NOTIFY changed)
    Q_PROPERTY(bool dockerNeedsPermission READ dockerNeedsPermission NOTIFY changed)
    Q_PROPERTY(QString tailscaleProblem READ tailscaleProblem NOTIFY changed)
    Q_PROPERTY(QString tailnetHost READ tailnetHost NOTIFY changed)
    Q_PROPERTY(QString tailnetName READ tailnetName NOTIFY changed)
    Q_PROPERTY(QString tailnetIp READ tailnetIp NOTIFY changed)

public:
    // Empty paths use $DEPOT_DOCKER and $DEPOT_TAILSCALE, then docker and
    // tailscale from PATH. Tests and screenshots pass fakes.
    explicit Backend(const QString &docker = {}, const QString &tailscale = {}, QObject *parent = nullptr,
                     int refreshInterval = 3000);

    QVariantList databases() const;
    QStringList names() const { return m_names; }
    QVariantMap totals() const;
    QStringList leftoverNames() const;
    static constexpr int historyLength = 60;
    QVariantList engineList() const;
    bool loaded() const { return m_loaded; }
    QString dockerProblem() const { return m_dockerProblem; }
    bool dockerNeedsPermission() const { return m_dockerNeedsPermission; }
    QString tailscaleProblem() const { return m_tailscaleProblem; }
    QString tailnetHost() const { return m_tailnet.hostName; }
    QString tailnetName() const { return m_tailnet.dnsName; }
    QString tailnetIp() const { return m_tailnet.ip; }

    Q_INVOKABLE void refresh();
    // An empty password means Depot generates one.
    // existingData is "reuse" or "fresh" when the name has leftover data.
    Q_INVOKABLE void create(const QString &engine, const QString &name, int port, bool expose,
                            const QString &password = {}, const QString &version = {},
                            const QString &existingData = {});
    // Empty when the engine can keep old data and take a new password.
    Q_INVOKABLE QString reuseProblem(const QString &engine) const { return ::reuseProblem(engine); }
    Q_INVOKABLE void setRunning(const QString &name, bool running);
    Q_INVOKABLE void setExposed(const QString &name, bool exposed);
    Q_INVOKABLE void remove(const QString &name, bool deleteData);
    Q_INVOKABLE void copy(const QString &text);
    Q_INVOKABLE int suggestPort(const QString &engine) const;
    Q_INVOKABLE QString suggestName(const QString &engine) const;
    Q_INVOKABLE QString nameProblem(const QString &name) const;
    Q_INVOKABLE QString portProblem(int port) const;
    // The password a blank field gives, or empty when one is generated.
    Q_INVOKABLE QString blankPassword(const QString &engine) const { return ::blankPassword(engine); }
    Q_INVOKABLE QString passwordProblem(const QString &engine, const QString &password) const {
        return ::passwordProblem(engine, password);
    }
    Q_INVOKABLE void openDockerSetup();

signals:
    void changed();
    void namesChanged();
    void notice(const QString &message, bool error);
    void created(const QString &name);

private:
    using Done = std::function<void(bool ok, const QByteArray &out, const QByteArray &err)>;
    void run(const QString &program, const QStringList &args, Done done);
    void docker(const QStringList &args, Done done) { run(m_docker, args, std::move(done)); }
    void tailscale(const QStringList &args, Done done) { run(m_tailscale, args, std::move(done)); }
    void finishRefreshStep();
    // Turns sharing on or off for each port in turn, stopping at the first failure.
    void serveAll(QList<int> ports, bool on, Done done);
    void sampleStats();
    void publish();
    void readDockerProblem(const QByteArray &err);
    const Database *find(const QString &name) const;
    bool portTaken(int port) const;

    QString m_docker;
    QString m_tailscale;
    QTimer m_timer;

    QList<Database> m_databases;
    QSet<int> m_served;
    QSet<QString> m_volumes;
    Tailnet m_tailnet;
    QHash<QString, QString> m_pending;  // Name to engine, while docker run works
    QHash<QString, QString> m_pendingImages;
    QSet<QString> m_busy;
    QStringList m_names;

    QTimer m_statsTimer;
    bool m_sampling = false;
    QHash<QString, Stats> m_stats;
    QHash<QString, QList<double>> m_cpuHistory;
    QHash<QString, QList<double>> m_memoryHistory;

    QList<Database> m_nextDatabases;
    QSet<int> m_nextServed;
    QSet<QString> m_nextVolumes;
    Tailnet m_nextTailnet;
    QString m_nextTailscaleProblem;
    int m_refreshSteps = 0;
    bool m_loaded = false;
    QString m_dockerProblem;
    bool m_dockerNeedsPermission = false;
    QString m_tailscaleProblem;
};
