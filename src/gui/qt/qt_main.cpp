// qt_main.cpp - Qt GUI 入口
#include <QApplication>

#include "gui/qt/MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("BackupSystem");

    MainWindow window;
    window.show();
    return QApplication::exec();
}
