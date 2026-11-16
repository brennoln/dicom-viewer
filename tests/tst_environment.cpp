#include <QtTest>
#include "core/DicomLoader.h"
#include "ui/MainWindow.h"

class EnvironmentTest : public QObject {
    Q_OBJECT

private slots:
    void testCpp17Standard() {
        QVERIFY2(__cplusplus >= 201703L, "O compilador deve estar configurado com C++17 ou superior.");
    }

    void testDcmtkDictionaryLoaded() {
        QVERIFY2(dicom::DicomLoader::isDictionaryLoaded(),
                 "O dicionario padrao do DCMTK deve estar carregado na inicializacao.");
    }

    void testMainWindowInstantiation() {
        dicom::MainWindow window;
        QCOMPARE(window.windowTitle(), QStringLiteral("Visualizador DICOM Clínico - Qt"));
        QVERIFY(window.width() >= 800);
        QVERIFY(window.height() >= 600);
    }
};

QTEST_MAIN(EnvironmentTest)
#include "tst_environment.moc"
