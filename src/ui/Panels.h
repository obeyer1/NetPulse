#pragma once

#include "core/Types.h"

#include <QFrame>
#include <QWidget>

class QLabel;
class QPushButton;

namespace netpulse {

// ---------------------------------------------------------------------------
// DiagnosisBanner — the colored strip at the top with the current conclusion.
// ---------------------------------------------------------------------------
class DiagnosisBanner : public QFrame {
    Q_OBJECT
public:
    explicit DiagnosisBanner(QWidget *parent = nullptr);
    void setDiagnosis(const Diagnosis &diagnosis);

private:
    QLabel *iconLabel_ = nullptr;
    QLabel *headlineLabel_ = nullptr;
    QLabel *detailLabel_ = nullptr;
    QLabel *timeLabel_ = nullptr;
};

// ---------------------------------------------------------------------------
// NetworkInfoPanel — read-only local network facts (interface, IPs, gateway,
// DNS servers).
// ---------------------------------------------------------------------------
class NetworkInfoPanel : public QFrame {
    Q_OBJECT
public:
    explicit NetworkInfoPanel(QWidget *parent = nullptr);
    void setSnapshot(const NetworkSnapshot &snapshot);

private:
    QLabel *ifaceLabel_ = nullptr;
    QLabel *ipLabel_ = nullptr;
    QLabel *gatewayLabel_ = nullptr;
    QLabel *dnsLabel_ = nullptr;
    QLabel *updatedLabel_ = nullptr;
};

// ---------------------------------------------------------------------------
// EmptyState — shown instead of the table until the user adds targets.
// Quick-add buttons only ever add targets when clicked by the user.
// ---------------------------------------------------------------------------
class EmptyState : public QWidget {
    Q_OBJECT
public:
    explicit EmptyState(QWidget *parent = nullptr);
    void setGateway(const QString &gateway);

signals:
    void quickAddRequested(const netpulse::TargetSpec &spec);
    void addCustomRequested();

private:
    QPushButton *gatewayButton_ = nullptr;
    QString gateway_;
};

} // namespace netpulse
