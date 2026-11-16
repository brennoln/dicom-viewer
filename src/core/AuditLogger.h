#pragma once

#include <QString>

namespace dicom {

/**
 * @brief Registra eventos operacionais e de segurança em arquivo de auditoria local
 * (conformidade com rastreabilidade clínica e LGPD, sem armazenar PHI nos logs).
 */
class AuditLogger {
public:
    explicit AuditLogger(const QString& logFilePath = defaultLogPath());

    static QString defaultLogPath();

    bool logEvent(const QString& eventType,
                  const QString& filePath,
                  const QString& outcome,
                  const QString& details = QString()) const;

    const QString& logFilePath() const { return m_logFilePath; }

private:
    QString m_logFilePath;
};

} // namespace dicom
