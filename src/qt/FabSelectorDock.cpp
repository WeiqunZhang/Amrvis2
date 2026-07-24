#include "FabSelectorDock.hpp"

#include <QAbstractTableModel>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QModelIndex>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QVBoxLayout>

#include <array>
#include <utility>

namespace amrvis::qt {
namespace {

QString formatBox(const IntBox& box, int dimension)
{
    QStringList lower;
    QStringList upper;
    QStringList centering;
    for (int axis = 0; axis < dimension; ++axis) {
        const auto index = static_cast<std::size_t>(axis);
        lower.push_back(QString::number(box.lower[index]));
        upper.push_back(QString::number(box.upper[index]));
        centering.push_back(QString::number(box.centering[index]));
    }
    return QStringLiteral("((%1) (%2) (%3))")
        .arg(lower.join(QLatin1Char(',')),
            upper.join(QLatin1Char(',')),
            centering.join(QLatin1Char(',')));
}

} // namespace

class FabSelectorDock::Model final : public QAbstractTableModel {
public:
    explicit Model(QObject* parent = nullptr)
        : QAbstractTableModel(parent)
    {}

    void setEntries(std::vector<FabSelectorEntry> entries)
    {
        beginResetModel();
        m_entries = std::move(entries);
        endResetModel();
    }

    [[nodiscard]] const std::vector<FabSelectorEntry>& entries() const noexcept
    {
        return m_entries;
    }

    int rowCount(const QModelIndex& parent = {}) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
    }

    int columnCount(const QModelIndex& parent = {}) const override
    {
        return parent.isValid() ? 0 : 9;
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() < 0
            || static_cast<std::size_t>(index.row()) >= m_entries.size()) {
            return {};
        }
        const auto& entry = m_entries[static_cast<std::size_t>(index.row())];
        if (role == Qt::UserRole) {
            return static_cast<qulonglong>(entry.ordinal);
        }
        if (role != Qt::DisplayRole) {
            return {};
        }
        switch (index.column()) {
        case 0: return static_cast<qulonglong>(entry.ordinal);
        case 1: return entry.level;
        case 2: return static_cast<qulonglong>(entry.blockIndex);
        case 3: return QString::fromStdString(entry.filePath.filename().string());
        case 4: return static_cast<qulonglong>(entry.fileOffset);
        case 5: return formatBox(entry.validBox, entry.dimension);
        case 6: return formatBox(entry.storedBox, entry.dimension);
        case 7: return entry.components;
        case 8: return entry.precision;
        default: return {};
        }
    }

    QVariant headerData(
        int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
            return {};
        }
        constexpr std::array<const char*, 9> headers{
            "#", "Level", "Grid", "File", "Offset", "Valid box",
            "Stored box", "Components", "Precision"};
        if (section < 0 || static_cast<std::size_t>(section) >= headers.size()) {
            return {};
        }
        return tr(headers[static_cast<std::size_t>(section)]);
    }

private:
    std::vector<FabSelectorEntry> m_entries;
};

FabSelectorDock::FabSelectorDock(QWidget* parent)
    : QDockWidget(tr("FAB Selector"), parent)
    , m_model(new Model(this))
    , m_proxy(new QSortFilterProxyModel(this))
{
    setObjectName(QStringLiteral("fabSelectorDock"));
    auto* content = new QWidget(this);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(4, 4, 4, 4);

    m_filter = new QLineEdit(content);
    m_filter->setPlaceholderText(tr("Filter FABs..."));
    layout->addWidget(m_filter);

    m_proxy->setSourceModel(m_model);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setFilterKeyColumn(-1);
    m_proxy->setSortRole(Qt::DisplayRole);

    m_table = new QTableView(content);
    m_table->setModel(m_proxy);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setSortingEnabled(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_table);

    auto* buttons = new QHBoxLayout;
    m_view = new QPushButton(tr("View FAB"), content);
    m_back = new QPushButton(tr("Back to MultiFab"), content);
    m_view->setObjectName(QStringLiteral("fabViewButton"));
    m_back->setObjectName(QStringLiteral("fabBackButton"));
    buttons->addWidget(m_view);
    buttons->addWidget(m_back);
    buttons->addStretch(1);
    layout->addLayout(buttons);
    setWidget(content);

    connect(m_filter, &QLineEdit::textChanged,
        m_proxy, &QSortFilterProxyModel::setFilterFixedString);
    connect(m_view, &QPushButton::clicked, this,
        [this] { activateCurrent(); });
    connect(m_table, &QTableView::doubleClicked, this,
        [this](const QModelIndex&) { activateCurrent(); });
    connect(m_back, &QPushButton::clicked, this,
        &FabSelectorDock::backRequested);
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged,
        this, [this] {
            m_view->setEnabled(m_table->currentIndex().isValid());
        });
    m_view->setEnabled(false);
    m_back->setVisible(false);
}

void FabSelectorDock::setEntries(std::vector<FabSelectorEntry> entries)
{
    m_model->setEntries(std::move(entries));
    m_filter->clear();
    m_view->setEnabled(false);
    if (m_proxy->rowCount() > 0) {
        m_table->selectRow(0);
    }
}

const std::vector<FabSelectorEntry>& FabSelectorDock::entries() const noexcept
{
    return m_model->entries();
}

void FabSelectorDock::setBackAvailable(bool available)
{
    m_back->setVisible(available);
    m_back->setEnabled(available);
}

void FabSelectorDock::selectEntry(std::size_t ordinal)
{
    for (int row = 0; row < m_proxy->rowCount(); ++row) {
        const auto index = m_proxy->index(row, 0);
        if (index.data(Qt::UserRole).toULongLong() == ordinal) {
            m_table->selectRow(row);
            m_table->scrollTo(index);
            return;
        }
    }
}

void FabSelectorDock::activateCurrent()
{
    const auto proxyIndex = m_table->currentIndex();
    if (!proxyIndex.isValid()) {
        return;
    }
    const auto sourceIndex = m_proxy->mapToSource(proxyIndex);
    if (sourceIndex.row() < 0) {
        return;
    }
    emit viewRequested(static_cast<std::size_t>(sourceIndex.row()));
}

} // namespace amrvis::qt
