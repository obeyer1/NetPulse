#include "ui/SettingsDialog.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

namespace netpulse {

AppSettings AppSettings::load()
{
    QSettings s;
    AppSettings a;
    a.defaultIntervalSecs =
        clampIntervalSecs(s.value(QStringLiteral("probe/defaultIntervalSecs"),
                                  kDefaultIntervalSecs).toInt());
    a.probe.pingTimeoutMs =
        qBound(200, s.value(QStringLiteral("probe/pingTimeoutMs"), 2000).toInt(), 10000);
    a.probe.dnsTimeoutMs =
        qBound(500, s.value(QStringLiteral("probe/dnsTimeoutMs"), 3000).toInt(), 15000);
    a.probe.httpsTimeoutMs =
        qBound(1000, s.value(QStringLiteral("probe/httpsTimeoutMs"), 8000).toInt(), 30000);
    a.probe.maxRetries =
        qBound(0, s.value(QStringLiteral("probe/maxRetries"), 1).toInt(), 3);
    a.retentionDays =
        qBound(1, s.value(QStringLiteral("storage/retentionDays"), 7).toInt(), 90);
    return a;
}

void AppSettings::save() const
{
    QSettings s;
    s.setValue(QStringLiteral("probe/defaultIntervalSecs"), defaultIntervalSecs);
    s.setValue(QStringLiteral("probe/pingTimeoutMs"), probe.pingTimeoutMs);
    s.setValue(QStringLiteral("probe/dnsTimeoutMs"), probe.dnsTimeoutMs);
    s.setValue(QStringLiteral("probe/httpsTimeoutMs"), probe.httpsTimeoutMs);
    s.setValue(QStringLiteral("probe/maxRetries"), probe.maxRetries);
    s.setValue(QStringLiteral("storage/retentionDays"), retentionDays);
}

SettingsDialog::SettingsDialog(const AppSettings &settings, const QString &dbPath,
                               QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("NetPulse Settings"));
    setModal(true);
    setMinimumWidth(440);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;

    intervalSpin_ = new QSpinBox(this);
    intervalSpin_->setRange(kMinIntervalSecs, 3600);
    intervalSpin_->setValue(settings.defaultIntervalSecs);
    intervalSpin_->setSuffix(tr(" s"));
    form->addRow(tr("Default check interval"), intervalSpin_);

    pingTimeoutSpin_ = new QSpinBox(this);
    pingTimeoutSpin_->setRange(200, 10000);
    pingTimeoutSpin_->setSingleStep(100);
    pingTimeoutSpin_->setValue(settings.probe.pingTimeoutMs);
    pingTimeoutSpin_->setSuffix(tr(" ms"));
    form->addRow(tr("Ping reply timeout"), pingTimeoutSpin_);

    dnsTimeoutSpin_ = new QSpinBox(this);
    dnsTimeoutSpin_->setRange(500, 15000);
    dnsTimeoutSpin_->setSingleStep(250);
    dnsTimeoutSpin_->setValue(settings.probe.dnsTimeoutMs);
    dnsTimeoutSpin_->setSuffix(tr(" ms"));
    form->addRow(tr("DNS lookup timeout"), dnsTimeoutSpin_);

    httpsTimeoutSpin_ = new QSpinBox(this);
    httpsTimeoutSpin_->setRange(1000, 30000);
    httpsTimeoutSpin_->setSingleStep(500);
    httpsTimeoutSpin_->setValue(settings.probe.httpsTimeoutMs);
    httpsTimeoutSpin_->setSuffix(tr(" ms"));
    form->addRow(tr("HTTPS request timeout"), httpsTimeoutSpin_);

    retriesSpin_ = new QSpinBox(this);
    retriesSpin_->setRange(0, 3);
    retriesSpin_->setValue(settings.probe.maxRetries);
    form->addRow(tr("Retries after transient failure"), retriesSpin_);

    retentionSpin_ = new QSpinBox(this);
    retentionSpin_->setRange(1, 90);
    retentionSpin_->setValue(settings.retentionDays);
    retentionSpin_->setSuffix(tr(" days"));
    form->addRow(tr("Keep history for"), retentionSpin_);

    layout->addLayout(form);

    auto *dbLabel = new QLabel(tr("History database: %1").arg(dbPath), this);
    dbLabel->setWordWrap(true);
    dbLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
    dbLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(dbLabel);

    auto *revealButton = new QPushButton(tr("Show data folder in Finder"), this);
    connect(revealButton, &QPushButton::clicked, this, [dbPath] {
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(QFileInfo(dbPath).absolutePath()));
    });
    layout->addWidget(revealButton, 0, Qt::AlignLeft);

    auto *privacy = new QLabel(
        tr("All data stays on this Mac. NetPulse never uploads, syncs or "
           "collects anything."), this);
    privacy->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
    privacy->setWordWrap(true);
    layout->addWidget(privacy);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

AppSettings SettingsDialog::settings() const
{
    AppSettings a;
    a.defaultIntervalSecs = clampIntervalSecs(intervalSpin_->value());
    a.probe.pingTimeoutMs = pingTimeoutSpin_->value();
    a.probe.dnsTimeoutMs = dnsTimeoutSpin_->value();
    a.probe.httpsTimeoutMs = httpsTimeoutSpin_->value();
    a.probe.maxRetries = retriesSpin_->value();
    a.retentionDays = retentionSpin_->value();
    return a;
}

} // namespace netpulse
