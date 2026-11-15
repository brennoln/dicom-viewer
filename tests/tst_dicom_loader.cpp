#include <QtTest>
#include <QTemporaryDir>
#include <QFile>

#include "core/DicomLoader.h"

#include <dcmtk/dcmdata/dcdatset.h>
#include <dcmtk/dcmdata/dcdeftag.h>
#include <dcmtk/dcmdata/dcfilefo.h>
#include <dcmtk/dcmdata/dcuid.h>
#include <vector>

class DicomLoaderTest : public QObject {
    Q_OBJECT

private:
    /**
     * @brief Auxiliar para criar arquivos DICOM (.dcm) sintéticos reais no disco usando DCMTK
     */
    static bool createSyntheticDicom(const QString& filePath,
                                     Uint16 width,
                                     Uint16 height,
                                     const char* photometric,
                                     Uint16 bitsAllocated,
                                     Uint16 bitsStored,
                                     const void* pixelBytes,
                                      unsigned long byteLength,
                                     int fovRotation = 0,
                                     bool fovHorizontalFlip = false,
                                     const char* pixelSpacing = nullptr) {
        DcmFileFormat fileFormat;
        DcmDataset* dataset = fileFormat.getDataset();

        dataset->putAndInsertString(DCM_SOPClassUID, UID_SecondaryCaptureImageStorage);
        dataset->putAndInsertString(DCM_SOPInstanceUID, "1.2.826.0.1.3680043.2.1125.1.1");
        dataset->putAndInsertString(DCM_Modality, "OT");
        dataset->putAndInsertUint16(DCM_SamplesPerPixel, 1);
        dataset->putAndInsertString(DCM_PhotometricInterpretation, photometric);
        dataset->putAndInsertUint16(DCM_Rows, height);
        dataset->putAndInsertUint16(DCM_Columns, width);
        dataset->putAndInsertUint16(DCM_BitsAllocated, bitsAllocated);
        dataset->putAndInsertUint16(DCM_BitsStored, bitsStored);
        dataset->putAndInsertUint16(DCM_HighBit, static_cast<Uint16>(bitsStored - 1));
        dataset->putAndInsertUint16(DCM_PixelRepresentation, 0);

        if (fovRotation != 0) {
            dataset->putAndInsertString(DCM_FieldOfViewRotation, QByteArray::number(fovRotation).constData());
        }
        if (fovHorizontalFlip) {
            dataset->putAndInsertString(DCM_FieldOfViewHorizontalFlip, "YES");
        }
        if (pixelSpacing) {
            dataset->putAndInsertString(DCM_PixelSpacing, pixelSpacing);
        }

        if (pixelBytes && byteLength > 0) {
            if (bitsAllocated <= 8) {
                dataset->putAndInsertUint8Array(DCM_PixelData, static_cast<const Uint8*>(pixelBytes), byteLength);
            } else {
                dataset->putAndInsertUint16Array(DCM_PixelData,
                                                 static_cast<const Uint16*>(pixelBytes),
                                                 byteLength / sizeof(Uint16));
            }
        }

        const QByteArray localPath = QDir::toNativeSeparators(filePath).toLocal8Bit();
        return fileFormat.saveFile(OFFilename(localPath.constData()), EXS_LittleEndianExplicit).good();
    }

private slots:
    void testInvalidAndCorruptedFiles() {
        dicom::DicomLoader loader;

        // 1. Caminho vazio
        const auto emptyPathRes = loader.loadFile(QStringLiteral("   "));
        QVERIFY(!emptyPathRes.isValid);
        QVERIFY(!emptyPathRes.errorMessage.isEmpty());

        // 2. Arquivo inexistente
        const auto missingRes = loader.loadFile(QStringLiteral("C:/caminho_inexistente/nao_existe_12345.dcm"));
        QVERIFY(!missingRes.isValid);
        QVERIFY(!missingRes.errorMessage.isEmpty());

        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        // 3. Arquivo vazio (0 bytes)
        const QString zeroBytePath = tempDir.filePath(QStringLiteral("empty.dcm"));
        {
            QFile f(zeroBytePath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.close();
        }
        const auto zeroByteRes = loader.loadFile(zeroBytePath);
        QVERIFY(!zeroByteRes.isValid);
        QVERIFY(!zeroByteRes.errorMessage.isEmpty());

        // 4. Arquivo de texto / corrompido (não é um DICOM válido)
        const QString corruptPath = tempDir.filePath(QStringLiteral("corrupt.dcm"));
        {
            QFile f(corruptPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("ESTE NAO E UM ARQUIVO DICOM VALIDO 1234567890");
            f.close();
        }
        const auto corruptRes = loader.loadFile(corruptPath);
        QVERIFY(!corruptRes.isValid);
        QVERIFY(!corruptRes.errorMessage.isEmpty());

        // 5. Arquivo DICOM válido sem tag PixelData
        const QString noPixelPath = tempDir.filePath(QStringLiteral("no_pixels.dcm"));
        QVERIFY(createSyntheticDicom(noPixelPath, 64, 64, "MONOCHROME2", 8, 8, nullptr, 0));
        const auto noPixelRes = loader.loadFile(noPixelPath);
        QVERIFY(!noPixelRes.isValid);
        QVERIFY(noPixelRes.errorMessage.contains(QStringLiteral("PixelData"), Qt::CaseInsensitive));
    }

    void testMonochrome2AndStrideAlignment() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        // Largura 65 (não divisível por 4) para testar alinhamento de scanline do QImage
        const Uint16 width = 65;
        const Uint16 height = 40;
        std::vector<Uint8> pixels(static_cast<size_t>(width) * height, 0);

        // Preenche metade esquerda com 0 (preto) e metade direita com 255 (branco)
        for (Uint16 y = 0; y < height; ++y) {
            for (Uint16 x = 0; x < width; ++x) {
                pixels[static_cast<size_t>(y) * width + x] = (x >= 32) ? 255 : 0;
            }
        }

        const QString dcmPath = tempDir.filePath(QStringLiteral("mono2_stride.dcm"));
        QVERIFY(createSyntheticDicom(dcmPath, width, height, "MONOCHROME2", 8, 8,
                                     pixels.data(), static_cast<unsigned long>(pixels.size())));

        dicom::DicomLoader loader;
        const auto res = loader.loadFile(dcmPath);
        QVERIFY2(res.isValid, qPrintable(res.errorMessage));
        QCOMPARE(res.width, 65);
        QCOMPARE(res.height, 40);
        QCOMPARE(res.image.format(), QImage::Format_Grayscale8);

        // Verifica que todas as linhas mantêm exatamente a fronteira em x=32 (sem inclinação/skew)
        for (int y = 0; y < res.height; ++y) {
            QCOMPARE(qGray(res.image.pixel(0, y)), 0);
            QCOMPARE(qGray(res.image.pixel(31, y)), 0);
            QCOMPARE(qGray(res.image.pixel(32, y)), 255);
            QCOMPARE(qGray(res.image.pixel(64, y)), 255);
        }
    }

    void testMonochrome1Inversion16Bit() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        // Em MONOCHROME1, valor mínimo (0) deve ser exibido como BRANCO (255)
        // e valor máximo (4095) deve ser exibido como PRETO (0)
        const Uint16 width = 32;
        const Uint16 height = 32;
        std::vector<Uint16> pixels(static_cast<size_t>(width) * height, 0);
        for (size_t i = 0; i < pixels.size(); ++i) {
            pixels[i] = (i < pixels.size() / 2) ? 0 : 4095;
        }

        const QString dcmPath = tempDir.filePath(QStringLiteral("mono1_16bit.dcm"));
        QVERIFY(createSyntheticDicom(dcmPath, width, height, "MONOCHROME1", 16, 12,
                                     pixels.data(), static_cast<unsigned long>(pixels.size() * sizeof(Uint16))));

        dicom::DicomLoader loader;
        const auto res = loader.loadFile(dcmPath);
        QVERIFY2(res.isValid, qPrintable(res.errorMessage));
        QCOMPARE(res.photometricInterpretation, QStringLiteral("MONOCHROME1"));
        QCOMPARE(res.bitsStored, 12);

        // Topo (valor 0 em MONOCHROME1) -> Branco (255)
        QCOMPARE(qGray(res.image.pixel(0, 0)), 255);
        // Base (valor 4095 em MONOCHROME1) -> Preto (0)
        QCOMPARE(qGray(res.image.pixel(width - 1, height - 1)), 0);
    }

    void testOrientationAndPixelSpacing() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        // Imagem 20x10 com rotação de 90 graus -> deve resultar em 10x20
        const Uint16 width = 20;
        const Uint16 height = 10;
        std::vector<Uint8> pixels(static_cast<size_t>(width) * height, 100);
        pixels[0] = 255; // Canto superior esquerdo original
        pixels[pixels.size() - 1] = 0;

        const QString rotPath = tempDir.filePath(QStringLiteral("rotated90.dcm"));
        QVERIFY(createSyntheticDicom(rotPath, width, height, "MONOCHROME2", 8, 8,
                                     pixels.data(), static_cast<unsigned long>(pixels.size()),
                                     90, false));

        dicom::DicomLoader loader;
        const auto rotRes = loader.loadFile(rotPath);
        QVERIFY2(rotRes.isValid, qPrintable(rotRes.errorMessage));
        QCOMPARE(rotRes.width, 10);
        QCOMPARE(rotRes.height, 20);

        // Teste de PixelSpacing não-quadrado (2.0 \ 1.0 -> altura física dobrada)
        std::vector<Uint8> pixels40x20(40 * 20, 128);
        const QString spacingPath = tempDir.filePath(QStringLiteral("spacing.dcm"));
        QVERIFY(createSyntheticDicom(spacingPath, 40, 20, "MONOCHROME2", 8, 8,
                                     pixels40x20.data(), static_cast<unsigned long>(pixels40x20.size()),
                                     0, false, "2.0\\1.0"));
        const auto spacingRes = loader.loadFile(spacingPath);
        QVERIFY2(spacingRes.isValid, qPrintable(spacingRes.errorMessage));
        QCOMPARE(spacingRes.width, 40);
        QCOMPARE(spacingRes.height, 40);
    }

    void testOfficialAnonymizedMammographyFile() {
        const QString officialPath = QStringLiteral(TEST_ASSETS_DIR "/anonymized_mamo.dcm");
        if (!QFileInfo::exists(officialPath)) {
            QSKIP("Arquivo oficial anonymized_mamo.dcm nao encontrado em assets/.");
        }

        dicom::DicomLoader loader;
        const auto res = loader.loadFile(officialPath);
        QVERIFY2(res.isValid, qPrintable(res.errorMessage));
        QVERIFY(res.errorMessage.isEmpty());

        // Verifica dimensões exatas do exame de mamografia (Columns=2394, Rows=2850)
        QCOMPARE(res.width, 2394);
        QCOMPARE(res.height, 2850);
        QCOMPARE(res.image.width(), 2394);
        QCOMPARE(res.image.height(), 2850);
        QCOMPARE(res.image.format(), QImage::Format_Grayscale8);

        // Verifica propriedades extraídas do DICOM
        QCOMPARE(res.bitsAllocated, 16);
        QCOMPARE(res.bitsStored, 12);
        QCOMPARE(res.photometricInterpretation, QStringLiteral("MONOCHROME2"));
        QCOMPARE(res.modality, QStringLiteral("MG"));
        QCOMPARE(res.imageLaterality, QStringLiteral("L"));
        QCOMPARE(res.viewPosition, QStringLiteral("MLO"));

        // Verifica que a região anatômica (esquerda) possui sinal luminoso (> 30)
        // e o fundo (direita) é escuro (0)
        const int tissueGray = qGray(res.image.pixel(250, 1400));
        const int backgroundGray = qGray(res.image.pixel(2000, 1400));
        QVERIFY2(tissueGray > 30, "A regiao do tecido mamario deve apresentar tons de cinza visiveis.");
        QCOMPARE(backgroundGray, 0);
    }
};

QTEST_MAIN(DicomLoaderTest)
#include "tst_dicom_loader.moc"
