#include "ui/TargetsModel.h"

#include <QDateTime>
#include <QFont>

namespace netpulse {
namespace {

QString fmtMs(double v)
{
    if (v < 0)
        return QStringLiteral("—");
    return QStringLiteral("%1 ms").arg(QString::number(v, 'f', v < 10 ? 1 : 0));
}

QString fmtPct(double v)
{
    if (v < 0)
        return QStringLiteral("—");
    return QStringLiteral("%1 %").arg(QString::number(v, 'f', 0));
}

} // namespace

TargetsModel::TargetsModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int TargetsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

int TargetsModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QColor TargetsModel::statusColor(const Row &row)
{
    if (!row.spec.enabled)
        return QColor(0x8e, 0x8e, 0x93);
    if (!row.hasSample)
        return QColor(0x8e, 0x8e, 0x93);
    switch (row.last.status) {
    case SampleStatus::Ok:       return QColor(0x30, 0xa4, 0x6c);
    case SampleStatus::Degraded: return QColor(0xd6, 0x9e, 0x2e);
    case SampleStatus::Failed:   return QColor(0xd6, 0x45, 0x45);
    }
    return QColor(0x8e, 0x8e, 0x93);
}

QString TargetsModel::statusText(const Row &row)
{
    if (!row.spec.enabled)
        return QStringLiteral("Paused");
    if (!row.hasSample)
        return row.probing ? QStringLiteral("Probing…") : QStringLiteral("Waiting…");
    switch (row.last.status) {
    case SampleStatus::Ok:       return QStringLiteral("Up");
    case SampleStatus::Degraded: return QStringLiteral("Degraded");
    case SampleStatus::Failed:   return QStringLiteral("Down");
    }
    return {};
}

QVariant TargetsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= rows_.size())
        return {};
    const Row &row = rows_[index.row()];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColStatus:
            return QStringLiteral("● %1").arg(statusText(row));
        case ColName:
            return row.spec.name;
        case ColKind:
            return probeKindLabel(row.spec.kind);
        case ColLatency:
            return row.hasSample ? fmtMs(row.last.latencyMs) : QStringLiteral("—");
        case ColJitter:
            return (row.hasSample && row.spec.kind == ProbeKind::Ping)
                ? fmtMs(row.last.jitterMs) : QStringLiteral("—");
        case ColLoss:
            return (row.hasSample && row.spec.kind == ProbeKind::Ping)
                ? fmtPct(row.last.lossPct) : QStringLiteral("—");
        case ColLastCheck:
            return row.hasSample
                ? QDateTime::fromMSecsSinceEpoch(row.last.timestampMs).toString(
                      QStringLiteral("hh:mm:ss"))
                : QStringLiteral("—");
        case ColNote:
            if (!row.hasSample)
                return QString();
            if (!row.last.error.isEmpty())
                return row.last.error;
            if (row.spec.kind == ProbeKind::Https && row.last.httpStatus > 0)
                return QStringLiteral("HTTP %1").arg(row.last.httpStatus);
            if (row.spec.kind == ProbeKind::Dns && !row.last.resolvedAddress.isEmpty())
                return QStringLiteral("→ %1").arg(row.last.resolvedAddress);
            return QString();
        }
    }

    if (role == Qt::ForegroundRole) {
        if (index.column() == ColStatus)
            return statusColor(row);
        if (index.column() == ColNote)
            return QColor(0x8e, 0x8e, 0x93);
    }

    if (role == Qt::FontRole && index.column() == ColStatus) {
        QFont f;
        f.setBold(true);
        return f;
    }

    if (role == Qt::CheckStateRole && index.column() == ColName)
        return row.spec.enabled ? Qt::Checked : Qt::Unchecked;

    if (role == Qt::ToolTipRole)
        return QStringLiteral("%1 — every %2 s")
            .arg(row.spec.describe()).arg(row.spec.intervalSecs);

    return {};
}

QVariant TargetsModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case ColStatus:    return QStringLiteral("Status");
    case ColName:      return QStringLiteral("Name");
    case ColKind:      return QStringLiteral("Probe");
    case ColLatency:   return QStringLiteral("Latency");
    case ColJitter:    return QStringLiteral("Jitter");
    case ColLoss:      return QStringLiteral("Loss");
    case ColLastCheck: return QStringLiteral("Last check");
    case ColNote:      return QStringLiteral("Note");
    }
    return {};
}

Qt::ItemFlags TargetsModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (index.column() == ColName)
        f |= Qt::ItemIsUserCheckable;
    return f;
}

bool TargetsModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::CheckStateRole || index.column() != ColName
        || index.row() >= rows_.size())
        return false;
    Row &row = rows_[index.row()];
    row.spec.enabled = value.toInt() == Qt::Checked;
    emit dataChanged(this->index(index.row(), 0),
                     this->index(index.row(), ColumnCount - 1));
    emit targetEnabledChanged(row.spec);
    return true;
}

void TargetsModel::setTargets(const QList<TargetSpec> &targets)
{
    beginResetModel();
    QHash<int, Row> old;
    for (const Row &r : std::as_const(rows_))
        old.insert(r.spec.id, r);
    rows_.clear();
    for (const TargetSpec &t : targets) {
        Row r;
        if (old.contains(t.id))
            r = old.value(t.id);
        r.spec = t;
        rows_ << r;
    }
    endResetModel();
}

void TargetsModel::upsertTarget(const TargetSpec &spec)
{
    const int row = rowForId(spec.id);
    if (row >= 0) {
        rows_[row].spec = spec;
        emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
        return;
    }
    beginInsertRows({}, static_cast<int>(rows_.size()), static_cast<int>(rows_.size()));
    Row r;
    r.spec = spec;
    rows_ << r;
    endInsertRows();
}

void TargetsModel::removeTarget(int targetId)
{
    const int row = rowForId(targetId);
    if (row < 0)
        return;
    beginRemoveRows({}, row, row);
    rows_.removeAt(row);
    endRemoveRows();
}

void TargetsModel::applySample(const ProbeSample &sample)
{
    const int row = rowForId(sample.targetId);
    if (row < 0)
        return;
    rows_[row].last = sample;
    rows_[row].hasSample = true;
    rows_[row].probing = false;
    emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
}

void TargetsModel::markProbing(int targetId)
{
    const int row = rowForId(targetId);
    if (row < 0)
        return;
    rows_[row].probing = true;
    emit dataChanged(index(row, ColStatus), index(row, ColStatus));
}

int TargetsModel::rowForId(int targetId) const
{
    for (qsizetype i = 0; i < rows_.size(); ++i)
        if (rows_[i].spec.id == targetId)
            return static_cast<int>(i);
    return -1;
}

const TargetsModel::Row *TargetsModel::rowAt(int row) const
{
    if (row < 0 || row >= rows_.size())
        return nullptr;
    return &rows_[row];
}

QList<TargetState> TargetsModel::targetStates() const
{
    QList<TargetState> states;
    states.reserve(rows_.size());
    for (const Row &r : rows_) {
        TargetState st;
        st.spec = r.spec;
        st.last = r.last;
        st.hasSample = r.hasSample;
        states << st;
    }
    return states;
}

QList<TargetSpec> TargetsModel::specs() const
{
    QList<TargetSpec> out;
    out.reserve(rows_.size());
    for (const Row &r : rows_)
        out << r.spec;
    return out;
}

} // namespace netpulse
