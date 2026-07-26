#include "ui/Panels.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTime>
#include <QVBoxLayout>

namespace netpulse {
namespace {

struct SeverityStyle {
    QString icon;
    QString bg;
    QString fg;
};

SeverityStyle styleFor(Severity s)
{
    switch (s) {
    case Severity::Ok:
        return {QStringLiteral("✔"), QStringLiteral("rgba(48,164,108,0.16)"),
                QStringLiteral("#30a46c")};
    case Severity::Warning:
        return {QStringLiteral("⚠"), QStringLiteral("rgba(214,158,46,0.16)"),
                QStringLiteral("#d69e2e")};
    case Severity::Error:
        return {QStringLiteral("✖"), QStringLiteral("rgba(214,69,69,0.16)"),
                QStringLiteral("#d64545")};
    case Severity::Info:
        break;
    }
    return {QStringLiteral("ℹ"), QStringLiteral("rgba(140,140,150,0.14)"),
            QStringLiteral("#8e8e93")};
}

} // namespace

// ---------------------------------------------------------------------------
// DiagnosisBanner
// ---------------------------------------------------------------------------

DiagnosisBanner::DiagnosisBanner(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("diagnosisBanner"));
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(12);

    iconLabel_ = new QLabel(this);
    QFont iconFont = iconLabel_->font();
    iconFont.setPointSizeF(iconFont.pointSizeF() + 6);
    iconLabel_->setFont(iconFont);
    layout->addWidget(iconLabel_);

    auto *textLayout = new QVBoxLayout;
    textLayout->setSpacing(1);
    headlineLabel_ = new QLabel(this);
    QFont hf = headlineLabel_->font();
    hf.setBold(true);
    hf.setPointSizeF(hf.pointSizeF() + 1);
    headlineLabel_->setFont(hf);
    detailLabel_ = new QLabel(this);
    detailLabel_->setWordWrap(true);
    textLayout->addWidget(headlineLabel_);
    textLayout->addWidget(detailLabel_);
    layout->addLayout(textLayout, 1);

    timeLabel_ = new QLabel(this);
    timeLabel_->setStyleSheet(QStringLiteral("color: gray;"));
    layout->addWidget(timeLabel_);

    setDiagnosis({Severity::Info, tr("Starting up…"), QString()});
}

void DiagnosisBanner::setDiagnosis(const Diagnosis &d)
{
    const SeverityStyle st = styleFor(d.severity);
    iconLabel_->setText(st.icon);
    iconLabel_->setStyleSheet(QStringLiteral("color: %1;").arg(st.fg));
    headlineLabel_->setText(d.headline);
    detailLabel_->setText(d.detail);
    detailLabel_->setVisible(!d.detail.isEmpty());
    timeLabel_->setText(QTime::currentTime().toString(QStringLiteral("hh:mm:ss")));
    setStyleSheet(QStringLiteral(
        "QFrame#diagnosisBanner { background-color: %1; border-radius: 8px; }")
        .arg(st.bg));
}

// ---------------------------------------------------------------------------
// NetworkInfoPanel
// ---------------------------------------------------------------------------

NetworkInfoPanel::NetworkInfoPanel(QWidget *parent)
    : QFrame(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 6, 14, 6);
    layout->setSpacing(18);

    const auto makePair = [this, layout](const QString &title, QLabel *&value) {
        auto *box = new QVBoxLayout;
        box->setSpacing(0);
        auto *caption = new QLabel(title, this);
        caption->setStyleSheet(QStringLiteral("color: gray; font-size: 10px;"));
        value = new QLabel(QStringLiteral("—"), this);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        box->addWidget(caption);
        box->addWidget(value);
        layout->addLayout(box);
    };

    makePair(tr("INTERFACE"), ifaceLabel_);
    makePair(tr("LOCAL IP"), ipLabel_);
    makePair(tr("GATEWAY"), gatewayLabel_);
    makePair(tr("DNS SERVERS"), dnsLabel_);
    layout->addStretch(1);

    updatedLabel_ = new QLabel(this);
    updatedLabel_->setStyleSheet(QStringLiteral("color: gray; font-size: 10px;"));
    layout->addWidget(updatedLabel_);
}

void NetworkInfoPanel::setSnapshot(const NetworkSnapshot &s)
{
    if (!s.valid) {
        ifaceLabel_->setText(QStringLiteral("—"));
        return;
    }
    if (s.interfaceName.isEmpty()) {
        ifaceLabel_->setText(tr("no default route"));
        ifaceLabel_->setStyleSheet(QStringLiteral("color: #d64545;"));
    } else {
        ifaceLabel_->setText(QStringLiteral("%1 %2").arg(
            s.interfaceName, s.linkUp ? tr("(up)") : tr("(down)")));
        ifaceLabel_->setStyleSheet(
            s.linkUp ? QStringLiteral("color: #30a46c;") : QStringLiteral("color: #d64545;"));
    }
    QString ip = s.ipv4.isEmpty() ? (s.ipv6.isEmpty() ? QStringLiteral("—") : s.ipv6.first())
                                  : s.ipv4.first();
    if (s.ipv4.size() + s.ipv6.size() > 1)
        ip += tr(" (+%1 more)").arg(s.ipv4.size() + s.ipv6.size() - 1);
    ipLabel_->setText(ip);
    gatewayLabel_->setText(s.gateway.isEmpty() ? QStringLiteral("—") : s.gateway);
    dnsLabel_->setText(s.dnsServers.isEmpty() ? QStringLiteral("—")
                                              : s.dnsServers.join(QStringLiteral(", ")));
    updatedLabel_->setText(tr("updated %1").arg(
        s.capturedAt.toString(QStringLiteral("hh:mm:ss"))));
}

// ---------------------------------------------------------------------------
// EmptyState
// ---------------------------------------------------------------------------

EmptyState::EmptyState(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->addStretch(2);

    auto *title = new QLabel(tr("Welcome to NetPulse"), this);
    QFont tf = title->font();
    tf.setPointSizeF(tf.pointSizeF() + 8);
    tf.setBold(true);
    title->setFont(tf);
    title->setAlignment(Qt::AlignCenter);
    outer->addWidget(title);

    auto *subtitle = new QLabel(
        tr("NetPulse only monitors targets you explicitly add.\n"
           "Start with one of these, or add your own:"), this);
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setStyleSheet(QStringLiteral("color: gray;"));
    outer->addWidget(subtitle);
    outer->addSpacing(12);

    auto *buttons = new QVBoxLayout;
    buttons->setSpacing(6);

    const auto addButton = [this, buttons](const QString &text,
                                           const std::function<TargetSpec()> &make) {
        auto *b = new QPushButton(text, this);
        b->setMinimumWidth(340);
        connect(b, &QPushButton::clicked, this, [this, make] {
            emit quickAddRequested(make());
        });
        auto *row = new QHBoxLayout;
        row->addStretch(1);
        row->addWidget(b);
        row->addStretch(1);
        buttons->addLayout(row);
        return b;
    };

    gatewayButton_ = addButton(tr("Ping my router"), [this] {
        TargetSpec s;
        s.kind = ProbeKind::Ping;
        s.host = gateway_;
        s.name = tr("My router");
        return s;
    });
    gatewayButton_->setVisible(false);

    addButton(tr("Ping 1.1.1.1  (Cloudflare DNS)"), [] {
        TargetSpec s;
        s.kind = ProbeKind::Ping;
        s.host = QStringLiteral("1.1.1.1");
        s.name = QStringLiteral("Cloudflare 1.1.1.1");
        return s;
    });
    addButton(tr("Ping 8.8.8.8  (Google DNS)"), [] {
        TargetSpec s;
        s.kind = ProbeKind::Ping;
        s.host = QStringLiteral("8.8.8.8");
        s.name = QStringLiteral("Google 8.8.8.8");
        return s;
    });
    addButton(tr("DNS lookup example.com  (system resolver)"), [] {
        TargetSpec s;
        s.kind = ProbeKind::Dns;
        s.host = QStringLiteral("example.com");
        s.name = QStringLiteral("DNS example.com");
        return s;
    });
    addButton(tr("HTTPS check https://www.apple.com"), [] {
        TargetSpec s;
        s.kind = ProbeKind::Https;
        s.host = QStringLiteral("https://www.apple.com");
        s.name = QStringLiteral("apple.com");
        return s;
    });

    outer->addLayout(buttons);
    outer->addSpacing(10);

    auto *customRow = new QHBoxLayout;
    customRow->addStretch(1);
    auto *customButton = new QPushButton(tr("Add a custom target…"), this);
    connect(customButton, &QPushButton::clicked, this, &EmptyState::addCustomRequested);
    customRow->addWidget(customButton);
    customRow->addStretch(1);
    outer->addLayout(customRow);

    outer->addStretch(3);
}

void EmptyState::setGateway(const QString &gateway)
{
    gateway_ = gateway;
    const bool usable = !gateway.isEmpty() && !gateway.contains(QLatin1Char('%'));
    gatewayButton_->setVisible(usable);
    if (usable)
        gatewayButton_->setText(tr("Ping my router  (%1)").arg(gateway));
}

} // namespace netpulse
