#include "core/DicomFileUtils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <dcmtk/dcmdata/dcfilefo.h>
#include <dcmtk/dcmdata/dcistrmb.h>

namespace dicom::file_utils {

QByteArray toDcmtkSafePath(const QString& filePath) {
    const QString nativePath = QDir::toNativeSeparators(filePath);
#ifdef _WIN32
    const std::wstring widePath = nativePath.toStdWString();
    const DWORD shortLen = ::GetShortPathNameW(widePath.c_str(), nullptr, 0);
    if (shortLen > 0) {
        std::wstring shortBuf(shortLen, L'\0');
        const DWORD copied = ::GetShortPathNameW(widePath.c_str(), shortBuf.data(), shortLen);
        if (copied > 0 && copied < shortLen) {
            shortBuf.resize(copied);
            return QString::fromStdWString(shortBuf).toLocal8Bit();
        }
    }
#endif
    return nativePath.toLocal8Bit();
}

bool loadDicomFormatSafe(const QString& filePath,
                         DcmFileFormat& fileFormat,
                         QString& outError) {
    const QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        outError = QStringLiteral("O arquivo selecionado nao existe ou nao e valido: %1").arg(filePath);
        return false;
    }

    if (fileInfo.size() == 0) {
        outError = QStringLiteral("O arquivo selecionado esta vazio (0 bytes).");
        return false;
    }

    constexpr qint64 kMaxDicomFileBytes = 512LL * 1024LL * 1024LL; // 512 MB
    if (fileInfo.size() > kMaxDicomFileBytes) {
        outError = QStringLiteral("Arquivo excede o limite de seguranca suportado (512 MB).");
        return false;
    }

    const QByteArray safePath = toDcmtkSafePath(fileInfo.absoluteFilePath());
    OFCondition loadStatus = fileFormat.loadFile(OFFilename(safePath.constData()));

    if (loadStatus.bad()) {
        QFile file(fileInfo.absoluteFilePath());
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray rawBytes = file.readAll();
            if (!rawBytes.isEmpty()) {
                DcmInputBufferStream stream;
                stream.setBuffer(rawBytes.constData(), static_cast<offile_off_t>(rawBytes.size()));
                stream.setEos();
                fileFormat.clear();
                fileFormat.transferInit();
                loadStatus = fileFormat.read(stream);
                fileFormat.transferEnd();
            }
        }
    }

    if (loadStatus.bad()) {
        outError = QStringLiteral("Falha ao ler arquivo DICOM (%1). Verifique se o arquivo e um .dcm valido.")
                       .arg(QString::fromLatin1(loadStatus.text()));
        return false;
    }

    return true;
}

} // namespace dicom::file_utils
