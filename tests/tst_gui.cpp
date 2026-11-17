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

    void testValidThenInvalidThenValidRecovery() {
        const QString officialPath = QStringLiteral(TEST_ASSETS_DIR "/anonymized_mamo.dcm");
        if (!QFileInfo::exists(officialPath)) {
            QSKIP("Arquivo oficial anonymized_mamo.dcm nao encontrado em assets/.");
        }

        dicom::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        // 1. Carrega arquivo válido
        QVERIFY(window.loadDicomFile(officialPath, false));
        QVERIFY(window.hasLoadedImage());
        QVERIFY(!window.isErrorBannerVisible());

        // 2. Tenta carregar arquivo inexistente (sem modal para teste rápido)
        QVERIFY(!window.loadDicomFile(QStringLiteral("C:/nao_existe_999.dcm"), false));
        QVERIFY(window.isErrorBannerVisible());
        QVERIFY(!window.lastErrorMessage().isEmpty());

        // 3. Carrega novamente um arquivo válido e verifica que o banner de erro sumiu
        QVERIFY(window.loadDicomFile(officialPath, false));
        QVERIFY(window.hasLoadedImage());
        QVERIFY(!window.isErrorBannerVisible());
        QVERIFY(window.lastErrorMessage().isEmpty());
    }

    void testMetadataPanelAndWindowLevelControlsInGui() {
        const QString officialPath = QStringLiteral(TEST_ASSETS_DIR "/anonymized_mamo.dcm");
        if (!QFileInfo::exists(officialPath)) {
            QSKIP("Arquivo oficial anonymized_mamo.dcm nao encontrado em assets/.");
        }

        dicom::MainWindow window;
        window.resize(1200, 820);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        QVERIFY(window.loadDicomFile(officialPath, false));
        QVERIFY(window.isMetadataPanelVisible());
        QVERIFY2(window.metadataRowCount() >= 10, "A tabela de metadados deve conter as tags do exame.");

        // Testa filtro de metadados
        auto* filterEdit = window.findChild<QLineEdit*>(QStringLiteral("metadataFilterEdit"));
        auto* metaTable = window.findChild<QTableWidget*>(QStringLiteral("metadataTable"));
        QVERIFY(filterEdit != nullptr);
        QVERIFY(metaTable != nullptr);

        filterEdit->setText(QStringLiteral("Senographe"));
        int visibleRows = 0;
        for (int r = 0; r < metaTable->rowCount(); ++r) {
            if (!metaTable->isRowHidden(r)) {
                ++visibleRows;
            }
        }
        QCOMPARE(visibleRows, 1);

        filterEdit->clear();

        // Testa alteração de Window Level / Window Width e Reset na interface
        window.setWindowLevelWidth(2500.0, 800.0);
        QCOMPARE(window.currentWindowCenter(), 2500.0);
        QCOMPARE(window.currentWindowWidth(), 800.0);

        window.resetWindowLevelWidth();
        QVERIFY(window.currentWindowWidth() != 800.0 || window.currentWindowCenter() != 2500.0);
    }

    void testClinicalToolsInvertRotateFlipAndMeasurement() {
        const QString officialPath = QStringLiteral(TEST_ASSETS_DIR "/anonymized_mamo.dcm");
        if (!QFileInfo::exists(officialPath)) {
            QSKIP("Arquivo oficial anonymized_mamo.dcm nao encontrado em assets/.");
        }

        dicom::MainWindow window;
        window.resize(1200, 820);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.loadDicomFile(officialPath, false));

        // 1. Inverter contraste (Negativo)
        const int beforeGray = qGray(window.displayedImage().pixel(2000, 1400));
        window.onInvertToggled(true);
        QVERIFY(window.isInverted());
        const int afterInvertGray = qGray(window.displayedImage().pixel(2000, 1400));
        QCOMPARE(afterInvertGray, 255 - beforeGray);
        window.onInvertToggled(false);

        // 2. Rotacionar 90 graus (2394x2850 -> 2850x2394)
        window.onRotate90Triggered();
        QCOMPARE(window.displayedImageSize(), QSize(2850, 2394));

        // 3. Espelhar horizontalmente
        window.onFlipHorizontalTriggered();
        QCOMPARE(window.displayedImageSize(), QSize(2850, 2394));

        // 4. Régua clínica em mm (anonymized_mamo.dcm tem ImagerPixelSpacing = 0.1 mm)
        // 100 pixels na horizontal * 0.1 mm/pixel = 10.0 mm
        const double distMm = window.measureDistanceMm(QPointF(100.0, 200.0), QPointF(200.0, 200.0));
        QVERIFY(qAbs(distMm - 10.0) < 0.01);
        QCOMPARE(window.measurementCount(), 2); // linha + rótulo

        // 5. ROI mapeada corretamente mesmo após rotação 90° + espelhamento
        const auto tissueRoi = window.measureRoiStats(QRectF(1420.0, 2120.0, 40.0, 40.0));
        QVERIFY(tissueRoi.valid);
        QVERIFY2(tissueRoi.mean > 1500.0, "ROI sobre tecido apos rotacao/espelhamento deve refletir sinal alto.");

        const auto bgRoi = window.measureRoiStats(QRectF(1420.0, 200.0, 40.0, 40.0));
        QVERIFY(bgRoi.valid);
        // Em mamografia GE (PixelPaddingRangeLimit=1060), o fundo calibrado fica <= 1060 enquanto o tecido fica > 1500
        QVERIFY2(bgRoi.mean <= 1060.0 && bgRoi.mean < tissueRoi.mean,
                 "ROI sobre fundo apos rotacao/espelhamento deve ser significativamente menor que sobre o tecido.");

        window.onClearMeasurementsTriggered();
        QCOMPARE(window.measurementCount(), 0);
    }
};

QTEST_MAIN(GuiTest)
#include "tst_gui.moc"
