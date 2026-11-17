#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "core/DicomLoader.h"

#include <dcmtk/dcmdata/dcdatset.h>
#include <dcmtk/dcmdata/dcdeftag.h>
#include <dcmtk/dcmdata/dcfilefo.h>
#include <dcmtk/dcmdata/dcuid.h>
#include <dcmtk/dcmdata/dcxfer.h>
#include <algorithm>
#include <vector>

class DicomEdgeCasesTest : public QObject {
    Q_OBJECT

private:
    static bool saveDataset(DcmFileFormat& fileFormat,
                            const QString& filePath,
                            E_TransferSyntax xfer = EXS_LittleEndianExplicit) {
        const QByteArray localPath = QDir::toNativeSeparators(filePath).toLocal8Bit();
        return fileFormat.saveFile(OFFilename(localPath.constData()), xfer).good();
    }

private slots:
    void testSigned16BitWithRescaleAndWindow() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        const Uint16 width = 32;
        const Uint16 height = 32;
        // Valores signed 16-bit: metade esquerda = -1000 (ar), metade direita = +1000 (osso)
        std::vector<Sint16> pixels(static_cast<size_t>(width) * height);
        for (Uint16 y = 0; y < height; ++y) {
            for (Uint16 x = 0; x < width; ++x) {
                pixels[static_cast<size_t>(y) * width + x] = (x < width / 2) ? -1000 : 1000;
            }
        }

        DcmFileFormat ff;
        DcmDataset* ds = ff.getDataset();
        ds->putAndInsertString(DCM_SOPClassUID, UID_CTImageStorage);
        ds->putAndInsertString(DCM_SOPInstanceUID, "1.2.826.0.1.3680043.2.1125.2.1");
        ds->putAndInsertString(DCM_Modality, "CT");
        ds->putAndInsertUint16(DCM_SamplesPerPixel, 1);
        ds->putAndInsertString(DCM_PhotometricInterpretation, "MONOCHROME2");
        ds->putAndInsertUint16(DCM_Rows, height);
        ds->putAndInsertUint16(DCM_Columns, width);
        ds->putAndInsertUint16(DCM_BitsAllocated, 16);
        ds->putAndInsertUint16(DCM_BitsStored, 16);
        ds->putAndInsertUint16(DCM_HighBit, 15);
        ds->putAndInsertUint16(DCM_PixelRepresentation, 1); // Signed
        ds->putAndInsertString(DCM_RescaleIntercept, "0");
        ds->putAndInsertString(DCM_RescaleSlope, "1");
        ds->putAndInsertString(DCM_WindowCenter, "40");
        ds->putAndInsertString(DCM_WindowWidth, "400");
        ds->putAndInsertUint16Array(DCM_PixelData,
                                    reinterpret_cast<const Uint16*>(pixels.data()),
                                    static_cast<unsigned long>(pixels.size()));

        const QString path = tempDir.filePath(QStringLiteral("ct_signed16.dcm"));
        QVERIFY(saveDataset(ff, path));

        dicom::DicomLoader loader;
        const auto res = loader.loadFile(path);
        QVERIFY2(res.isValid, qPrintable(res.errorMessage));
        QVERIFY(res.isSigned);
        QCOMPARE(res.modality, QStringLiteral("CT"));
        QCOMPARE(res.width, 32);
        QCOMPARE(res.height, 32);

        // -1000 HU (abaixo da janela) deve renderizar preto (0); +1000 HU (acima da janela) deve renderizar branco (255)
        QCOMPARE(qGray(res.image.pixel(2, 16)), 0);
        QCOMPARE(qGray(res.image.pixel(28, 16)), 255);
    }

    void testInvalidWindowWidthFallsBackToMinMax() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        const Uint16 width = 16;
        const Uint16 height = 16;
        std::vector<Uint16> pixels(static_cast<size_t>(width) * height);
        for (Uint16 y = 0; y < height; ++y) {
            for (Uint16 x = 0; x < width; ++x) {
                pixels[static_cast<size_t>(y) * width + x] = (x < 8) ? 100 : 2000;
            }
        }

        DcmFileFormat ff;
        DcmDataset* ds = ff.getDataset();
        ds->putAndInsertString(DCM_SOPClassUID, UID_SecondaryCaptureImageStorage);
        ds->putAndInsertString(DCM_SOPInstanceUID, "1.2.826.0.1.3680043.2.1125.2.2");
        ds->putAndInsertUint16(DCM_SamplesPerPixel, 1);
        ds->putAndInsertString(DCM_PhotometricInterpretation, "MONOCHROME2");
        ds->putAndInsertUint16(DCM_Rows, height);
        ds->putAndInsertUint16(DCM_Columns, width);
        ds->putAndInsertUint16(DCM_BitsAllocated, 16);
        ds->putAndInsertUint16(DCM_BitsStored, 12);
        ds->putAndInsertUint16(DCM_HighBit, 11);
        ds->putAndInsertUint16(DCM_PixelRepresentation, 0);
        // WindowWidth inválido (0): sem fallback seguro, a imagem falharia ou ficaria sem contraste
        ds->putAndInsertString(DCM_WindowCenter, "500");
        ds->putAndInsertString(DCM_WindowWidth, "0");
        ds->putAndInsertUint16Array(DCM_PixelData, pixels.data(), static_cast<unsigned long>(pixels.size()));

        const QString path = tempDir.filePath(QStringLiteral("invalid_ww.dcm"));
        QVERIFY(saveDataset(ff, path));

        dicom::DicomLoader loader;
        const auto res = loader.loadFile(path);
        QVERIFY2(res.isValid, qPrintable(res.errorMessage));
        QVERIFY(qGray(res.image.pixel(2, 8)) < qGray(res.image.pixel(12, 8)));
    }

    void testRgbColorAndPlanarConfigurations() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        const Uint16 width = 3;
        const Uint16 height = 2;

        // PlanarConfiguration = 0 (RGB interleaved: pixel0=Red, pixel1=Green, pixel2=Blue)
        std::vector<Uint8> interleaved = {
            255, 0, 0,   0, 255, 0,   0, 0, 255,
            255, 0, 0,   0, 255, 0,   0, 0, 255
        };

        DcmFileFormat ff0;
        DcmDataset* ds0 = ff0.getDataset();
        ds0->putAndInsertString(DCM_SOPClassUID, UID_SecondaryCaptureImageStorage);
        ds0->putAndInsertString(DCM_SOPInstanceUID, "1.2.826.0.1.3680043.2.1125.2.3");
        ds0->putAndInsertUint16(DCM_SamplesPerPixel, 3);
        ds0->putAndInsertString(DCM_PhotometricInterpretation, "RGB");
        ds0->putAndInsertUint16(DCM_PlanarConfiguration, 0);
        ds0->putAndInsertUint16(DCM_Rows, height);
        ds0->putAndInsertUint16(DCM_Columns, width);
        ds0->putAndInsertUint16(DCM_BitsAllocated, 8);
        ds0->putAndInsertUint16(DCM_BitsStored, 8);
        ds0->putAndInsertUint16(DCM_HighBit, 7);
        ds0->putAndInsertUint16(DCM_PixelRepresentation, 0);
        ds0->putAndInsertUint8Array(DCM_PixelData, interleaved.data(), static_cast<unsigned long>(interleaved.size()));

        const QString path0 = tempDir.filePath(QStringLiteral("rgb_interleaved.dcm"));
        QVERIFY(saveDataset(ff0, path0));

        dicom::DicomLoader loader;
        const auto res0 = loader.loadFile(path0);
        QVERIFY2(res0.isValid, qPrintable(res0.errorMessage));
        QCOMPARE(res0.image.format(), QImage::Format_RGB888);
        QCOMPARE(res0.image.pixelColor(0, 0), QColor(255, 0, 0));
        QCOMPARE(res0.image.pixelColor(1, 0), QColor(0, 255, 0));
        QCOMPARE(res0.image.pixelColor(2, 0), QColor(0, 0, 255));

        // PlanarConfiguration = 1 (RRR... GGG... BBB...)
        std::vector<Uint8> planar = {
            255, 0, 0, 255, 0, 0, // R
            0, 255, 0, 0, 255, 0, // G
            0, 0, 255, 0, 0, 255  // B
        };
        DcmFileFormat ff1;
        DcmDataset* ds1 = ff1.getDataset();
        ds1->putAndInsertString(DCM_SOPClassUID, UID_SecondaryCaptureImageStorage);
        ds1->putAndInsertString(DCM_SOPInstanceUID, "1.2.826.0.1.3680043.2.1125.2.4");
        ds1->putAndInsertUint16(DCM_SamplesPerPixel, 3);
        ds1->putAndInsertString(DCM_PhotometricInterpretation, "RGB");
        ds1->putAndInsertUint16(DCM_PlanarConfiguration, 1);
        ds1->putAndInsertUint16(DCM_Rows, height);
        ds1->putAndInsertUint16(DCM_Columns, width);
        ds1->putAndInsertUint16(DCM_BitsAllocated, 8);
        ds1->putAndInsertUint16(DCM_BitsStored, 8);
        ds1->putAndInsertUint16(DCM_HighBit, 7);
        ds1->putAndInsertUint16(DCM_PixelRepresentation, 0);
        ds1->putAndInsertUint8Array(DCM_PixelData, planar.data(), static_cast<unsigned long>(planar.size()));

        const QString path1 = tempDir.filePath(QStringLiteral("rgb_planar.dcm"));
        QVERIFY(saveDataset(ff1, path1));

        const auto res1 = loader.loadFile(path1);
        QVERIFY2(res1.isValid, qPrintable(res1.errorMessage));
        QCOMPARE(res1.image.pixelColor(0, 0), QColor(255, 0, 0));
        QCOMPARE(res1.image.pixelColor(1, 0), QColor(0, 255, 0));
        QCOMPARE(res1.image.pixelColor(2, 0), QColor(0, 0, 255));
    }

    void testDifferentTransferSyntaxes() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        const Uint16 width = 16;
        const Uint16 height = 16;
        std::vector<Uint16> pixels(static_cast<size_t>(width) * height);
        for (size_t i = 0; i < pixels.size(); ++i) {
            pixels[i] = static_cast<Uint16>((i % width) * 250);
        }

        const std::vector<std::pair<QString, E_TransferSyntax>> syntaxes = {
            {QStringLiteral("implicit_le.dcm"), EXS_LittleEndianImplicit},
            {QStringLiteral("explicit_le.dcm"), EXS_LittleEndianExplicit},
            {QStringLiteral("explicit_be.dcm"), EXS_BigEndianExplicit}
        };

        dicom::DicomLoader loader;
        for (const auto& [name, xfer] : syntaxes) {
            DcmFileFormat ff;
            DcmDataset* ds = ff.getDataset();
            ds->putAndInsertString(DCM_SOPClassUID, UID_SecondaryCaptureImageStorage);
            ds->putAndInsertString(DCM_SOPInstanceUID, "1.2.826.0.1.3680043.2.1125.2.5");
            ds->putAndInsertUint16(DCM_SamplesPerPixel, 1);
            ds->putAndInsertString(DCM_PhotometricInterpretation, "MONOCHROME2");
            ds->putAndInsertUint16(DCM_Rows, height);
            ds->putAndInsertUint16(DCM_Columns, width);
            ds->putAndInsertUint16(DCM_BitsAllocated, 16);
            ds->putAndInsertUint16(DCM_BitsStored, 16);
            ds->putAndInsertUint16(DCM_HighBit, 15);
            ds->putAndInsertUint16(DCM_PixelRepresentation, 0);
            ds->putAndInsertUint16Array(DCM_PixelData, pixels.data(), static_cast<unsigned long>(pixels.size()));

            const QString path = tempDir.filePath(name);
            QVERIFY(saveDataset(ff, path, xfer));

            const auto res = loader.loadFile(path);
            QVERIFY2(res.isValid, qPrintable(res.errorMessage));
            QCOMPARE(res.width, 16);
            QCOMPARE(res.height, 16);
            QVERIFY(qGray(res.image.pixel(0, 0)) < qGray(res.image.pixel(15, 0)));
        }
    }

    void testMultiFrameAndUniformPixels() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        // Multi-frame: 3 quadros 8x8
        const Uint16 width = 8;
        const Uint16 height = 8;
        const size_t framePixels = static_cast<size_t>(width) * height;
        std::vector<Uint8> multiPixels(framePixels * 3);
        std::fill(multiPixels.begin(), multiPixels.begin() + framePixels, 200);
        std::fill(multiPixels.begin() + framePixels, multiPixels.begin() + 2 * framePixels, 100);
        std::fill(multiPixels.begin() + 2 * framePixels, multiPixels.end(), 10);

        DcmFileFormat ffMulti;
        DcmDataset* dsMulti = ffMulti.getDataset();
        dsMulti->putAndInsertString(DCM_SOPClassUID, UID_SecondaryCaptureImageStorage);
        dsMulti->putAndInsertString(DCM_SOPInstanceUID, "1.2.826.0.1.3680043.2.1125.2.6");
        dsMulti->putAndInsertString(DCM_NumberOfFrames, "3");
        dsMulti->putAndInsertUint16(DCM_SamplesPerPixel, 1);
        dsMulti->putAndInsertString(DCM_PhotometricInterpretation, "MONOCHROME2");
        dsMulti->putAndInsertUint16(DCM_Rows, height);
        dsMulti->putAndInsertUint16(DCM_Columns, width);
        dsMulti->putAndInsertUint16(DCM_BitsAllocated, 8);
        dsMulti->putAndInsertUint16(DCM_BitsStored, 8);
        dsMulti->putAndInsertUint16(DCM_HighBit, 7);
        dsMulti->putAndInsertUint16(DCM_PixelRepresentation, 0);
        dsMulti->putAndInsertUint8Array(DCM_PixelData, multiPixels.data(), static_cast<unsigned long>(multiPixels.size()));

        const QString multiPath = tempDir.filePath(QStringLiteral("multiframe.dcm"));
        QVERIFY(saveDataset(ffMulti, multiPath));

        dicom::DicomLoader loader;
        const auto multiRes = loader.loadFile(multiPath);
        QVERIFY2(multiRes.isValid, qPrintable(multiRes.errorMessage));
        QCOMPARE(multiRes.width, 8);
        QCOMPARE(multiRes.height, 8);

        // Imagem 1x1 uniforme (testa proteção contra divisão por zero em MinMax)
        std::vector<Uint8> singlePixel = {128};
        DcmFileFormat ffOne;
        DcmDataset* dsOne = ffOne.getDataset();
        dsOne->putAndInsertString(DCM_SOPClassUID, UID_SecondaryCaptureImageStorage);
        dsOne->putAndInsertString(DCM_SOPInstanceUID, "1.2.826.0.1.3680043.2.1125.2.7");
        dsOne->putAndInsertUint16(DCM_SamplesPerPixel, 1);
        dsOne->putAndInsertString(DCM_PhotometricInterpretation, "MONOCHROME2");
        dsOne->putAndInsertUint16(DCM_Rows, 1);
        dsOne->putAndInsertUint16(DCM_Columns, 1);
        dsOne->putAndInsertUint16(DCM_BitsAllocated, 8);
        dsOne->putAndInsertUint16(DCM_BitsStored, 8);
        dsOne->putAndInsertUint16(DCM_HighBit, 7);
        dsOne->putAndInsertUint16(DCM_PixelRepresentation, 0);
        dsOne->putAndInsertUint8Array(DCM_PixelData, singlePixel.data(), 1);

        const QString onePath = tempDir.filePath(QStringLiteral("one_pixel.dcm"));
        QVERIFY(saveDataset(ffOne, onePath));

        const auto oneRes = loader.loadFile(onePath);
        QVERIFY2(oneRes.isValid, qPrintable(oneRes.errorMessage));
        QCOMPARE(oneRes.width, 1);
        QCOMPARE(oneRes.height, 1);
    }

    void testDirectoryTruncatedAndUnicodePaths() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        dicom::DicomLoader loader;

        // 1. Passar um diretório em vez de arquivo
        const auto dirRes = loader.loadFile(tempDir.path());
        QVERIFY(!dirRes.isValid);
        QVERIFY(!dirRes.errorMessage.isEmpty());

        // 2. Criar arquivo válido e depois truncar pela metade para simular corrupção parcial
        const Uint16 width = 64;
        const Uint16 height = 64;
        std::vector<Uint8> pixels(static_cast<size_t>(width) * height, 150);

        DcmFileFormat ff;
        DcmDataset* ds = ff.getDataset();
        ds->putAndInsertString(DCM_SOPClassUID, UID_SecondaryCaptureImageStorage);
        ds->putAndInsertString(DCM_SOPInstanceUID, "1.2.826.0.1.3680043.2.1125.2.8");
        ds->putAndInsertUint16(DCM_SamplesPerPixel, 1);
        ds->putAndInsertString(DCM_PhotometricInterpretation, "MONOCHROME2");
        ds->putAndInsertUint16(DCM_Rows, height);
        ds->putAndInsertUint16(DCM_Columns, width);
        ds->putAndInsertUint16(DCM_BitsAllocated, 8);
        ds->putAndInsertUint16(DCM_BitsStored, 8);
        ds->putAndInsertUint16(DCM_HighBit, 7);
        ds->putAndInsertUint16(DCM_PixelRepresentation, 0);
        ds->putAndInsertUint8Array(DCM_PixelData, pixels.data(), static_cast<unsigned long>(pixels.size()));

        const QString unicodePath = tempDir.filePath(QStringLiteral("exame_tórax_coração_çãõ.dcm"));
        // Salva via QFile + caminho local seguro primeiro e testa o carregamento pelo caminho Unicode
        const QString tempAsciiPath = tempDir.filePath(QStringLiteral("temp_source.dcm"));
        QVERIFY(saveDataset(ff, tempAsciiPath));
        QVERIFY(QFile::copy(tempAsciiPath, unicodePath));

        const auto unicodeRes = loader.loadFile(unicodePath);
        QVERIFY2(unicodeRes.isValid, qPrintable(unicodeRes.errorMessage));
        QCOMPARE(unicodeRes.width, 64);
        QCOMPARE(unicodeRes.height, 64);

        // Trunca o arquivo para 150 bytes (quebra no meio do header/pixeldata)
        const QString truncatedPath = tempDir.filePath(QStringLiteral("truncated.dcm"));
        QVERIFY(QFile::copy(tempAsciiPath, truncatedPath));
        QFile truncFile(truncatedPath);
        QVERIFY(truncFile.open(QIODevice::ReadWrite));
        QVERIFY(truncFile.resize(150));
        truncFile.close();

        const auto truncRes = loader.loadFile(truncatedPath);
        QVERIFY(!truncRes.isValid);
        QVERIFY(!truncRes.errorMessage.isEmpty());
    }

    void testDynamicWindowLevelAndResetAndMetadata() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        const Uint16 width = 16;
        const Uint16 height = 16;
        std::vector<Uint16> pixels(static_cast<size_t>(width) * height);
        for (Uint16 y = 0; y < height; ++y) {
            for (Uint16 x = 0; x < width; ++x) {
                pixels[static_cast<size_t>(y) * width + x] = (x < 8) ? 500 : 1500;
            }
        }

        DcmFileFormat ff;
        DcmDataset* ds = ff.getDataset();
        ds->putAndInsertString(DCM_SOPClassUID, UID_SecondaryCaptureImageStorage);
        ds->putAndInsertString(DCM_SOPInstanceUID, "1.2.826.0.1.3680043.2.1125.2.9");
        ds->putAndInsertString(DCM_PatientName, "TESTE^PACIENTE");
        ds->putAndInsertString(DCM_Modality, "CR");
        ds->putAndInsertUint16(DCM_SamplesPerPixel, 1);
        ds->putAndInsertString(DCM_PhotometricInterpretation, "MONOCHROME2");
        ds->putAndInsertUint16(DCM_Rows, height);
        ds->putAndInsertUint16(DCM_Columns, width);
        ds->putAndInsertUint16(DCM_BitsAllocated, 16);
        ds->putAndInsertUint16(DCM_BitsStored, 12);
        ds->putAndInsertUint16(DCM_HighBit, 11);
        ds->putAndInsertUint16(DCM_PixelRepresentation, 0);
        ds->putAndInsertString(DCM_WindowCenter, "1000");
        ds->putAndInsertString(DCM_WindowWidth, "1000");
        ds->putAndInsertUint16Array(DCM_PixelData, pixels.data(), static_cast<unsigned long>(pixels.size()));

        const QString path = tempDir.filePath(QStringLiteral("wl_dynamic.dcm"));
        QVERIFY(saveDataset(ff, path));

        dicom::DicomLoader loader;
        const auto base = loader.loadFile(path);
        QVERIFY2(base.isValid, qPrintable(base.errorMessage));
        QCOMPARE(base.windowCenter, 1000.0);
        QCOMPARE(base.windowWidth, 1000.0);
        QVERIFY(!base.metadata.isEmpty());

        // Move janela bem para baixo (center=400, width=100) -> ambos 500 e 1500 ficam brancos (255)
        const auto lowWin = loader.applyWindowLevel(base, 400.0, 100.0);
        QVERIFY(lowWin.isValid);
        QCOMPARE(qGray(lowWin.image.pixel(2, 8)), 255);
        QCOMPARE(qGray(lowWin.image.pixel(12, 8)), 255);

        // Move janela bem para cima (center=2000, width=100) -> ambos 500 e 1500 ficam pretos (0)
        const auto highWin = loader.applyWindowLevel(base, 2000.0, 100.0);
        QVERIFY(highWin.isValid);
        QCOMPARE(qGray(highWin.image.pixel(2, 8)), 0);
        QCOMPARE(qGray(highWin.image.pixel(12, 8)), 0);

        // Restaura janela original
        const auto reset = loader.resetWindowLevel(highWin);
        QVERIFY(reset.isValid);
        QCOMPARE(reset.windowCenter, 1000.0);
        QCOMPARE(reset.windowWidth, 1000.0);
        QCOMPARE(qGray(reset.image.pixel(2, 8)), 0);
        QCOMPARE(qGray(reset.image.pixel(12, 8)), 255);

        // Testa amostragem clínica do valor calibrado sob coordenada (x, y)
        double sampledLeft = 0.0;
        double sampledRight = 0.0;
        QVERIFY(dicom::DicomLoader::samplePixelValue(reset, 2, 8, sampledLeft));
        QVERIFY(dicom::DicomLoader::samplePixelValue(reset, 12, 8, sampledRight));
        QCOMPARE(sampledLeft, 500.0);
        QCOMPARE(sampledRight, 1500.0);
        double outOfBounds = 0.0;
        QVERIFY(!dicom::DicomLoader::samplePixelValue(reset, -1, 0, outOfBounds));
        QVERIFY(!dicom::DicomLoader::samplePixelValue(reset, 100, 100, outOfBounds));
    }
};

QTEST_MAIN(DicomEdgeCasesTest)
#include "tst_dicom_edge_cases.moc"
