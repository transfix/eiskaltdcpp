/*
 * Custom main for Qt-based tests.
 * Creates a QCoreApplication so that Qt models, signals, etc. work correctly.
 */

#include <QApplication>
#include <catch2/catch_session.hpp>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);  // QApplication for QWidget-based code paths
    return Catch::Session().run(argc, argv);
}
