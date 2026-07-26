#pragma once

#include "core/Types.h"

#include <QList>
#include <QWidget>

class QChart;
class QChartView;
class QComboBox;
class QDateTimeAxis;
class QLabel;
class QLineSeries;
class QScatterSeries;
class QValueAxis;

namespace netpulse {

class Storage;

// History charts for the selected target: latency/jitter (plus TTFB for
// HTTPS) and packet loss, over a selectable window (1 h / 6 h / 24 h).
class ChartsPanel : public QWidget {
    Q_OBJECT
public:
    explicit ChartsPanel(Storage *storage, QWidget *parent = nullptr);

    void setTarget(const TargetSpec &spec);
    void clearTarget();
    void appendSample(const ProbeSample &sample);

private slots:
    void onSamplesFetched(quint64 requestId, int targetId,
                          const QList<netpulse::ProbeSample> &samples);
    void reload();

private:
    qint64 windowMs() const;
    void rebuildSeries();
    void updateAxes();
    void updateStatsLabel();
    void applyChartStyle(QChart *chart);

    Storage *storage_ = nullptr;
    TargetSpec spec_;
    bool hasTarget_ = false;
    quint64 requestCounter_ = 0;
    quint64 pendingRequest_ = 0;

    QList<ProbeSample> window_;

    QLabel *titleLabel_ = nullptr;
    QLabel *statsLabel_ = nullptr;
    QComboBox *rangeCombo_ = nullptr;

    QChart *latencyChart_ = nullptr;
    QChartView *latencyView_ = nullptr;
    QLineSeries *latencySeries_ = nullptr;
    QLineSeries *auxSeries_ = nullptr;       // jitter (ping) or TTFB (https)
    QScatterSeries *failSeries_ = nullptr;
    QDateTimeAxis *latencyAxisX_ = nullptr;
    QValueAxis *latencyAxisY_ = nullptr;

    QChart *lossChart_ = nullptr;
    QChartView *lossView_ = nullptr;
    QLineSeries *lossSeries_ = nullptr;
    QDateTimeAxis *lossAxisX_ = nullptr;
    QValueAxis *lossAxisY_ = nullptr;
};

} // namespace netpulse
