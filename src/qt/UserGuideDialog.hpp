#pragma once

#include <QDialog>

namespace amrvis::qt {

class UserGuideDialog final : public QDialog {
public:
    explicit UserGuideDialog(QWidget* parent = nullptr);
};

} // namespace amrvis::qt
