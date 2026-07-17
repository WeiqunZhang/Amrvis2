#include "MainWindow.hpp"

#include <QApplication>
#include <QTimer>

#include <filesystem>
#include <string_view>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    amrvis::qt::MainWindow window;
    window.show();
    if (argc == 3 && std::string_view(argv[1]) == "--smoke-test") {
        const std::filesystem::path path(argv[2]);
        QObject::connect(&window, &amrvis::qt::MainWindow::datasetOpenFinished,
            &application, [&application](bool success) {
                application.exit(success ? 0 : 1);
            });
        QTimer::singleShot(0, &window,
            [&window, path] { window.openDataset(path, true); });
    } else if (argc == 3 && std::string_view(argv[1]) == "--slice-smoke-test") {
        const std::filesystem::path path(argv[2]);
        QObject::connect(&window, &amrvis::qt::MainWindow::initialSliceFinished,
            &application, [&application](bool success) {
                application.exit(success ? 0 : 1);
            });
        QTimer::singleShot(0, &window, [&window, path] { window.openDataset(path); });
    } else if (argc == 2) {
        const std::filesystem::path path(argv[1]);
        QTimer::singleShot(0, &window, [&window, path] { window.openDataset(path); });
    }
    return application.exec();
}
