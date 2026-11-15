#include <QtTest>
#include <QApplication>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTimer>

#include "ui/MainWindow.h"

class GuiTest : public QObject {
    Q_OBJECT

private slots:
    void testInitialUiState() {
        dicom::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        QVERIFY(!window.hasLoadedImage());
        QVERIFY(!window.isErrorBannerVisible());
        QVERIFY(window.lastErrorMessage().isEmpty());

        auto* view = window.findChild<QGraphicsView*>(QStringLiteral("dicomGraphicsView"));
        QVERIFY2(view != nullptr, "A interface deve conter a area de visualizacao QGraphicsView.");
        QVERIFY(view->isVisible());
    }

    void testInvalidFileErrorDisplayAndModalDialog() {
        dicom::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QString invalidPath = tempDir.filePath(QStringLiteral("invalido.dcm"));
        {
            QFile f(invalidPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("ARQUIVO INVALIDO PARA TESTE DE GUI");
            f.close();
        }

        // Fecha automaticamente o QMessageBox modal disparado pelo erro
        bool modalDialogShown = false;
        QTimer::singleShot(150, [&modalDialogShown]() {
            if (QWidget* modal = QApplication::activeModalWidget()) {
                if (auto* msgBox = qobject_cast<QMessageBox*>(modal)) {
                    modalDialogShown = true;
                    msgBox->accept();
                }
            }
        });

        const bool ok = window.loadDicomFile(invalidPath, true);
        QVERIFY(!ok);
        QVERIFY(modalDialogShown);
        QVERIFY(!window.hasLoadedImage());
        QVERIFY(window.isErrorBannerVisible());
        QVERIFY(!window.lastErrorMessage().isEmpty());
    }

    void testLoadOfficialDicomInGuiAndZoomControls() {
        const QString officialPath = QStringLiteral(TEST_ASSETS_DIR "/anonymized_mamo.dcm");
        if (!QFileInfo::exists(officialPath)) {
            QSKIP("Arquivo oficial anonymized_mamo.dcm nao encontrado em assets/.");
        }

        dicom::MainWindow window;
        window.resize(1100, 850);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        const bool ok = window.loadDicomFile(officialPath, false);
        QVERIFY2(ok, qPrintable(window.lastErrorMessage()));
        QVERIFY(window.hasLoadedImage());
        QVERIFY(!window.isErrorBannerVisible());
        QCOMPARE(window.displayedImageSize(), QSize(2394, 2850));

        const QString summary = window.statusSummaryText();
        QVERIFY(summary.contains(QStringLiteral("2394 x 2850")));
        QVERIFY(summary.contains(QStringLiteral("MONOCHROME2")));
        QVERIFY(summary.contains(QStringLiteral("MG")));

        // Testa operações de zoom na área de visualização
        auto* view = window.findChild<QGraphicsView*>(QStringLiteral("dicomGraphicsView"));
        QVERIFY(view != nullptr);

        window.onResetZoom();
        QCOMPARE(view->transform().m11(), 1.0);

        window.onZoomIn();
        QVERIFY(view->transform().m11() > 1.0);

        window.onZoomOut();
        window.onFitToWindow();
        QVERIFY(view->transform().m11() < 1.0); // 2850px ajustado dentro de ~850px
    }
};

QTEST_MAIN(GuiTest)
#include "tst_gui.moc"
