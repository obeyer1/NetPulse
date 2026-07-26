#include "ui/TargetDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSpinBox>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace netpulse {

TargetDialog::TargetDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Add Target"));
    setModal(true);
    setMinimumWidth(460);

    auto *layout = new QVBoxLayout(this);

    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    nameEdit_ = new QLineEdit(this);
    nameEdit_->setPlaceholderText(tr("Optional — defaults to the host"));
    form->addRow(tr("Name"), nameEdit_);

    kindCombo_ = new QComboBox(this);
    kindCombo_->addItem(tr("Ping (ICMP echo)"), static_cast<int>(ProbeKind::Ping));
    kindCombo_->addItem(tr("DNS lookup"), static_cast<int>(ProbeKind::Dns));
    kindCombo_->addItem(tr("HTTPS request"), static_cast<int>(ProbeKind::Https));
    form->addRow(tr("Probe type"), kindCombo_);
    layout->addLayout(form);

    stack_ = new QStackedWidget(this);

    // --- Ping page
    auto *pingPage = new QWidget(this);
    auto *pingForm = new QFormLayout(pingPage);
    pingForm->setContentsMargins(0, 0, 0, 0);
    pingHostEdit_ = new QLineEdit(pingPage);
    pingHostEdit_->setPlaceholderText(tr("e.g. 192.168.1.1, 1.1.1.1 or my-nas.local"));
    pingForm->addRow(tr("Host / IP"), pingHostEdit_);
    pingCountSpin_ = new QSpinBox(pingPage);
    pingCountSpin_->setRange(1, 10);
    pingCountSpin_->setValue(5);
    pingCountSpin_->setSuffix(tr(" echoes per check"));
    pingForm->addRow(tr("Packets"), pingCountSpin_);
    stack_->addWidget(pingPage);

    // --- DNS page
    auto *dnsPage = new QWidget(this);
    auto *dnsForm = new QFormLayout(dnsPage);
    dnsForm->setContentsMargins(0, 0, 0, 0);
    dnsNameEdit_ = new QLineEdit(dnsPage);
    dnsNameEdit_->setPlaceholderText(tr("Hostname to resolve, e.g. example.com"));
    dnsForm->addRow(tr("Lookup"), dnsNameEdit_);
    dnsServerEdit_ = new QLineEdit(dnsPage);
    dnsServerEdit_->setPlaceholderText(tr("Optional DNS server IP — empty = system resolver"));
    dnsForm->addRow(tr("DNS server"), dnsServerEdit_);
    stack_->addWidget(dnsPage);

    // --- HTTPS page
    auto *httpsPage = new QWidget(this);
    auto *httpsForm = new QFormLayout(httpsPage);
    httpsForm->setContentsMargins(0, 0, 0, 0);
    httpsUrlEdit_ = new QLineEdit(httpsPage);
    httpsUrlEdit_->setPlaceholderText(tr("e.g. https://www.apple.com"));
    httpsForm->addRow(tr("URL"), httpsUrlEdit_);
    stack_->addWidget(httpsPage);

    layout->addWidget(stack_);

    auto *bottomForm = new QFormLayout;
    intervalSpin_ = new QSpinBox(this);
    intervalSpin_->setRange(kMinIntervalSecs, 3600);
    intervalSpin_->setValue(kDefaultIntervalSecs);
    intervalSpin_->setSuffix(tr(" s"));
    bottomForm->addRow(tr("Check every"), intervalSpin_);
    enabledCheck_ = new QCheckBox(tr("Enabled"), this);
    enabledCheck_->setChecked(true);
    bottomForm->addRow(QString(), enabledCheck_);
    layout->addLayout(bottomForm);

    hintLabel_ = new QLabel(
        tr("NetPulse only probes targets you add here — no scanning, ever. "
           "Minimum interval: %1 s.").arg(kMinIntervalSecs), this);
    hintLabel_->setWordWrap(true);
    hintLabel_->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
    layout->addWidget(hintLabel_);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &TargetDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &TargetDialog::reject);
    layout->addWidget(buttons);

    connect(kindCombo_, &QComboBox::currentIndexChanged, this,
            &TargetDialog::syncKindPage);
    syncKindPage();
}

void TargetDialog::syncKindPage()
{
    stack_->setCurrentIndex(kindCombo_->currentIndex());
}

void TargetDialog::setSpec(const TargetSpec &spec)
{
    editingId_ = spec.id;
    setWindowTitle(spec.id < 0 ? tr("Add Target") : tr("Edit Target"));
    nameEdit_->setText(spec.name);
    kindCombo_->setCurrentIndex(static_cast<int>(spec.kind));
    switch (spec.kind) {
    case ProbeKind::Ping:
        pingHostEdit_->setText(spec.host);
        break;
    case ProbeKind::Dns:
        dnsNameEdit_->setText(spec.host);
        dnsServerEdit_->setText(spec.dnsServer);
        break;
    case ProbeKind::Https:
        httpsUrlEdit_->setText(spec.host);
        break;
    }
    pingCountSpin_->setValue(spec.pingCount);
    intervalSpin_->setValue(clampIntervalSecs(spec.intervalSecs));
    enabledCheck_->setChecked(spec.enabled);
    syncKindPage();
}

TargetSpec TargetDialog::spec() const
{
    TargetSpec s;
    s.id = editingId_;
    s.kind = static_cast<ProbeKind>(kindCombo_->currentData().toInt());
    switch (s.kind) {
    case ProbeKind::Ping:
        s.host = pingHostEdit_->text().trimmed();
        break;
    case ProbeKind::Dns:
        s.host = dnsNameEdit_->text().trimmed();
        s.dnsServer = dnsServerEdit_->text().trimmed();
        break;
    case ProbeKind::Https: {
        QString url = httpsUrlEdit_->text().trimmed();
        if (!url.isEmpty() && !url.contains(QLatin1String("://")))
            url.prepend(QLatin1String("https://"));
        s.host = url;
        break;
    }
    }
    s.name = nameEdit_->text().trimmed();
    if (s.name.isEmpty()) {
        s.name = s.kind == ProbeKind::Https ? QUrl(s.host).host() : s.host;
        if (s.name.isEmpty())
            s.name = s.host;
    }
    s.pingCount = pingCountSpin_->value();
    s.intervalSecs = clampIntervalSecs(intervalSpin_->value());
    s.enabled = enabledCheck_->isChecked();
    return s;
}

bool TargetDialog::validate(QString *error) const
{
    const TargetSpec s = spec();
    if (s.host.isEmpty()) {
        *error = tr("Please enter a host, name or URL to monitor.");
        return false;
    }
    if (s.kind == ProbeKind::Https) {
        const QUrl url(s.host);
        if (!url.isValid() || url.host().isEmpty()) {
            *error = tr("\"%1\" is not a valid URL.").arg(s.host);
            return false;
        }
        const QString scheme = url.scheme().toLower();
        if (scheme != QLatin1String("https") && scheme != QLatin1String("http")) {
            *error = tr("Only http(s) URLs are supported.");
            return false;
        }
    }
    if (s.kind == ProbeKind::Dns && !s.dnsServer.isEmpty()
        && QHostAddress(s.dnsServer).isNull()) {
        *error = tr("The DNS server must be an IP address (e.g. 1.1.1.1), "
                    "or leave it empty for the system resolver.");
        return false;
    }
    return true;
}

void TargetDialog::accept()
{
    QString error;
    if (!validate(&error)) {
        QMessageBox::warning(this, tr("Invalid target"), error);
        return;
    }
    QDialog::accept();
}

} // namespace netpulse
