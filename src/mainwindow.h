/*
 * SPDX-FileCopyrightText: 2014-2026 Megan Conkle <megan.conkle@kdemail.net>
 * SPDX-FileCopyrightText: 2026 Nate Peterson
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QAction>
#include <QEvent>
#include <QLabel>
#include <QList>
#include <QMainWindow>
#include <QMap>
#include <QMetaObject>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStringLiteral>
#include <QTabBar>
#include <QToolButton>

#include <KActionCollection>
#include <KHelpMenu>
#include <KStandardAction>

#include "preview/htmlpreview.h"
#include "settings/appsettings.h"
#include "spelling/spellcheckdecorator.h"
#include "statistics/documentstatistics.h"
#include "statistics/documentstatisticswidget.h"
#include "statistics/sessionstatistics.h"
#include "statistics/sessionstatisticswidget.h"
#include "statistics/statisticsindicator.h"
#include "theme/svgicontheme.h"
#include "theme/theme.h"
#include "theme/themerepository.h"

#include "appactions.h"
#include "bookmark.h"
#include "documentmanager.h"
#include "documenttab.h"
#include "findreplace.h"
#include "folderviewwidget.h"
#include "outlinewidget.h"
#include "sidebar.h"
#include "timelabel.h"

class QShowEvent;

namespace ghostwriter
{
/**
 * Main window for the application.
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const QString &filePath = QString(), QWidget *parent = nullptr);
    virtual ~MainWindow();

protected:
    QSize sizeHint() const  override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *e) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;

private slots:
    void quitApplication();
    void changeTheme();
    void openPreferencesDialog();
    void toggleHemingwayMode(bool checked);
    void toggleFocusMode(bool checked);
    void toggleFullScreen(bool checked);
    void toggleHideMenuBarInFullScreen(bool checked);
    void toggleFileHistoryEnabled(bool checked);
    void toggleFolderViewShowAllFilesEnabled(bool checked);
    void toggleDisplayTimeInFullScreen(bool checked);
    void changeEditorWidth(EditorWidth editorWidth);
    void changeInterfaceStyle(InterfaceStyle style);
    void showQuickReferenceGuide();
    void showWikiPage();
    void changeFocusMode(FocusMode focusMode);
    void applyTheme();
    void refreshRecentFiles();
    void clearRecentFileHistory();
    void changeDocumentDisplayName(const QString &displayName);
    void onOperationStarted(const QString &description);
    void onOperationFinished();
    void changeFont();
    void onFontSizeChanged(int size);
    void onSetLocale();
    void copyHtml();
    void showPreviewOptions();
    void onAboutToHideMenuBarMenu();
    void onAboutToShowMenuBarMenu();
    void onSidebarVisibilityChanged(bool visible);
    void toggleSidebarVisible(bool visible);
    void runSpellCheck();

private:
    // Per-tab storage and chrome.
    QTabBar *tabBar;
    QStackedWidget *editorStack;
    QStackedWidget *previewStack;
    QWidget *editorEmptyPane = nullptr;
    QWidget *previewEmptyPane = nullptr;
    QList<DocumentTab *> tabs;
    int activeTabIndex;
    QToolButton *newTabButton;

    FindReplace* findReplace;
    QSplitter *splitter;
    ThemeRepository *themeRepo;
    Theme theme;
    QString language;
    Sidebar *sidebar;
    StatisticsIndicator *statisticsIndicator;
    QLabel *statusIndicator;
    TimeLabel *timeIndicator;
    FolderViewWidget *folderViewWidget = nullptr;
    OutlineWidget *outlineWidget;
    DocumentStatisticsWidget *documentStatsWidget;
    SessionStatistics *sessionStats;
    SessionStatisticsWidget *sessionStatsWidget;
    QListWidget *cheatSheetWidget;
    bool menuBarMenuActivated;
    bool sidebarHiddenForResize;
    bool focusModeEnabled;
    bool hemingwayModeEnabled;
    SvgIconTheme *primaryIconTheme;
    SvgIconTheme *secondaryIconTheme;

    QList<QAction *> recentFilesActions;
    QList<QWidget *> statusBarWidgets;
    QList<QMetaObject::Connection> perTabConnections;
    QActionGroup *layoutActionGroup;

    AppSettings *appSettings;

    AppActions *m_actions;
    KActionCollection *m_actionCollection;

    KHelpMenu *m_helpMenu;

    KActionCollection *actionCollection() const;

    QMenu *addMenuBarMenu(const QString &name);

    QAction *appAction(AppActions::ActionType actionType) const;

    // Per-tab accessors.
    DocumentTab *currentTab() const;
    MarkdownEditor *currentEditor() const;
    MarkdownDocument *currentDocument() const;
    DocumentManager *currentDocumentManager() const;
    HtmlPreview *currentHtmlPreview() const;
    DocumentStatistics *currentDocumentStats() const;

    // Tab management.
    DocumentTab *addDocumentTab(const Bookmark &location = Bookmark(), bool activate = true);
    void activateTab(int index);
    bool closeTabAt(int index);
    void wireActiveTab();
    void updateTabLabel(int index);
    void detachActiveTab(int index, bool wasActive);
    void removeTabWidgets(DocumentTab *tab, int index);

    // Multi-tab session persistence.
    void persistOpenTabs();
    BookmarkList loadPersistedTabs(int *activeOut) const;

    // Focus view.
    void applyFocusView(FocusView view);
    void syncFocusViewActions(FocusView view);

    void loadTheme();
    QString htmlPreviewStyleSheetForCurrentTheme() const;
    void applyHtmlPreviewStyleSheetToAllTabs(const QString &css);
    void setupActions();
    void setupRecentFileActions(const BookmarkList &recentFiles);
    void setupGui();
    void setupMenuBar();
    void setupStatusBar();
    void setupSidebar();
    void setupTabBar();

    void adjustEditor();
    void adjustTabBarHeight();
};
} // namespace ghostwriter

#endif
