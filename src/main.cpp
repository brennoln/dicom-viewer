#include "ui/MainWindow.h"
#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("DicomViewer"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Visualizador de Imagens DICOM com Qt e C++17"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("arquivo"), QStringLiteral("Arquivo DICOM (.dcm) opcional para abrir ao iniciar."));
    parser.process(app);

    dicom::MainWindow window;
    window.show();

    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        window.loadDicomFile(args.first(), true);
    }

    return app.exec();
}
