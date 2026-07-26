#pragma once

#include "core/Types.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QStackedWidget;

namespace netpulse {

// Add/edit dialog for one monitoring target, with validation.
class TargetDialog : public QDialog {
    Q_OBJECT
public:
    explicit TargetDialog(QWidget *parent = nullptr);

    void setSpec(const TargetSpec &spec);
    TargetSpec spec() const;

    void accept() override;

private:
    void syncKindPage();
    bool validate(QString *error) const;

    QLineEdit *nameEdit_ = nullptr;
    QComboBox *kindCombo_ = nullptr;
    QStackedWidget *stack_ = nullptr;

    QLineEdit *pingHostEdit_ = nullptr;
    QSpinBox *pingCountSpin_ = nullptr;
    QLineEdit *dnsNameEdit_ = nullptr;
    QLineEdit *dnsServerEdit_ = nullptr;
    QLineEdit *httpsUrlEdit_ = nullptr;

    QSpinBox *intervalSpin_ = nullptr;
    QCheckBox *enabledCheck_ = nullptr;
    QLabel *hintLabel_ = nullptr;

    int editingId_ = -1;
};

} // namespace netpulse
