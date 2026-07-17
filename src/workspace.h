/*
 * SPDX-FileCopyrightText: 2026 Nate Peterson
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef WORKSPACE_H
#define WORKSPACE_H

#include <QObject>
#include <QString>
#include <QStringList>

namespace ghostwriterpp
{
/**
 * Named workspace backed by a .ghpp-workspace JSON file.
 * Membership is independent of open editor tabs.
 */
class Workspace : public QObject
{
    Q_OBJECT

public:
    static constexpr const char *fileSuffix = "ghpp-workspace";

    explicit Workspace(QObject *parent = nullptr);

    static bool isWorkspaceFile(const QString &path);
    static QString fileDialogFilter();

    bool isAssociated() const;
    QString filePath() const;
    QString displayName() const;

    QStringList memberAbsolutePaths() const;
    bool contains(const QString &absolutePath) const;

    bool load(const QString &workspaceFilePath, QString *errorOut = nullptr);
    bool save(QString *errorOut = nullptr);
    bool saveAs(const QString &newPath, QString *errorOut = nullptr);
    void clear();

    bool addMember(const QString &absolutePath, QString *errorOut = nullptr);
    bool removeMember(const QString &absolutePath, QString *errorOut = nullptr);
    void setMembers(const QStringList &absolutePaths);
    void copyStateFrom(const Workspace &other);

signals:
    void membershipChanged();
    void associationChanged();

private:
    QString m_filePath;
    QStringList m_members;

    QString absoluteMemberKey(const QString &path) const;
    QString pathForStorage(const QString &absolutePath) const;
    bool writeToPath(const QString &workspaceFilePath, QString *errorOut);
};
} // namespace ghostwriterpp

#endif // WORKSPACE_H
