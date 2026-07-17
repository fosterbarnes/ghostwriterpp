/*
 * SPDX-FileCopyrightText: 2026 Nate Peterson
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef WORKSPACE_VIEW_WIDGET_H
#define WORKSPACE_VIEW_WIDGET_H

#include <QScopedPointer>
#include <QStringList>
#include <QWidget>

namespace ghostwriterpp
{
class Workspace;

/**
 * Sidebar list of workspace member files (independent of open tabs).
 */
class WorkspaceViewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit WorkspaceViewWidget(QWidget *parent = nullptr);
    ~WorkspaceViewWidget() override;

    void setWorkspace(Workspace *workspace);
    void refresh();
    QStringList selectedMemberPaths() const;

signals:
    void fileSelected(const QString &filePath);
    void addFilesRequested();
    void removeSelectedRequested();
    void openWorkspaceRequested();
    void saveWorkspaceAsRequested();

private:
    class Private;
    QScopedPointer<Private> d;
};
} // namespace ghostwriterpp

#endif // WORKSPACE_VIEW_WIDGET_H
