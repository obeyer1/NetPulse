#pragma once

#include <QList>
#include <QString>
#include <atomic>

namespace netpulse {

// Result of one ping batch (N echo requests to a single host).
struct PingBatchResult {
    QString resolvedAddress;
    QString error;                 // empty on full success
    int sent = 0;
    int received = 0;
    QList<double> rttsMs;          // RTT of each received reply
    double avgMs = -1;
    double minMs = -1;
    double maxMs = -1;
    double jitterMs = -1;          // mean absolute delta between consecutive RTTs
    double lossPct = 100.0;
};

// Blocking ICMP echo implementation meant to run on a worker thread pool.
//
// Uses unprivileged ICMP datagram sockets (SOCK_DGRAM + IPPROTO_ICMP /
// IPPROTO_ICMPV6), which macOS allows without root and which can only send
// echo requests — no raw packet crafting, no sniffing, no scanning.
class IcmpPinger {
public:
    struct Options {
        int count = 5;             // echo requests per batch (clamped 1..10)
        int timeoutMsPerReply = 2000;
        int spacingMs = 250;       // pause between sends
    };

    // `cancelled` is polled between short waits so a batch aborts promptly.
    static PingBatchResult run(const QString &host,
                               Options options,
                               const std::atomic_bool &cancelled);
};

} // namespace netpulse
