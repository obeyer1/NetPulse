#pragma once

#include "core/Types.h"

#include <QList>
#include <QObject>
#include <QSqlDatabase>
#include <QThread>

class QTimer;

namespace netpulse {

// ---------------------------------------------------------------------------
// DatabaseWorker — owns the SQLite connection on a dedicated thread. All
// writes are batched into transactions; all reads are answered via signals,
// so the UI thread never blocks on disk I/O.
// ---------------------------------------------------------------------------
class DatabaseWorker : public QObject {
    Q_OBJECT
public:
    explicit DatabaseWorker(QObject *parent = nullptr);

public slots:
    void open(const QString &dbPath);
    void saveSample(const netpulse::ProbeSample &sample);
    void upsertTarget(const netpulse::TargetSpec &spec);
    void removeTarget(int targetId);
    void loadTargets();
    void fetchSamples(quint64 requestId, int targetId, qint64 fromMs, qint64 toMs,
                      int maxPoints);
    void setRetentionDays(int days);
    void flush();
    void shutdown();

signals:
    void opened(bool ok, const QString &error, const QString &path);
    void targetsLoaded(const QList<netpulse::TargetSpec> &targets);
    void targetSaved(const netpulse::TargetSpec &spec);
    void targetRemoved(int targetId);
    void samplesFetched(quint64 requestId, int targetId,
                        const QList<netpulse::ProbeSample> &samples);

private:
    void prune();
    bool migrate();

    QSqlDatabase db_;
    QString connectionName_;
    QList<ProbeSample> pending_;
    QTimer *flushTimer_ = nullptr;
    QTimer *pruneTimer_ = nullptr;
    int retentionDays_ = 7;
    bool ready_ = false;
};

// ---------------------------------------------------------------------------
// Storage — UI-thread facade owning the database thread.
// ---------------------------------------------------------------------------
class Storage : public QObject {
    Q_OBJECT
public:
    explicit Storage(QObject *parent = nullptr);
    ~Storage() override;

    void open(const QString &dbPath);
    void saveSample(const ProbeSample &sample);
    void upsertTarget(const TargetSpec &spec);
    void removeTarget(int targetId);
    void loadTargets();
    void fetchSamples(quint64 requestId, int targetId, qint64 fromMs, qint64 toMs,
                      int maxPoints = 4000);
    void setRetentionDays(int days);
    void shutdownAndWait();

signals:
    void opened(bool ok, const QString &error, const QString &path);
    void targetsLoaded(const QList<netpulse::TargetSpec> &targets);
    void targetSaved(const netpulse::TargetSpec &spec);
    void targetRemoved(int targetId);
    void samplesFetched(quint64 requestId, int targetId,
                        const QList<netpulse::ProbeSample> &samples);

private:
    QThread *thread_ = nullptr;
    DatabaseWorker *worker_ = nullptr;
    bool stopped_ = false;
};

} // namespace netpulse
