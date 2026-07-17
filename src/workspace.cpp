/*
 * SPDX-FileCopyrightText: 2026 Nate Peterson
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

#include "workspace.h"

namespace ghostwriterpp
{

Workspace::Workspace(QObject *parent)
    : QObject(parent)
{
}

bool Workspace::isWorkspaceFile(const QString &path)
{
    if (path.isEmpty()) {
        return false;
    }
    // suffix() is only after the last dot ("workspace"); the real extension is ".ghpp-workspace".
    const QString name = QFileInfo(path).fileName();
    return name.endsWith(QLatin1Char('.') + QLatin1String(fileSuffix), Qt::CaseInsensitive);
}

QString Workspace::fileDialogFilter()
{
    return tr("ghostwriter++ workspaces (*.%1);;All files (*)").arg(QLatin1String(fileSuffix));
}

bool Workspace::isAssociated() const
{
    return !m_filePath.isEmpty();
}

QString Workspace::filePath() const
{
    return m_filePath;
}

QString Workspace::displayName() const
{
    if (m_filePath.isEmpty()) {
        return QString();
    }
    return QFileInfo(m_filePath).completeBaseName();
}

QStringList Workspace::memberAbsolutePaths() const
{
    return m_members;
}

bool Workspace::contains(const QString &absolutePath) const
{
    const QString key = absoluteMemberKey(absolutePath);
    if (key.isEmpty()) {
        return false;
    }
    return m_members.contains(key);
}

bool Workspace::load(const QString &workspaceFilePath, QString *errorOut)
{
    const QString absWs = QFileInfo(workspaceFilePath).absoluteFilePath();
    QFile file(absWs);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorOut) {
            *errorOut = file.errorString();
        }
        return false;
    }

    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorOut) {
            *errorOut = parseError.error != QJsonParseError::NoError
                ? parseError.errorString()
                : tr("Workspace file is not a JSON object.");
        }
        return false;
    }

    const QJsonObject root = doc.object();
    const QJsonArray files = root.value(QStringLiteral("files")).toArray();

    QStringList loaded;
    loaded.reserve(files.size());
    QSet<QString> seen;

    for (const QJsonValue &value : files) {
        if (!value.isObject()) {
            continue;
        }
        const QString stored = value.toObject().value(QStringLiteral("path")).toString().trimmed();
        if (stored.isEmpty()) {
            continue;
        }

        QString resolved;
        const QFileInfo storedInfo(stored);
        if (storedInfo.isAbsolute()) {
            resolved = storedInfo.absoluteFilePath();
        } else {
            resolved = QFileInfo(QFileInfo(absWs).absoluteDir(), stored).absoluteFilePath();
        }

        const QString abs = absoluteMemberKey(resolved);
        if (abs.isEmpty() || seen.contains(abs)) {
            continue;
        }
        seen.insert(abs);
        loaded.append(abs);
    }

    const QString previousPath = m_filePath;
    m_filePath = absWs;
    m_members = loaded;

    if (previousPath != m_filePath) {
        emit associationChanged();
    }
    emit membershipChanged();
    return true;
}

bool Workspace::save(QString *errorOut)
{
    if (!isAssociated()) {
        if (errorOut) {
            *errorOut = tr("No workspace file is associated.");
        }
        return false;
    }
    return writeToPath(m_filePath, errorOut);
}

bool Workspace::saveAs(const QString &newPath, QString *errorOut)
{
    const QString absWs = QFileInfo(newPath).absoluteFilePath();
    const QString previousPath = m_filePath;
    m_filePath = absWs;

    if (!writeToPath(absWs, errorOut)) {
        m_filePath = previousPath;
        return false;
    }

    if (previousPath != absWs) {
        emit associationChanged();
    }
    emit membershipChanged();
    return true;
}

void Workspace::clear()
{
    const bool hadAssociation = isAssociated();
    const bool hadMembers = !m_members.isEmpty();
    m_filePath.clear();
    m_members.clear();
    if (hadAssociation) {
        emit associationChanged();
    }
    if (hadMembers || hadAssociation) {
        emit membershipChanged();
    }
}

bool Workspace::addMember(const QString &absolutePath, QString *errorOut)
{
    if (!isAssociated()) {
        return false;
    }

    const QString key = absoluteMemberKey(absolutePath);
    if (key.isEmpty() || m_members.contains(key)) {
        return false;
    }

    m_members.append(key);
    if (!save(errorOut)) {
        m_members.removeAll(key);
        return false;
    }

    emit membershipChanged();
    return true;
}

bool Workspace::removeMember(const QString &absolutePath, QString *errorOut)
{
    if (!isAssociated()) {
        return false;
    }

    const QString key = absoluteMemberKey(absolutePath);
    const int index = m_members.indexOf(key);
    if (key.isEmpty() || index < 0) {
        return false;
    }

    m_members.removeAt(index);
    if (!save(errorOut)) {
        m_members.insert(index, key);
        return false;
    }

    emit membershipChanged();
    return true;
}

void Workspace::setMembers(const QStringList &absolutePaths)
{
    QStringList next;
    QSet<QString> seen;
    for (const QString &path : absolutePaths) {
        const QString key = absoluteMemberKey(path);
        if (key.isEmpty() || seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        next.append(key);
    }
    m_members = next;
    emit membershipChanged();
}

void Workspace::copyStateFrom(const Workspace &other)
{
    const bool assocChanged = (m_filePath != other.m_filePath);
    const bool membersChanged = (m_members != other.m_members);
    m_filePath = other.m_filePath;
    m_members = other.m_members;
    if (assocChanged) {
        emit associationChanged();
    }
    if (membersChanged || assocChanged) {
        emit membershipChanged();
    }
}

QString Workspace::absoluteMemberKey(const QString &path) const
{
    if (path.isEmpty()) {
        return QString();
    }
    return QFileInfo(path).absoluteFilePath();
}

QString Workspace::pathForStorage(const QString &absolutePath) const
{
    const QString abs = absoluteMemberKey(absolutePath);
    if (abs.isEmpty() || m_filePath.isEmpty()) {
        return abs;
    }

    const QDir wsDir = QFileInfo(m_filePath).absoluteDir();
    QString rel = wsDir.relativeFilePath(abs);
    rel.replace(QLatin1Char('\\'), QLatin1Char('/'));

    if (rel.startsWith(QLatin1String("../")) || rel == QLatin1String("..") || QFileInfo(rel).isAbsolute()) {
        QString native = abs;
        native.replace(QLatin1Char('\\'), QLatin1Char('/'));
        return native;
    }

    return rel;
}

bool Workspace::writeToPath(const QString &workspaceFilePath, QString *errorOut)
{
    QJsonArray files;
    for (const QString &abs : std::as_const(m_members)) {
        QJsonObject entry;
        entry.insert(QStringLiteral("path"), pathForStorage(abs));
        files.append(entry);
    }

    QJsonObject root;
    root.insert(QStringLiteral("files"), files);
    root.insert(QStringLiteral("settings"), QJsonObject());

    QFile file(workspaceFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorOut) {
            *errorOut = file.errorString();
        }
        return false;
    }

    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        if (errorOut) {
            *errorOut = file.errorString();
        }
        file.close();
        return false;
    }

    file.close();
    return true;
}

} // namespace ghostwriterpp
