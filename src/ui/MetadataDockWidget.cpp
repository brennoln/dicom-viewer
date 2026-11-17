#include "ui/MetadataDockWidget.h"

#include <QHeaderView>
#include <QVBoxLayout>
#include <QWidget>

namespace dicom {

MetadataDockWidget::MetadataDockWidget(QWidget* parent)
    : QDockWidget(QStringLiteral("Metadados DICOM"), parent) {
    setObjectName(QStringLiteral("metadataDock"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setMinimumWidth(290);

    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    m_filterEdit = new QLineEdit(container);
    m_filterEdit->setObjectName(QStringLiteral("metadataFilterEdit"));
    m_filterEdit->setPlaceholderText(QStringLiteral("Filtrar tags ou valores..."));
    m_filterEdit->setClearButtonEnabled(true);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &MetadataDockWidget::applyFilter);

    m_table = new QTableWidget(0, 3, container);
    m_table->setObjectName(QStringLiteral("metadataTable"));
    m_table->setHorizontalHeaderLabels({QStringLiteral("Tag"), QStringLiteral("Atributo"), QStringLiteral("Valor")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);

    layout->addWidget(m_filterEdit);
    layout->addWidget(m_table, 1);
    setWidget(container);
}

void MetadataDockWidget::setEntries(const QVector<DicomMetadataEntry>& entries) {
    m_table->setRowCount(0);
    m_table->setRowCount(entries.size());

    for (int i = 0; i < entries.size(); ++i) {
        const auto& entry = entries.at(i);
        m_table->setItem(i, 0, new QTableWidgetItem(entry.tag));
        m_table->setItem(i, 1, new QTableWidgetItem(entry.name));
        m_table->setItem(i, 2, new QTableWidgetItem(entry.value));
    }

    applyFilter(m_filterEdit->text());
}

void MetadataDockWidget::clearEntries() {
    m_table->setRowCount(0);
}

int MetadataDockWidget::rowCount() const {
    return m_table ? m_table->rowCount() : 0;
}

void MetadataDockWidget::applyFilter(const QString& filterText) {
    const QString needle = filterText.trimmed();
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (needle.isEmpty()) {
            m_table->setRowHidden(row, false);
            continue;
        }
        bool match = false;
        for (int col = 0; col < m_table->columnCount(); ++col) {
            if (const auto* item = m_table->item(row, col)) {
                if (item->text().contains(needle, Qt::CaseInsensitive)) {
                    match = true;
                    break;
                }
            }
        }
        m_table->setRowHidden(row, !match);
    }
}

} // namespace dicom
