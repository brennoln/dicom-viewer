#include <QtTest>
#include <QDir>
#include <QFuture>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtConcurrent/QtConcurrentRun>

#include "core/DicomLoader.h"
#include "ui/MainWindow.h"

#include <dcmtk/dcmdata/dcdatset.h>
#include <dcmtk/dcmdata/dcdeftag.h>
#include <dcmtk/dcmdata/dcfilefo.h>
#include <dcmtk/dcmdata/dcuid.h>
#include <cmath>
#include <memory>
#include <vector>

class CppRecruiterTest : public QObject {
    Q_OBJECT

private:
    static bool saveDataset(DcmFileFormat& ff, const QString& path) {
        const QByteArray local = QDir::toNativeSeparators(path).toLocal8Bit();
        return ff.saveFile(OFFilename(local.constData()), EXS_LittleEndianExplicit).good();
    }

    static QString createMultiFrameDicom(const QTemporaryDir& dir, const QString& name, int frames) {
        const Uint16 w = 16;
        const Uint16 h = 16;
        const size_t framePixels = static_cast<size_t>(w) * h;
        std::vector<Uint8> pixels(framePixels * static_cast<size_t>(frames));
        for (int f = 0; f < frames; ++f) {
            const Uint8 fillVal = static_cast<Uint8>(20 + f * 40);
            std::fill(pixels.begin() + static_cast<size_t>(f) * framePixels,
                      pixels.begin() + static_cast<size_t>(f + 1) * framePixels,
                      fillVal);
        }

        DcmFileFormat ff;
        DcmDataset* ds = ff.getDataset();
        ds->putAndInsertString(DCM_SOPClassUID, UID_SecondaryCaptureImageStorage);
        ds->putAndInsertString(DCM_SOPInstanceUID, "1.2.826.0.1.3680043.2.1125.7.1");
        ds->putAndInsertString(DCM_Modality, "US");
        ds->putAndInsertString(DCM_NumberOfFrames, QByteArray::number(frames).constData());
        ds->putAndInsertString(DCM_PixelSpacing, "0.5\\0.5");
        ds->putAndInsertUint16(DCM_SamplesPerPixel, 1);
        ds->putAndInsertString(DCM_PhotometricInterpretation, "MONOCHROME2");
        ds->putAndInsertUint16(DCM_Rows, h);
        ds->putAndInsertUint16(DCM_Columns, w);
        ds->putAndInsertUint16(DCM_BitsAllocated, 8);
        ds->putAndInsertUint16(DCM_BitsStored, 8);
        ds->putAndInsertUint16(DCM_HighBit, 7);
        ds->putAndInsertUint16(DCM_PixelRepresentation, 0);
        ds->putAndInsertUint8Array(DCM_PixelData, pixels.data(), static_cast<unsigned long>(pixels.size()));

        const QString path = dir.filePath(name);
        return saveDataset(ff, path) ? path : QString();
    }

private slots:
    void testRaiiAndMoveSemanticsLifetime() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QString path = createMultiFrameDicom(tempDir, QStringLiteral("raii.dcm"), 2);
        QVERIFY(!path.isEmpty());

        dicom::DicomImageData movedData;
        {
            auto scopedLoader = std::make_unique<dicom::DicomLoader>();
            dicom::DicomImageData temp = scopedLoader->loadFile(path);
            QVERIFY(temp.isValid);
            movedData = std::move(temp);
        } // scopedLoader é destruído aqui; renderContext deve permanecer vivo via shared_ptr

        QVERIFY(movedData.isValid);
        double val = 0.0;
        QVERIFY(dicom::DicomLoader::samplePixelValue(movedData, 4, 4, val));
        QCOMPARE(val, 20.0);

        dicom::DicomLoader secondLoader;
        const auto frame1 = secondLoader.loadFrame(movedData, 1);
        QVERIFY(frame1.isValid);
        QVERIFY(dicom::DicomLoader::samplePixelValue(frame1, 4, 4, val));
        QCOMPARE(val, 60.0);
    }

    void testThreadSafetyConcurrentLoads() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QString path = createMultiFrameDicom(tempDir, QStringLiteral("concurrent.dcm"), 4);
        QVERIFY(!path.isEmpty());

        QList<QFuture<bool>> futures;
        for (int t = 0; t < 8; ++t) {
            futures.append(QtConcurrent::run([path, t]() {
                dicom::DicomLoader localLoader;
                const int targetFrame = t % 4;
                const auto data = localLoader.loadFile(path, targetFrame);
                if (!data.isValid || data.frameCount != 4 || data.currentFrame != targetFrame) {
                    return false;
                }
                double sampled = 0.0;
                if (!dicom::DicomLoader::samplePixelValue(data, 5, 5, sampled)) {
                    return false;
                }
                const double expected = 20.0 + targetFrame * 40.0;
                if (std::abs(sampled - expected) > 1e-3) {
                    return false;
                }
                const auto windowed = localLoader.applyWindowLevel(data, 100.0, 120.0);
                return windowed.isValid && !windowed.image.isNull();
            }));
        }

        for (auto& f : futures) {
            f.waitForFinished();
            QVERIFY2(f.result(), "Falha de concorrencia/thread-safety em carregamento paralelo.");
        }
    }

    void testMultiFrameNavigationAndBounds() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QString path = createMultiFrameDicom(tempDir, QStringLiteral("multiframe5.dcm"), 5);
        QVERIFY(!path.isEmpty());

        dicom::DicomLoader loader;
        const auto base = loader.loadFile(path, 0);
        QVERIFY2(base.isValid, qPrintable(base.errorMessage));
        QCOMPARE(base.frameCount, 5);
        QCOMPARE(base.currentFrame, 0);

        for (int f = 0; f < 5; ++f) {
            const auto frameData = loader.loadFrame(base, f);
            QVERIFY2(frameData.isValid, qPrintable(frameData.errorMessage));
            QCOMPARE(frameData.currentFrame, f);
            double sampled = 0.0;
            QVERIFY(dicom::DicomLoader::samplePixelValue(frameData, 0, 0, sampled));
            QCOMPARE(sampled, 20.0 + f * 40.0);
        }

        // Fora dos limites deve retornar erro limpo
        QVERIFY(!loader.loadFrame(base, -1).isValid);
        QVERIFY(!loader.loadFrame(base, 5).isValid);
        QVERIFY(!loader.loadFile(path, 10).isValid);
    }

    void testQuantitativeRoiStatistics() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        // Matriz 20x20: colunas 0..9 = 100, colunas 10..19 = 200; PixelSpacing = 0.5 x 0.5 mm
        const Uint16 w = 20;
        const Uint16 h = 20;
        std::vector<Uint16> pixels(static_cast<size_t>(w) * h);
        for (Uint16 y = 0; y < h; ++y) {
            for (Uint16 x = 0; x < w; ++x) {
                pixels[static_cast<size_t>(y) * w + x] = (x < 10) ? 100 : 200;
            }
        }

        DcmFileFormat ff;
        DcmDataset* ds = ff.getDataset();
        ds->putAndInsertString(DCM_SOPClassUID, UID_CTImageStorage);
        ds->putAndInsertString(DCM_SOPInstanceUID, "1.2.826.0.1.3680043.2.1125.7.2");
        ds->putAndInsertString(DCM_Modality, "CT");
        ds->putAndInsertString(DCM_PixelSpacing, "0.5\\0.5");
        ds->putAndInsertUint16(DCM_SamplesPerPixel, 1);
        ds->putAndInsertString(DCM_PhotometricInterpretation, "MONOCHROME2");
        ds->putAndInsertUint16(DCM_Rows, h);
        ds->putAndInsertUint16(DCM_Columns, w);
        ds->putAndInsertUint16(DCM_BitsAllocated, 16);
        ds->putAndInsertUint16(DCM_BitsStored, 12);
        ds->putAndInsertUint16(DCM_HighBit, 11);
        ds->putAndInsertUint16(DCM_PixelRepresentation, 0);
        ds->putAndInsertUint16Array(DCM_PixelData, pixels.data(), static_cast<unsigned long>(pixels.size()));

        const QString path = tempDir.filePath(QStringLiteral("roi_stats.dcm"));
        QVERIFY(saveDataset(ff, path));

        dicom::DicomLoader loader;
        const auto data = loader.loadFile(path);
        QVERIFY(data.isValid);

        // ROI cobrindo colunas 5..14 (10 cols: 5 com valor 100 e 5 com valor 200) x linhas 0..9 (10 linhas) = 100 pixels
        const auto stats = dicom::DicomLoader::computeRoiStatistics(data, QRect(QPoint(5, 0), QPoint(14, 9)));
        QVERIFY(stats.valid);
        QCOMPARE(stats.pixelCount, 100);
        QCOMPARE(stats.mean, 150.0);
        QCOMPARE(stats.stdDev, 50.0);
        QCOMPARE(stats.minVal, 100.0);
        QCOMPARE(stats.maxVal, 200.0);
        QCOMPARE(stats.areaMm2, 25.0); // 100 pixels * (0.5 * 0.5 mm^2)
    }

    void testGuiAsyncLoadingMultiFramePresetsAndRoi() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QString path = createMultiFrameDicom(tempDir, QStringLiteral("gui_multiframe.dcm"), 4);
        QVERIFY(!path.isEmpty());

        dicom::MainWindow window;
        window.resize(1200, 820);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        // 1. Carregamento assíncrono em thread separada sem travar a UI
        QSignalSpy spy(&window, &dicom::MainWindow::asyncLoadCompleted);
        window.loadDicomFileAsync(path, false);
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toBool(), true);

        // 2. Verifica barra multi-frame e navegação
        QVERIFY(window.hasLoadedImage());
        QCOMPARE(window.totalFrameCount(), 4);
        QVERIFY(window.isFrameBarVisible());
        QVERIFY(window.setCurrentFrame(2));
        QCOMPARE(window.currentFrameIndex(), 2);

        // 3. Presets clínicos (Pulmão e Osso)
        window.applyClinicalWindowPreset(1); // Pulmão: -600 / 1500
        QCOMPARE(window.currentWindowCenter(), -600.0);
        QCOMPARE(window.currentWindowWidth(), 1500.0);

        window.applyClinicalWindowPreset(2); // Osso: 300 / 1500
        QCOMPARE(window.currentWindowCenter(), 300.0);
        QCOMPARE(window.currentWindowWidth(), 1500.0);

        // 4. ROI na interface gráfica
        const auto roi = window.measureRoiStats(QRectF(2.0, 2.0, 6.0, 6.0));
        QVERIFY(roi.valid);
        QCOMPARE(roi.mean, 100.0); // frame 2 = 20 + 2*40 = 100
        QCOMPARE(window.measurementCount(), 2); // retângulo + label
    }

    void testWindowLevelPerformanceBenchmark() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QString path = createMultiFrameDicom(tempDir, QStringLiteral("bench.dcm"), 1);
        QVERIFY(!path.isEmpty());

        dicom::DicomLoader loader;
        const auto data = loader.loadFile(path);
        QVERIFY(data.isValid);

        double wc = 100.0;
        QBENCHMARK {
            const auto updated = loader.applyWindowLevel(data, wc, 200.0);
            QVERIFY(updated.isValid);
            wc += 1.0;
        }
    }
};

QTEST_MAIN(CppRecruiterTest)
#include "tst_cpp_recruiter.moc"
