#include "ui/MainWindow.h"

#include "core/Diagnostics.h"
#include "core/ProbeScheduler.h"
#include "core/SystemInfo.h"
#include "storage/Storage.h"
#include "ui/ChartsPanel.h"
#include "ui/Panels.h"
#include "ui/TargetDialog.h"
#include "ui/TargetsModel.h"

#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableView>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

namespace netpulse {

namespace {
const QString kProjectUrl = QStringLiteral("https://github.com/obeyer1/NetPulse");
}

MainWindow::MainWindow(Storage *storage, ProbeService *probes,
                       SystemInfoService *sysinfo, const QString &dbPath,
                       QWidget *parent)
    : QMainWindow(parent)
    , storage_(storage)
    , probes_(probes)
    , sysinfo_(sysinfo)
    , dbPath_(dbPath)
{
    settings_ = AppSettings::load();
    probes_->setConfig(settings_.probe);
    storage_->setRetentionDays(settings_.retentionDays);

    buildUi();
    buildMenusAndToolbar();

    connect(storage_, &Storage::opened, this, &MainWindow::onStorageOpened);
    connect(storage_, &Storage::targetsLoaded, this, &MainWindow::onTargetsLoaded);
    connect(storage_, &Storage::targetSaved, this, &MainWindow::onTargetSaved);
    connect(storage_, &Storage::targetRemoved, this, &MainWindow::onTargetRemoved);
    connect(probes_, &ProbeService::sampleReady, this, &MainWindow::onSampleReady);
    connect(probes_, &ProbeService::probeStarted, model_, &TargetsModel::markProbing);
    connect(sysinfo_, &SystemInfoService::snapshotReady, this,
            &MainWindow::onSnapshotReady);

    setWindowTitle(tr("NetPulse — Local Network Health Monitor"));
    resize(1280, 800);
    setMinimumSize(1000, 640);
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(10, 10, 10, 4);
    rootLayout->setSpacing(8);

    banner_ = new DiagnosisBanner(central);
    rootLayout->addWidget(banner_);

    auto *splitter = new QSplitter(Qt::Horizontal, central);
    splitter->setChildrenCollapsible(false);

    // Left: targets table (or the empty state).
    model_ = new TargetsModel(this);
    table_ = new QTableView(splitter);
    table_->setModel(model_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setColumnWidth(TargetsModel::ColStatus, 110);
    table_->setColumnWidth(TargetsModel::ColName, 170);
    table_->setColumnWidth(TargetsModel::ColKind, 70);
    table_->setColumnWidth(TargetsModel::ColLatency, 85);
    table_->setColumnWidth(TargetsModel::ColJitter, 80);
    table_->setColumnWidth(TargetsModel::ColLoss, 60);
    table_->setColumnWidth(TargetsModel::ColLastCheck, 90);
    table_->setAlternatingRowColors(true);
    table_->setShowGrid(false);

    emptyState_ = new EmptyState(splitter);
    connect(emptyState_, &EmptyState::quickAddRequested, this,
            &MainWindow::addQuickTarget);
    connect(emptyState_, &EmptyState::addCustomRequested, this, &MainWindow::addTarget);

    leftStack_ = new QStackedWidget(splitter);
    leftStack_->addWidget(emptyState_);
    leftStack_->addWidget(table_);
    splitter->addWidget(leftStack_);

    // Right: charts for the selected target.
    charts_ = new ChartsPanel(storage_, splitter);
    splitter->addWidget(charts_);
    splitter->setStretchFactor(0, 5);
    splitter->setStretchFactor(1, 4);
    splitter->setSizes({680, 560});

    rootLayout->addWidget(splitter, 1);

    networkPanel_ = new NetworkInfoPanel(central);
    rootLayout->addWidget(networkPanel_);

    setCentralWidget(central);

    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &MainWindow::onSelectionChanged);
    connect(model_, &TargetsModel::targetEnabledChanged, this,
            [this](const TargetSpec &spec) { saveTarget(spec); });
    connect(table_, &QTableView::doubleClicked, this, &MainWindow::editSelectedTarget);

    statusLabel_ = new QLabel(this);
    statusBar()->addWidget(statusLabel_);
    auto *privacyLabel =
        new QLabel(tr("Local-only · no data leaves this Mac"), this);
    privacyLabel->setStyleSheet(QStringLiteral("color: gray;"));
    statusBar()->addPermanentWidget(privacyLabel);
    statusLabel_->setText(tr("Opening history database…"));
}

void MainWindow::buildMenusAndToolbar()
{
    auto *toolbar = addToolBar(tr("Main"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);

    auto *addAction = toolbar->addAction(tr("＋ Add Target"), this, &MainWindow::addTarget);
    addAction->setShortcut(QKeySequence::New);
    editAction_ = toolbar->addAction(tr("Edit"), this, &MainWindow::editSelectedTarget);
    removeAction_ = toolbar->addAction(tr("Remove"), this,
                                       &MainWindow::removeSelectedTarget);
    removeAction_->setShortcut(QKeySequence::Delete);
    toolbar->addSeparator();
    probeNowAction_ = toolbar->addAction(tr("Probe Now"), this,
                                         &MainWindow::probeSelectedNow);
    pauseAction_ = toolbar->addAction(tr("Pause All"));
    pauseAction_->setCheckable(true);
    connect(pauseAction_, &QAction::toggled, this, &MainWindow::togglePauseAll);
    toolbar->addSeparator();
    toolbar->addAction(tr("Settings…"), this, &MainWindow::openSettings);

    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("Settings…"), this, &MainWindow::openSettings);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Quit"), QKeySequence::Quit, qApp, &QApplication::quit);

    auto *targetMenu = menuBar()->addMenu(tr("&Target"));
    targetMenu->addAction(tr("Add Target…"), this, &MainWindow::addTarget);
    targetMenu->addAction(tr("Edit Selected"), this, &MainWindow::editSelectedTarget);
    targetMenu->addAction(tr("Remove Selected"), this,
                          &MainWindow::removeSelectedTarget);
    targetMenu->addSeparator();
    targetMenu->addAction(tr("Probe Selected Now"), this,
                          &MainWindow::probeSelectedNow);

    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("About NetPulse"), this, &MainWindow::showAbout);
    helpMenu->addAction(tr("Project on GitHub"), this, [] {
        QDesktopServices::openUrl(QUrl(kProjectUrl));
    });

    editAction_->setEnabled(false);
    removeAction_->setEnabled(false);
    probeNowAction_->setEnabled(false);
}

void MainWindow::onStorageOpened(bool ok, const QString &error, const QString &path)
{
    if (!ok) {
        QMessageBox::critical(this, tr("Database error"),
                              tr("Could not open the history database at\n%1\n\n%2")
                                  .arg(path, error));
        statusLabel_->setText(tr("Database unavailable — history disabled"));
        return;
    }
    statusLabel_->setText(tr("History: %1").arg(path));
    storage_->loadTargets();
}

void MainWindow::onTargetsLoaded(const QList<TargetSpec> &targets)
{
    model_->setTargets(targets);
    probes_->setTargets(targets);
    updateEmptyState();
    if (model_->rowCount() > 0)
        table_->selectRow(0);
    refreshDiagnosis();
}

void MainWindow::onTargetSaved(const TargetSpec &spec)
{
    model_->upsertTarget(spec);
    probes_->upsertTarget(spec);
    updateEmptyState();
    if (model_->rowCount() == 1)
        table_->selectRow(0);
    refreshDiagnosis();
}

void MainWindow::onTargetRemoved(int targetId)
{
    model_->removeTarget(targetId);
    probes_->removeTarget(targetId);
    updateEmptyState();
    if (selectedTargetId() < 0)
        charts_->clearTarget();
    refreshDiagnosis();
}

void MainWindow::onSampleReady(const ProbeSample &sample)
{
    // Persistence happens via the direct sampleReady→Storage connection made
    // in main(); this slot only refreshes the UI.
    ++samplesSeen_;
    model_->applySample(sample);
    if (sample.targetId == selectedTargetId())
        charts_->appendSample(sample);
    refreshDiagnosis();
}

void MainWindow::onSnapshotReady(const NetworkSnapshot &snapshot)
{
    snapshot_ = snapshot;
    networkPanel_->setSnapshot(snapshot);
    emptyState_->setGateway(snapshot.gateway);
    refreshDiagnosis();
}

void MainWindow::onSelectionChanged()
{
    const int id = selectedTargetId();
    const bool has = id >= 0;
    editAction_->setEnabled(has);
    removeAction_->setEnabled(has);
    probeNowAction_->setEnabled(has && !paused_);
    if (!has) {
        charts_->clearTarget();
        return;
    }
    const int row = model_->rowForId(id);
    if (const auto *r = model_->rowAt(row))
        charts_->setTarget(r->spec);
}

int MainWindow::selectedTargetId() const
{
    const QModelIndexList rows = table_->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return -1;
    const auto *row = model_->rowAt(rows.first().row());
    return row ? row->spec.id : -1;
}

void MainWindow::addTarget()
{
    TargetDialog dialog(this);
    TargetSpec fresh;
    fresh.intervalSecs = settings_.defaultIntervalSecs;
    dialog.setSpec(fresh);
    if (dialog.exec() == QDialog::Accepted)
        saveTarget(dialog.spec());
}

void MainWindow::addQuickTarget(const TargetSpec &specIn)
{
    TargetSpec spec = specIn;
    spec.intervalSecs = settings_.defaultIntervalSecs;
    saveTarget(spec);
}

void MainWindow::saveTarget(TargetSpec spec)
{
    spec.intervalSecs = clampIntervalSecs(spec.intervalSecs);
    storage_->upsertTarget(spec);
}

void MainWindow::editSelectedTarget()
{
    const int id = selectedTargetId();
    const int row = model_->rowForId(id);
    const auto *r = model_->rowAt(row);
    if (!r)
        return;
    TargetDialog dialog(this);
    dialog.setSpec(r->spec);
    if (dialog.exec() == QDialog::Accepted)
        saveTarget(dialog.spec());
}

void MainWindow::removeSelectedTarget()
{
    const int id = selectedTargetId();
    const int row = model_->rowForId(id);
    const auto *r = model_->rowAt(row);
    if (!r)
        return;
    const auto answer = QMessageBox::question(
        this, tr("Remove target"),
        tr("Remove \"%1\" and delete its stored history?").arg(r->spec.name));
    if (answer == QMessageBox::Yes)
        storage_->removeTarget(id);
}

void MainWindow::probeSelectedNow()
{
    const int id = selectedTargetId();
    if (id >= 0)
        probes_->probeNow(id);
}

void MainWindow::togglePauseAll(bool paused)
{
    paused_ = paused;
    probes_->setPaused(paused);
    probeNowAction_->setEnabled(selectedTargetId() >= 0 && !paused);
    pauseAction_->setText(paused ? tr("Resume All") : tr("Pause All"));
    if (paused)
        statusBar()->showMessage(tr("Monitoring paused"), 5000);
    else
        statusBar()->showMessage(tr("Monitoring resumed"), 3000);
    refreshDiagnosis();
}

void MainWindow::openSettings()
{
    SettingsDialog dialog(settings_, dbPath_, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    settings_ = dialog.settings();
    settings_.save();
    probes_->setConfig(settings_.probe);
    storage_->setRetentionDays(settings_.retentionDays);
    statusBar()->showMessage(tr("Settings applied"), 3000);
}

void MainWindow::refreshDiagnosis()
{
    if (paused_) {
        banner_->setDiagnosis({Severity::Info, tr("Monitoring paused"),
                               tr("Probes are stopped. Click \"Resume All\" to "
                                  "continue.")});
        return;
    }
    const Diagnosis d = evaluateDiagnosis(model_->targetStates(), snapshot_,
                                          QDateTime::currentMSecsSinceEpoch());
    banner_->setDiagnosis(d);
}

void MainWindow::updateEmptyState()
{
    leftStack_->setCurrentWidget(model_->rowCount() > 0
                                     ? static_cast<QWidget *>(table_)
                                     : static_cast<QWidget *>(emptyState_));
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, tr("About NetPulse"),
        tr("<h3>NetPulse %1</h3>"
           "<p>Local Network Health Monitor for macOS.</p>"
           "<p>NetPulse periodically probes <b>only the targets you add</b> "
           "(ping, DNS lookups, HTTPS requests), stores the history in a local "
           "SQLite file and explains problems in plain language.</p>"
           "<p>No scanning. No telemetry. Everything stays on this Mac.</p>"
           "<p><a href=\"%2\">%2</a></p>"
           "<p>Released under the MIT License.</p>")
            .arg(QCoreApplication::applicationVersion(), kProjectUrl));
}

} // namespace netpulse
