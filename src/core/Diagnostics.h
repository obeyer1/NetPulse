#pragma once

#include "core/Types.h"

#include <QList>

class QHostAddress;

namespace netpulse {

// Latest known state of one monitored target.
struct TargetState {
    TargetSpec spec;
    ProbeSample last;
    bool hasSample = false;
};

// True for RFC1918/link-local/loopback/ULA addresses.
bool isPrivateAddress(const QHostAddress &addr);

// Turns the latest per-target results plus the local network snapshot into a
// single human-readable conclusion ("DNS error", "External network
// unreachable, but local router normal", ...). Pure logic — unit tested.
Diagnosis evaluateDiagnosis(const QList<TargetState> &states,
                            const NetworkSnapshot &net,
                            qint64 nowMs);

} // namespace netpulse
