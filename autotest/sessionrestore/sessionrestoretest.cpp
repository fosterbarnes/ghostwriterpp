/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include "../../src/sessionrestore.h"

using namespace ghostwriterpp;

class SessionRestoreTest : public QObject
{
    Q_OBJECT

private slots:
    void activeIdentity_data();
    void activeIdentity();
};

void SessionRestoreTest::activeIdentity_data()
{
    QTest::addColumn<QStringList>("entries");
    QTest::addColumn<int>("savedActive");
    QTest::addColumn<QStringList>("expectedFiles");
    QTest::addColumn<QString>("expectedActive");
    QTest::newRow("missing-before-active")
        << QStringList{"missing.md", "first.md", "second.md"} << 2
        << QStringList{"first.md", "second.md"} << QString("second.md");
    QTest::newRow("duplicate-active")
        << QStringList{"first.md", "second.md", "first.md"} << 2
        << QStringList{"first.md", "second.md"} << QString("first.md");
    QTest::newRow("missing-active")
        << QStringList{"first.md", "missing.md", "second.md"} << 1
        << QStringList{"first.md", "second.md"} << QString("missing.md");
    QTest::newRow("empty-before-active")
        << QStringList{"", "first.md", "second.md"} << 1
        << QStringList{"first.md", "second.md"} << QString("first.md");
    QTest::newRow("invalid-index")
        << QStringList{"first.md", "second.md"} << 99
        << QStringList{"first.md", "second.md"} << QString();
    QTest::newRow("no-active")
        << QStringList{"first.md", "second.md"} << -1
        << QStringList{"first.md", "second.md"} << QString();
}

void SessionRestoreTest::activeIdentity()
{
    QFETCH(QStringList, entries);
    QFETCH(int, savedActive);
    QFETCH(QStringList, expectedFiles);
    QFETCH(QString, expectedActive);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    for (const QString &name : {QString("first.md"), QString("second.md")}) {
        QFile file(directory.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("example"), qint64(7));
    }

    QSettings settings(directory.filePath("session.ini"), QSettings::IniFormat);
    settings.beginWriteArray("Session/openTabs", entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue("filePath", entries[i].isEmpty()
            ? QString() : directory.filePath(entries[i]));
        settings.setValue("cursor", i * 10);
    }
    settings.endArray();
    settings.setValue("Session/activeTab", savedActive);
    settings.sync();
    QCOMPARE(settings.status(), QSettings::NoError);

    QString activePath;
    const BookmarkList bookmarks = loadPersistedTabs(settings, activePath);
    QCOMPARE(bookmarks.size(), expectedFiles.size());
    for (int i = 0; i < bookmarks.size(); ++i) {
        QCOMPARE(bookmarks[i].filePath(), directory.filePath(expectedFiles[i]));
        QCOMPARE(bookmarks[i].cursorPosition(), entries.indexOf(expectedFiles[i]) * 10);
    }
    QCOMPARE(activePath, expectedActive.isEmpty()
        ? QString() : directory.filePath(expectedActive));
}

QTEST_GUILESS_MAIN(SessionRestoreTest)
#include "sessionrestoretest.moc"
