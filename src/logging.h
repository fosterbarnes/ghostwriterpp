/*
 * SPDX-FileCopyrightText: 2024 Megan Conkle <megan.conkle@kdemail.net>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef LOGGING_H
#define LOGGING_H

#include <QLoggingCategory>
#include <QMessageLogContext>
#include <QString>
#include <QtDebug>

#include <iostream>

namespace ghostwriterpp
{
/**
 * Message handler function for directing Qt qDebug(), qWarning(), qError(),
 * etc., messages to stdout/stderr.  Install this function by passing it to
 * qInstallMessageHandler().
 */
void logMessage(QtMsgType type, const QMessageLogContext &context, const QString &message);

/**
 * Duplicate all formatted log lines to this file (append). Pass an empty path
 * to disable. Used with --debug-log / --log-file or GHOSTWRITER_LOG_FILE.
 */
void setLogMirrorFilePath(const QString &path);
} // namespace ghostwriterpp

#endif // LOGGING_H