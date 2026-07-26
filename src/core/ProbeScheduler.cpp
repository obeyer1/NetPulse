#include "core/ProbeScheduler.h"

#include "core/IcmpPinger.h"
#include "core/Probes.h"

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QTimer>

namespace netpulse {

// ---------------------------------------------------------------------------
// ProbeScheduler
// ---------------------------------------------------------------------------

ProbeScheduler::ProbeScheduler(QObject *parent)
    : QObject(parent)
{
    pingPool_.setMaxThreadCount(4);
}

void ProbeScheduler::start()
{
    if (tick_)
        return;
    nam_ = new QNetworkAccessManager(this);
    tick_ = new QTimer(this);
    tick_->setInterval(500);
    connect(tick_, &QTimer::timeout, this, &ProbeScheduler::onTick);
    tick_->start();
}

void ProbeScheduler::setConfig(const ProbeConfig &cfg)
{
    cfg_ = cfg;
}

void ProbeScheduler::setTargets(const QList<TargetSpec> &targets)
{
    QHash<int, Entry> next;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    int stagger = 0;
    for (const TargetSpec &t : targets) {
        TargetSpec spec = t;
        spec.intervalSecs = clampIntervalSecs(spec.intervalSecs);
        Entry e;
        if (entries_.contains(spec.id))
            e = entries_.take(spec.id);
        e.spec = spec;
        if (e.nextDueMs == 0)
            e.nextDueMs = now + 250 * stagger++; // spread initial probes out a little
        next.insert(spec.id, e);
    }
    // Anything left in entries_ was removed: cancel its in-flight work.
    for (auto &orphan : entries_)
        cancelEntry(orphan);
    entries_ = std::move(next);
}

void ProbeScheduler::upsertTarget(const TargetSpec &specIn)
{
    TargetSpec spec = specIn;
    spec.intervalSecs = clampIntervalSecs(spec.intervalSecs);
    auto it = entries_.find(spec.id);
    if (it == entries_.end()) {
        Entry e;
        e.spec = spec;
        e.nextDueMs = QDateTime::currentMSecsSinceEpoch();
        entries_.insert(spec.id, e);
    } else {
        it->spec = spec;
        if (!spec.enabled)
            cancelEntry(*it);
    }
}

void ProbeScheduler::removeTarget(int targetId)
{
    auto it = entries_.find(targetId);
    if (it == entries_.end())
        return;
    cancelEntry(*it);
    entries_.erase(it);
}

void ProbeScheduler::setPaused(bool paused)
{
    paused_ = paused;
}

void ProbeScheduler::probeNow(int targetId)
{
    auto it = entries_.find(targetId);
    if (it == entries_.end() || it->inFlight)
        return;
    it->nextDueMs = 0;
    onTick();
}

void ProbeScheduler::onTick()
{
    if (paused_ || shuttingDown_)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto &entry : entries_) {
        if (!entry.spec.enabled || entry.inFlight || now < entry.nextDueMs)
            continue;
        dispatch(entry);
    }
}

void ProbeScheduler::dispatch(Entry &entry)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    entry.inFlight = true;
    entry.nextDueMs = now + static_cast<qint64>(clampIntervalSecs(entry.spec.intervalSecs)) * 1000;
    emit probeStarted(entry.spec.id);

    const TargetSpec spec = entry.spec;
    const ProbeConfig cfg = cfg_;

    switch (spec.kind) {
    case ProbeKind::Ping: {
        auto flag = std::make_shared<std::atomic_bool>(false);
        entry.cancelFlag = flag;
        pingPool_.start([this, spec, cfg, flag] {
            IcmpPinger::Options opt;
            opt.count = spec.pingCount;
            opt.timeoutMsPerReply = cfg.pingTimeoutMs;
            opt.spacingMs = cfg.pingSpacingMs;
            const PingBatchResult r = IcmpPinger::run(spec.host, opt, *flag);

            ProbeSample s;
            s.targetId = spec.id;
            s.timestampMs = QDateTime::currentMSecsSinceEpoch();
            s.kind = ProbeKind::Ping;
            s.resolvedAddress = r.resolvedAddress;
            s.sent = r.sent;
            s.received = r.received;
            s.latencyMs = r.avgMs;
            s.jitterMs = r.jitterMs;
            s.lossPct = r.lossPct;
            s.error = r.error;
            if (r.received == 0)
                s.status = SampleStatus::Failed;
            else if (r.received < r.sent || !r.error.isEmpty())
                s.status = SampleStatus::Degraded;
            else
                s.status = SampleStatus::Ok;

            if (flag->load())
                return; // shutting down or target removed; drop silently
            QMetaObject::invokeMethod(this, [this, s] { onSampleFinished(s); },
                                      Qt::QueuedConnection);
        });
        break;
    }
    case ProbeKind::Dns: {
        auto *probe = new DnsProbe(spec, cfg, this);
        entry.activeProbe = probe;
        connect(probe, &DnsProbe::finished, this, [this, probe](const ProbeSample &s) {
            probe->deleteLater();
            onSampleFinished(s);
        });
        probe->start();
        break;
    }
    case ProbeKind::Https: {
        auto *probe = new HttpsProbe(spec, cfg, nam_, this);
        entry.activeProbe = probe;
        connect(probe, &HttpsProbe::finished, this, [this, probe](const ProbeSample &s) {
            probe->deleteLater();
            onSampleFinished(s);
        });
        probe->start();
        break;
    }
    }
}

void ProbeScheduler::onSampleFinished(const ProbeSample &sample)
{
    auto it = entries_.find(sample.targetId);
    if (it != entries_.end()) {
        it->inFlight = false;
        it->activeProbe.clear();
        it->cancelFlag.reset();
    }
    if (!shuttingDown_)
        emit sampleReady(sample);
}

void ProbeScheduler::cancelEntry(Entry &entry)
{
    if (entry.cancelFlag)
        entry.cancelFlag->store(true);
    if (auto *dns = qobject_cast<DnsProbe *>(entry.activeProbe.data()))
        dns->cancel();
    else if (auto *https = qobject_cast<HttpsProbe *>(entry.activeProbe.data()))
        https->cancel();
    entry.inFlight = false;
}

void ProbeScheduler::shutdown()
{
    shuttingDown_ = true;
    if (tick_)
        tick_->stop();
    for (auto &entry : entries_)
        cancelEntry(entry);
    pingPool_.waitForDone(4000);
}

// ---------------------------------------------------------------------------
// ProbeService
// ---------------------------------------------------------------------------

ProbeService::ProbeService(QObject *parent)
    : QObject(parent)
{
    thread_ = new QThread(this);
    thread_->setObjectName(QStringLiteral("netpulse-probes"));
    worker_ = new ProbeScheduler;
    worker_->moveToThread(thread_);

    connect(worker_, &ProbeScheduler::sampleReady, this, &ProbeService::sampleReady);
    connect(worker_, &ProbeScheduler::probeStarted, this, &ProbeService::probeStarted);

    thread_->start();
    QMetaObject::invokeMethod(worker_, &ProbeScheduler::start, Qt::QueuedConnection);
}

ProbeService::~ProbeService()
{
    shutdownAndWait();
}

void ProbeService::setConfig(const ProbeConfig &cfg)
{
    QMetaObject::invokeMethod(worker_, [w = worker_, cfg] { w->setConfig(cfg); },
                              Qt::QueuedConnection);
}

void ProbeService::setTargets(const QList<TargetSpec> &targets)
{
    QMetaObject::invokeMethod(worker_, [w = worker_, targets] { w->setTargets(targets); },
                              Qt::QueuedConnection);
}

void ProbeService::upsertTarget(const TargetSpec &spec)
{
    QMetaObject::invokeMethod(worker_, [w = worker_, spec] { w->upsertTarget(spec); },
                              Qt::QueuedConnection);
}

void ProbeService::removeTarget(int targetId)
{
    QMetaObject::invokeMethod(worker_, [w = worker_, targetId] { w->removeTarget(targetId); },
                              Qt::QueuedConnection);
}

void ProbeService::setPaused(bool paused)
{
    QMetaObject::invokeMethod(worker_, [w = worker_, paused] { w->setPaused(paused); },
                              Qt::QueuedConnection);
}

void ProbeService::probeNow(int targetId)
{
    QMetaObject::invokeMethod(worker_, [w = worker_, targetId] { w->probeNow(targetId); },
                              Qt::QueuedConnection);
}

void ProbeService::shutdownAndWait()
{
    if (stopped_)
        return;
    stopped_ = true;
    QMetaObject::invokeMethod(worker_, &ProbeScheduler::shutdown,
                              Qt::BlockingQueuedConnection);
    thread_->quit();
    thread_->wait(5000);
    delete worker_;
    worker_ = nullptr;
}

} // namespace netpulse
