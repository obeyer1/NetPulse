#include "storage/Storage.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimer>
#include <QVariant>

namespace netpulse {
namespace {

constexpr int kFlushIntervalMs = 2000;
constexpr int kFlushBatchSize = 64;
constexpr int kPruneIntervalMs = 60 * 60 * 1000; // hourly

TargetSpec specFromQuery(const QSqlQuery &q)
{
    TargetSpec t;
    t.id = q.value(0).toInt();
    t.name = q.value(1).toString();
    t.kind = probeKindFromString(q.value(2).toString());
    t.host = q.value(3).toString();
    t.dnsServer = q.value(4).toString();
    t.intervalSecs = clampIntervalSecs(q.value(5).toInt());
    t.pingCount = qBound(1, q.value(6).toInt(), 10);
    t.enabled = q.value(7).toInt() != 0;
    return t;
}

} // namespace

// ---------------------------------------------------------------------------
// DatabaseWorker
// ---------------------------------------------------------------------------

DatabaseWorker::DatabaseWorker(QObject *parent)
    : QObject(parent)
{
}

void DatabaseWorker::open(const QString &dbPath)
{
    connectionName_ = QStringLiteral("netpulse_db");
    db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    db_.setDatabaseName(dbPath);

    if (!db_.open()) {
        emit opened(false, db_.lastError().text(), dbPath);
        return;
    }

    QSqlQuery pragma(db_);
    pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));

    if (!migrate()) {
        emit opened(false, db_.lastError().text(), dbPath);
        return;
    }

    flushTimer_ = new QTimer(this);
    flushTimer_->setInterval(kFlushIntervalMs);
    connect(flushTimer_, &QTimer::timeout, this, &DatabaseWorker::flush);
    flushTimer_->start();

    pruneTimer_ = new QTimer(this);
    pruneTimer_->setInterval(kPruneIntervalMs);
    connect(pruneTimer_, &QTimer::timeout, this, &DatabaseWorker::prune);
    pruneTimer_->start();

    ready_ = true;
    prune();
    emit opened(true, QString(), dbPath);
}

bool DatabaseWorker::migrate()
{
    QSqlQuery q(db_);
    const bool okTargets = q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS targets ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " name TEXT NOT NULL,"
        " kind TEXT NOT NULL,"
        " host TEXT NOT NULL,"
        " dns_server TEXT NOT NULL DEFAULT '',"
        " interval_secs INTEGER NOT NULL DEFAULT 30,"
        " ping_count INTEGER NOT NULL DEFAULT 5,"
        " enabled INTEGER NOT NULL DEFAULT 1,"
        " created_ms INTEGER NOT NULL)"));
    const bool okSamples = q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS samples ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " target_id INTEGER NOT NULL,"
        " ts_ms INTEGER NOT NULL,"
        " kind TEXT NOT NULL,"
        " status INTEGER NOT NULL,"
        " latency_ms REAL,"
        " jitter_ms REAL,"
        " loss_pct REAL,"
        " sent INTEGER,"
        " received INTEGER,"
        " http_status INTEGER,"
        " ttfb_ms REAL,"
        " resolved TEXT,"
        " error TEXT,"
        " attempts INTEGER NOT NULL DEFAULT 1)"));
    const bool okIndex = q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_samples_target_ts"
        " ON samples(target_id, ts_ms)"));
    return okTargets && okSamples && okIndex;
}

void DatabaseWorker::saveSample(const ProbeSample &sample)
{
    if (!ready_)
        return;
    pending_.append(sample);
    if (pending_.size() >= kFlushBatchSize)
        flush();
}

void DatabaseWorker::flush()
{
    if (!ready_ || pending_.isEmpty())
        return;

    db_.transaction();
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "INSERT INTO samples (target_id, ts_ms, kind, status, latency_ms, jitter_ms,"
        " loss_pct, sent, received, http_status, ttfb_ms, resolved, error, attempts)"
        " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    for (const ProbeSample &s : std::as_const(pending_)) {
        q.addBindValue(s.targetId);
        q.addBindValue(s.timestampMs);
        q.addBindValue(probeKindName(s.kind));
        q.addBindValue(static_cast<int>(s.status));
        q.addBindValue(s.latencyMs < 0 ? QVariant() : QVariant(s.latencyMs));
        q.addBindValue(s.jitterMs < 0 ? QVariant() : QVariant(s.jitterMs));
        q.addBindValue(s.lossPct < 0 ? QVariant() : QVariant(s.lossPct));
        q.addBindValue(s.sent);
        q.addBindValue(s.received);
        q.addBindValue(s.httpStatus);
        q.addBindValue(s.ttfbMs < 0 ? QVariant() : QVariant(s.ttfbMs));
        q.addBindValue(s.resolvedAddress);
        q.addBindValue(s.error);
        q.addBindValue(s.attempts);
        q.exec();
    }
    db_.commit();
    pending_.clear();
}

void DatabaseWorker::upsertTarget(const TargetSpec &specIn)
{
    if (!ready_)
        return;
    TargetSpec spec = specIn;
    spec.intervalSecs = clampIntervalSecs(spec.intervalSecs);

    QSqlQuery q(db_);
    if (spec.id < 0) {
        q.prepare(QStringLiteral(
            "INSERT INTO targets (name, kind, host, dns_server, interval_secs,"
            " ping_count, enabled, created_ms) VALUES (?,?,?,?,?,?,?,?)"));
        q.addBindValue(spec.name);
        q.addBindValue(probeKindName(spec.kind));
        q.addBindValue(spec.host);
        q.addBindValue(spec.dnsServer);
        q.addBindValue(spec.intervalSecs);
        q.addBindValue(spec.pingCount);
        q.addBindValue(spec.enabled ? 1 : 0);
        q.addBindValue(QDateTime::currentMSecsSinceEpoch());
        if (q.exec())
            spec.id = q.lastInsertId().toInt();
    } else {
        q.prepare(QStringLiteral(
            "UPDATE targets SET name=?, kind=?, host=?, dns_server=?, interval_secs=?,"
            " ping_count=?, enabled=? WHERE id=?"));
        q.addBindValue(spec.name);
        q.addBindValue(probeKindName(spec.kind));
        q.addBindValue(spec.host);
        q.addBindValue(spec.dnsServer);
        q.addBindValue(spec.intervalSecs);
        q.addBindValue(spec.pingCount);
        q.addBindValue(spec.enabled ? 1 : 0);
        q.addBindValue(spec.id);
        q.exec();
    }
    emit targetSaved(spec);
}

void DatabaseWorker::removeTarget(int targetId)
{
    if (!ready_)
        return;
    flush();
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("DELETE FROM samples WHERE target_id=?"));
    q.addBindValue(targetId);
    q.exec();
    q.prepare(QStringLiteral("DELETE FROM targets WHERE id=?"));
    q.addBindValue(targetId);
    q.exec();
    emit targetRemoved(targetId);
}

void DatabaseWorker::loadTargets()
{
    if (!ready_)
        return;
    QList<TargetSpec> targets;
    QSqlQuery q(db_);
    q.exec(QStringLiteral(
        "SELECT id, name, kind, host, dns_server, interval_secs, ping_count, enabled"
        " FROM targets ORDER BY id"));
    while (q.next())
        targets << specFromQuery(q);
    emit targetsLoaded(targets);
}

void DatabaseWorker::fetchSamples(quint64 requestId, int targetId, qint64 fromMs,
                                  qint64 toMs, int maxPoints)
{
    if (!ready_)
        return;
    flush(); // make sure the freshest points are visible to this query

    QList<ProbeSample> samples;
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "SELECT target_id, ts_ms, kind, status, latency_ms, jitter_ms, loss_pct,"
        " sent, received, http_status, ttfb_ms, resolved, error, attempts"
        " FROM samples WHERE target_id=? AND ts_ms BETWEEN ? AND ? ORDER BY ts_ms"));
    q.addBindValue(targetId);
    q.addBindValue(fromMs);
    q.addBindValue(toMs);
    q.exec();
    while (q.next()) {
        ProbeSample s;
        s.targetId = q.value(0).toInt();
        s.timestampMs = q.value(1).toLongLong();
        s.kind = probeKindFromString(q.value(2).toString());
        s.status = static_cast<SampleStatus>(q.value(3).toInt());
        s.latencyMs = q.value(4).isNull() ? -1 : q.value(4).toDouble();
        s.jitterMs = q.value(5).isNull() ? -1 : q.value(5).toDouble();
        s.lossPct = q.value(6).isNull() ? -1 : q.value(6).toDouble();
        s.sent = q.value(7).toInt();
        s.received = q.value(8).toInt();
        s.httpStatus = q.value(9).toInt();
        s.ttfbMs = q.value(10).isNull() ? -1 : q.value(10).toDouble();
        s.resolvedAddress = q.value(11).toString();
        s.error = q.value(12).toString();
        s.attempts = q.value(13).toInt();
        samples << s;
    }

    // Downsample politely for the charts, always keeping failed samples so
    // outages remain visible.
    if (maxPoints > 0 && samples.size() > maxPoints) {
        const int stride = static_cast<int>((samples.size() + maxPoints - 1) / maxPoints);
        QList<ProbeSample> reduced;
        reduced.reserve(maxPoints + 16);
        for (qsizetype i = 0; i < samples.size(); ++i) {
            if (samples[i].status == SampleStatus::Failed || i % stride == 0)
                reduced << samples[i];
        }
        samples = reduced;
    }

    emit samplesFetched(requestId, targetId, samples);
}

void DatabaseWorker::setRetentionDays(int days)
{
    retentionDays_ = qBound(1, days, 365);
    prune();
}

void DatabaseWorker::prune()
{
    if (!ready_)
        return;
    const qint64 cutoff = QDateTime::currentMSecsSinceEpoch()
        - static_cast<qint64>(retentionDays_) * 24 * 60 * 60 * 1000;
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("DELETE FROM samples WHERE ts_ms < ?"));
    q.addBindValue(cutoff);
    q.exec();
}

void DatabaseWorker::shutdown()
{
    flush();
    if (flushTimer_)
        flushTimer_->stop();
    if (pruneTimer_)
        pruneTimer_->stop();
    ready_ = false;
    if (db_.isOpen())
        db_.close();
    db_ = QSqlDatabase();
    if (!connectionName_.isEmpty())
        QSqlDatabase::removeDatabase(connectionName_);
}

// ---------------------------------------------------------------------------
// Storage
// ---------------------------------------------------------------------------

Storage::Storage(QObject *parent)
    : QObject(parent)
{
    thread_ = new QThread(this);
    thread_->setObjectName(QStringLiteral("netpulse-storage"));
    worker_ = new DatabaseWorker;
    worker_->moveToThread(thread_);

    connect(worker_, &DatabaseWorker::opened, this, &Storage::opened);
    connect(worker_, &DatabaseWorker::targetsLoaded, this, &Storage::targetsLoaded);
    connect(worker_, &DatabaseWorker::targetSaved, this, &Storage::targetSaved);
    connect(worker_, &DatabaseWorker::targetRemoved, this, &Storage::targetRemoved);
    connect(worker_, &DatabaseWorker::samplesFetched, this, &Storage::samplesFetched);

    thread_->start();
}

Storage::~Storage()
{
    shutdownAndWait();
}

void Storage::open(const QString &dbPath)
{
    QMetaObject::invokeMethod(worker_, [w = worker_, dbPath] { w->open(dbPath); },
                              Qt::QueuedConnection);
}

void Storage::saveSample(const ProbeSample &sample)
{
    QMetaObject::invokeMethod(worker_, [w = worker_, sample] { w->saveSample(sample); },
                              Qt::QueuedConnection);
}

void Storage::upsertTarget(const TargetSpec &spec)
{
    QMetaObject::invokeMethod(worker_, [w = worker_, spec] { w->upsertTarget(spec); },
                              Qt::QueuedConnection);
}

void Storage::removeTarget(int targetId)
{
    QMetaObject::invokeMethod(worker_, [w = worker_, targetId] { w->removeTarget(targetId); },
                              Qt::QueuedConnection);
}

void Storage::loadTargets()
{
    QMetaObject::invokeMethod(worker_, &DatabaseWorker::loadTargets, Qt::QueuedConnection);
}

void Storage::fetchSamples(quint64 requestId, int targetId, qint64 fromMs, qint64 toMs,
                           int maxPoints)
{
    QMetaObject::invokeMethod(
        worker_,
        [w = worker_, requestId, targetId, fromMs, toMs, maxPoints] {
            w->fetchSamples(requestId, targetId, fromMs, toMs, maxPoints);
        },
        Qt::QueuedConnection);
}

void Storage::setRetentionDays(int days)
{
    QMetaObject::invokeMethod(worker_, [w = worker_, days] { w->setRetentionDays(days); },
                              Qt::QueuedConnection);
}

void Storage::shutdownAndWait()
{
    if (stopped_)
        return;
    stopped_ = true;
    QMetaObject::invokeMethod(worker_, &DatabaseWorker::shutdown,
                              Qt::BlockingQueuedConnection);
    thread_->quit();
    thread_->wait(4000);
    delete worker_;
    worker_ = nullptr;
}

} // namespace netpulse
