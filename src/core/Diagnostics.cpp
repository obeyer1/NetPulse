#include "core/Diagnostics.h"

#include <QHostAddress>
#include <QStringList>

#include <algorithm>

namespace netpulse {
namespace {

enum class Category { Router, LocalPing, ExternalPing, Dns, Https };

struct Agg {
    int total = 0;
    int reachable = 0;   // Ok or Degraded
    int down = 0;        // Failed
    QStringList downNames;
};

Category categorize(const TargetState &st, const NetworkSnapshot &net)
{
    switch (st.spec.kind) {
    case ProbeKind::Dns:   return Category::Dns;
    case ProbeKind::Https: return Category::Https;
    case ProbeKind::Ping:  break;
    }
    const QString addrText = !st.last.resolvedAddress.isEmpty()
        ? st.last.resolvedAddress
        : st.spec.host.trimmed();
    const QHostAddress addr(addrText);
    if (addr.isNull())
        return Category::ExternalPing; // unresolved hostname: assume external
    if (!net.gateway.isEmpty()) {
        // The gateway may carry a scope suffix ("fe80::1%en0").
        const QString gw = net.gateway.section(QLatin1Char('%'), 0, 0);
        if (addr == QHostAddress(gw))
            return Category::Router;
    }
    return isPrivateAddress(addr) ? Category::LocalPing : Category::ExternalPing;
}

bool isFresh(const TargetState &st, qint64 nowMs)
{
    if (!st.hasSample)
        return false;
    const qint64 window =
        qMax<qint64>(3LL * clampIntervalSecs(st.spec.intervalSecs) * 1000, 90000);
    return nowMs - st.last.timestampMs <= window;
}

double median(QList<double> values)
{
    if (values.isEmpty())
        return -1;
    std::sort(values.begin(), values.end());
    const auto n = values.size();
    return n % 2 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2.0;
}

QString joinNames(const QStringList &names, int max = 3)
{
    if (names.size() <= max)
        return names.join(QStringLiteral(", "));
    return names.mid(0, max).join(QStringLiteral(", "))
        + QStringLiteral(" and %1 more").arg(names.size() - max);
}

} // namespace

bool isPrivateAddress(const QHostAddress &addr)
{
    if (addr.isNull())
        return false;
    if (addr.isLoopback() || addr.isLinkLocal() || addr.isUniqueLocalUnicast())
        return true;
    bool ok = false;
    const quint32 v4 = addr.toIPv4Address(&ok);
    if (!ok)
        return false;
    const quint8 a = (v4 >> 24) & 0xff;
    const quint8 b = (v4 >> 16) & 0xff;
    if (a == 10)
        return true;
    if (a == 172 && b >= 16 && b <= 31)
        return true;
    if (a == 192 && b == 168)
        return true;
    if (a == 100 && b >= 64 && b <= 127) // CGNAT — local side of the ISP
        return true;
    return false;
}

Diagnosis evaluateDiagnosis(const QList<TargetState> &states,
                            const NetworkSnapshot &net,
                            qint64 nowMs)
{
    int enabledCount = 0;
    QList<TargetState> fresh;
    for (const TargetState &st : states) {
        if (!st.spec.enabled)
            continue;
        ++enabledCount;
        if (isFresh(st, nowMs))
            fresh << st;
    }

    if (enabledCount == 0)
        return {Severity::Info, QStringLiteral("No targets configured"),
                QStringLiteral("Add a target — your router, a public IP, a DNS lookup or "
                               "an HTTPS URL — to begin monitoring.")};
    if (fresh.isEmpty())
        return {Severity::Info, QStringLiteral("Collecting data…"),
                QStringLiteral("Waiting for the first probe results.")};

    Agg byCat[5];
    Agg all;
    QList<double> latencies;
    QString worstLossName, worstJitterName, worstLatencyName;
    double worstLoss = 0, worstJitter = 0, worstLatency = 0;

    for (const TargetState &st : fresh) {
        const Category cat = categorize(st, net);
        Agg &agg = byCat[static_cast<int>(cat)];
        ++agg.total;
        ++all.total;
        if (st.last.reachable()) {
            ++agg.reachable;
            ++all.reachable;
            if (st.last.latencyMs >= 0) {
                latencies << st.last.latencyMs;
                if (st.last.latencyMs > worstLatency) {
                    worstLatency = st.last.latencyMs;
                    worstLatencyName = st.spec.name;
                }
            }
            if (st.last.lossPct > worstLoss) {
                worstLoss = st.last.lossPct;
                worstLossName = st.spec.name;
            }
            if (st.last.jitterMs > worstJitter) {
                worstJitter = st.last.jitterMs;
                worstJitterName = st.spec.name;
            }
        } else {
            ++agg.down;
            ++all.down;
            agg.downNames << st.spec.name;
            all.downNames << st.spec.name;
        }
    }

    const Agg &router = byCat[static_cast<int>(Category::Router)];
    const Agg &localPing = byCat[static_cast<int>(Category::LocalPing)];
    const Agg &extPing = byCat[static_cast<int>(Category::ExternalPing)];
    const Agg &dns = byCat[static_cast<int>(Category::Dns)];
    const Agg &https = byCat[static_cast<int>(Category::Https)];

    const int externalTotal = extPing.total + dns.total + https.total;
    const int externalDown = extPing.down + dns.down + https.down;
    const bool externalAllDown = externalTotal > 0 && externalDown == externalTotal;
    const bool routerReachable = router.total > 0 && router.reachable > 0;
    const bool localReachable =
        routerReachable || (localPing.total > 0 && localPing.reachable > 0);

    // 1. Total blackout — nothing responds at all.
    if (all.down == all.total) {
        if (net.valid && (!net.linkUp || !net.hasAnyAddress()))
            return {Severity::Error, QStringLiteral("No active network connection"),
                    QStringLiteral("No network interface is up with an IP address. "
                                   "Check Wi‑Fi or the Ethernet cable.")};
        if (router.total > 0)
            return {Severity::Error, QStringLiteral("Local network unreachable"),
                    QStringLiteral("Even your router/gateway does not respond. Check the "
                                   "Wi‑Fi connection, the cable, or restart the router.")};
        return {Severity::Error, QStringLiteral("All targets unreachable"),
                QStringLiteral("Every monitored target is failing. Add your gateway as a "
                               "ping target to tell local problems from internet problems.")};
    }

    // 2. Everything beyond the router is failing, but the router answers.
    if (externalAllDown && localReachable)
        return {Severity::Error,
                QStringLiteral("External network unreachable — local router is fine"),
                QStringLiteral("Your gateway responds, but no external target does. The "
                               "problem is likely between your router and your ISP "
                               "(modem, uplink or provider outage).")};

    // 3. DNS-specific failure: lookups fail while raw IP connectivity works.
    if (dns.total > 0 && dns.down == dns.total
        && (extPing.reachable > 0 || https.reachable > 0 || routerReachable))
        return {Severity::Error, QStringLiteral("DNS error"),
                QStringLiteral("Name resolution is failing while IP connectivity works. "
                               "Your DNS server may be down — consider testing an "
                               "alternative resolver (e.g. 1.1.1.1 or 8.8.8.8).")};

    // 4. HTTPS failing across the board while lower layers are fine.
    if (https.total > 0 && https.down == https.total
        && (dns.reachable > 0 || extPing.reachable > 0))
        return {Severity::Error, QStringLiteral("HTTPS connectivity failing"),
                QStringLiteral("Web requests fail although ping/DNS work — possible "
                               "captive portal, proxy, firewall or TLS interception.")};

    // 5. Partial outage.
    if (all.down > 0)
        return {Severity::Warning,
                QStringLiteral("%1 of %2 targets unreachable").arg(all.down).arg(all.total),
                QStringLiteral("Failing: %1. Other targets look fine, so this is likely "
                               "specific to those endpoints.").arg(joinNames(all.downNames))};

    // 6. Reachable but shaky: loss, jitter, latency.
    if (worstLoss >= 10.0)
        return {Severity::Warning, QStringLiteral("Packet loss detected"),
                QStringLiteral("%1 is losing %2% of packets — the connection is unstable.")
                    .arg(worstLossName).arg(QString::number(worstLoss, 'f', 0))};
    if (worstJitter >= 30.0)
        return {Severity::Warning, QStringLiteral("High jitter"),
                QStringLiteral("%1 shows %2 ms jitter — real-time apps (calls, games) "
                               "may stutter.")
                    .arg(worstJitterName).arg(QString::number(worstJitter, 'f', 0))};
    if (worstLatency >= 400.0)
        return {Severity::Warning, QStringLiteral("High latency"),
                QStringLiteral("%1 responds in %2 ms — noticeably slow right now.")
                    .arg(worstLatencyName).arg(QString::number(worstLatency, 'f', 0))};

    const double med = median(latencies);
    QString detail = QStringLiteral("%1 target(s) healthy.").arg(all.total);
    if (med >= 0)
        detail = QStringLiteral("%1 target(s) healthy · median latency %2 ms.")
                     .arg(all.total).arg(QString::number(med, 'f', 1));
    return {Severity::Ok, QStringLiteral("All systems normal"), detail};
}

} // namespace netpulse
