#include "core/DicomAnonymizer.h"
#include "core/DicomLoader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <dcmtk/dcmdata/dcdatset.h>
#include <dcmtk/dcmdata/dcdeftag.h>
#include <dcmtk/dcmdata/dcfilefo.h>
#include <dcmtk/dcmdata/dcistrmb.h>
#include <dcmtk/dcmdata/dcmetinf.h>
#include <dcmtk/dcmdata/dcuid.h>

namespace dicom {

namespace {

QByteArray toSafeLocalPath(const QString& filePath) {
    const QString nativePath = QDir::toNativeSeparators(filePath);
#ifdef _WIN32
    const std::wstring widePath = nativePath.toStdWString();
    DWORD shortLen = ::GetShortPathNameW(widePath.c_str(), nullptr, 0);
    if (shortLen > 0) {
        std::wstring shortBuf(shortLen, L'\0');
        DWORD copied = ::GetShortPathNameW(widePath.c_str(), shortBuf.data(), shortLen);
        if (copied > 0 && copied < shortLen) {
            shortBuf.resize(copied);
            return QString::fromStdWString(shortBuf).toLocal8Bit();
        }
    }
#endif
    return nativePath.toLocal8Bit();
}

void scrubOrRemoveTag(DcmItem* item, const DcmTagKey& tag, const char* replacement = nullptr) {
    if (!item) {
        return;
    }
    if (replacement) {
        item->putAndInsertString(tag, replacement);
    } else {
        item->findAndDeleteElement(tag, OFTrue, OFTrue);
    }
}

} // namespace

bool DicomAnonymizer::anonymizeFile(const QString& inputPath,
                                    const QString& outputPath,
                                    QString& outError) {
    DicomLoader::ensureCodecsRegistered();

    const QFileInfo inInfo(inputPath);
    if (!inInfo.exists() || !inInfo.isFile()) {
        outError = QStringLiteral("Arquivo de origem nao encontrado para anonimizacao.");
        return false;
    }

    DcmFileFormat fileFormat;
    OFCondition status = fileFormat.loadFile(OFFilename(toSafeLocalPath(inInfo.absoluteFilePath()).constData()));
    if (status.bad()) {
        QFile file(inInfo.absoluteFilePath());
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray raw = file.readAll();
            if (!raw.isEmpty()) {
                DcmInputBufferStream stream;
                stream.setBuffer(raw.constData(), static_cast<offile_off_t>(raw.size()));
                stream.setEos();
                fileFormat.clear();
                fileFormat.transferInit();
                status = fileFormat.read(stream);
                fileFormat.transferEnd();
            }
        }
    }

    if (status.bad()) {
        outError = QStringLiteral("Falha ao ler arquivo DICOM para anonimizacao: %1")
                       .arg(QString::fromLatin1(status.text()));
        return false;
    }

    DcmDataset* dataset = fileFormat.getDataset();
    if (!dataset) {
        outError = QStringLiteral("Dataset DICOM ausente.");
        return false;
    }

    // Substitui/remove tags de identificação pessoal (PHI - LGPD)
    scrubOrRemoveTag(dataset, DCM_PatientName, "ANONYMIZED");
    scrubOrRemoveTag(dataset, DCM_PatientID, "ANON_0000");
    scrubOrRemoveTag(dataset, DCM_PatientBirthDate);
    scrubOrRemoveTag(dataset, DCM_PatientBirthTime);
    scrubOrRemoveTag(dataset, DCM_PatientSex);
    scrubOrRemoveTag(dataset, DCM_PatientAge);
    scrubOrRemoveTag(dataset, DCM_PatientWeight);
    scrubOrRemoveTag(dataset, DCM_PatientSize);
    scrubOrRemoveTag(dataset, DCM_PatientAddress);
    scrubOrRemoveTag(dataset, DCM_PatientTelephoneNumbers);
    scrubOrRemoveTag(dataset, DCM_RETIRED_OtherPatientIDs);
    scrubOrRemoveTag(dataset, DCM_OtherPatientIDsSequence);
    scrubOrRemoveTag(dataset, DCM_OtherPatientNames);
    scrubOrRemoveTag(dataset, DCM_PatientComments);

    // Remove informações da instituição, médico e operador
    scrubOrRemoveTag(dataset, DCM_InstitutionName);
    scrubOrRemoveTag(dataset, DCM_InstitutionAddress);
    scrubOrRemoveTag(dataset, DCM_InstitutionalDepartmentName);
    scrubOrRemoveTag(dataset, DCM_ReferringPhysicianName);
    scrubOrRemoveTag(dataset, DCM_ReferringPhysicianAddress);
    scrubOrRemoveTag(dataset, DCM_ReferringPhysicianTelephoneNumbers);
    scrubOrRemoveTag(dataset, DCM_PhysiciansOfRecord);
    scrubOrRemoveTag(dataset, DCM_PerformingPhysicianName);
    scrubOrRemoveTag(dataset, DCM_NameOfPhysiciansReadingStudy);
    scrubOrRemoveTag(dataset, DCM_OperatorsName);
    scrubOrRemoveTag(dataset, DCM_StationName);
    scrubOrRemoveTag(dataset, DCM_AccessionNumber);
    scrubOrRemoveTag(dataset, DCM_StudyID);
    scrubOrRemoveTag(dataset, DCM_DeviceSerialNumber);

    // Regenera UIDs de estudo, série e instância para desvinculação completa
    char newStudyUid[100];
    char newSeriesUid[100];
    char newSopUid[100];
    dcmGenerateUniqueIdentifier(newStudyUid, SITE_STUDY_UID_ROOT);
    dcmGenerateUniqueIdentifier(newSeriesUid, SITE_SERIES_UID_ROOT);
    dcmGenerateUniqueIdentifier(newSopUid, SITE_INSTANCE_UID_ROOT);

    dataset->putAndInsertString(DCM_StudyInstanceUID, newStudyUid);
    dataset->putAndInsertString(DCM_SeriesInstanceUID, newSeriesUid);
    dataset->putAndInsertString(DCM_SOPInstanceUID, newSopUid);
    if (DcmMetaInfo* meta = fileFormat.getMetaInfo()) {
        meta->putAndInsertString(DCM_MediaStorageSOPInstanceUID, newSopUid);
    }

    // Marca o arquivo como anonimizado segundo o padrão DICOM
    dataset->putAndInsertString(DCM_PatientIdentityRemoved, "YES");
    dataset->putAndInsertString(DCM_DeidentificationMethod, "LGPD_BASIC_PROFILE");

    const QFileInfo outInfo(outputPath);
    QDir().mkpath(outInfo.absolutePath());

    // Garante que o arquivo possa ser gravado preservando a sintaxe original
    const E_TransferSyntax originalXfer = dataset->getOriginalXfer();
    const QString tempOut = outInfo.absoluteFilePath();
    // Cria arquivo vazio caso não exista para garantir geração de ShortPath no Windows
    {
        QFile touchFile(tempOut);
        if (!touchFile.open(QIODevice::WriteOnly)) {
            outError = QStringLiteral("Nao foi possivel criar arquivo de destino para anonimizacao.");
            return false;
        }
    }

    const OFCondition saveStatus = fileFormat.saveFile(
        OFFilename(toSafeLocalPath(tempOut).constData()),
        originalXfer);

    if (saveStatus.bad()) {
        QFile::remove(tempOut);
        outError = QStringLiteral("Falha ao salvar arquivo DICOM anonimizado: %1")
                       .arg(QString::fromLatin1(saveStatus.text()));
        return false;
    }

    return true;
}

} // namespace dicom
