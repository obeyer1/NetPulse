#include "core/Probes.h"

#include <QDateTime>
#include <QDnsLookup>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QTimer>
#include <QUrl>

namespace netpulse {

// ---------------------------------------------------------------------------
// DnsProbe
// ---------------------------------------------------------------------------

DnsProbe::DnsProbe(const TargetSpec &spec, const ProbeConfig &cfg, QObject *parent)
    : QObject(parent), spec_(spec), cfg_(cfg)
{
    timeoutTimer_ = new QTimer(this);
    timeoutTimer_->setSingleShot(true);
    connect(timeoutTimer_, &QTimer::timeout, this, [this] {
        timedOut_ = true;
        if (lookup_)
            lookup_->abort();
    });
}

void DnsProbe::start()
{
    attempt();
}

void DnsProbe::cancel()
{
    cancelled_ = true;
    timeoutTimer_->stop();
    if (lookup_)
        lookup_->abort();
}

void DnsProbe::attempt()
{
    if (cancelled_ || done_)
        return;
    ++attemptsUsed_;
    timedOut_ = false;

    if (lookup_)
        lookup_->deleteLater();
    lookup_ = new QDnsLookup(QDnsLookup::A, spec_.host.trimmed(), this);
    if (!spec_.dnsServer.trimmed().isEmpty())
        lookup_->setNameserver(QHostAddress(spec_.dnsServer.trimmed()));

    connect(lookup_, &QDnsLookup::finished, this, &DnsProbe::onLookupFinished);
    elapsed_.start();
    timeoutTimer_->start(cfg_.dnsTimeoutMs);
    lookup_->lookup();
}

void DnsProbe::onLookupFinished()
{
    timeoutTimer_->stop();
    if (cancelled_ || done_ || !lookup_)
        return;

    const double ms = static_cast<double>(elapsed_.nsecsElapsed()) / 1e6;

    ProbeSample s;
    s.targetId = spec_.id;
    s.timestampMs = QDateTime::currentMSecsSinceEpoch();
    s.kind = ProbeKind::Dns;
    s.attempts = attemptsUsed_;

    if (lookup_->error() == QDnsLookup::NoError) {
        QStringList addrs;
        const auto records = lookup_->hostAddressRecords();
        for (const auto &rec : records)
            addrs << rec.value().toString();
        s.latencyMs = ms;
        if (addrs.isEmpty()) {
            s.status = SampleStatus::Degraded;
            s.error = QStringLiteral("Lookup succeeded but returned no A records");
        } else {
            s.status = SampleStatus::Ok;
            s.resolvedAddress = addrs.first();
        }
        emitSample(s);
        return;
    }

    const auto err = lookup_->error();
    const bool retryable =
        timedOut_ || err == QDnsLookup::ServerFailureError || err == QDnsLookup::TimeoutError;

    if (retryable && attemptsUsed_ <= cfg_.maxRetries) {
        QTimer::singleShot(400, this, &DnsProbe::attempt);
        return;
    }

    s.status = SampleStatus::Failed;
    switch (err) {
    case QDnsLookup::NotFoundError:
        s.error = QStringLiteral("Name does not exist (NXDOMAIN)");
        break;
    case QDnsLookup::ServerFailureError:
        s.error = QStringLiteral("DNS server reported a failure (SERVFAIL)");
        break;
    case QDnsLookup::ServerRefusedError:
        s.error = QStringLiteral("DNS server refused the query");
        break;
    case QDnsLookup::InvalidRequestError:
        s.error = QStringLiteral("Invalid DNS request — check the hostname");
        break;
    case QDnsLookup::TimeoutError:
        s.error = QStringLiteral("DNS lookup timed out after %1 s (%2 attempt(s))")
                      .arg(cfg_.dnsTimeoutMs / 1000.0, 0, 'f', 1)
                      .arg(attemptsUsed_);
        break;
    case QDnsLookup::OperationCancelledError:
        if (!timedOut_)
            return; // cancelled by us on purpose; report nothing
        s.error = QStringLiteral("DNS lookup timed out after %1 s (%2 attempt(s))")
                      .arg(cfg_.dnsTimeoutMs / 1000.0, 0, 'f', 1)
                      .arg(attemptsUsed_);
        break;
    default:
        s.error = lookup_->errorString();
        break;
    }
    emitSample(s);
}

void DnsProbe::emitSample(ProbeSample sample)
{
    if (done_)
        return;
    done_ = true;
    emit finished(sample);
}

// ---------------------------------------------------------------------------
// HttpsProbe
// ---------------------------------------------------------------------------

HttpsProbe::HttpsProbe(const TargetSpec &spec, const ProbeConfig &cfg,
                       QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent), spec_(spec), cfg_(cfg), nam_(nam)
{
}

void HttpsProbe::start()
{
    ++attemptsUsed_;
    request(true);
}

void HttpsProbe::cancel()
{
    cancelled_ = true;
    if (reply_)
        reply_->abort();
}

void HttpsProbe::request(bool useHead)
{
    if (cancelled_ || done_)
        return;

    QUrl url = QUrl::fromUserInput(spec_.host.trimmed());
    if (url.scheme().isEmpty())
        url.setScheme(QStringLiteral("https"));

    QNetworkRequest req(url);
    req.setTransferTimeout(cfg_.httpsTimeoutMs);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("NetPulse/1.0 (local network health monitor)"));

    ttfbMs_ = -1;
    elapsed_.start();
    reply_ = useHead ? nam_->head(req) : nam_->get(req);

    connect(reply_, &QNetworkReply::metaDataChanged, this, [this] {
        if (ttfbMs_ < 0)
            ttfbMs_ = static_cast<double>(elapsed_.nsecsElapsed()) / 1e6;
    });
    connect(reply_, &QNetworkReply::finished, this, &HttpsProbe::onReplyFinished);
}

void HttpsProbe::onReplyFinished()
{
    if (done_ || !reply_)
        return;
    QNetworkReply *reply = reply_;
    reply->deleteLater();
    reply_.clear();
    if (cancelled_)
        return;

    const double totalMs = static_cast<double>(elapsed_.nsecsElapsed()) / 1e6;
    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString reason =
        reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();

    ProbeSample s;
    s.targetId = spec_.id;
    s.timestampMs = QDateTime::currentMSecsSinceEpoch();
    s.kind = ProbeKind::Https;
    s.attempts = attemptsUsed_;
    s.httpStatus = httpStatus;
    s.ttfbMs = ttfbMs_;

    if (reply->error() == QNetworkReply::NoError) {
        s.latencyMs = totalMs;
        if (httpStatus >= 400) {
            s.status = SampleStatus::Degraded;
            s.error = QStringLiteral("Server reachable but returned HTTP %1 %2")
                          .arg(httpStatus).arg(reason);
        } else {
            s.status = SampleStatus::Ok;
        }
        emitSample(s);
        return;
    }

    // Some servers reject HEAD; try once more with GET (same attempt).
    if ((httpStatus == 405 || httpStatus == 501) && !triedGetFallback_) {
        triedGetFallback_ = true;
        request(false);
        return;
    }

    const auto err = reply->error();
    const bool retryable = err == QNetworkReply::OperationCanceledError // transfer timeout
        || err == QNetworkReply::TimeoutError
        || err == QNetworkReply::ConnectionRefusedError
        || err == QNetworkReply::RemoteHostClosedError
        || err == QNetworkReply::TemporaryNetworkFailureError
        || err == QNetworkReply::UnknownNetworkError;

    if (retryable && attemptsUsed_ <= cfg_.maxRetries) {
        ++attemptsUsed_;
        QTimer::singleShot(600, this, [this] { request(!triedGetFallback_); });
        return;
    }

    s.status = SampleStatus::Failed;
    switch (err) {
    case QNetworkReply::OperationCanceledError:
    case QNetworkReply::TimeoutError:
        s.error = QStringLiteral("Timed out after %1 s (%2 attempt(s))")
                      .arg(cfg_.httpsTimeoutMs / 1000.0, 0, 'f', 1)
                      .arg(attemptsUsed_);
        break;
    case QNetworkReply::HostNotFoundError:
        s.error = QStringLiteral("DNS lookup failed for this URL");
        break;
    case QNetworkReply::ConnectionRefusedError:
        s.error = QStringLiteral("Connection refused by the server");
        break;
    case QNetworkReply::RemoteHostClosedError:
        s.error = QStringLiteral("Server closed the connection unexpectedly");
        break;
    case QNetworkReply::SslHandshakeFailedError:
        s.error = QStringLiteral("TLS handshake failed — certificate or protocol problem");
        break;
    default:
        s.error = reply->errorString();
        break;
    }
    emitSample(s);
}

void HttpsProbe::emitSample(ProbeSample sample)
{
    if (done_)
        return;
    done_ = true;
    emit finished(sample);
}

} // namespace netpulse
