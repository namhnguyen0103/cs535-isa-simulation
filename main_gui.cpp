#include <QApplication>
#include "mainwindow.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setStyle("Fusion");   // consistent cross-platform look

    MainWindow window;
    window.show();

    return app.exec();
}
