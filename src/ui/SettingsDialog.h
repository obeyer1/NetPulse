#pragma once

#include "core/Types.h"

#include <QDialog>

class QSpinBox;

namespace netpulse {

// Application-wide settings persisted with QSettings.
struct AppSettings {
    ProbeConfig probe;
    int defaultIntervalSecs = kDefaultIntervalSecs;
    int retentionDays = 7;

    static AppSettings load();
    void save() const;
};

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(const AppSettings &settings, const QString &dbPath,
                            QWidget *parent = nullptr);

    AppSettings settings() const;

private:
    QSpinBox *intervalSpin_ = nullptr;
    QSpinBox *pingTimeoutSpin_ = nullptr;
    QSpinBox *dnsTimeoutSpin_ = nullptr;
    QSpinBox *httpsTimeoutSpin_ = nullptr;
    QSpinBox *retriesSpin_ = nullptr;
    QSpinBox *retentionSpin_ = nullptr;
};

} // namespace netpulse
