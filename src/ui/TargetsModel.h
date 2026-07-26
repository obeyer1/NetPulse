#pragma once

#include "core/Diagnostics.h"
#include "core/Types.h"

#include <QAbstractTableModel>
#include <QColor>
#include <QList>

namespace netpulse {

// Table model of monitored targets and their latest sample.
class TargetsModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColStatus = 0,
        ColName,
        ColKind,
        ColLatency,
        ColJitter,
        ColLoss,
        ColLastCheck,
        ColNote,
        ColumnCount
    };

    struct Row {
        TargetSpec spec;
        ProbeSample last;
        bool hasSample = false;
        bool probing = false;
    };

    explicit TargetsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;

    void setTargets(const QList<TargetSpec> &targets);
    void upsertTarget(const TargetSpec &spec);
    void removeTarget(int targetId);
    void applySample(const ProbeSample &sample);
    void markProbing(int targetId);

    int rowForId(int targetId) const;
    const Row *rowAt(int row) const;
    QList<TargetState> targetStates() const;
    QList<TargetSpec> specs() const;

    static QColor statusColor(const Row &row);
    static QString statusText(const Row &row);

signals:
    void targetEnabledChanged(const netpulse::TargetSpec &spec);

private:
    QList<Row> rows_;
};

} // namespace netpulse
