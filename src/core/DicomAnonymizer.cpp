#include "core/DicomAnonymizer.h"
#include "core/DicomFileUtils.h"
#include "core/DicomLoader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <dcmtk/dcmdata/dcdatset.h>
#include <dcmtk/dcmdata/dcdeftag.h>
#include <dcmtk/dcmdata/dcfilefo.h>
#include <dcmtk/dcmdata/dcmetinf.h>
#include <dcmtk/dcmdata/dcuid.h>

namespace dicom {

namespace {

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

    DcmFileFormat fileFormat;
    if (!file_utils::loadDicomFormatSafe(inputPath, fileFormat, outError)) {
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
    scrubOrRemoveTag(dataset, DcmTagKey(0x0010, 0x1000)); // OtherPatientIDs (compatível 3.6.x e 3.7.x)
    scrubOrRemoveTag(dataset, DcmTagKey(0x0010, 0x1002)); // OtherPatientIDsSequence
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

    // Remove tags privadas (grupos ímpares) onde fabricantes podem armazenar PHI proprietária
    for (unsigned long i = dataset->card(); i > 0; --i) {
        DcmElement* elem = dataset->getElement(i - 1);
        if (elem && elem->getTag().isPrivate()) {
            delete dataset->remove(i - 1);
        }
    }

    // Marca o arquivo como anonimizado segundo o padrão DICOM
    dataset->putAndInsertString(DCM_PatientIdentityRemoved, "YES");
    dataset->putAndInsertString(DCM_DeidentificationMethod, "LGPD_STRICT_PROFILE");

    const QFileInfo outInfo(outputPath);
    QDir().mkpath(outInfo.absolutePath());

    const E_TransferSyntax originalXfer = dataset->getOriginalXfer();
    const QString tempOut = outInfo.absoluteFilePath();
    {
        QFile touchFile(tempOut);
        if (!touchFile.open(QIODevice::WriteOnly)) {
            outError = QStringLiteral("Nao foi possivel criar arquivo de destino para anonimizacao.");
            return false;
        }
    }

    const OFCondition saveStatus = fileFormat.saveFile(
        OFFilename(file_utils::toDcmtkSafePath(tempOut).constData()),
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
