#pragma once

#include <QString>

namespace dicom {

/**
 * @brief Responsável pela remoção irreversível de dados pessoais e sensíveis (PHI)
 * de arquivos DICOM em conformidade com a LGPD (Lei nº 13.709/2018).
 */
class DicomAnonymizer {
public:
    /**
     * @brief Cria uma cópia anonimizada de um arquivo DICOM, removendo tags identificáveis
     * do paciente, equipe médica e instituição, preservando os dados de imagem.
     */
    static bool anonymizeFile(const QString& inputPath,
                              const QString& outputPath,
                              QString& outError);
};

} // namespace dicom
