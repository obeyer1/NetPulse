#pragma once

#include "core/Types.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>

class QDnsLookup;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace netpulse {

// ---------------------------------------------------------------------------
// DnsProbe — measures how long resolving spec.host takes, optionally against
// a user-chosen DNS server. Async; lives on the scheduler thread.
// ---------------------------------------------------------------------------
class DnsProbe : public QObject {
    Q_OBJECT
public:
    DnsProbe(const TargetSpec &spec, const ProbeConfig &cfg, QObject *parent = nullptr);

    void start();
    void cancel();

signals:
    void finished(const netpulse::ProbeSample &sample);

private:
    void attempt();
    void onLookupFinished();
    void emitSample(ProbeSample sample);

    TargetSpec spec_;
    ProbeConfig cfg_;
    QPointer<QDnsLookup> lookup_;
    QTimer *timeoutTimer_ = nullptr;
    QElapsedTimer elapsed_;
    int attemptsUsed_ = 0;
    bool timedOut_ = false;
    bool cancelled_ = false;
    bool done_ = false;
};

// ---------------------------------------------------------------------------
// HttpsProbe — issues a HEAD request (falling back to GET when the server
// rejects HEAD) and measures time-to-first-byte and total time.
// ---------------------------------------------------------------------------
class HttpsProbe : public QObject {
    Q_OBJECT
public:
    HttpsProbe(const TargetSpec &spec, const ProbeConfig &cfg,
               QNetworkAccessManager *nam, QObject *parent = nullptr);

    void start();
    void cancel();

signals:
    void finished(const netpulse::ProbeSample &sample);

private:
    void request(bool useHead);
    void onReplyFinished();
    void emitSample(ProbeSample sample);

    TargetSpec spec_;
    ProbeConfig cfg_;
    QNetworkAccessManager *nam_ = nullptr;
    QPointer<QNetworkReply> reply_;
    QElapsedTimer elapsed_;
    double ttfbMs_ = -1;
    int attemptsUsed_ = 0;
    bool triedGetFallback_ = false;
    bool cancelled_ = false;
    bool done_ = false;
};

} // namespace netpulse
