#pragma once

#include "core/Types.h"
#include "ui/SettingsDialog.h"

#include <QMainWindow>

class QAction;
class QLabel;
class QStackedWidget;
class QTableView;

namespace netpulse {

class ChartsPanel;
class DiagnosisBanner;
class EmptyState;
class NetworkInfoPanel;
class ProbeService;
class Storage;
class SystemInfoService;
class TargetsModel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(Storage *storage, ProbeService *probes, SystemInfoService *sysinfo,
               const QString &dbPath, QWidget *parent = nullptr);

private slots:
    void onStorageOpened(bool ok, const QString &error, const QString &path);
    void onTargetsLoaded(const QList<netpulse::TargetSpec> &targets);
    void onTargetSaved(const netpulse::TargetSpec &spec);
    void onTargetRemoved(int targetId);
    void onSampleReady(const netpulse::ProbeSample &sample);
    void onSnapshotReady(const netpulse::NetworkSnapshot &snapshot);
    void onSelectionChanged();

    void addTarget();
    void addQuickTarget(const netpulse::TargetSpec &spec);
    void editSelectedTarget();
    void removeSelectedTarget();
    void probeSelectedNow();
    void togglePauseAll(bool paused);
    void openSettings();
    void showAbout();

private:
    void buildUi();
    void buildMenusAndToolbar();
    void refreshDiagnosis();
    void updateEmptyState();
    int selectedTargetId() const;
    void saveTarget(TargetSpec spec);

    Storage *storage_ = nullptr;
    ProbeService *probes_ = nullptr;
    SystemInfoService *sysinfo_ = nullptr;
    QString dbPath_;
    AppSettings settings_;
    NetworkSnapshot snapshot_;

    TargetsModel *model_ = nullptr;
    QTableView *table_ = nullptr;
    QStackedWidget *leftStack_ = nullptr;
    EmptyState *emptyState_ = nullptr;
    ChartsPanel *charts_ = nullptr;
    DiagnosisBanner *banner_ = nullptr;
    NetworkInfoPanel *networkPanel_ = nullptr;
    QLabel *statusLabel_ = nullptr;

    QAction *editAction_ = nullptr;
    QAction *removeAction_ = nullptr;
    QAction *probeNowAction_ = nullptr;
    QAction *pauseAction_ = nullptr;

    bool paused_ = false;
    qint64 samplesSeen_ = 0;
};

} // namespace netpulse
