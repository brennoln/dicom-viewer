#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "core/AuditLogger.h"
#include "core/DicomAnonymizer.h"
#include "core/DicomLoader.h"

#include <dcmtk/dcmdata/dcdatset.h>
#include <dcmtk/dcmdata/dcdeftag.h>
#include <dcmtk/dcmdata/dcfilefo.h>
#include <dcmtk/dcmdata/dcuid.h>
#include <random>
#include <vector>

class DicomSecurityTest : public QObject {
    Q_OBJECT

private:
    static bool saveDataset(DcmFileFormat& ff, const QString& path) {
        const QByteArray local = QDir::toNativeSeparators(path).toLocal8Bit();
        return ff.saveFile(OFFilename(local.constData()), EXS_LittleEndianExplicit).good();
    }

private slots:
    void testDecompressionBombAndInvalidChannelsRejected() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        // 1. DICOM com dimensões absurdas (50000 x 50000 = 2.5 Gigapixels) simulando DoS / exaustão de RAM
        DcmFileFormat bombFf;
        DcmDataset* bombDs = bombFf.getDataset();
        bombDs->putAndInsertString(DCM_SOPClassUID, UID_SecondaryCaptureImageStorage);
        bombDs->putAndInsertString(DCM_SOPInstanceUID, "1.2.826.0.1.3680043.2.1125.9.1");
        bombDs->putAndInsertUint16(DCM_SamplesPerPixel, 1);
        bombDs->putAndInsertString(DCM_PhotometricInterpretation, "MONOCHROME2");
        bombDs->putAndInsertUint16(DCM_Rows, 50000);
        bombDs->putAndInsertUint16(DCM_Columns, 50000);
        bombDs->putAndInsertUint16(DCM_BitsAllocated, 8);
        bombDs->putAndInsertUint16(DCM_BitsStored, 8);
        bombDs->putAndInsertUint16(DCM_HighBit, 7);
        bombDs->putAndInsertUint16(DCM_PixelRepresentation, 0);
        std::vector<Uint8> dummy(16, 0);
        bombDs->putAndInsertUint8Array(DCM_PixelData, dummy.data(), static_cast<unsigned long>(dummy.size()));

        const QString bombPath = tempDir.filePath(QStringLiteral("bomb.dcm"));
        QVERIFY(saveDataset(bombFf, bombPath));

        dicom::DicomLoader loader;
        const auto bombRes = loader.loadFile(bombPath);
        QVERIFY(!bombRes.isValid);
        QVERIFY(bombRes.errorMessage.contains(QStringLiteral("limite seguro"), Qt::CaseInsensitive));

        // 2. DICOM com SamplesPerPixel inválido (99)
        DcmFileFormat chanFf;
        DcmDataset* chanDs = chanFf.getDataset();
        chanDs->putAndInsertString(DCM_SOPClassUID, UID_SecondaryCaptureImageStorage);
        chanDs->putAndInsertString(DCM_SOPInstanceUID, "1.2.826.0.1.3680043.2.1125.9.2");
        chanDs->putAndInsertUint16(DCM_SamplesPerPixel, 99);
        chanDs->putAndInsertString(DCM_PhotometricInterpretation, "MONOCHROME2");
        chanDs->putAndInsertUint16(DCM_Rows, 4);
        chanDs->putAndInsertUint16(DCM_Columns, 4);
        chanDs->putAndInsertUint16(DCM_BitsAllocated, 8);
        chanDs->putAndInsertUint16(DCM_BitsStored, 8);
        chanDs->putAndInsertUint16(DCM_HighBit, 7);
        chanDs->putAndInsertUint16(DCM_PixelRepresentation, 0);
        chanDs->putAndInsertUint8Array(DCM_PixelData, dummy.data(), static_cast<unsigned long>(dummy.size()));

        const QString chanPath = tempDir.filePath(QStringLiteral("bad_channels.dcm"));
        QVERIFY(saveDataset(chanFf, chanPath));

        const auto chanRes = loader.loadFile(chanPath);
        QVERIFY(!chanRes.isValid);
        QVERIFY(chanRes.errorMessage.contains(QStringLiteral("SamplesPerPixel"), Qt::CaseInsensitive));
    }

    void testMalformedFuzzingInputsDoNotCrash() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        // Cria um DICOM base válido para aplicar mutações de fuzzing
        DcmFileFormat baseFf;
        DcmDataset* baseDs = baseFf.getDataset();
        baseDs->putAndInsertString(DCM_SOPClassUID, UID_SecondaryCaptureImageStorage);
        baseDs->putAndInsertString(DCM_SOPInstanceUID, "1.2.826.0.1.3680043.2.1125.9.3");
        baseDs->putAndInsertUint16(DCM_SamplesPerPixel, 1);
        baseDs->putAndInsertString(DCM_PhotometricInterpretation, "MONOCHROME2");
        baseDs->putAndInsertUint16(DCM_Rows, 8);
        baseDs->putAndInsertUint16(DCM_Columns, 8);
        baseDs->putAndInsertUint16(DCM_BitsAllocated, 8);
        baseDs->putAndInsertUint16(DCM_BitsStored, 8);
        baseDs->putAndInsertUint16(DCM_HighBit, 7);
        baseDs->putAndInsertUint16(DCM_PixelRepresentation, 0);
        std::vector<Uint8> pixels(64, 120);
        baseDs->putAndInsertUint8Array(DCM_PixelData, pixels.data(), static_cast<unsigned long>(pixels.size()));

        const QString basePath = tempDir.filePath(QStringLiteral("base_fuzz.dcm"));
        QVERIFY(saveDataset(baseFf, basePath));

        QFile baseFile(basePath);
        QVERIFY(baseFile.open(QIODevice::ReadOnly));
        const QByteArray validBytes = baseFile.readAll();
        baseFile.close();

        std::mt19937 rng(20261116);
        dicom::DicomLoader loader;

        // 25 mutações aleatórias determinísticas no cabeçalho/payload + lixo binário puro
        for (int i = 0; i < 25; ++i) {
            QByteArray mutated = validBytes;
            if (i < 5) {
                // Lixo binário aleatório simulando arquivo não-DICOM malicioso
                mutated = QByteArray(512, '\0');
                for (int b = 0; b < mutated.size(); ++b) {
                    mutated[b] = static_cast<char>(rng() & 0xFF);
                }
                if (i == 0) {
                    // Simula cabeçalho executável PE ("MZ") mascarado como .dcm
                    mutated[0] = 'M';
                    mutated[1] = 'Z';
                }
            } else {
                // Mutações pontuais no stream DICOM (incluindo preamble, DICM magic, tags e comprimentos)
                const int numMutations = 4 + (i % 12);
                for (int m = 0; m < numMutations; ++m) {
                    const int pos = static_cast<int>(rng() % static_cast<unsigned int>(mutated.size()));
                    mutated[pos] = static_cast<char>(rng() & 0xFF);
                }
            }

            const QString fuzzPath = tempDir.filePath(QStringLiteral("fuzz_%1.dcm").arg(i));
            QFile f(fuzzPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(mutated);
            f.close();

            // O requisito de segurança é: nunca travar/crashar e, se falhar, retornar mensagem limpa
            const auto res = loader.loadFile(fuzzPath);
            if (!res.isValid) {
                QVERIFY(!res.errorMessage.isEmpty());
            } else {
                QVERIFY(!res.image.isNull());
            }
        }
    }

    void testLgpdAnonymizationRemovesAllPhi() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        DcmFileFormat ff;
        DcmDataset* ds = ff.getDataset();
        ds->putAndInsertString(DCM_SOPClassUID, UID_SecondaryCaptureImageStorage);
        ds->putAndInsertString(DCM_StudyInstanceUID, "1.2.826.0.1.3680043.2.1125.999.1");
        ds->putAndInsertString(DCM_SeriesInstanceUID, "1.2.826.0.1.3680043.2.1125.999.2");
        ds->putAndInsertString(DCM_SOPInstanceUID, "1.2.826.0.1.3680043.2.1125.999.3");
        ds->putAndInsertString(DCM_PatientName, "MARIA DA SILVA");
        ds->putAndInsertString(DCM_PatientID, "12345678900");
        ds->putAndInsertString(DCM_PatientBirthDate, "19800512");
        ds->putAndInsertString(DCM_PatientSex, "F");
        ds->putAndInsertString(DCM_PatientAge, "046Y");
        ds->putAndInsertString(DCM_PatientAddress, "Rua das Flores 100");
        ds->putAndInsertString(DCM_ReferringPhysicianName, "DR JOAO CARLOS");
        ds->putAndInsertString(DCM_InstitutionName, "CLINICA MEDICA EXEMPLO");
        ds->putAndInsertString(DCM_OperatorsName, "TECNICO ROBERTO");
        ds->putAndInsertString(DCM_AccessionNumber, "ACC-2026-001");
        ds->putAndInsertString(DCM_Modality, "CR");

        ds->putAndInsertUint16(DCM_SamplesPerPixel, 1);
        ds->putAndInsertString(DCM_PhotometricInterpretation, "MONOCHROME2");
        ds->putAndInsertUint16(DCM_Rows, 16);
        ds->putAndInsertUint16(DCM_Columns, 16);
        ds->putAndInsertUint16(DCM_BitsAllocated, 8);
        ds->putAndInsertUint16(DCM_BitsStored, 8);
        ds->putAndInsertUint16(DCM_HighBit, 7);
        ds->putAndInsertUint16(DCM_PixelRepresentation, 0);
        std::vector<Uint8> pixels(256, 200);
        ds->putAndInsertUint8Array(DCM_PixelData, pixels.data(), static_cast<unsigned long>(pixels.size()));

        const QString srcPath = tempDir.filePath(QStringLiteral("paciente_sensivel.dcm"));
        const QString anonPath = tempDir.filePath(QStringLiteral("paciente_anonimo.dcm"));
        QVERIFY(saveDataset(ff, srcPath));

        QString anonError;
        QVERIFY2(dicom::DicomAnonymizer::anonymizeFile(srcPath, anonPath, anonError), qPrintable(anonError));

        // Reabre o arquivo anonimizado com DCMTK e verifica que nenhum dado pessoal permaneceu
        DcmFileFormat checkFf;
        const QByteArray localAnon = QDir::toNativeSeparators(anonPath).toLocal8Bit();
        QVERIFY(checkFf.loadFile(OFFilename(localAnon.constData())).good());
        DcmDataset* checkDs = checkFf.getDataset();
        QVERIFY(checkDs != nullptr);

        OFString val;
        QVERIFY(checkDs->findAndGetOFString(DCM_PatientName, val).good());
        QCOMPARE(QString::fromLatin1(val.c_str()), QStringLiteral("ANONYMIZED"));

        QVERIFY(checkDs->findAndGetOFString(DCM_PatientID, val).good());
        QCOMPARE(QString::fromLatin1(val.c_str()), QStringLiteral("ANON_0000"));

        QVERIFY(!checkDs->tagExistsWithValue(DCM_PatientBirthDate));
        QVERIFY(!checkDs->tagExistsWithValue(DCM_PatientAddress));
        QVERIFY(!checkDs->tagExistsWithValue(DCM_ReferringPhysicianName));
        QVERIFY(!checkDs->tagExistsWithValue(DCM_InstitutionName));
        QVERIFY(!checkDs->tagExistsWithValue(DCM_OperatorsName));
        QVERIFY(!checkDs->tagExistsWithValue(DCM_AccessionNumber));

        // UIDs devem ter sido regenerados
        QVERIFY(checkDs->findAndGetOFString(DCM_StudyInstanceUID, val).good());
        QVERIFY(QString::fromLatin1(val.c_str()) != QStringLiteral("1.2.826.0.1.3680043.2.1125.999.1"));

        // Imagem continua íntegra e carregável no visualizador
        dicom::DicomLoader loader;
        const auto loaded = loader.loadFile(anonPath);
        QVERIFY2(loaded.isValid, qPrintable(loaded.errorMessage));
        QCOMPARE(loaded.width, 16);
        QCOMPARE(loaded.height, 16);
    }

    void testAuditLoggerTraceabilityAndLogInjectionSafety() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        const QString logPath = tempDir.filePath(QStringLiteral("audit_test.log"));
        dicom::AuditLogger logger(logPath);

        QVERIFY(logger.logEvent(QStringLiteral("OPEN_SUCCESS"),
                                QStringLiteral("C:/pacientes/segredo/exame_01.dcm"),
                                QStringLiteral("OK"),
                                QStringLiteral("2394x2850 MG")));
        // Simula tentativa de injeção de quebra de linha nos detalhes
        QVERIFY(logger.logEvent(QStringLiteral("OPEN_FAILED"),
                                QStringLiteral("C:/tmp/malicioso.dcm"),
                                QStringLiteral("ERROR"),
                                QStringLiteral("linha1\nFAKE_ADMIN_ENTRY\rlinha2")));

        QFile f(logPath);
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString content = QString::fromUtf8(f.readAll());
        const QStringList lines = content.split(QLatin1Char('\n'), Qt::SkipEmptyParts);

        QCOMPARE(lines.size(), 2);
        QVERIFY(lines[0].contains(QStringLiteral("OPEN_SUCCESS")));
        QVERIFY(lines[0].contains(QStringLiteral("file=exame_01.dcm")));
        // Não deve vazar caminho completo do diretório de pacientes
        QVERIFY(!lines[0].contains(QStringLiteral("segredo")));
        // Quebras de linha injetadas devem ter sido neutralizadas
        QVERIFY(lines[1].contains(QStringLiteral("FAKE_ADMIN_ENTRY")));
    }
};

QTEST_MAIN(DicomSecurityTest)
#include "tst_security.moc"
