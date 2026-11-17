#pragma once

#include "core/DicomImageData.h"

#include <QDockWidget>
#include <QLineEdit>
#include <QTableWidget>
#include <QVector>

namespace dicom {

/**
 * @brief Painel lateral acoplável (Dock) para exibição e filtragem em tempo real
 * dos metadados extraídos do arquivo DICOM.
 */
class MetadataDockWidget : public QDockWidget {
    Q_OBJECT

public:
    explicit MetadataDockWidget(QWidget* parent = nullptr);
    ~MetadataDockWidget() override = default;

    void setEntries(const QVector<DicomMetadataEntry>& entries);
    void clearEntries();
    int rowCount() const;

public slots:
    void applyFilter(const QString& filterText);

private:
    QLineEdit* m_filterEdit = nullptr;
    QTableWidget* m_table = nullptr;
};

} // namespace dicom
