/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

#include "../../src/export/exporterfactory.h"

using namespace ghostwriterpp;

class ExporterFactoryTest : public QObject
{
    Q_OBJECT

private slots:
    void discovery();
};

void ExporterFactoryTest::discovery()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString fixture = QCoreApplication::applicationDirPath() + "/versionprobe.exe";
    const bool missingTool = qEnvironmentVariableIsSet("GW_TEST_MISSING_TOOL");
    const QString selectedName = qEnvironmentVariable("GW_TEST_EXPORTER", "cmark");

    for (const QString &tool : {QString("pandoc"), QString("multimarkdown"), QString("cmark")}) {
        if (missingTool && tool == "multimarkdown") {
            continue;
        }
        QVERIFY(QFile::copy(fixture, directory.filePath(tool + ".exe")));
    }

    const QByteArray oldPath = qgetenv("PATH");
    const auto restorePath = qScopeGuard([oldPath]() { qputenv("PATH", oldPath); });
    qputenv("PATH", directory.path().toUtf8());

    const QString logPath = directory.filePath("probes.log");
    auto probes = [&logPath]() {
        QFile log(logPath);
        if (!log.exists()) {
            return QStringList();
        }
        if (!log.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qFatal("Could not read version-probe log");
        }
        return QString::fromUtf8(log.readAll()).split('\n', Qt::SkipEmptyParts);
    };

    auto *factory = ExporterFactory::instance();
    auto *builtIn = factory->exporterByName("cmark-gfm");
    QVERIFY(builtIn);
    QVERIFY(!factory->exporterByName("Unknown exporter"));
    QVERIFY(probes().isEmpty());

    auto *selected = factory->exporterByName(selectedName);
    if (missingTool) {
        QVERIFY(!selected);
        selected = factory->exporterByName("cmark-gfm");
        QVERIFY(QFile::copy(fixture, directory.filePath("multimarkdown.exe")));
    } else {
        QVERIFY(selected);
    }
    selected->setOptions("--test-preserved-options");

    QStringList initialProbes;
    if (!missingTool && selectedName != "cmark-gfm") {
        initialProbes.append(selectedName.startsWith("Pandoc") ? "pandoc"
            : selectedName == "MultiMarkdown" ? "multimarkdown" : "cmark");
    }
    QCOMPARE(probes(), initialProbes);
    QCOMPARE(factory->exporterByName(selectedName), missingTool ? nullptr : selected);
    QCOMPARE(probes(), initialProbes);

    const auto htmlExporters = factory->htmlExporters();
    QStringList names;
    for (auto *exporter : htmlExporters) {
        names.append(exporter->name());
    }
    QStringList expectedNames{"cmark-gfm", "Pandoc", "Pandoc CommonMark",
        "Pandoc GitHub-flavored Markdown", "Pandoc PHP Markdown Extra",
        "Pandoc MultiMarkdown", "Pandoc Strict", "MultiMarkdown", "cmark"};
    if (missingTool) {
        expectedNames.removeAll("MultiMarkdown");
    }
    QCOMPARE(names, expectedNames);
    QCOMPARE(htmlExporters.first(), builtIn);
    QCOMPARE(factory->fileExporters(), htmlExporters);
    QCOMPARE(factory->htmlExporters(), htmlExporters);
    QCOMPARE(factory->exporterByName(selected->name()), selected);
    QCOMPARE(selected->options(), QString("--test-preserved-options"));

    const QStringList finalProbes = probes();
    QCOMPARE(finalProbes.count("pandoc"), 1);
    QCOMPARE(finalProbes.count("multimarkdown"), missingTool ? 0 : 1);
    QCOMPARE(finalProbes.count("cmark"), 1);
    QCOMPARE(finalProbes.size(), missingTool ? 2 : 3);
}

QTEST_GUILESS_MAIN(ExporterFactoryTest)
#include "exporterfactorytest.moc"
