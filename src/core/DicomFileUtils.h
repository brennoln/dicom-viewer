#pragma once

#include <QByteArray>
#include <QString>

class DcmFileFormat;

namespace dicom {

/**
 * @brief Utilitários compartilhados de I/O seguro para leitura e gravação
 * de arquivos DICOM com suporte a caminhos Unicode/acentuados no Windows.
 */
namespace file_utils {

QByteArray toDcmtkSafePath(const QString& filePath);

bool loadDicomFormatSafe(const QString& filePath,
                         DcmFileFormat& fileFormat,
                         QString& outError);

} // namespace file_utils
} // namespace dicom
