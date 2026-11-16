#include "core/AuditLogger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

namespace dicom {

AuditLogger::AuditLogger(const QString& logFilePath)
    : m_logFilePath(logFilePath.isEmpty() ? defaultLogPath() : logFilePath) {}

QString AuditLogger::defaultLogPath() {
    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (baseDir.isEmpty()) {
        baseDir = QDir::tempPath();
    }
    return QDir(baseDir).filePath(QStringLiteral("dicom_viewer_audit.log"));
}

bool AuditLogger::logEvent(const QString& eventType,
                           const QString& filePath,
                           const QString& outcome,
                           const QString& details) const {
    const QFileInfo logInfo(m_logFilePath);
    QDir().mkpath(logInfo.absolutePath());

    QFile file(m_logFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return false;
    }

    // Registra apenas o nome base do arquivo para evitar exposição desnecessária de caminhos locais completos
    const QString safeFileName = filePath.isEmpty()
        ? QStringLiteral("-")
        : QFileInfo(filePath).fileName();

    QString sanitizedDetails = details;
    sanitizedDetails.replace(QLatin1Char('\n'), QLatin1Char(' '));
    sanitizedDetails.replace(QLatin1Char('\r'), QLatin1Char(' '));

    QTextStream out(&file);
    out << QDateTime::currentDateTimeUtc().toString(Qt::ISODate)
        << " | " << eventType
        << " | file=" << safeFileName
        << " | outcome=" << outcome;
    if (!sanitizedDetails.isEmpty()) {
        out << " | details=" << sanitizedDetails;
    }
    out << '\n';
    return true;
}

} // namespace dicom
