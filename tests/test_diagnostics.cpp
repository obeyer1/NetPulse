#include "core/Diagnostics.h"
#include "core/Types.h"

#include <QHostAddress>
#include <QtTest>

using namespace netpulse;

namespace {

constexpr qint64 kNow = 1'753'600'000'000; // fixed reference time

TargetState makeState(int id, const QString &name, ProbeKind kind,
                      const QString &host, SampleStatus status,
                      const QString &resolved = QString(), double lossPct = 0,
                      double jitterMs = 1, double latencyMs = 20)
{
    TargetState st;
    st.spec.id = id;
    st.spec.name = name;
    st.spec.kind = kind;
    st.spec.host = host;
    st.spec.intervalSecs = 30;
    st.hasSample = true;
    st.last.targetId = id;
    st.last.kind = kind;
    st.last.status = status;
    st.last.timestampMs = kNow - 5000;
    st.last.resolvedAddress = resolved;
    if (status != SampleStatus::Failed) {
        st.last.latencyMs = latencyMs;
        if (kind == ProbeKind::Ping) {
            st.last.lossPct = lossPct;
            st.last.jitterMs = jitterMs;
        }
    }
    return st;
}

NetworkSnapshot makeSnapshot()
{
    NetworkSnapshot net;
    net.valid = true;
    net.linkUp = true;
    net.interfaceName = QStringLiteral("en0");
    net.ipv4 << QStringLiteral("192.168.1.20");
    net.gateway = QStringLiteral("192.168.1.1");
    net.dnsServers << QStringLiteral("192.168.1.1");
    return net;
}

} // namespace

class TestDiagnostics : public QObject {
    Q_OBJECT

private slots:
    void intervalClampEnforcesMinimum()
    {
        QCOMPARE(clampIntervalSecs(1), kMinIntervalSecs);
        QCOMPARE(clampIntervalSecs(4), kMinIntervalSecs);
        QCOMPARE(clampIntervalSecs(5), 5);
        QCOMPARE(clampIntervalSecs(30), 30);
        QCOMPARE(clampIntervalSecs(1'000'000), kMaxIntervalSecs);
        QCOMPARE(clampIntervalSecs(-10), kMinIntervalSecs);
    }

    void privateAddressDetection()
    {
        QVERIFY(isPrivateAddress(QHostAddress(QStringLiteral("192.168.1.1"))));
        QVERIFY(isPrivateAddress(QHostAddress(QStringLiteral("10.0.0.5"))));
        QVERIFY(isPrivateAddress(QHostAddress(QStringLiteral("172.16.9.9"))));
        QVERIFY(isPrivateAddress(QHostAddress(QStringLiteral("127.0.0.1"))));
        QVERIFY(!isPrivateAddress(QHostAddress(QStringLiteral("1.1.1.1"))));
        QVERIFY(!isPrivateAddress(QHostAddress(QStringLiteral("8.8.8.8"))));
    }

    void noTargetsGivesInfo()
    {
        const Diagnosis d = evaluateDiagnosis({}, makeSnapshot(), kNow);
        QCOMPARE(d.severity, Severity::Info);
        QVERIFY(d.headline.contains(QStringLiteral("No targets")));
    }

    void allHealthyGivesOk()
    {
        QList<TargetState> states;
        states << makeState(1, "Router", ProbeKind::Ping, "192.168.1.1",
                            SampleStatus::Ok, "192.168.1.1");
        states << makeState(2, "Cloudflare", ProbeKind::Ping, "1.1.1.1",
                            SampleStatus::Ok, "1.1.1.1");
        states << makeState(3, "Web", ProbeKind::Https, "https://example.com",
                            SampleStatus::Ok);
        const Diagnosis d = evaluateDiagnosis(states, makeSnapshot(), kNow);
        QCOMPARE(d.severity, Severity::Ok);
    }

    void dnsFailureIsCalledOut()
    {
        QList<TargetState> states;
        states << makeState(1, "Cloudflare", ProbeKind::Ping, "1.1.1.1",
                            SampleStatus::Ok, "1.1.1.1");
        states << makeState(2, "DNS", ProbeKind::Dns, "example.com",
                            SampleStatus::Failed);
        const Diagnosis d = evaluateDiagnosis(states, makeSnapshot(), kNow);
        QCOMPARE(d.severity, Severity::Error);
        QCOMPARE(d.headline, QStringLiteral("DNS error"));
    }

    void externalDownButRouterUp()
    {
        QList<TargetState> states;
        states << makeState(1, "Router", ProbeKind::Ping, "192.168.1.1",
                            SampleStatus::Ok, "192.168.1.1");
        states << makeState(2, "Cloudflare", ProbeKind::Ping, "1.1.1.1",
                            SampleStatus::Failed, "1.1.1.1");
        states << makeState(3, "Web", ProbeKind::Https, "https://example.com",
                            SampleStatus::Failed);
        const Diagnosis d = evaluateDiagnosis(states, makeSnapshot(), kNow);
        QCOMPARE(d.severity, Severity::Error);
        QVERIFY(d.headline.contains(QStringLiteral("External network unreachable")));
        QVERIFY(d.headline.contains(QStringLiteral("router")));
    }

    void everythingDownIncludingRouter()
    {
        QList<TargetState> states;
        states << makeState(1, "Router", ProbeKind::Ping, "192.168.1.1",
                            SampleStatus::Failed, "192.168.1.1");
        states << makeState(2, "Cloudflare", ProbeKind::Ping, "1.1.1.1",
                            SampleStatus::Failed, "1.1.1.1");
        const Diagnosis d = evaluateDiagnosis(states, makeSnapshot(), kNow);
        QCOMPARE(d.severity, Severity::Error);
        QVERIFY(d.headline.contains(QStringLiteral("Local network")));
    }

    void partialOutageIsWarning()
    {
        QList<TargetState> states;
        states << makeState(1, "Cloudflare", ProbeKind::Ping, "1.1.1.1",
                            SampleStatus::Ok, "1.1.1.1");
        states << makeState(2, "Quad9", ProbeKind::Ping, "9.9.9.9",
                            SampleStatus::Failed, "9.9.9.9");
        states << makeState(3, "Web", ProbeKind::Https, "https://example.com",
                            SampleStatus::Ok);
        const Diagnosis d = evaluateDiagnosis(states, makeSnapshot(), kNow);
        QCOMPARE(d.severity, Severity::Warning);
        QVERIFY(d.detail.contains(QStringLiteral("Quad9")));
    }

    void packetLossIsWarning()
    {
        QList<TargetState> states;
        states << makeState(1, "Cloudflare", ProbeKind::Ping, "1.1.1.1",
                            SampleStatus::Degraded, "1.1.1.1", /*loss*/ 40.0);
        const Diagnosis d = evaluateDiagnosis(states, makeSnapshot(), kNow);
        QCOMPARE(d.severity, Severity::Warning);
        QVERIFY(d.headline.contains(QStringLiteral("Packet loss")));
    }

    void staleSamplesAreIgnored()
    {
        TargetState st = makeState(1, "Cloudflare", ProbeKind::Ping, "1.1.1.1",
                                   SampleStatus::Ok, "1.1.1.1");
        st.last.timestampMs = kNow - 10 * 60 * 1000; // far older than 3 intervals
        const Diagnosis d = evaluateDiagnosis({st}, makeSnapshot(), kNow);
        QCOMPARE(d.severity, Severity::Info);
        QVERIFY(d.headline.contains(QStringLiteral("Collecting")));
    }
};

QTEST_GUILESS_MAIN(TestDiagnostics)
#include "test_diagnostics.moc"
