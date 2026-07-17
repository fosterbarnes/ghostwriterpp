/*
 * SPDX-FileCopyrightText: 2020-2022 Megan Conkle <megan.conkle@kdemail.net>
 * SPDX-FileCopyrightText: 2026 Nate Peterson
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef SIDEBAR_H
#define SIDEBAR_H

#include <QFrame>
#include <QHideEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QScopedPointer>
#include <QShowEvent>

#include "settings/appsettings.h"

namespace ghostwriterpp
{
/**
 * Sidebar similar to VS Code's activity bar/sidebar.  Tab icons can sit in a
 * vertical strip on the left or a horizontal row on top (see
 * ActivityBarLocation).  At the trailing end of the activity bar are action
 * buttons (e.g. Settings).  For styling with style sheets, use QPushButton
 * and its various pseudo-states.  Object name is "sidebar".
 */
class SidebarPrivate;
class Sidebar : public QFrame
{
    Q_OBJECT
    Q_DECLARE_PRIVATE(Sidebar)

public:
    /**
     * Constructor.
     */
    Sidebar(QWidget *parent = nullptr);

    /**
     * Destructor
     */
    ~Sidebar();

    /**
     * Adds a new tab at the bottom of the tabs with the given glyph icon
     * to represent it, and its corresponding widget to be displayed
     * when the tab is selected.
     * 
     * A QObject object name can be optionally specified if, for example, a
     * different style needs to be applied to the tab (such as the font of the
     * glyph icon) from the other tabs within the style sheet.
     */
    void addTab(const QIcon &icon, QWidget *widget, const QString &tooltip = QString(), const QString &objectName = QString());

    /**
     * Inserts a new tab at the given tab index with the given glyph icon
     * to represent it, and its corresponding widget to be displayed
     * when the tab is selected.  Valid tab index values are from zero
     * to tabCount().
     * 
     * A QObject object name can be optionally specified if, for example, a
     * different style needs to be applied to the tab (such as the font of the
     * glyph icon) from the other tabs within the style sheet.
     */
    void insertTab(int index, const QIcon &icon, QWidget *widget, const QString &tooltip = QString(), const QString &objectName = QString());

    /**
     * Removes the tab and its corresponding widget at the given index.
     * Valid tab index values are from zero to tabCount() - 1.
     */
    void removeTab(int index);

    /**
     * Sets the currently selected tab to be the tab at the given
     * index, displaying its corresponding widget.  Valid tab index
     * values are from zero to tabCount() - 1.
     */
    void setCurrentTabIndex(int index);

    /**
     * Adds an action button with the given glyph icon at the bottom of the
     * sidebar, and returns a pointer to the button so that connections can be
     * made.  Note that ownership of the button belongs to the sidebar.
     * 
     * A QObject object name can be optionally specified if, for example, a
     * different style needs to be applied to the button (such as the font of
     * the glyph icon) from the other buttons within the style sheet.
     */
    QPushButton *addButton(const QIcon &icon, const QString &tooltip = QString(), const QString &objectName = QString());

    /**
     * Inserts an action button with the given glyph icon at the given index at
     * the bottom of the sidebar, and returns a pointer to the button so that
     * connections can be made.  Note that ownership of the button belongs to
     * the sidebar.  Valid button index values are from zero to buttonCount().
     * 
     * A QObject object name can be optionally specified if, for example, a
     * different style needs to be applied to the button (such as the font of
     * the glyph icon) from the other buttons within the style sheet.
     */
    QPushButton *insertButton(int index, const QIcon &icon, const QString &tooltip = QString(), const QString &objectName = QString());

    /**
     * Removes the action button at the given button index from the button
     * area of the bottom of the sidebar.  Valid button index values are from
     * zero to buttonCount() - 1.
     */
    void removeButton(int index);

    /**
     * Returns the number of tabs in the sidebar.
     */
    int tabCount() const;

    /**
     * Returns the number of action buttons in the button area of the
     * bottom of the sidebar.
     */
    int buttonCount() const;

    /**
     * Sets whether this sidebar should hide itself when it loses focus.
     */
    void setAutoHideEnabled(bool enabled);

    /**
     * Returns whether this sidebar will hide itself when it loses focus.
     */
    bool autoHideEnabled() const;

    /**
     * Sets where the activity bar (tab icons) is placed.
     */
    void setActivityBarLocation(ActivityBarLocation location);

    /**
     * Returns where the activity bar (tab icons) is placed.
     */
    ActivityBarLocation activityBarLocation() const;

    /**
     * Sets the icon for the top-bar overflow ("more") button.
     */
    void setOverflowIcon(const QIcon &icon);

signals:
    /**
     * Emitted when the sidebar's visibility has changed.
     */
    void visibilityChanged(bool visible);

protected slots:
    void onFocusChanged(QWidget *old, QWidget *now);

protected:
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QScopedPointer<SidebarPrivate> d_ptr;
};
} // namespace ghostwriterpp

#endif // SIDEBAR_H
