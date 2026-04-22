/*
 * SPDX-FileCopyrightText: 2024 Megan Conkle <megan.conkle@kdemail.net>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "logging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>
#include <QtLogging>

#include <memory>

namespace ghostwriterpp
{
namespace
{
QMutex s_mirrorMutex;
std::unique_ptr<QFile> s_mirrorFile;
} // namespace

void setLogMirrorFilePath(const QString &path)
{
    QMutexLocker locker(&s_mirrorMutex);
    s_mirrorFile.reset();
    if (path.isEmpty()) {
        return;
    }

    auto f = std::make_unique<QFile>(path);
    if (!f->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }

    QTextStream ts(f.get());
    ts << "\n--- ghostwriter++ log " << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << " ---\n";
    ts.flush();
    s_mirrorFile = std::move(f);
}

void logMessage(QtMsgType type, const QMessageLogContext &context, const QString &message)

{
    FILE *dest = stdout;
    QMessageLogContext shortContext(context.file, context.line, context.function, context.category);

    if ((QtFatalMsg == type) || (QtCriticalMsg == type) || (QtWarningMsg == type)) {
        dest = stderr;
    }

    if (context.file != nullptr) {
        auto filePath = QDir::fromNativeSeparators(context.file);
        auto srcDirIndex = filePath.lastIndexOf("/src/");
        auto offset = 5;

        if (srcDirIndex < 0) {
            srcDirIndex = 0;

            if (filePath.startsWith("src/")) {
                offset = 4;
            } else {
                offset = 0;
            }
        }

        if (offset > 0) {
            filePath = filePath.right(filePath.length() - (srcDirIndex + offset));
        }

        shortContext.file = filePath.toUtf8().data();
    }

    QTextStream stream(dest);

    auto text = qFormatLogMessage(type, shortContext, message);
    stream << text << Qt::endl;
    stream.flush();

    {
        QMutexLocker locker(&s_mirrorMutex);
        if (s_mirrorFile && s_mirrorFile->isOpen()) {
            QTextStream mstream(s_mirrorFile.get());
            mstream << text << Qt::endl;
            mstream.flush();
        }
    }

    if (QtFatalMsg == type) {
        stream << Qt::endl << "    GAME OVER" << Qt::endl;
        stream << "   Insert Coin" << Qt::endl << Qt::endl;
        stream.flush();
        {
            QMutexLocker locker(&s_mirrorMutex);
            if (s_mirrorFile && s_mirrorFile->isOpen()) {
                QTextStream mstream(s_mirrorFile.get());
                mstream << Qt::endl << "GAME OVER (qFatal)" << Qt::endl;
                mstream.flush();
            }
        }
        abort();
    }
}
} // namespace ghostwriterpp