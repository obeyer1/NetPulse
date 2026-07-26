#pragma once

#include "core/Types.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <QThreadPool>

#include <atomic>
#include <memory>

class QNetworkAccessManager;
class QTimer;

namespace netpulse {

// ---------------------------------------------------------------------------
// ProbeScheduler — lives on a dedicated worker thread. Ticks twice a second,
// dispatches due probes, enforces the minimum interval, and guards against
// overlapping probes of the same target.
// ---------------------------------------------------------------------------
class ProbeScheduler : public QObject {
    Q_OBJECT
public:
    explicit ProbeScheduler(QObject *parent = nullptr);

public slots:
    void start();
    void setConfig(const netpulse::ProbeConfig &cfg);
    void setTargets(const QList<netpulse::TargetSpec> &targets);
    void upsertTarget(const netpulse::TargetSpec &spec);
    void removeTarget(int targetId);
    void setPaused(bool paused);
    void probeNow(int targetId);
    void shutdown();

signals:
    void sampleReady(const netpulse::ProbeSample &sample);
    void probeStarted(int targetId);

private:
    struct Entry {
        TargetSpec spec;
        qint64 nextDueMs = 0;
        bool inFlight = false;
        QPointer<QObject> activeProbe;                    // DnsProbe / HttpsProbe
        std::shared_ptr<std::atomic_bool> cancelFlag;     // ping batches
    };

    void onTick();
    void dispatch(Entry &entry);
    void onSampleFinished(const ProbeSample &sample);
    void cancelEntry(Entry &entry);

    QHash<int, Entry> entries_;
    ProbeConfig cfg_;
    QTimer *tick_ = nullptr;
    QNetworkAccessManager *nam_ = nullptr;
    QThreadPool pingPool_;
    bool paused_ = false;
    bool shuttingDown_ = false;
};

// ---------------------------------------------------------------------------
// ProbeService — thin UI-thread facade owning the worker thread.
// ---------------------------------------------------------------------------
class ProbeService : public QObject {
    Q_OBJECT
public:
    explicit ProbeService(QObject *parent = nullptr);
    ~ProbeService() override;

    void setConfig(const ProbeConfig &cfg);
    void setTargets(const QList<TargetSpec> &targets);
    void upsertTarget(const TargetSpec &spec);
    void removeTarget(int targetId);
    void setPaused(bool paused);
    void probeNow(int targetId);
    void shutdownAndWait();

signals:
    void sampleReady(const netpulse::ProbeSample &sample);
    void probeStarted(int targetId);

private:
    QThread *thread_ = nullptr;
    ProbeScheduler *worker_ = nullptr;
    bool stopped_ = false;
};

} // namespace netpulse
