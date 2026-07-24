#pragma once

#include <amrvis/core/Geometry.hpp>

#include <QDockWidget>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

class QLineEdit;
class QPushButton;
class QSortFilterProxyModel;
class QTableView;

namespace amrvis::qt {

struct FabSelectorEntry {
    std::size_t ordinal = 0;
    int level = 0;
    std::size_t blockIndex = 0;
    std::filesystem::path filePath;
    std::uint64_t fileOffset = 0;
    IntBox validBox;
    IntBox storedBox;
    int dimension = 0;
    int components = 0;
    QString precision;
    bool rawRecord = false;
};

class FabSelectorDock final : public QDockWidget {
    Q_OBJECT

public:
    explicit FabSelectorDock(QWidget* parent = nullptr);

    void setEntries(std::vector<FabSelectorEntry> entries);
    [[nodiscard]] const std::vector<FabSelectorEntry>& entries() const noexcept;
    void setBackAvailable(bool available);
    void selectEntry(std::size_t ordinal);

signals:
    void viewRequested(std::size_t entry);
    void backRequested();

private:
    void activateCurrent();

    class Model;
    Model* m_model = nullptr;
    QSortFilterProxyModel* m_proxy = nullptr;
    QLineEdit* m_filter = nullptr;
    QTableView* m_table = nullptr;
    QPushButton* m_view = nullptr;
    QPushButton* m_back = nullptr;
};

} // namespace amrvis::qt
