#include "core/IcmpPinger.h"

#include <QHash>
#include <QRandomGenerator>

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/icmp6.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std::chrono;

namespace netpulse {
namespace {

constexpr int kPayloadLen = 16;        // 8-byte magic + 8-byte send timestamp
constexpr int kIcmpHeaderLen = 8;
constexpr int kPacketLen = kIcmpHeaderLen + kPayloadLen;

qint64 nowNs()
{
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

quint16 inetChecksum(const quint8 *data, int len)
{
    quint32 sum = 0;
    while (len > 1) {
        quint16 word;
        std::memcpy(&word, data, 2);
        sum += word;
        data += 2;
        len -= 2;
    }
    if (len == 1)
        sum += *data;
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    return static_cast<quint16>(~sum);
}

struct Resolved {
    bool ok = false;
    bool ipv6 = false;
    sockaddr_storage addr {};
    socklen_t addrLen = 0;
    QString text;
    QString error;
};

Resolved resolveHost(const QString &host)
{
    Resolved r;
    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_ADDRCONFIG;

    addrinfo *results = nullptr;
    const QByteArray hostUtf8 = host.trimmed().toUtf8();
    const int rc = ::getaddrinfo(hostUtf8.constData(), nullptr, &hints, &results);
    if (rc != 0 || !results) {
        r.error = QStringLiteral("Could not resolve \"%1\": %2")
                      .arg(host, QString::fromUtf8(gai_strerror(rc)));
        return r;
    }

    // Prefer IPv4; fall back to the first IPv6 result.
    const addrinfo *chosen = nullptr;
    for (const addrinfo *ai = results; ai; ai = ai->ai_next) {
        if (ai->ai_family == AF_INET) { chosen = ai; break; }
        if (!chosen && ai->ai_family == AF_INET6) chosen = ai;
    }
    if (!chosen) {
        ::freeaddrinfo(results);
        r.error = QStringLiteral("No usable address for \"%1\"").arg(host);
        return r;
    }

    std::memcpy(&r.addr, chosen->ai_addr, chosen->ai_addrlen);
    r.addrLen = static_cast<socklen_t>(chosen->ai_addrlen);
    r.ipv6 = chosen->ai_family == AF_INET6;

    char buf[INET6_ADDRSTRLEN] = {};
    if (r.ipv6) {
        const auto *sin6 = reinterpret_cast<const sockaddr_in6 *>(&r.addr);
        ::inet_ntop(AF_INET6, &sin6->sin6_addr, buf, sizeof buf);
    } else {
        const auto *sin = reinterpret_cast<const sockaddr_in *>(&r.addr);
        ::inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof buf);
    }
    r.text = QString::fromLatin1(buf);
    r.ok = true;
    ::freeaddrinfo(results);
    return r;
}

QString sendErrorText(int err)
{
    switch (err) {
    case ENETUNREACH:  return QStringLiteral("Network unreachable");
    case EHOSTUNREACH: return QStringLiteral("Host unreachable");
    case ENETDOWN:     return QStringLiteral("Network is down");
    case EACCES:
    case EPERM:        return QStringLiteral("System denied sending ICMP");
    default:           return QString::fromLocal8Bit(strerror(err));
    }
}

// Parsed inbound packet: echo reply carrying our magic, or nothing.
struct ReplyInfo {
    bool valid = false;
    quint16 seq = 0;
    qint64 sentNs = 0;
};

ReplyInfo parseReply(const quint8 *buf, ssize_t len, bool ipv6, quint64 magic)
{
    ReplyInfo info;
    int off = 0;
    if (!ipv6) {
        // macOS delivers the full IP header on ICMP datagram sockets.
        if (len >= 20 && (buf[0] >> 4) == 4)
            off = (buf[0] & 0x0f) * 4;
    }
    if (len - off < kPacketLen)
        return info;

    const quint8 type = buf[off];
    const bool isEchoReply = ipv6 ? (type == ICMP6_ECHO_REPLY) : (type == ICMP_ECHOREPLY);
    if (!isEchoReply)
        return info;

    quint64 gotMagic = 0;
    std::memcpy(&gotMagic, buf + off + kIcmpHeaderLen, sizeof gotMagic);
    if (gotMagic != magic)
        return info;

    quint16 seqNet = 0;
    std::memcpy(&seqNet, buf + off + 6, sizeof seqNet);
    info.seq = ntohs(seqNet);
    std::memcpy(&info.sentNs, buf + off + kIcmpHeaderLen + 8, sizeof info.sentNs);
    info.valid = true;
    return info;
}

} // namespace

PingBatchResult IcmpPinger::run(const QString &host,
                                Options options,
                                const std::atomic_bool &cancelled)
{
    PingBatchResult res;
    options.count = qBound(1, options.count, 10);
    options.timeoutMsPerReply = qBound(200, options.timeoutMsPerReply, 10000);
    options.spacingMs = qBound(0, options.spacingMs, 2000);

    const Resolved dest = resolveHost(host);
    if (!dest.ok) {
        res.error = dest.error;
        return res;
    }
    res.resolvedAddress = dest.text;
    if (cancelled.load())
        return res;

    const int fd = ::socket(dest.ipv6 ? AF_INET6 : AF_INET, SOCK_DGRAM,
                            dest.ipv6 ? IPPROTO_ICMPV6 : IPPROTO_ICMP);
    if (fd < 0) {
        res.error = (errno == EACCES || errno == EPERM)
            ? QStringLiteral("The system denied creating an ICMP socket")
            : QString::fromLocal8Bit(strerror(errno));
        return res;
    }
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

    if (::connect(fd, reinterpret_cast<const sockaddr *>(&dest.addr), dest.addrLen) < 0) {
        res.error = sendErrorText(errno);
        ::close(fd);
        return res;
    }

    const quint64 magic = QRandomGenerator::global()->generate64();
    const quint16 ident = static_cast<quint16>(::getpid() & 0xffff);

    QHash<quint16, qint64> outstanding;   // seq -> send time (ns), not yet answered
    QList<double> rtts;
    QString firstSendError;

    // Drains the socket for up to `waitMs`, recording any replies (including
    // late ones from earlier sequences). Returns early once `untilSeq` has
    // been answered, if >= 0.
    const auto drainFor = [&](int waitMs, int untilSeq) {
        const auto deadline = steady_clock::now() + milliseconds(waitMs);
        while (steady_clock::now() < deadline) {
            if (cancelled.load())
                return;
            if (untilSeq >= 0 && !outstanding.contains(static_cast<quint16>(untilSeq)))
                return;
            const qint64 remaining =
                duration_cast<milliseconds>(deadline - steady_clock::now()).count();
            const int slice = static_cast<int>(std::min<qint64>(100, std::max<qint64>(0, remaining)));
            pollfd pf { fd, POLLIN, 0 };
            const int pr = ::poll(&pf, 1, slice);
            if (pr <= 0)
                continue;
            quint8 buf[2048];
            for (;;) {
                const ssize_t n = ::recv(fd, buf, sizeof buf, 0);
                if (n < 0)
                    break;
                const ReplyInfo info = parseReply(buf, n, dest.ipv6, magic);
                if (info.valid && outstanding.contains(info.seq)) {
                    outstanding.remove(info.seq);
                    rtts.append(static_cast<double>(nowNs() - info.sentNs) / 1e6);
                }
            }
        }
    };

    for (int i = 0; i < options.count; ++i) {
        if (cancelled.load())
            break;

        quint8 pkt[kPacketLen] = {};
        pkt[0] = dest.ipv6 ? ICMP6_ECHO_REQUEST : ICMP_ECHO;
        pkt[1] = 0;
        const quint16 identNet = htons(ident);
        const quint16 seqNet = htons(static_cast<quint16>(i));
        std::memcpy(pkt + 4, &identNet, 2);
        std::memcpy(pkt + 6, &seqNet, 2);
        std::memcpy(pkt + kIcmpHeaderLen, &magic, sizeof magic);
        const qint64 sendNs = nowNs();
        std::memcpy(pkt + kIcmpHeaderLen + 8, &sendNs, sizeof sendNs);
        if (!dest.ipv6) {
            const quint16 sum = inetChecksum(pkt, kPacketLen);
            std::memcpy(pkt + 2, &sum, 2);
        } // ICMPv6 checksum is filled in by the kernel.

        if (::send(fd, pkt, sizeof pkt, 0) < 0) {
            if (firstSendError.isEmpty())
                firstSendError = sendErrorText(errno);
            ++res.sent; // count the attempt as a lost packet
            drainFor(qMin(options.spacingMs, 100), -1);
            continue;
        }
        ++res.sent;
        outstanding.insert(static_cast<quint16>(i), sendNs);

        drainFor(options.timeoutMsPerReply, i);
        if (i + 1 < options.count && options.spacingMs > 0 && !cancelled.load())
            drainFor(options.spacingMs, -1);
    }

    // Short grace period for stragglers from the last sequence.
    if (!outstanding.isEmpty() && !cancelled.load())
        drainFor(150, -1);

    ::close(fd);

    res.received = static_cast<int>(rtts.size());
    res.rttsMs = rtts;
    if (res.sent > 0)
        res.lossPct = 100.0 * (res.sent - res.received) / res.sent;

    if (!rtts.isEmpty()) {
        double sum = 0, mn = rtts.first(), mx = rtts.first();
        for (double v : rtts) {
            sum += v;
            mn = qMin(mn, v);
            mx = qMax(mx, v);
        }
        res.avgMs = sum / rtts.size();
        res.minMs = mn;
        res.maxMs = mx;
        if (rtts.size() >= 2) {
            double jsum = 0;
            for (qsizetype i = 1; i < rtts.size(); ++i)
                jsum += qAbs(rtts[i] - rtts[i - 1]);
            res.jitterMs = jsum / (rtts.size() - 1);
        } else {
            res.jitterMs = 0;
        }
    }

    if (res.received == 0) {
        res.error = !firstSendError.isEmpty()
            ? firstSendError
            : QStringLiteral("Request timed out — no reply to %1 echo request(s)").arg(res.sent);
    } else if (!firstSendError.isEmpty()) {
        res.error = firstSendError;
    } else if (res.received < res.sent) {
        res.error = QStringLiteral("%1 of %2 packets lost")
                        .arg(res.sent - res.received).arg(res.sent);
    }
    return res;
}

} // namespace netpulse
