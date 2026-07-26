#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QStringList>

namespace netpulse {

// ---------------------------------------------------------------------------
// Probe kinds and targets
// ---------------------------------------------------------------------------

enum class ProbeKind { Ping, Dns, Https };

inline QString probeKindName(ProbeKind k)
{
    switch (k) {
    case ProbeKind::Ping:  return QStringLiteral("ping");
    case ProbeKind::Dns:   return QStringLiteral("dns");
    case ProbeKind::Https: return QStringLiteral("https");
    }
    return QStringLiteral("ping");
}

inline ProbeKind probeKindFromString(const QString &s)
{
    if (s == QLatin1String("dns"))   return ProbeKind::Dns;
    if (s == QLatin1String("https")) return ProbeKind::Https;
    return ProbeKind::Ping;
}

inline QString probeKindLabel(ProbeKind k)
{
    switch (k) {
    case ProbeKind::Ping:  return QStringLiteral("Ping");
    case ProbeKind::Dns:   return QStringLiteral("DNS");
    case ProbeKind::Https: return QStringLiteral("HTTPS");
    }
    return {};
}

// Rate limits (a hard product constraint, enforced in the dialog, the
// scheduler and the storage layer alike).
inline constexpr int kMinIntervalSecs = 5;
inline constexpr int kMaxIntervalSecs = 86400;
inline constexpr int kDefaultIntervalSecs = 30;

inline int clampIntervalSecs(int secs)
{
    if (secs < kMinIntervalSecs) return kMinIntervalSecs;
    if (secs > kMaxIntervalSecs) return kMaxIntervalSecs;
    return secs;
}

// A monitoring target, always created explicitly by the user.
struct TargetSpec {
    int id = -1;                       // database id, -1 until persisted
    QString name;                      // display name
    ProbeKind kind = ProbeKind::Ping;
    QString host;                      // ping: host/IP · dns: name to resolve · https: URL
    QString dnsServer;                 // dns only: server to query ("" = system resolver)
    int intervalSecs = kDefaultIntervalSecs;
    int pingCount = 5;                 // echo requests per ping batch (1..10)
    bool enabled = true;

    QString describe() const
    {
        switch (kind) {
        case ProbeKind::Ping:
            return QStringLiteral("Ping %1").arg(host);
        case ProbeKind::Dns:
            return QStringLiteral("Resolve %1 via %2")
                .arg(host, dnsServer.isEmpty() ? QStringLiteral("system resolver") : dnsServer);
        case ProbeKind::Https:
            return QStringLiteral("Request %1").arg(host);
        }
        return host;
    }
};

// ---------------------------------------------------------------------------
// Probe results
// ---------------------------------------------------------------------------

enum class SampleStatus { Ok = 0, Degraded = 1, Failed = 2 };

struct ProbeSample {
    int targetId = -1;
    qint64 timestampMs = 0;            // epoch milliseconds
    ProbeKind kind = ProbeKind::Ping;
    SampleStatus status = SampleStatus::Failed;

    double latencyMs = -1;             // ping: avg RTT · dns: lookup time · https: total time
    double jitterMs = -1;              // ping only (mean delta between consecutive RTTs)
    double lossPct = -1;               // ping only
    int sent = 0;                      // ping only
    int received = 0;                  // ping only
    int httpStatus = 0;                // https only
    double ttfbMs = -1;                // https only (time to first response byte)

    QString resolvedAddress;           // address actually probed / first record returned
    QString error;                     // human-readable failure/degradation reason
    int attempts = 1;                  // attempts used (retry accounting)

    bool reachable() const { return status != SampleStatus::Failed; }
};

// Tunables shared by all probes; adjustable in Settings.
struct ProbeConfig {
    int pingTimeoutMs = 2000;          // per echo reply
    int pingSpacingMs = 250;           // gap between echoes in a batch
    int dnsTimeoutMs = 3000;
    int httpsTimeoutMs = 8000;
    int maxRetries = 1;                // extra attempts for DNS/HTTPS after a transient failure
};

// ---------------------------------------------------------------------------
// Local network state
// ---------------------------------------------------------------------------

struct NetworkSnapshot {
    bool valid = false;
    QString interfaceName;             // e.g. "en0"
    QString hardwareAddress;
    QStringList ipv4;
    QStringList ipv6;
    QString gateway;                   // default route gateway
    QStringList dnsServers;
    bool linkUp = false;
    QDateTime capturedAt;

    bool hasAnyAddress() const { return !ipv4.isEmpty() || !ipv6.isEmpty(); }
};

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

enum class Severity { Info, Ok, Warning, Error };

struct Diagnosis {
    Severity severity = Severity::Info;
    QString headline;
    QString detail;
};

} // namespace netpulse

Q_DECLARE_METATYPE(netpulse::TargetSpec)
Q_DECLARE_METATYPE(netpulse::ProbeSample)
Q_DECLARE_METATYPE(netpulse::ProbeConfig)
Q_DECLARE_METATYPE(netpulse::NetworkSnapshot)
Q_DECLARE_METATYPE(netpulse::Diagnosis)
