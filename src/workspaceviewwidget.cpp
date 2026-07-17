/*
 * SPDX-FileCopyrightText: 2026 Nate Peterson
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <QAbstractItemView>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPalette>
#include <QPushButton>
#include <QVBoxLayout>

#include "workspace.h"
#include "workspaceviewwidget.h"

namespace ghostwriterpp
{

class WorkspaceViewWidget::Private
{
public:
    Workspace *workspace = nullptr;
    QLabel *emptyLabel = nullptr;
    QListWidget *list = nullptr;
    QWidget *listPane = nullptr;
    QPushButton *addButton = nullptr;
    QPushButton *removeButton = nullptr;
    QPushButton *openButton = nullptr;
    QPushButton *saveAsButton = nullptr;
};

WorkspaceViewWidget::WorkspaceViewWidget(QWidget *parent)
    : QWidget(parent),
      d(new Private)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(6);

    d->emptyLabel = new QLabel(this);
    d->emptyLabel->setWordWrap(true);
    d->emptyLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    root->addWidget(d->emptyLabel);

    d->listPane = new QWidget(this);
    auto *listLayout = new QVBoxLayout(d->listPane);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(4);

    d->list = new QListWidget(d->listPane);
    d->list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    d->list->setAlternatingRowColors(false);
    d->list->setToolTip(tr("Double-click or press Enter to open"));
    listLayout->addWidget(d->list, 1);

    auto *row = new QHBoxLayout();
    d->addButton = new QPushButton(tr("Add..."), d->listPane);
    d->removeButton = new QPushButton(tr("Remove"), d->listPane);
    row->addWidget(d->addButton);
    row->addWidget(d->removeButton);
    listLayout->addLayout(row);

    root->addWidget(d->listPane, 1);

    auto *emptyRow = new QHBoxLayout();
    d->openButton = new QPushButton(tr("Open Workspace..."), this);
    d->saveAsButton = new QPushButton(tr("Save Workspace As..."), this);
    emptyRow->addWidget(d->openButton);
    emptyRow->addWidget(d->saveAsButton);
    root->addLayout(emptyRow);

    connect(d->list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (!item) {
            return;
        }
        const QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty()) {
            emit fileSelected(path);
        }
    });
    connect(d->addButton, &QPushButton::clicked, this, &WorkspaceViewWidget::addFilesRequested);
    connect(d->removeButton, &QPushButton::clicked, this, &WorkspaceViewWidget::removeSelectedRequested);
    connect(d->openButton, &QPushButton::clicked, this, &WorkspaceViewWidget::openWorkspaceRequested);
    connect(d->saveAsButton, &QPushButton::clicked, this, &WorkspaceViewWidget::saveWorkspaceAsRequested);

    refresh();
}

WorkspaceViewWidget::~WorkspaceViewWidget() = default;

void WorkspaceViewWidget::setWorkspace(Workspace *workspace)
{
    d->workspace = workspace;
    refresh();
}

void WorkspaceViewWidget::refresh()
{
    d->list->clear();

    const bool associated = d->workspace && d->workspace->isAssociated();
    d->listPane->setVisible(associated);
    d->emptyLabel->setVisible(!associated);
    d->openButton->setVisible(!associated);
    d->saveAsButton->setVisible(true);

    if (!associated) {
        d->emptyLabel->setText(tr("No workspace open.\nOpen a workspace file or save the current files as a workspace."));
        d->removeButton->setEnabled(false);
        return;
    }

    d->emptyLabel->clear();
    const QColor missingColor = palette().color(QPalette::Disabled, QPalette::Text);
    const QStringList members = d->workspace->memberAbsolutePaths();
    for (const QString &path : members) {
        const QFileInfo info(path);
        auto *item = new QListWidgetItem(info.fileName(), d->list);
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
        if (!info.exists()) {
            item->setForeground(missingColor);
            item->setText(tr("%1 (missing)").arg(info.fileName()));
        }
    }

    d->removeButton->setEnabled(d->list->count() > 0);
}

QStringList WorkspaceViewWidget::selectedMemberPaths() const
{
    QStringList paths;
    const QList<QListWidgetItem *> items = d->list->selectedItems();
    for (QListWidgetItem *item : items) {
        const QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty()) {
            paths.append(path);
        }
    }
    return paths;
}

} // namespace ghostwriterpp
