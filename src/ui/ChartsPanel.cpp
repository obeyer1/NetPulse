#include "ui/ChartsPanel.h"

#include "storage/Storage.h"

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QLineSeries>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QValueAxis>

#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QSettings>
#include <QVBoxLayout>

#include <algorithm>

namespace netpulse {
namespace {

const QColor kLatencyColor(0x4c, 0x8d, 0xff);
const QColor kAuxColor(0x9b, 0x7b, 0xff);
const QColor kFailColor(0xd6, 0x45, 0x45);
const QColor kLossColor(0xd6, 0x45, 0x45);

} // namespace

ChartsPanel::ChartsPanel(Storage *storage, QWidget *parent)
    : QWidget(parent), storage_(storage)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(6);

    auto *header = new QHBoxLayout;
    titleLabel_ = new QLabel(tr("Select a target to see its history"), this);
    QFont tf = titleLabel_->font();
    tf.setPointSizeF(tf.pointSizeF() + 2);
    tf.setBold(true);
    titleLabel_->setFont(tf);
    header->addWidget(titleLabel_, 1);

    rangeCombo_ = new QComboBox(this);
    rangeCombo_->addItem(tr("Last 15 minutes"), 15);
    rangeCombo_->addItem(tr("Last hour"), 60);
    rangeCombo_->addItem(tr("Last 6 hours"), 360);
    rangeCombo_->addItem(tr("Last 24 hours"), 1440);
    const int savedMinutes =
        QSettings().value(QStringLiteral("charts/rangeMinutes"), 1440).toInt();
    const int savedIndex = rangeCombo_->findData(savedMinutes);
    rangeCombo_->setCurrentIndex(savedIndex >= 0 ? savedIndex : 3);
    connect(rangeCombo_, &QComboBox::currentIndexChanged, this, [this] {
        QSettings().setValue(QStringLiteral("charts/rangeMinutes"),
                             rangeCombo_->currentData().toInt());
        reload();
    });
    header->addWidget(rangeCombo_);
    layout->addLayout(header);

    statsLabel_ = new QLabel(this);
    statsLabel_->setStyleSheet(QStringLiteral("color: gray;"));
    statsLabel_->setWordWrap(true);
    layout->addWidget(statsLabel_);

    // ---- Latency chart -----------------------------------------------------
    latencyChart_ = new QChart;
    latencySeries_ = new QLineSeries;
    latencySeries_->setName(tr("Latency"));
    latencySeries_->setPen(QPen(kLatencyColor, 1.8));
    auxSeries_ = new QLineSeries;
    auxSeries_->setName(tr("Jitter"));
    QPen auxPen(kAuxColor, 1.2);
    auxPen.setStyle(Qt::DashLine);
    auxSeries_->setPen(auxPen);
    failSeries_ = new QScatterSeries;
    failSeries_->setName(tr("Failures"));
    failSeries_->setColor(kFailColor);
    failSeries_->setBorderColor(Qt::transparent);
    failSeries_->setMarkerSize(7.0);

    latencyChart_->addSeries(latencySeries_);
    latencyChart_->addSeries(auxSeries_);
    latencyChart_->addSeries(failSeries_);

    latencyAxisX_ = new QDateTimeAxis;
    latencyAxisX_->setFormat(QStringLiteral("hh:mm"));
    latencyAxisX_->setTickCount(7);
    latencyAxisY_ = new QValueAxis;
    latencyAxisY_->setTitleText(tr("ms"));
    latencyAxisY_->setLabelFormat(QStringLiteral("%.0f"));
    latencyChart_->addAxis(latencyAxisX_, Qt::AlignBottom);
    latencyChart_->addAxis(latencyAxisY_, Qt::AlignLeft);
    latencySeries_->attachAxis(latencyAxisX_);
    latencySeries_->attachAxis(latencyAxisY_);
    auxSeries_->attachAxis(latencyAxisX_);
    auxSeries_->attachAxis(latencyAxisY_);
    failSeries_->attachAxis(latencyAxisX_);
    failSeries_->attachAxis(latencyAxisY_);

    applyChartStyle(latencyChart_);
    latencyView_ = new QChartView(latencyChart_, this);
    latencyView_->setRenderHint(QPainter::Antialiasing);
    layout->addWidget(latencyView_, 3);

    // ---- Loss chart --------------------------------------------------------
    lossChart_ = new QChart;
    lossSeries_ = new QLineSeries;
    lossSeries_->setName(tr("Packet loss"));
    lossSeries_->setPen(QPen(kLossColor, 1.5));
    lossChart_->addSeries(lossSeries_);

    lossAxisX_ = new QDateTimeAxis;
    lossAxisX_->setFormat(QStringLiteral("hh:mm"));
    lossAxisX_->setTickCount(7);
    lossAxisY_ = new QValueAxis;
    lossAxisY_->setRange(0, 100);
    lossAxisY_->setTitleText(tr("loss %"));
    lossAxisY_->setLabelFormat(QStringLiteral("%.0f"));
    lossChart_->addAxis(lossAxisX_, Qt::AlignBottom);
    lossChart_->addAxis(lossAxisY_, Qt::AlignLeft);
    lossSeries_->attachAxis(lossAxisX_);
    lossSeries_->attachAxis(lossAxisY_);

    applyChartStyle(lossChart_);
    lossView_ = new QChartView(lossChart_, this);
    lossView_->setRenderHint(QPainter::Antialiasing);
    layout->addWidget(lossView_, 2);

    connect(storage_, &Storage::samplesFetched, this, &ChartsPanel::onSamplesFetched);
}

void ChartsPanel::applyChartStyle(QChart *chart)
{
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    chart->setTheme(dark ? QChart::ChartThemeDark : QChart::ChartThemeLight);
    chart->setBackgroundVisible(false);
    chart->setPlotAreaBackgroundVisible(false);
    chart->legend()->setVisible(true);
    chart->legend()->setAlignment(Qt::AlignBottom);
    chart->setMargins(QMargins(4, 4, 4, 0));

    // The theme call above resets series colors; restore ours.
    if (chart == latencyChart_) {
        latencySeries_->setPen(QPen(kLatencyColor, 1.8));
        QPen auxPen(kAuxColor, 1.2);
        auxPen.setStyle(Qt::DashLine);
        auxSeries_->setPen(auxPen);
        failSeries_->setColor(kFailColor);
        failSeries_->setBorderColor(Qt::transparent);
        failSeries_->setMarkerSize(7.0);
    } else if (chart == lossChart_) {
        lossSeries_->setPen(QPen(kLossColor, 1.5));
    }
}

qint64 ChartsPanel::windowMs() const
{
    return static_cast<qint64>(rangeCombo_->currentData().toInt()) * 60 * 1000;
}

void ChartsPanel::setTarget(const TargetSpec &spec)
{
    spec_ = spec;
    hasTarget_ = true;
    titleLabel_->setText(spec.name);
    auxSeries_->setName(spec.kind == ProbeKind::Https ? tr("First byte") : tr("Jitter"));
    lossView_->setVisible(spec.kind == ProbeKind::Ping);
    reload();
}

void ChartsPanel::clearTarget()
{
    hasTarget_ = false;
    spec_ = {};
    window_.clear();
    titleLabel_->setText(tr("Select a target to see its history"));
    statsLabel_->clear();
    rebuildSeries();
}

void ChartsPanel::reload()
{
    if (!hasTarget_)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    pendingRequest_ = ++requestCounter_;
    storage_->fetchSamples(pendingRequest_, spec_.id, now - windowMs(), now);
}

void ChartsPanel::onSamplesFetched(quint64 requestId, int targetId,
                                   const QList<ProbeSample> &samples)
{
    if (!hasTarget_ || requestId != pendingRequest_ || targetId != spec_.id)
        return;
    window_ = samples;
    rebuildSeries();
    updateStatsLabel();
}

void ChartsPanel::appendSample(const ProbeSample &sample)
{
    if (!hasTarget_ || sample.targetId != spec_.id)
        return;
    window_.append(sample);
    const qint64 cutoff = QDateTime::currentMSecsSinceEpoch() - windowMs();
    while (!window_.isEmpty() && window_.first().timestampMs < cutoff)
        window_.removeFirst();
    rebuildSeries();
    updateStatsLabel();
}

void ChartsPanel::rebuildSeries()
{
    QList<QPointF> latencyPts, auxPts, failPts, lossPts;
    latencyPts.reserve(window_.size());
    for (const ProbeSample &s : std::as_const(window_)) {
        const auto x = static_cast<qreal>(s.timestampMs);
        if (s.status == SampleStatus::Failed) {
            failPts.append({x, 0.0});
        } else if (s.latencyMs >= 0) {
            latencyPts.append({x, s.latencyMs});
            if (s.kind == ProbeKind::Ping && s.jitterMs >= 0)
                auxPts.append({x, s.jitterMs});
            else if (s.kind == ProbeKind::Https && s.ttfbMs >= 0)
                auxPts.append({x, s.ttfbMs});
        }
        if (s.kind == ProbeKind::Ping)
            lossPts.append({x, s.lossPct < 0 ? 100.0 : s.lossPct});
    }
    latencySeries_->replace(latencyPts);
    auxSeries_->replace(auxPts);
    failSeries_->replace(failPts);
    lossSeries_->replace(lossPts);
    updateAxes();
}

void ChartsPanel::updateAxes()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QDateTime to = QDateTime::fromMSecsSinceEpoch(now);
    const QDateTime from = QDateTime::fromMSecsSinceEpoch(now - windowMs());
    latencyAxisX_->setRange(from, to);
    lossAxisX_->setRange(from, to);

    double maxY = 10.0;
    const auto pts = latencySeries_->points();
    for (const QPointF &p : pts)
        maxY = std::max(maxY, p.y());
    const auto auxPts = auxSeries_->points();
    for (const QPointF &p : auxPts)
        maxY = std::max(maxY, p.y());
    latencyAxisY_->setRange(0, maxY * 1.15);
}

void ChartsPanel::updateStatsLabel()
{
    if (window_.isEmpty()) {
        statsLabel_->setText(tr("No samples in this window yet."));
        return;
    }
    int ok = 0, fail = 0;
    double sum = 0, mn = 1e18, mx = 0, lossSum = 0;
    int latCount = 0, lossCount = 0;
    for (const ProbeSample &s : std::as_const(window_)) {
        if (s.status == SampleStatus::Failed) {
            ++fail;
        } else {
            ++ok;
            if (s.latencyMs >= 0) {
                sum += s.latencyMs;
                mn = std::min(mn, s.latencyMs);
                mx = std::max(mx, s.latencyMs);
                ++latCount;
            }
        }
        if (s.kind == ProbeKind::Ping && s.lossPct >= 0) {
            lossSum += s.lossPct;
            ++lossCount;
        }
    }
    const double availability = 100.0 * ok / (ok + fail);
    QString text = tr("%1 checks · %2% reachable")
                       .arg(ok + fail)
                       .arg(QString::number(availability, 'f', 1));
    if (latCount > 0) {
        text += tr(" · latency avg %1 / min %2 / max %3 ms")
                    .arg(QString::number(sum / latCount, 'f', 1),
                         QString::number(mn, 'f', 1),
                         QString::number(mx, 'f', 1));
    }
    if (lossCount > 0)
        text += tr(" · avg loss %1%").arg(QString::number(lossSum / lossCount, 'f', 1));
    statsLabel_->setText(text);
}

} // namespace netpulse
