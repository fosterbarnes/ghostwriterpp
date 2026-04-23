/*
 * SPDX-FileCopyrightText: 2014-2026 Megan Conkle <megan.conkle@kdemail.net>
 * SPDX-FileCopyrightText: 2009-2014 Graeme Gott <graeme@gottcode.org>
 * SPDX-FileCopyrightText: 2026 Nate Peterson
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDialog>
#include <QGridLayout>
#include <QGuiApplication>
#include <QIODevice>
#include <QIcon>
#include <QImageReader>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMimeDatabase>
#include <QMimeType>
#include <QPushButton>
#include <QHash>
#include <QScrollBar>
#include <QScreen>
#include <QSet>
#include <QSettings>
#include <QShowEvent>
#include <QSizePolicy>
#include <QTextCursor>
#include <QStatusBar>
#include <QStyle>
#include <QStyleFactory>
#include <QTextDocumentFragment>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWhatsThis>

#include <KAboutData>
#include <KActionCollection>
#include <KHelpMenu>
#include <KStandardAction>

#include "export/exporter.h"
#include "export/exporterfactory.h"
#include "settings/fontsettingsdialog.h"
#include "settings/localedialog.h"
#include "settings/preferencesdialog.h"
#include "settings/previewoptionsdialog.h"
#include "theme/stylesheetbuilder.h"
#include "theme/themeselectiondialog.h"
#include "spelling/spellcheckdecorator.h"
#include "spelling/spellcheckdialog.h"

#include "documenttabbar.h"
#include "findreplace.h"
#include "library.h"
#include "mainwindow.h"
#include "messageboxhelper.h"
#include "windowframechrome.h"

#define GW_MAIN_WINDOW_GEOMETRY_KEY "Window/mainWindowGeometry"
#define GW_MAIN_WINDOW_STATE_KEY "Window/mainWindowState"
#define GW_SPLITTER_GEOMETRY_KEY "Window/splitterGeometry"
#define GW_SESSION_OPEN_TABS_KEY "Session/openTabs"
#define GW_SESSION_ACTIVE_TAB_KEY "Session/activeTab"
#define GW_SESSION_TAB_PATH_KEY "filePath"
#define GW_SESSION_TAB_CURSOR_KEY "cursor"

#define MAX_RECENT_FILES (AppActions::OpenLeastRecent - AppActions::OpenMostRecent + 1)

namespace ghostwriterpp
{
using namespace std::placeholders;

namespace {
int layoutScreenWidth()
{
    if (QScreen *s = QGuiApplication::primaryScreen()) {
        return s->size().width();
    }
    const QList<QScreen *> screens = QGuiApplication::screens();
    if (!screens.isEmpty()) {
        return screens.first()->size().width();
    }
    return 1280;
}

int layoutScreenAvailableWidth()
{
    if (QScreen *s = QGuiApplication::primaryScreen()) {
        return s->availableSize().width();
    }
    const QList<QScreen *> screens = QGuiApplication::screens();
    if (!screens.isEmpty()) {
        return screens.first()->availableSize().width();
    }
    return layoutScreenWidth();
}

void ensureTopLevelVisibleOnScreen(QWidget *w)
{
    if (!w || !w->isWindow()) {
        return;
    }

    Qt::WindowStates st = w->windowState();
    if (st & Qt::WindowMinimized) {
        w->setWindowState(st & ~Qt::WindowMinimized);
    }

    if (w->isMaximized() || w->isFullScreen()) {
        w->raise();
        w->activateWindow();
        return;
    }

    QRect frame = w->frameGeometry();
    if (frame.width() < 64 || frame.height() < 64) {
        const QSize cap(1200, 900);
        QSize ns = w->sizeHint().expandedTo(w->minimumSize());
        ns = ns.boundedTo(cap).expandedTo(QSize(400, 300));
        w->resize(ns);
        frame = w->frameGeometry();
    }

    for (QScreen *s : QGuiApplication::screens()) {
        if (s->availableGeometry().intersects(frame)) {
            w->raise();
            w->activateWindow();
            return;
        }
    }

    QScreen *ps = QGuiApplication::primaryScreen();
    QRect avail = ps ? ps->availableGeometry() : QRect(0, 0, 1280, 720);
    QSize sh = w->sizeHint().expandedTo(w->minimumSize()).boundedTo(avail.size());
    sh = sh.expandedTo(QSize(400, 300));
    w->setGeometry(QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter, sh, avail));
    w->raise();
    w->activateWindow();
}
} // namespace

static QString absoluteFileKey(const QString &path)
{
    return QFileInfo(path).absoluteFilePath();
}

enum SidebarTabIndex {
    FirstSidebarTab,
    FolderViewSidebarTab = FirstSidebarTab,
    OutlineSidebarTab,
    SessionStatsSidebarTab,
    DocumentStatsSidebarTab,
    CheatSheetSidebarTab,
    LastSidebarTab = CheatSheetSidebarTab
};

MainWindow::MainWindow(const QString &filePath, QWidget *parent)
    : QMainWindow(parent),
      tabBar(nullptr),
      editorStack(nullptr),
      previewStack(nullptr),
      activeTabIndex(-1),
      newTabButton(nullptr),
      layoutActionGroup(nullptr)
{
    Bookmark fileToOpen(filePath);

    focusModeEnabled = false;
    hemingwayModeEnabled = false;
    sidebarHiddenForResize = false;
    appSettings = AppSettings::instance();

    loadTheme();
    m_actionCollection = new KActionCollection(this);
    m_actions = new AppActions(actionCollection(), primaryIconTheme, this);

    setupGui();
    setupActions();

    // Tab bar height should match the menu bar row.
    adjustTabBarHeight();

    if (!fileToOpen.isValid() && !fileToOpen.isNull()) {
        QFile file(fileToOpen.filePath());
        if (!file.open(QIODevice::WriteOnly)) {
            fileToOpen = Bookmark();
            MessageBoxHelper::critical(this, tr("Could not create file: %1").arg(filePath), file.errorString());
        } else {
            file.close();
        }
    }

    connect(appSettings, &AppSettings::focusModeChanged, this, &MainWindow::changeFocusMode);
    connect(appSettings, &AppSettings::hideMenuBarInFullScreenChanged, this, &MainWindow::toggleHideMenuBarInFullScreen);
    connect(appSettings, &AppSettings::fileHistoryChanged, this, &MainWindow::toggleFileHistoryEnabled);
    connect(appSettings, &AppSettings::folderViewShowAllFilesChanged, this, &MainWindow::toggleFolderViewShowAllFilesEnabled);
    connect(appSettings, &AppSettings::displayTimeInFullScreenChanged, this, &MainWindow::toggleDisplayTimeInFullScreen);
    connect(appSettings, &AppSettings::editorWidthChanged, this, &MainWindow::changeEditorWidth);
    connect(appSettings, &AppSettings::interfaceStyleChanged, this, &MainWindow::changeInterfaceStyle);
    connect(appSettings, &AppSettings::editorFontChanged, this, [this](const QFont &font) {
        for (auto *tab : tabs) {
            if (tab->editor()) {
                tab->editor()->setFont(font.family(), font.pointSize());
            }
        }
    });
    connect(appSettings, &AppSettings::editorFontChanged, this, &MainWindow::applyTheme);
    connect(appSettings, &AppSettings::previewTextFontChanged, this, &MainWindow::applyTheme);
    connect(appSettings, &AppSettings::previewCodeFontChanged, this, &MainWindow::applyTheme);
    connect(appSettings, &AppSettings::focusViewChanged, this, [this](FocusView view) {
        applyFocusView(view);
        syncFocusViewActions(view);
    });

    connect(folderViewWidget, &FolderViewWidget::fileSelected, this, [this](const QString &filePath) {
        addDocumentTab(Bookmark(filePath));
    });

    qApp->installEventFilter(this);

    toggleHideMenuBarInFullScreen(appSettings->hideMenuBarInFullScreenEnabled());
    menuBarMenuActivated = false;

    qApp->processEvents();

    show();

    applyTheme();
    adjustEditor();
    adjustTabBarHeight();

    qApp->processEvents();

    // Restore persisted multi-tab session (if enabled), then layer any CLI
    // file on top. Always end up with at least one active tab.
    int savedActiveIndex = -1;
    BookmarkList persisted;

    if (appSettings->restoreSessionEnabled()) {
        persisted = loadPersistedTabs(&savedActiveIndex);
    }

    for (const Bookmark &bm : std::as_const(persisted)) {
        addDocumentTab(bm, /*activate=*/ false);
    }

    if (fileToOpen.isValid()) {
        addDocumentTab(fileToOpen, /*activate=*/ true);
    } else if (!tabs.isEmpty()) {
        int idx = savedActiveIndex;
        if (idx < 0 || idx >= tabs.size()) {
            idx = 0;
        }
        if (tabBar->currentIndex() != idx) {
            tabBar->setCurrentIndex(idx);
        } else {
            activateTab(idx);
        }
    }

    if (tabs.isEmpty()) {
        auto *seed = addDocumentTab(Bookmark(), /*activate=*/ true);
        if (seed && seed->documentManager()) {
            seed->documentManager()->createUntitled();
        }
    }

    applyFocusView(appSettings->focusView());
    syncFocusViewActions(appSettings->focusView());

    QString previewSheet = htmlPreviewStyleSheetForCurrentTheme();
    if (previewSheet.isNull()) {
        qCritical() << "Invalid HTML preview style sheet provided.";
    } else {
        applyHtmlPreviewStyleSheetToAllTabs(previewSheet);
    }

    ensureTopLevelVisibleOnScreen(this);
}

MainWindow::~MainWindow()
{
    qDeleteAll(tabs);
    tabs.clear();

    if (primaryIconTheme) {
        delete primaryIconTheme;
        primaryIconTheme = nullptr;
    }

    if (secondaryIconTheme) {
        delete secondaryIconTheme;
        secondaryIconTheme = nullptr;
    }
}

QSize MainWindow::sizeHint() const
{
    return QSize(800, 500);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    int width = event->size().width();

    if (width < (0.5 * layoutScreenWidth())) {
        this->sidebar->setVisible(false);
        this->sidebar->setAutoHideEnabled(true);
        this->sidebarHiddenForResize = true;
    }
    else {
        this->sidebarHiddenForResize = false;

        if (!this->focusModeEnabled && this->appSettings->sidebarVisible()) {
            this->sidebar->setAutoHideEnabled(false);
            this->sidebar->setVisible(true);
        }
        else {
            this->sidebar->setAutoHideEnabled(true);
            this->sidebar->setVisible(false);
        }
    }

    adjustEditor();
}

void MainWindow::keyPressEvent(QKeyEvent *e)
{
    int key = e->key();

    switch (key) {
    case Qt::Key_Escape:
    case Qt::Key_F11:
        if (this->isFullScreen()) {
            toggleFullScreen(false);
        }
        break;
    case Qt::Key_Alt:
        if (this->isFullScreen() && appSettings->hideMenuBarInFullScreenEnabled()) {
            if (!this->menuBar()->isVisible()) {
                this->menuBar()->show();
            } else {
                this->menuBar()->hide();
            }
        }
        break;
    case Qt::Key_Tab:
        if (findReplace->isVisible() && findReplace->hasFocus()) {
            findReplace->keyPressEvent(e);
            return;
        }
        else if (currentEditor() && !currentEditor()->hasFocus()) {
            QMainWindow::keyPressEvent(e);
        }
        break;
    default:
        break;
    }

    QMainWindow::keyPressEvent(e);
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (this->isFullScreen() && appSettings->hideMenuBarInFullScreenEnabled()) {
        if ((this->menuBar() == obj)
                && (QEvent::Leave == event->type())
                && !menuBarMenuActivated) {
            this->menuBar()->hide();
        } else if (QEvent::MouseMove == event->type()) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);

            if ((mouseEvent->globalPosition().y() <= 0) && !this->menuBar()->isVisible()) {
                this->menuBar()->show();
            }
        } else if ((this == obj)
                && (((QEvent::Leave == event->type()) && !menuBarMenuActivated)
                    || (QEvent::WindowDeactivate == event->type()))) {
            this->menuBar()->hide();
        }
    }

    return false;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Save prompts only — do not call DocumentManager::close(), which clears
    // each tab; persistOpenTabs() must still see file paths on quit.
    for (int i = tabs.size() - 1; i >= 0; --i) {
        if (!tabs[i]->documentManager()->prepareApplicationQuit()) {
            event->ignore();
            return;
        }
    }

    event->accept();
    this->quitApplication();
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    applyDarkModeToWindowFrame(this, appSettings->darkModeEnabled());
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        applyDarkModeToWindowFrame(this, appSettings->darkModeEnabled());
    }
}

void MainWindow::quitApplication()
{
    persistOpenTabs();
    appSettings->store();

    QSettings windowSettings;

    windowSettings.setValue(GW_MAIN_WINDOW_GEOMETRY_KEY, saveGeometry());
    windowSettings.setValue(GW_MAIN_WINDOW_STATE_KEY, saveState());
    windowSettings.setValue(GW_SPLITTER_GEOMETRY_KEY, splitter->saveState());
    windowSettings.sync();

    for (auto *tab : tabs) {
        if (tab->editor()) {
            tab->editor()->document()->disconnect();
            tab->editor()->disconnect();
        }
        if (tab->htmlPreview()) {
            tab->htmlPreview()->disconnect();
        }
    }

    for (auto *tab : tabs) {
        HtmlPreview *preview = tab->htmlPreview();
        if (preview) {
            previewStack->removeWidget(preview);
            tab->releaseHtmlPreview();
        }
    }

    StyleSheetBuilder::clearCache();

    qApp->quit();
}

void MainWindow::changeTheme()
{
    ThemeSelectionDialog *themeDialog = new ThemeSelectionDialog(theme.name(), appSettings->darkModeEnabled(), this);
    themeDialog->setAttribute(Qt::WA_DeleteOnClose);

    this->connect(themeDialog, &ThemeSelectionDialog::finished, themeDialog, [this, themeDialog](int result) {
        Q_UNUSED(result)
        this->theme = themeDialog->theme();
        applyTheme();
    });

    themeDialog->open();
}

void MainWindow::openPreferencesDialog()
{
    PreferencesDialog *preferencesDialog = new PreferencesDialog(this);
    preferencesDialog->setAttribute(Qt::WA_DeleteOnClose);
    preferencesDialog->show();
}

void MainWindow::toggleHemingwayMode(bool checked)
{
    hemingwayModeEnabled = checked;

    for (auto *tab : tabs) {
        if (tab->editor()) {
            tab->editor()->setHemingWayModeEnabled(checked);
        }
    }
}

void MainWindow::toggleFocusMode(bool checked)
{
    this->focusModeEnabled = checked;

    const FocusMode mode = checked ? appSettings->focusMode() : FocusModeDisabled;

    for (auto *tab : tabs) {
        if (tab->editor()) {
            tab->editor()->setFocusMode(mode);
        }
    }

    if (checked) {
        sidebar->setVisible(false);
        sidebar->setAutoHideEnabled(true);
    } else if (!this->sidebarHiddenForResize && this->appSettings->sidebarVisible()) {
        sidebar->setAutoHideEnabled(false);
        sidebar->setVisible(true);
    }
}

void MainWindow::toggleFullScreen(bool checked)
{
    static bool lastStateWasMaximized = false;

    if (this->isFullScreen() || !checked) {
        if (appSettings->displayTimeInFullScreenEnabled()) {
            timeIndicator->hide();
        }

        if (lastStateWasMaximized) {
            showMaximized();
        } else {
            showNormal();
        }

        if (appSettings->hideMenuBarInFullScreenEnabled()) {
            this->menuBar()->show();
        }
    } else {
        if (appSettings->displayTimeInFullScreenEnabled()) {
            timeIndicator->show();
        }

        lastStateWasMaximized = this->isMaximized();

        showFullScreen();

        if (appSettings->hideMenuBarInFullScreenEnabled()) {
            this->menuBar()->hide();
        }
    }
}

void MainWindow::toggleHideMenuBarInFullScreen(bool checked)
{
    if (this->isFullScreen()) {
        if (checked) {
            this->menuBar()->hide();
        } else {
            this->menuBar()->show();
        }
    }
}

void MainWindow::toggleFileHistoryEnabled(bool checked)
{
    if (!checked) {
        this->clearRecentFileHistory();
    }

    for (auto *tab : tabs) {
        tab->documentManager()->setFileHistoryEnabled(checked);
    }
}

void MainWindow::toggleFolderViewShowAllFilesEnabled(bool checked)
{
    if (folderViewWidget != nullptr) {
        folderViewWidget->setShowAllFiles(checked);
    }
}

void MainWindow::toggleDisplayTimeInFullScreen(bool checked)
{
    if (this->isFullScreen()) {
        if (checked) {
            this->timeIndicator->show();
        } else {
            this->timeIndicator->hide();
        }
    }
}

void MainWindow::changeEditorWidth(EditorWidth editorWidth)
{
    for (auto *tab : tabs) {
        if (tab->editor()) {
            tab->editor()->setEditorWidth(editorWidth);
        }
    }

    adjustEditor();

    QString previewSheet = htmlPreviewStyleSheetForCurrentTheme();
    if (!previewSheet.isNull()) {
        applyHtmlPreviewStyleSheetToAllTabs(previewSheet);
    }
}

void MainWindow::changeInterfaceStyle(InterfaceStyle style)
{
    Q_UNUSED(style);

    applyTheme();
}

void MainWindow::showQuickReferenceGuide()
{
    QDesktopServices::openUrl(QUrl("https://ghostwriter.kde.org/documentation"));
}

void MainWindow::showWikiPage()
{
    QDesktopServices::openUrl(QUrl("https://github.com/KDE/ghostwriter/wiki"));
}

void MainWindow::changeFocusMode(FocusMode focusMode)
{
    for (auto *tab : tabs) {
        if (tab->editor() && tab->editor()->focusMode() != FocusModeDisabled) {
            tab->editor()->setFocusMode(focusMode);
        }
    }
}

void MainWindow::refreshRecentFiles()
{
    if (appSettings->fileHistoryEnabled()) {
        Library library;
        BookmarkList recentFiles = library.recentFiles(MAX_RECENT_FILES);

        for (int i = 0; i < recentFilesActions.size(); i++) {
            QAction *action = recentFilesActions.at(i);

            if (i < recentFiles.size()) {
                QString path = recentFiles.at(i).filePath();
                action->setText(path);
                action->setData(path);
                action->setVisible(true);
            } else {
                action->setText("");
                action->setData(QVariant());
                action->setVisible(false);
            }
        }

        appAction(AppActions::ReopenLastClosed)->setEnabled(!recentFiles.isEmpty());
    } else {
        appAction(AppActions::ReopenLastClosed)->setEnabled(false);
    }
}

void MainWindow::clearRecentFileHistory()
{
    Library library;
    library.clearHistory();

    for (auto action : recentFilesActions) {
        action->setText("");
        action->setData(QVariant());
        action->setVisible(false);
    }
}

void MainWindow::changeDocumentDisplayName(const QString &displayName)
{
    const QString appCaption = []()
    {
        const QString d = QGuiApplication::applicationDisplayName();
        return d.isEmpty() ? QStringLiteral("ghostwriter++") : d;
    }();

    if (displayName.isEmpty()) {
        setWindowTitle(appCaption);
    } else {
        setWindowTitle(displayName + QStringLiteral("[*] - ") + appCaption);
    }

    auto *dm = currentDocumentManager();
    if (dm && dm->document()->isModified()) {
        setWindowModified(!appSettings->autoSaveEnabled());
    } else {
        setWindowModified(false);
    }

    if (activeTabIndex >= 0) {
        updateTabLabel(activeTabIndex);
    }
}

void MainWindow::onOperationStarted(const QString &description)
{
    if (!description.isNull()) {
        statusIndicator->setText(description);
    }

    statisticsIndicator->hide();
    statusIndicator->show();
    this->update();
    qApp->processEvents();
}

void MainWindow::onOperationFinished()
{
    statusIndicator->setText(QString());
    statisticsIndicator->show();
    statusIndicator->hide();
    this->update();
    qApp->processEvents();
}

void MainWindow::changeFont()
{
    FontSettingsDialog dialog(this);
    dialog.exec();
}

void MainWindow::onFontSizeChanged(int size)
{
    if (!currentEditor()) {
        return;
    }

    QFont font = appSettings->editorFont();
    font.setPointSize(size);
    appSettings->setEditorFont(font);
}

void MainWindow::onSetLocale()
{
    LocaleDialog *dialog = new LocaleDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void MainWindow::copyHtml()
{
    Exporter *htmlExporter = appSettings->currentHtmlExporter();
    auto *editor = currentEditor();

    if (nullptr != htmlExporter && nullptr != editor) {
        QTextCursor c = editor->textCursor();
        QString markdownText;
        QString html;

        if (c.hasSelection()) {
            markdownText = c.selection().toPlainText();
        } else {
            markdownText = editor->toPlainText();
        }

        htmlExporter->exportToHtml(markdownText, html);

        QClipboard *clipboard = QApplication::clipboard();
        clipboard->setText(html);
    }
}

void MainWindow::showPreviewOptions()
{
    PreviewOptionsDialog *dialog = new PreviewOptionsDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModal(false);
    dialog->show();
}

void MainWindow::onAboutToHideMenuBarMenu()
{
    menuBarMenuActivated = false;

    if (!this->menuBar()->underMouse()
            && this->isFullScreen()
            && appSettings->hideMenuBarInFullScreenEnabled()
            && this->menuBar()->isVisible()) {
        this->menuBar()->hide();
    }
}

void MainWindow::onAboutToShowMenuBarMenu()
{
    menuBarMenuActivated = true;

    if (this->isFullScreen()
            && appSettings->hideMenuBarInFullScreenEnabled()
            && !this->menuBar()->isVisible()) {
        this->menuBar()->show();
    }
}

void MainWindow::onSidebarVisibilityChanged(bool visible)
{
    if (!visible && currentEditor()) {
        currentEditor()->setFocus();
    }

    this->adjustEditor();
}

void MainWindow::toggleSidebarVisible(bool visible)
{
    this->appSettings->setSidebarVisible(visible);

    if (!this->sidebarHiddenForResize
            && !this->focusModeEnabled
            && this->appSettings->sidebarVisible()) {
        sidebar->setAutoHideEnabled(false);
    }
    else {
        sidebar->setAutoHideEnabled(true);
    }

    this->sidebar->setVisible(visible);
    this->sidebar->setFocus();
    adjustEditor();
}

KActionCollection *MainWindow::actionCollection() const
{
    return m_actionCollection;
}

QMenu *MainWindow::addMenuBarMenu(const QString &name)
{
    QMenu *menu = new QMenu(name, this);
    connect(menu, &QMenu::aboutToShow, this, &MainWindow::onAboutToShowMenuBarMenu);
    connect(menu, &QMenu::aboutToHide, this, &MainWindow::onAboutToHideMenuBarMenu);
    menuBar()->addMenu(menu);

    return menu;
}

QAction *MainWindow::appAction(AppActions::ActionType actionType) const
{
    auto action = m_actions->get(actionType);

    if (nullptr == action) {
        qCritical() << "Unknown action type:" << actionType;
    }

    return action;
}

DocumentTab *MainWindow::currentTab() const
{
    if (activeTabIndex < 0 || activeTabIndex >= tabs.size()) {
        return nullptr;
    }
    return tabs[activeTabIndex];
}

MarkdownEditor *MainWindow::currentEditor() const
{
    return currentTab() ? currentTab()->editor() : nullptr;
}

MarkdownDocument *MainWindow::currentDocument() const
{
    return currentTab() ? currentTab()->document() : nullptr;
}

DocumentManager *MainWindow::currentDocumentManager() const
{
    return currentTab() ? currentTab()->documentManager() : nullptr;
}

HtmlPreview *MainWindow::currentHtmlPreview() const
{
    return currentTab() ? currentTab()->htmlPreview() : nullptr;
}

DocumentStatistics *MainWindow::currentDocumentStats() const
{
    return currentTab() ? currentTab()->documentStats() : nullptr;
}

DocumentTab *MainWindow::addDocumentTab(const Bookmark &location, bool activate)
{
    if (location.isValid()) {
        const QString absPath = absoluteFileKey(location.filePath());
        for (int i = 0; i < tabs.size(); ++i) {
            MarkdownDocument *doc = tabs[i]->document();
            if (!doc || doc->isNew()) {
                continue;
            }
            if (absoluteFileKey(doc->filePath()) != absPath) {
                continue;
            }

            if (MarkdownEditor *ed = tabs[i]->editor()) {
                if (location.cursorPosition() >= 0) {
                    QTextDocument *qdoc = ed->document();
                    int maxPos = qMax(0, qdoc->characterCount() - 1);
                    int pos = qBound(0, location.cursorPosition(), maxPos);
                    QTextCursor cur = ed->textCursor();
                    cur.setPosition(pos);
                    ed->setTextCursor(cur);
                }
            }
            if (activate) {
                tabBar->setCurrentIndex(i);
            }
            return tabs[i];
        }
    }

    ColorScheme colorScheme = appSettings->darkModeEnabled()
        ? theme.darkColorScheme()
        : theme.lightColorScheme();

    auto *tab = new DocumentTab(colorScheme, this);
    tabs.append(tab);

    auto *editor = tab->editor();
    editor->setMinimumWidth(0.1 * layoutScreenWidth());
    editor->setEditorWidth((EditorWidth)appSettings->editorWidth());
    editor->setEditorCorners((InterfaceStyle)appSettings->interfaceStyle());
    editor->setHemingWayModeEnabled(hemingwayModeEnabled);

    if (focusModeEnabled) {
        editor->setFocusMode(appSettings->focusMode());
    }

    auto *preview = tab->htmlPreview();
    preview->setMinimumWidth(0.1 * layoutScreenWidth());

    editorStack->addWidget(editor);
    previewStack->addWidget(preview);

    int tabIndex = tabBar->addTab(tab->document()->displayName());
    tabBar->setTabToolTip(tabIndex, tab->document()->displayName());

    connect(editor, &MarkdownEditor::fontSizeChanged, this, &MainWindow::onFontSizeChanged);
    connect(tab->documentManager(), &DocumentManager::documentDisplayNameChanged, this, [this, tab](const QString &) {
        int idx = tabs.indexOf(tab);
        if (idx >= 0) {
            updateTabLabel(idx);
        }
    });
    connect(tab->documentManager(), &DocumentManager::documentModifiedChanged, this, [this, tab](bool) {
        int idx = tabs.indexOf(tab);
        if (idx >= 0) {
            updateTabLabel(idx);
        }
    });

    if (activate) {
        tabBar->setCurrentIndex(tabIndex);
    }

    if (location.isValid()) {
        tab->documentManager()->openFileAt(location);
    }

    QString previewSheet = htmlPreviewStyleSheetForCurrentTheme();
    if (previewSheet.isNull()) {
        qCritical() << "Invalid HTML preview style sheet provided.";
    } else {
        preview->setStyleSheet(previewSheet);
    }

    return tab;
}

void MainWindow::activateTab(int index)
{
    if (index < 0 || index >= tabs.size()) {
        return;
    }

    activeTabIndex = index;
    auto *tab = tabs[index];

    editorStack->setCurrentWidget(tab->editor());
    previewStack->setCurrentWidget(tab->htmlPreview());

    wireActiveTab();

    if (tab->editor()) {
        setFocusProxy(tab->editor());
        tab->editor()->setFocus();
    }

    adjustEditor();
}

bool MainWindow::closeTabAt(int index)
{
    if (index < 0 || index >= tabs.size()) {
        return false;
    }

    auto *tab = tabs[index];
    bool wasActive = (index == activeTabIndex);

    if (!tab->documentManager()->confirmTabRemoval(wasActive)) {
        return false;
    }

    // Never-tabless policy: if this is the last tab, spawn a fresh untitled
    // replacement BEFORE tearing down, so the stacks never go empty and we
    // do not trip the wireActiveTab(nullptr) path. Activating the new tab
    // makes the old one non-active, so the rest of the teardown is a plain
    // non-active removal.
    if (tabs.size() == 1) {
        auto *replacement = addDocumentTab(Bookmark(), /*activate=*/ true);
        if (replacement && replacement->documentManager()) {
            replacement->documentManager()->createUntitled();
        }
        wasActive = false; // the new replacement is active now, not the old tab
    }

    detachActiveTab(index, wasActive);
    removeTabWidgets(tab, index);
    tab->deleteLater();

    if (tabs.isEmpty()) {
        // Defensive - never-tabless policy should prevent this, but keep the
        // stacks showing empty panes if something odd happens.
        activeTabIndex = -1;
        editorStack->setCurrentWidget(editorEmptyPane);
        previewStack->setCurrentWidget(previewEmptyPane);
        adjustEditor();
        return true;
    }

    int newIndex = qBound(0,
                          wasActive ? qMin(index, tabs.size() - 1) : activeTabIndex,
                          tabs.size() - 1);

    if (tabBar->currentIndex() != newIndex) {
        tabBar->setCurrentIndex(newIndex);
    } else if (activeTabIndex != newIndex) {
        activateTab(newIndex);
    }

    return true;
}

void MainWindow::detachActiveTab(int index, bool wasActive)
{
    if (!wasActive) {
        return;
    }

    for (const auto &c : std::as_const(perTabConnections)) {
        QObject::disconnect(c);
    }
    perTabConnections.clear();

    setFocusProxy(nullptr);

    if (outlineWidget) {
        outlineWidget->setEditor(nullptr);
    }
    if (findReplace) {
        findReplace->setEditor(nullptr);
    }
    if (statisticsIndicator) {
        statisticsIndicator->setDocumentStats(nullptr);
    }

    activeTabIndex = -1;
    Q_UNUSED(index);
}

void MainWindow::removeTabWidgets(DocumentTab *tab, int index)
{
    if (!tab) {
        return;
    }

    tabs.removeAt(index);

    if (tab->editor()) {
        editorStack->removeWidget(tab->editor());
    }
    if (tab->htmlPreview()) {
        previewStack->removeWidget(tab->htmlPreview());
    }

    tabBar->blockSignals(true);
    tabBar->removeTab(index);
    tabBar->blockSignals(false);

    if (activeTabIndex >= 0 && index < activeTabIndex) {
        activeTabIndex--;
    }
}

void MainWindow::wireActiveTab()
{
    for (const auto &c : std::as_const(perTabConnections)) {
        QObject::disconnect(c);
    }
    perTabConnections.clear();

    auto *tab = currentTab();

    if (!tab) {
        outlineWidget->setEditor(nullptr);
        findReplace->setEditor(nullptr);
        setFocusProxy(nullptr);
        changeDocumentDisplayName(QString());
        setWindowModified(false);
        if (statisticsIndicator) {
            statisticsIndicator->setDocumentStats(nullptr);
        }
        documentStatsWidget->setWordCount(0);
        documentStatsWidget->setCharacterCount(0);
        documentStatsWidget->setSentenceCount(0);
        documentStatsWidget->setParagraphCount(0);
        documentStatsWidget->setPageCount(0);
        documentStatsWidget->setReadingTime(0);
        documentStatsWidget->setComplexWords(0);
        documentStatsWidget->setLixReadingEase(0);
        documentStatsWidget->setReadabilityIndex(0);
        sessionStats->startNewSession(0);
        return;
    }

    auto *editor = tab->editor();
    auto *dm = tab->documentManager();
    auto *stats = tab->documentStats();
    auto *preview = tab->htmlPreview();

    perTabConnections << connect(dm, &DocumentManager::documentDisplayNameChanged, this, &MainWindow::changeDocumentDisplayName);
    perTabConnections << connect(dm, &DocumentManager::documentModifiedChanged, this, &MainWindow::setWindowModified);
    perTabConnections << connect(dm, &DocumentManager::operationStarted, this, &MainWindow::onOperationStarted);
    perTabConnections << connect(dm, &DocumentManager::operationUpdate, this, &MainWindow::onOperationStarted);
    perTabConnections << connect(dm, &DocumentManager::operationFinished, this, &MainWindow::onOperationFinished);
    perTabConnections << connect(dm, &DocumentManager::sessionHistoryChanged, this, &MainWindow::refreshRecentFiles);

    perTabConnections << connect(dm, &DocumentManager::documentLoaded, this, [this]() {
        if (auto *st = currentDocumentStats()) {
            sessionStats->startNewSession(st->wordCount());
        }
        refreshRecentFiles();
        if (folderViewWidget && currentDocument()) {
            folderViewWidget->reloadFolderViewFromPath(currentDocument()->filePath(), appSettings->folderViewShowAllFilesEnabled());
        }
    });

    perTabConnections << connect(dm, &DocumentManager::documentClosed, this, [this]() {
        sessionStats->startNewSession(0);
    });

    perTabConnections << connect(stats, &DocumentStatistics::wordCountChanged, documentStatsWidget, &DocumentStatisticsWidget::setWordCount);
    perTabConnections << connect(stats, &DocumentStatistics::characterCountChanged, documentStatsWidget, &DocumentStatisticsWidget::setCharacterCount);
    perTabConnections << connect(stats, &DocumentStatistics::sentenceCountChanged, documentStatsWidget, &DocumentStatisticsWidget::setSentenceCount);
    perTabConnections << connect(stats, &DocumentStatistics::paragraphCountChanged, documentStatsWidget, &DocumentStatisticsWidget::setParagraphCount);
    perTabConnections << connect(stats, &DocumentStatistics::pageCountChanged, documentStatsWidget, &DocumentStatisticsWidget::setPageCount);
    perTabConnections << connect(stats, &DocumentStatistics::complexWordsChanged, documentStatsWidget, &DocumentStatisticsWidget::setComplexWords);
    perTabConnections << connect(stats, &DocumentStatistics::readingTimeChanged, documentStatsWidget, &DocumentStatisticsWidget::setReadingTime);
    perTabConnections << connect(stats, &DocumentStatistics::lixReadingEaseChanged, documentStatsWidget, &DocumentStatisticsWidget::setLixReadingEase);
    perTabConnections << connect(stats, &DocumentStatistics::readabilityIndexChanged, documentStatsWidget, &DocumentStatisticsWidget::setReadabilityIndex);

    // Seed the sidebar stats widget with the new tab's current values.
    documentStatsWidget->setWordCount(stats->wordCount());
    documentStatsWidget->setCharacterCount(stats->characterCount());
    documentStatsWidget->setSentenceCount(stats->sentenceCount());
    documentStatsWidget->setParagraphCount(stats->paragraphCount());
    documentStatsWidget->setPageCount(stats->pageCount());
    documentStatsWidget->setReadingTime(stats->readingTime());

    perTabConnections << connect(stats, &DocumentStatistics::totalWordCountChanged, sessionStats, &SessionStatistics::onDocumentWordCountChanged);
    perTabConnections << connect(editor, &MarkdownEditor::typingPaused, sessionStats, &SessionStatistics::onTypingPaused);
    perTabConnections << connect(editor, &MarkdownEditor::typingResumed, sessionStats, &SessionStatistics::onTypingResumed);

    outlineWidget->setEditor(editor);
    perTabConnections << connect(outlineWidget, &OutlineWidget::headingNumberNavigated, preview, &HtmlPreview::navigateToHeading);

    findReplace->setEditor(editor);

    // Push current title/modified state into the window chrome.
    changeDocumentDisplayName(tab->document()->displayName());
    setWindowModified(tab->document()->isModified() && !appSettings->autoSaveEnabled());

    if (statisticsIndicator) {
        statisticsIndicator->setDocumentStats(stats);
    }

    sessionStats->startNewSession(stats->wordCount());
}

void MainWindow::persistOpenTabs()
{
    if (!appSettings->restoreSessionEnabled()) {
        return;
    }

    QSettings s;
    s.remove(GW_SESSION_OPEN_TABS_KEY);

    s.beginWriteArray(GW_SESSION_OPEN_TABS_KEY);
    int written = 0;
    int activeOut = -1;
    QHash<QString, int> pathToWrittenIndex;

    for (int i = 0; i < tabs.size(); ++i) {
        auto *tab = tabs[i];
        if (!tab) continue;

        auto *doc = tab->document();
        auto *editor = tab->editor();
        if (!doc || !editor) continue;
        if (doc->isNew() || doc->filePath().isEmpty()) continue;

        const QString absPath = absoluteFileKey(doc->filePath());
        if (pathToWrittenIndex.contains(absPath)) {
            if (i == activeTabIndex) {
                activeOut = pathToWrittenIndex.value(absPath);
            }
            continue;
        }

        s.setArrayIndex(written);
        s.setValue(GW_SESSION_TAB_PATH_KEY, doc->filePath());
        s.setValue(GW_SESSION_TAB_CURSOR_KEY, editor->textCursor().position());

        pathToWrittenIndex.insert(absPath, written);
        if (i == activeTabIndex) {
            activeOut = written;
        }

        ++written;
    }

    s.endArray();
    s.setValue(GW_SESSION_ACTIVE_TAB_KEY, activeOut);
}

BookmarkList MainWindow::loadPersistedTabs(int *activeOut) const
{
    BookmarkList result;
    if (activeOut) {
        *activeOut = -1;
    }

    QSettings s;
    int count = s.beginReadArray(GW_SESSION_OPEN_TABS_KEY);
    QSet<QString> seenPaths;

    for (int i = 0; i < count; ++i) {
        s.setArrayIndex(i);
        QString path = s.value(GW_SESSION_TAB_PATH_KEY).toString();
        int cursor = s.value(GW_SESSION_TAB_CURSOR_KEY, 0).toInt();
        if (path.isEmpty()) continue;

        Bookmark bm(path, cursor);
        if (!bm.isValid()) continue;

        const QString absPath = bm.filePath();
        if (seenPaths.contains(absPath)) {
            continue;
        }
        seenPaths.insert(absPath);

        result.append(bm);
    }

    s.endArray();

    if (activeOut) {
        *activeOut = s.value(GW_SESSION_ACTIVE_TAB_KEY, -1).toInt();
    }

    return result;
}

void MainWindow::updateTabLabel(int index)
{
    if (index < 0 || index >= tabs.size() || !tabBar) {
        return;
    }

    auto *tab = tabs[index];
    QString name = tab->document()->displayName();

    if (tab->document()->isModified()) {
        name = "• " + name;
    }

    tabBar->setTabText(index, name);
    tabBar->setTabToolTip(index, tab->document()->filePath().isEmpty() ? name : tab->document()->filePath());
}

void MainWindow::applyFocusView(FocusView view)
{
    const bool showEditor = (view != FocusViewPreviewOnly);
    const bool showPreview = (view != FocusViewEditorOnly);

    bool previewWasHidden = false;
    if (previewStack) {
        previewWasHidden = !previewStack->isVisible();
    }

    if (editorStack) {
        editorStack->setVisible(showEditor);
    }
    if (previewStack) {
        previewStack->setVisible(showPreview);
    }

    // Keep legacy htmlPreviewVisible in sync so older code paths stay happy.
    appSettings->setHtmlPreviewVisible(showPreview);

    adjustEditor();

    if (showPreview && previewWasHidden) {
        for (DocumentTab *tab : tabs) {
            if (tab && tab->htmlPreview()) {
                tab->htmlPreview()->updatePreview();
            }
        }
    }
}

void MainWindow::syncFocusViewActions(FocusView view)
{
    QAction *split = appAction(AppActions::LayoutSplit);
    QAction *editorOnly = appAction(AppActions::LayoutEditorOnly);
    QAction *previewOnly = appAction(AppActions::LayoutPreviewOnly);

    if (split) split->setChecked(view == FocusViewSplit);
    if (editorOnly) editorOnly->setChecked(view == FocusViewEditorOnly);
    if (previewOnly) previewOnly->setChecked(view == FocusViewPreviewOnly);
}

void MainWindow::loadTheme()
{
    QString err;
    QString themeName = appSettings->themeName();
    ThemeRepository themeRepo(appSettings->themeDirectoryPath());

    theme = themeRepo.loadTheme(themeName, err);

    if (!theme.name().isEmpty()) {
        appSettings->setThemeName(theme.name());
    }

    ColorScheme colorScheme;

    if (appSettings->darkModeEnabled()) {
        colorScheme = theme.darkColorScheme();
    } else {
        colorScheme = theme.lightColorScheme();
    }

    ChromeColors chromeColors(colorScheme);

    primaryIconTheme = new SvgIconTheme(":/icons");
    primaryIconTheme->setColor(QIcon::Normal, chromeColors.color(ChromeColors::SecondaryLabel, ChromeColors::NormalState));
    primaryIconTheme->setColor(QIcon::Active, chromeColors.color(ChromeColors::SecondaryLabel, ChromeColors::ActiveState));
    primaryIconTheme->setColor(QIcon::Selected, chromeColors.color(ChromeColors::SecondaryLabel, ChromeColors::PressedState));
    primaryIconTheme->setColor(QIcon::Disabled, chromeColors.color(ChromeColors::SecondaryLabel, ChromeColors::DisabledState));

    secondaryIconTheme = new SvgIconTheme(":/icons");
    secondaryIconTheme->setColor(QIcon::Normal, chromeColors.color(ChromeColors::SecondaryLabel, ChromeColors::NormalState));
    secondaryIconTheme->setColor(QIcon::Active, chromeColors.color(ChromeColors::SecondaryLabel, ChromeColors::NormalState));
    secondaryIconTheme->setColor(QIcon::Selected, chromeColors.color(ChromeColors::SecondaryLabel, ChromeColors::PressedState));
    secondaryIconTheme->setColor(QIcon::Disabled, chromeColors.color(ChromeColors::SecondaryLabel, ChromeColors::DisabledState));
}

void MainWindow::setupActions()
{
    // File Menu Actions

    m_actions->connect(AppActions::New, this, [this]() {
        addDocumentTab();
    });
    m_actions->connect(AppActions::Open, this, [this]() {
        QString startPath = currentDocument() ? currentDocument()->filePath() : QString();
        QFileInfo info(startPath);
        QString startDir = info.exists() ? info.absolutePath() : QString();

        QString filePath = QFileDialog::getOpenFileName(this, tr("Open File"), startDir,
            tr("Markdown files (*.md *.markdown *.mdown *.mkd *.mkdn *.txt);;All files (*)"));

        if (!filePath.isEmpty()) {
            addDocumentTab(Bookmark(filePath));
        }
    });

    auto reopenLastAction = appAction(AppActions::ReopenLastClosed);

    for (int i = AppActions::OpenMostRecent; i <= AppActions::OpenLeastRecent; i++) {
        int index = i - AppActions::OpenMostRecent;
        bool enableReopenLast = false;
        auto action = appAction((AppActions::ActionType)i);

        Library library;
        BookmarkList recentFiles = library.recentFiles();

        if (recentFiles.length() > index) {
            auto filePath = recentFiles.at(index).filePath();
            action->setText(filePath);
            action->setData(filePath);
            action->setVisible(true);
            enableReopenLast = true;
        } else {
            action->setVisible(false);
        }

        reopenLastAction->setEnabled(enableReopenLast);
        recentFilesActions.append(action);

        m_actions->connect((AppActions::ActionType)i, this, [this, action](bool checked) {
            Q_UNUSED(checked)

            if (action->data().isValid()) {
                Library library;
                Bookmark location = library.lookup(action->data().toString());

                if (location.isNull()) {
                    location = Bookmark(action->data().toString());
                }

                addDocumentTab(location);
                refreshRecentFiles();
            }
        });
    }

    m_actions->connect(AppActions::ClearRecentFilesList, this, &MainWindow::clearRecentFileHistory);
    m_actions->connect(AppActions::Save, this, [this]() {
        if (auto *dm = currentDocumentManager()) dm->saveFile();
    });
    m_actions->connect(AppActions::SaveAs, this, [this]() {
        if (auto *dm = currentDocumentManager()) dm->saveAs();
    });
    m_actions->connect(AppActions::RenameFile, this, [this]() {
        if (auto *dm = currentDocumentManager()) dm->rename();
    });
    m_actions->connect(AppActions::Reload, this, [this]() {
        if (auto *dm = currentDocumentManager()) dm->reload();
    });
    m_actions->connect(AppActions::Export, this, [this]() {
        if (auto *dm = currentDocumentManager()) dm->exportFile();
    });

    m_actions->connect(AppActions::CloseTab, this, [this]() {
        if (activeTabIndex >= 0) {
            closeTabAt(activeTabIndex);
        }
    });
    m_actions->connect(AppActions::NextTab, this, [this]() {
        if (tabs.size() < 2) return;
        int next = (activeTabIndex + 1) % tabs.size();
        tabBar->setCurrentIndex(next);
    });
    m_actions->connect(AppActions::PrevTab, this, [this]() {
        if (tabs.size() < 2) return;
        int prev = (activeTabIndex - 1 + tabs.size()) % tabs.size();
        tabBar->setCurrentIndex(prev);
    });

    m_actions->connect(AppActions::Quit, this, [this]() {
        close();
    });

    // Edit Menu Actions - delegated to current tab's editor via lambdas.

    auto editorAction = [this]<typename Ret>(Ret (MarkdownEditor::*method)()) {
        return [this, method]() {
            if (auto *ed = currentEditor()) (ed->*method)();
        };
    };

    m_actions->connect(AppActions::Undo, this, editorAction(&MarkdownEditor::undo));
    m_actions->connect(AppActions::Redo, this, editorAction(&MarkdownEditor::redo));
    m_actions->connect(AppActions::Cut, this, editorAction(&MarkdownEditor::cut));
    m_actions->connect(AppActions::Copy, this, editorAction(&MarkdownEditor::copy));
    m_actions->connect(AppActions::Paste, this, editorAction(&MarkdownEditor::paste));
    m_actions->connect(AppActions::CopyHTML, this, &MainWindow::copyHtml);
    m_actions->connect(AppActions::SelectAll, this, editorAction(&MarkdownEditor::selectAll));
    m_actions->connect(AppActions::Deselect, this, editorAction(&MarkdownEditor::deselectText));
    m_actions->connect(AppActions::InsertImage, this, editorAction(&MarkdownEditor::insertImage));
    m_actions->connect(AppActions::Find, findReplace, &FindReplace::showFindView);
    m_actions->connect(AppActions::Replace, findReplace, &FindReplace::showReplaceView);
    m_actions->connect(AppActions::FindNext, findReplace, &FindReplace::findNext);
    m_actions->connect(AppActions::FindPrev, findReplace, &FindReplace::findPrevious);
    m_actions->connect(AppActions::Spelling, this, &MainWindow::runSpellCheck);

    // Format Menu Actions

    m_actions->connect(AppActions::Strong, this, editorAction(&MarkdownEditor::bold));
    m_actions->connect(AppActions::Emphasis, this, editorAction(&MarkdownEditor::italic));
    m_actions->connect(AppActions::Strikethrough, this, editorAction(&MarkdownEditor::strikethrough));
    m_actions->connect(AppActions::InsertHTMLComment, this, editorAction(&MarkdownEditor::insertComment));
    m_actions->connect(AppActions::IndentText, this, editorAction(&MarkdownEditor::indentText));
    m_actions->connect(AppActions::UnindentText, this, editorAction(&MarkdownEditor::unindentText));
    m_actions->connect(AppActions::CodeFences, this, editorAction(&MarkdownEditor::insertCodeFences));
    m_actions->connect(AppActions::BlockQuote, this, editorAction(&MarkdownEditor::createBlockquote));
    m_actions->connect(AppActions::StripBlockQuote, this, editorAction(&MarkdownEditor::removeBlockquote));
    m_actions->connect(AppActions::BulletListAsterisk, this, editorAction(&MarkdownEditor::createBulletListWithAsteriskMarker));
    m_actions->connect(AppActions::BulletListMinus, this, editorAction(&MarkdownEditor::createBulletListWithMinusMarker));
    m_actions->connect(AppActions::BulletListPlus, this, editorAction(&MarkdownEditor::createBulletListWithPlusMarker));
    m_actions->connect(AppActions::NumberedListPeriod, this, editorAction(&MarkdownEditor::createNumberedListWithPeriodMarker));
    m_actions->connect(AppActions::NumberedListParenthesis, this, editorAction(&MarkdownEditor::createNumberedListWithParenthesisMarker));
    m_actions->connect(AppActions::TaskList, this, editorAction(&MarkdownEditor::createTaskList));
    m_actions->connect(AppActions::TaskComplete, this, editorAction(&MarkdownEditor::toggleTaskComplete));

    // View Menu Actions

    appAction(AppActions::FullScreen)->setChecked(isFullScreen());
    m_actions->connect(AppActions::FullScreen, this, &MainWindow::toggleFullScreen);
    appAction(AppActions::DistractionFreeMode)->setChecked(false);
    m_actions->connect(AppActions::DistractionFreeMode, this, &MainWindow::toggleFocusMode);

    // Layout actions - exclusive group driving the focus view.
    layoutActionGroup = new QActionGroup(this);
    layoutActionGroup->setExclusive(true);
    layoutActionGroup->addAction(appAction(AppActions::LayoutSplit));
    layoutActionGroup->addAction(appAction(AppActions::LayoutEditorOnly));
    layoutActionGroup->addAction(appAction(AppActions::LayoutPreviewOnly));

    m_actions->connect(AppActions::LayoutSplit, this, [this]() {
        appSettings->setFocusView(FocusViewSplit);
    });
    m_actions->connect(AppActions::LayoutEditorOnly, this, [this]() {
        appSettings->setFocusView(FocusViewEditorOnly);
    });
    m_actions->connect(AppActions::LayoutPreviewOnly, this, [this]() {
        appSettings->setFocusView(FocusViewPreviewOnly);
    });

    m_actions->connect(AppActions::HemingwayMode, this, &MainWindow::toggleHemingwayMode);
    appAction(AppActions::DarkMode)->setChecked(appSettings->darkModeEnabled());
    m_actions->connect(AppActions::DarkMode, this, [this](bool enabled) {
        appSettings->setDarkModeEnabled(enabled);
        applyTheme();
    });
    appAction(AppActions::ShowSidebar)->setChecked(appSettings->sidebarVisible());
    m_actions->connect(AppActions::ShowSidebar, this, &MainWindow::toggleSidebarVisible);
    m_actions->connect(AppActions::ShowOutline, this, [this]() {
        sidebar->setVisible(true);
        sidebar->setCurrentTabIndex(OutlineSidebarTab);
    });
    m_actions->connect(AppActions::ShowSessionStatistics, this, [this]() {
        sidebar->setVisible(true);
        sidebar->setCurrentTabIndex(SessionStatsSidebarTab);
    });
    m_actions->connect(AppActions::ShowDocumentStatistics, this, [this]() {
        sidebar->setVisible(true);
        sidebar->setCurrentTabIndex(DocumentStatsSidebarTab);
    });
    m_actions->connect(AppActions::ShowCheatSheet, this, [this]() {
        sidebar->setVisible(true);
        sidebar->setCurrentTabIndex(CheatSheetSidebarTab);
    });
    m_actions->connect(AppActions::ZoomIn, this, editorAction(&MarkdownEditor::increaseFontSize));
    m_actions->connect(AppActions::ZoomOut, this, editorAction(&MarkdownEditor::decreaseFontSize));

    // Settings Menu Actions

    m_actions->connect(AppActions::ChangeTheme, this, &MainWindow::changeTheme);
    m_actions->connect(AppActions::ChangeFont, this, &MainWindow::changeFont);
    m_actions->connect(AppActions::SwitchApplicationLanguage, this, &MainWindow::onSetLocale);
    m_actions->connect(AppActions::PreviewOptions, this, &MainWindow::showPreviewOptions);
    m_actions->connect(AppActions::Preferences, this, &MainWindow::openPreferencesDialog);

    // Help Menu Actions

    m_helpMenu = new KHelpMenu(this);
    m_actions->connect(AppActions::AboutApp, m_helpMenu, &KHelpMenu::aboutApplication);
    m_actions->connect(AppActions::AboutKDE, m_helpMenu, &KHelpMenu::aboutKDE);
    m_actions->connect(AppActions::HelpContents, this, &MainWindow::showQuickReferenceGuide);
    m_actions->connect(AppActions::ReportBug, m_helpMenu, &KHelpMenu::reportBug);
    m_actions->connect(AppActions::Donate, m_helpMenu, &KHelpMenu::donate);
    m_actions->connect(AppActions::WhatsThis, this, &QWhatsThis::enterWhatsThisMode);
}

void MainWindow::setupGui()
{
    setObjectName("mainWindow");
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    findReplace = new FindReplace(nullptr, this);
    statusBarWidgets.append(findReplace);
    findReplace->setVisible(false);
    findReplace->setMatchCaseIcon(primaryIconTheme->icon("match-case"));
    findReplace->setWholeWordIcon(primaryIconTheme->icon("whole-word"));
    findReplace->setRegexSearchIcon(primaryIconTheme->icon("regex-search"));
    findReplace->setHighlightMatchesIcon(primaryIconTheme->icon("highlight-matches"));
    findReplace->setFindNextIcon(primaryIconTheme->icon("find-next"));
    findReplace->setFindPreviousIcon(primaryIconTheme->icon("find-previous"));
    findReplace->setCloseIcon(primaryIconTheme->icon("close"));

    setupSidebar();
    setupMenuBar();
    setupStatusBar();
    setupTabBar();

    editorStack = new QStackedWidget(this);
    previewStack = new QStackedWidget(this);
    editorStack->setMinimumWidth(0.1 * layoutScreenWidth());
    previewStack->setMinimumWidth(0.1 * layoutScreenWidth());
    editorStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    previewStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    editorEmptyPane = new QWidget(this);
    previewEmptyPane = new QWidget(this);
    editorEmptyPane->setObjectName(QStringLiteral("emptyEditorPane"));
    previewEmptyPane->setObjectName(QStringLiteral("emptyPreviewPane"));
    editorStack->addWidget(editorEmptyPane);
    previewStack->addWidget(previewEmptyPane);

    splitter = new QSplitter(this);
    splitter->addWidget(sidebar);
    splitter->addWidget(editorStack);
    splitter->addWidget(previewStack);
    splitter->setChildrenCollapsible(false);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 2);
    splitter->setStretchFactor(2, 1);
    splitter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    connect(splitter, &QSplitter::splitterMoved, splitter, [this](int pos, int index) {
        Q_UNUSED(pos)
        Q_UNUSED(index)
        adjustEditor();
    });

    // Wrap the splitter with a vertical container so the tab bar sits right
    // below the menu bar, sharing the same height as the menu bar row.
    QWidget *container = new QWidget(this);
    container->setObjectName("centralContainer");
    container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QWidget *tabBarContainer = new QWidget(container);
    tabBarContainer->setObjectName("tabBarContainer");
    tabBarContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // Ensure QSS background-color actually paints on this plain QWidget.
    tabBarContainer->setAttribute(Qt::WA_StyledBackground, true);

    QHBoxLayout *tabRow = new QHBoxLayout(tabBarContainer);
    tabRow->setContentsMargins(0, 0, 0, 0);
    tabRow->setSpacing(0);
    tabRow->addWidget(tabBar, 0);
    tabRow->addWidget(newTabButton, 0, Qt::AlignVCenter);
    tabRow->addStretch(1);

    QVBoxLayout *vbox = new QVBoxLayout(container);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);
    vbox->addWidget(tabBarContainer, 0);
    vbox->addWidget(splitter, 1);

    setCentralWidget(container);

    // Geometry / state restore must happen AFTER all dock widgets, toolbars,
    // and the central widget are in place, otherwise QMainWindow can't place
    // them correctly and leaves phantom gaps.
    QSettings windowSettings;

    if (windowSettings.contains(GW_MAIN_WINDOW_GEOMETRY_KEY)) {
        restoreGeometry(windowSettings.value(GW_MAIN_WINDOW_GEOMETRY_KEY).toByteArray());
        restoreState(windowSettings.value(GW_MAIN_WINDOW_STATE_KEY).toByteArray());
    } else {
        adjustSize();
    }

    QList<int> sizes;
    int sidebarWidth = width() * 0.2;
    int otherWidth = (width() - sidebarWidth) / 2;
    sizes.append(sidebarWidth);
    sizes.append(otherWidth);
    sizes.append(otherWidth);

    splitter->setSizes(sizes);

    if (windowSettings.contains(GW_SPLITTER_GEOMETRY_KEY)) {
        splitter->restoreState(windowSettings.value(GW_SPLITTER_GEOMETRY_KEY).toByteArray());
    }

    // Tab seeding (session restore + CLI file + fallback untitled) is driven
    // by the MainWindow ctor after setupGui() returns.
}

void MainWindow::setupTabBar()
{
    tabBar = new DocumentTabBar(this);
    tabBar->setObjectName("documentTabBar");
    tabBar->setTabsClosable(true);
    tabBar->setMovable(true);
    tabBar->setExpanding(false);
    tabBar->setDocumentMode(true);
    tabBar->setUsesScrollButtons(true);
    tabBar->setElideMode(Qt::ElideRight);
    tabBar->setDrawBase(false);
    // Hug content width so the new-tab button sits right after the last tab,
    // but still allow shrinking when the row is narrower than the tabs need
    // (QTabBar then shows its scroll arrows).
    tabBar->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);

    newTabButton = new QToolButton(this);
    newTabButton->setObjectName("newTabButton");
    newTabButton->setAutoRaise(true);
    newTabButton->setFocusPolicy(Qt::NoFocus);
    newTabButton->setToolTip(tr("New Tab"));
    newTabButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    newTabButton->setIcon(primaryIconTheme->icon(QStringLiteral("add-tab")));

    connect(newTabButton, &QToolButton::clicked, this, [this]() {
        addDocumentTab();
    });

    connect(tabBar, &QTabBar::currentChanged, this, [this](int index) {
        if (index >= 0 && index < tabs.size()) {
            activateTab(index);
        }
    });

    connect(tabBar, &QTabBar::tabCloseRequested, this, [this](int index) {
        closeTabAt(index);
    });

    connect(tabBar, &QTabBar::tabMoved, this, [this](int from, int to) {
        if (from == to) return;
        if (from < 0 || from >= tabs.size() || to < 0 || to >= tabs.size()) return;

        DocumentTab *activeBeforeMove = (activeTabIndex >= 0 && activeTabIndex < tabs.size())
            ? tabs[activeTabIndex] : nullptr;

        auto *tab = tabs.takeAt(from);
        tabs.insert(to, tab);

        if (activeBeforeMove) {
            activeTabIndex = tabs.indexOf(activeBeforeMove);
        }
    });
}

void MainWindow::setupMenuBar()
{
    QMenu *menu;

    // File Menu

    menu = addMenuBarMenu(tr("&File"));
    menu->addAction(appAction(AppActions::New));
    menu->addAction(appAction(AppActions::Open));

    QAction *openRecentAction = appAction(AppActions::OpenRecent);
    QMenu *submenu = menu->addMenu(openRecentAction->icon(), openRecentAction->text());
    connect(submenu, &QMenu::aboutToShow, this, &MainWindow::onAboutToShowMenuBarMenu);
    connect(submenu, &QMenu::aboutToHide, this, &MainWindow::onAboutToHideMenuBarMenu);

    submenu->addAction(appAction(AppActions::ReopenLastClosed));
    submenu->addSeparator();

    for (int i = AppActions::OpenMostRecent; i <= AppActions::OpenLeastRecent; i++) {
        submenu->addAction(appAction((AppActions::ActionType)i));
    }

    submenu->addSeparator();
    submenu->addAction(appAction(AppActions::ClearRecentFilesList));

    menu->addAction(appAction(AppActions::Save));
    menu->addAction(appAction(AppActions::SaveAs));
    menu->addAction(appAction(AppActions::RenameFile));
    menu->addAction(appAction(AppActions::Reload));
    menu->addSeparator();
    menu->addAction(appAction(AppActions::Export));
    menu->addSeparator();
    menu->addAction(appAction(AppActions::CloseTab));
    menu->addAction(appAction(AppActions::NextTab));
    menu->addAction(appAction(AppActions::PrevTab));
    menu->addSeparator();
    menu->addAction(appAction(AppActions::Quit));

    // Edit Menu

    menu = addMenuBarMenu(tr("&Edit"));
    menu->addAction(appAction(AppActions::Undo));
    menu->addAction(appAction(AppActions::Redo));
    menu->addSeparator();
    menu->addAction(appAction(AppActions::Cut));
    menu->addAction(appAction(AppActions::Copy));
    menu->addAction(appAction(AppActions::Paste));
    menu->addAction(appAction(AppActions::CopyHTML));
    menu->addSeparator();
    menu->addAction(appAction(AppActions::SelectAll));
    menu->addAction(appAction(AppActions::Deselect));
    menu->addSeparator();
    menu->addAction(appAction(AppActions::InsertImage));
    menu->addSeparator();
    menu->addAction(appAction(AppActions::Find));
    menu->addAction(appAction(AppActions::Replace));
    menu->addAction(appAction(AppActions::FindNext));
    menu->addAction(appAction(AppActions::FindPrev));
    menu->addSeparator();
    menu->addAction(appAction(AppActions::Spelling));

    // Format Menu

    menu = addMenuBarMenu("&Format");
    menu->addAction(appAction(AppActions::Strong));
    menu->addAction(appAction(AppActions::Emphasis));
    menu->addAction(appAction(AppActions::Strikethrough));
    menu->addAction(appAction(AppActions::InsertHTMLComment));
    menu->addSeparator();
    menu->addAction(appAction(AppActions::IndentText));
    menu->addAction(appAction(AppActions::UnindentText));
    menu->addSeparator();
    menu->addAction(appAction(AppActions::CodeFences));
    menu->addSeparator();
    menu->addAction(appAction(AppActions::BlockQuote));
    menu->addAction(appAction(AppActions::StripBlockQuote));
    menu->addSeparator();
    menu->addAction(appAction(AppActions::BulletListAsterisk));
    menu->addAction(appAction(AppActions::BulletListMinus));
    menu->addAction(appAction(AppActions::BulletListPlus));
    menu->addSeparator();
    menu->addAction(appAction(AppActions::NumberedListPeriod));
    menu->addAction(appAction(AppActions::NumberedListParenthesis));
    menu->addSeparator();
    menu->addAction(appAction(AppActions::TaskList));
    menu->addAction(appAction(AppActions::TaskComplete));

    // View Menu

    menu = addMenuBarMenu(tr("&View"));
    menu->addAction(appAction(AppActions::FullScreen));
    menu->addAction(appAction(AppActions::DistractionFreeMode));

    QMenu *layoutMenu = menu->addMenu(primaryIconTheme->icon(QStringLiteral("view-layout")), tr("Layout"));
    connect(layoutMenu, &QMenu::aboutToShow, this, &MainWindow::onAboutToShowMenuBarMenu);
    connect(layoutMenu, &QMenu::aboutToHide, this, &MainWindow::onAboutToHideMenuBarMenu);
    layoutMenu->addAction(appAction(AppActions::LayoutSplit));
    layoutMenu->addAction(appAction(AppActions::LayoutEditorOnly));
    layoutMenu->addAction(appAction(AppActions::LayoutPreviewOnly));

    menu->addAction(appAction(AppActions::HemingwayMode));
    menu->addAction(appAction(AppActions::DarkMode));
    menu->addSeparator();
    menu->addAction(appAction(AppActions::ShowSidebar));
    menu->addAction(appAction(AppActions::ShowOutline));
    menu->addAction(appAction(AppActions::ShowSessionStatistics));
    menu->addAction(appAction(AppActions::ShowDocumentStatistics));
    menu->addAction(appAction(AppActions::ShowCheatSheet));
    menu->addSeparator();
    menu->addAction(appAction(AppActions::ZoomIn));
    menu->addAction(appAction(AppActions::ZoomOut));

    // Settings Menu

    menu = addMenuBarMenu(tr("&Settings"));
    menu->addAction(appAction(AppActions::ChangeTheme));
    menu->addAction(appAction(AppActions::ChangeFont));
    menu->addAction(appAction(AppActions::SwitchApplicationLanguage));
    menu->addAction(appAction(AppActions::PreviewOptions));
    menu->addAction(appAction(AppActions::Preferences));

    // Help Menu

    menu = addMenuBarMenu(tr("&Help"));
    menu->addAction(appAction(AppActions::HelpContents));
    menu->addAction(appAction(AppActions::WhatsThis));
    menu->addSeparator();
    menu->addAction(appAction(AppActions::ReportBug));
    menu->addSeparator();
    menu->addAction(appAction(AppActions::Donate));
    menu->addSeparator();
    menu->addAction(appAction(AppActions::AboutApp));
    menu->addAction(appAction(AppActions::AboutKDE));

    if (appSettings->fileHistoryEnabled()) {
        refreshRecentFiles();
    }

    if (isFullScreen() && appSettings->hideMenuBarInFullScreenEnabled()) {
        menuBar()->hide();
    }
}

void MainWindow::setupStatusBar()
{
    QGridLayout *statusBarLayout = new QGridLayout();
    statusBarLayout->setSpacing(0);
    statusBarLayout->setContentsMargins(0, 0, 0, 0);

    statusBarLayout->addWidget(findReplace, 0, 0, 1, 3);

    QFrame *leftWidget = new QFrame(statusBar());
    leftWidget->setObjectName("leftStatusBarWidget");
    QFrame *midWidget = new QFrame(statusBar());
    midWidget->setObjectName("midStatusBarWidget");
    QFrame *rightWidget = new QFrame(statusBar());
    rightWidget->setObjectName("rightStatusBarWidget");

    QHBoxLayout *leftLayout = new QHBoxLayout(leftWidget);
    leftWidget->setLayout(leftLayout);
    leftLayout->setContentsMargins(0,0,0,0);
    QHBoxLayout *midLayout = new QHBoxLayout(midWidget);
    midWidget->setLayout(midLayout);
    midLayout->setContentsMargins(0,0,0,0);
    QHBoxLayout *rightLayout = new QHBoxLayout(rightWidget);
    rightWidget->setLayout(rightLayout);
    rightLayout->setContentsMargins(0,0,0,0);

    QToolButton *button = new QToolButton();
    button->setDefaultAction(appAction(AppActions::ShowSidebar));
    button->setIcon(primaryIconTheme->icon("show-sidebar"));
    button->setObjectName("showSidebarButton");
    button->setFocusPolicy(Qt::NoFocus);

    leftLayout->addWidget(button, 0, Qt::AlignLeft);
    statusBarWidgets.append(button);

    timeIndicator = new TimeLabel(this);
    leftLayout->addWidget(timeIndicator, 0, Qt::AlignLeft);
    leftWidget->setContentsMargins(0, 0, 0, 0);
    statusBarWidgets.append(timeIndicator);

    if (!isFullScreen() || appSettings->displayTimeInFullScreenEnabled()) {
        timeIndicator->hide();
    }

    statusBarLayout->addWidget(leftWidget, 1, 0, 1, 1, Qt::AlignLeft);

    statusIndicator = new QLabel();
    midLayout->addWidget(statusIndicator, 0, Qt::AlignCenter);
    statusIndicator->hide();

    // Start the statistics indicator without a document stats source - it
    // will be retargeted to the active tab's DocumentStatistics via
    // wireActiveTab().
    statisticsIndicator = new StatisticsIndicator(nullptr, this->sessionStats, this);

    if ((appSettings->favoriteStatistic() >= 0)
            && (appSettings->favoriteStatistic() < statisticsIndicator->count())) {
        statisticsIndicator->setCurrentIndex(appSettings->favoriteStatistic());
    }
    else {
        statisticsIndicator->setCurrentIndex(0);
    }

    connect(statisticsIndicator, QOverload<int>::of(&QComboBox::currentIndexChanged), appSettings, &AppSettings::setFavoriteStatistic);

    midLayout->addWidget(statisticsIndicator, 0, Qt::AlignCenter);
    midWidget->setContentsMargins(0, 0, 0, 0);
    statusBarLayout->addWidget(midWidget, 1, 1, 1, 1, Qt::AlignCenter);
    statusBarWidgets.append(statisticsIndicator);

    button = new QToolButton();
    button->setDefaultAction(appAction(AppActions::DarkMode));
    button->setIcon(secondaryIconTheme->icon("dark-mode"));
    button->setFocusPolicy(Qt::NoFocus);

    rightLayout->addWidget(button, 0, Qt::AlignRight);
    statusBarWidgets.append(button);

    button = new QToolButton();
    button->setDefaultAction(appAction(AppActions::LayoutEditorOnly));
    button->setIcon(secondaryIconTheme->icon(QStringLiteral("layout-editor-only")));
    button->setIconSize(QSize(16, 16));
    button->setFocusPolicy(Qt::NoFocus);
    rightLayout->addWidget(button, 0, Qt::AlignRight);
    statusBarWidgets.append(button);

    button = new QToolButton();
    button->setDefaultAction(appAction(AppActions::LayoutSplit));
    button->setIcon(secondaryIconTheme->icon(QStringLiteral("view-layout")));
    button->setIconSize(QSize(16, 16));
    button->setFocusPolicy(Qt::NoFocus);
    rightLayout->addWidget(button, 0, Qt::AlignRight);
    statusBarWidgets.append(button);

    button = new QToolButton();
    button->setDefaultAction(appAction(AppActions::LayoutPreviewOnly));
    button->setIcon(secondaryIconTheme->icon(QStringLiteral("layout-preview-only")));
    button->setIconSize(QSize(16, 16));
    button->setFocusPolicy(Qt::NoFocus);
    rightLayout->addWidget(button, 0, Qt::AlignRight);
    statusBarWidgets.append(button);

    button = new QToolButton();
    button->setDefaultAction(appAction(AppActions::HemingwayMode));
    button->setIcon(secondaryIconTheme->icon("hemingway-mode"));
    button->setFocusPolicy(Qt::NoFocus);
    rightLayout->addWidget(button, 0, Qt::AlignRight);
    statusBarWidgets.append(button);

    button = new QToolButton();
    button->setDefaultAction(appAction(AppActions::DistractionFreeMode));
    button->setIcon(secondaryIconTheme->icon("distraction-free-mode"));
    button->setFocusPolicy(Qt::NoFocus);
    rightLayout->addWidget(button, 0, Qt::AlignRight);
    statusBarWidgets.append(button);

    button = new QToolButton();
    button->setDefaultAction(appAction(AppActions::FullScreen));
    button->setIcon(secondaryIconTheme->icon("full-screen"));
    button->setFocusPolicy(Qt::NoFocus);
    button->setObjectName("fullscreenButton");
    rightLayout->addWidget(button, 0, Qt::AlignRight);
    statusBarWidgets.append(button);

    rightWidget->setContentsMargins(0, 0, 0, 0);
    statusBarLayout->addWidget(rightWidget, 1, 2, 1, 1, Qt::AlignRight);

    QWidget *container = new QWidget(this);
    container->setObjectName("statusBarWidgetContainer");
    container->setLayout(statusBarLayout);
    container->setContentsMargins(0, 0, 2, 0);
    container->setStyleSheet("#statusBarWidgetContainer { border: 0; margin: 0; padding: 0 }");

    statusBar()->addWidget(container, 1);
    statusBar()->setSizeGripEnabled(false);
}

void MainWindow::setupSidebar()
{
    cheatSheetWidget = new QListWidget(this);

    cheatSheetWidget->setSelectionMode(QAbstractItemView::NoSelection);
    cheatSheetWidget->setAlternatingRowColors(false);

    cheatSheetWidget->addItem(tr("# Heading 1"));
    cheatSheetWidget->addItem(tr("## Heading 2"));
    cheatSheetWidget->addItem(tr("### Heading 3"));
    cheatSheetWidget->addItem(tr("#### Heading 4"));
    cheatSheetWidget->addItem(tr("##### Heading 5"));
    cheatSheetWidget->addItem(tr("###### Heading 6"));
    cheatSheetWidget->addItem(tr("*Emphasis* _Emphasis_"));
    cheatSheetWidget->addItem(tr("**Strong** __Strong__"));
    cheatSheetWidget->addItem(tr("1. Numbered List"));
    cheatSheetWidget->addItem(tr("* Bullet List"));
    cheatSheetWidget->addItem(tr("+ Bullet List"));
    cheatSheetWidget->addItem(tr("- Bullet List"));
    cheatSheetWidget->addItem(tr("> Block Quote"));
    cheatSheetWidget->addItem(tr("`Code Span`"));
    cheatSheetWidget->addItem(tr("``` Code Block"));
    cheatSheetWidget->addItem(tr("[Link](http://url.com \"Title\")"));
    cheatSheetWidget->addItem(tr("[Reference Link][ID]"));
    cheatSheetWidget->addItem(tr("[ID]: http://url.com \"Reference Definition\""));
    cheatSheetWidget->addItem(tr("![Image](./image.jpg \"Title\")"));
    cheatSheetWidget->addItem(tr("--- *** ___ Horizontal Rule"));

    documentStatsWidget = new DocumentStatisticsWidget(this);
    documentStatsWidget->setSelectionMode(QAbstractItemView::NoSelection);
    documentStatsWidget->setAlternatingRowColors(false);

    sessionStatsWidget = new SessionStatisticsWidget(this);
    sessionStatsWidget->setSelectionMode(QAbstractItemView::NoSelection);
    sessionStatsWidget->setAlternatingRowColors(false);

    outlineWidget = new OutlineWidget(nullptr, this);
    outlineWidget->setAlternatingRowColors(false);

    sessionStats = new SessionStatistics(this);
    connect(sessionStats, &SessionStatistics::wordCountChanged, sessionStatsWidget, &SessionStatisticsWidget::setWordCount);
    connect(sessionStats, &SessionStatistics::pageCountChanged, sessionStatsWidget, &SessionStatisticsWidget::setPageCount);
    connect(sessionStats, &SessionStatistics::wordsPerMinuteChanged, sessionStatsWidget, &SessionStatisticsWidget::setWordsPerMinute);
    connect(sessionStats, &SessionStatistics::writingTimeChanged, sessionStatsWidget, &SessionStatisticsWidget::setWritingTime);
    connect(sessionStats, &SessionStatistics::idleTimePercentageChanged, sessionStatsWidget, &SessionStatisticsWidget::setIdleTime);

    sidebar = new Sidebar(this);
    sidebar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    sidebar->setMinimumWidth(0.1 * layoutScreenAvailableWidth());
    sidebar->setMaximumWidth(0.5 * layoutScreenAvailableWidth());

    folderViewWidget = new FolderViewWidget(this);
    sidebar->addTab(primaryIconTheme->icon("open-file"), folderViewWidget, tr("Folder View"));
    sidebar->addTab(primaryIconTheme->icon("outline"), outlineWidget, tr("Outline"));
    sidebar->addTab(primaryIconTheme->icon("session-statistics"), sessionStatsWidget, tr("Session Statistics"));
    sidebar->addTab(primaryIconTheme->icon("document-statistics"), documentStatsWidget, tr("Document Statistics"));
    sidebar->addTab(primaryIconTheme->icon("cheat-sheet"), cheatSheetWidget, tr("Cheat Sheet"), "cheatSheetTab");

    int tabIndex = QSettings().value("sidebarCurrentTab", (int)FirstSidebarTab).toInt();

    if ((tabIndex < 0) || (tabIndex >= sidebar->tabCount())) {
        tabIndex = (int) FirstSidebarTab;
    }

    sidebar->setCurrentTabIndex(tabIndex);

    QPushButton *button = sidebar->addButton(primaryIconTheme->icon("settings"), tr("Settings"));
    connect(button, &QPushButton::clicked, button, [this, button]() {
        static QMenu *popupMenu = nullptr;

        if (nullptr == popupMenu) {
            popupMenu = new QMenu(button);
        }

        popupMenu->addAction(appAction(AppActions::ChangeTheme));
        popupMenu->addAction(appAction(AppActions::ChangeFont));
        popupMenu->addAction(appAction(AppActions::SwitchApplicationLanguage));
        popupMenu->addAction(appAction(AppActions::PreviewOptions));
        popupMenu->addAction(appAction(AppActions::Preferences));
        popupMenu->popup(button->mapToGlobal(QPoint(button->width() / 2, -(button->height() / 2) - 10)));
    });

    if (!sidebarHiddenForResize && !focusModeEnabled && appSettings->sidebarVisible()) {
        sidebar->setAutoHideEnabled(false);
        sidebar->setVisible(true);
    } else {
        sidebar->setAutoHideEnabled(true);
        sidebar->setVisible(false);
    }

    connect(sidebar, &Sidebar::visibilityChanged, this, &MainWindow::onSidebarVisibilityChanged);

    sidebar->setMinimumWidth(0.1 * layoutScreenWidth());
}

void MainWindow::adjustEditor()
{
    qApp->processEvents();

    int width = this->width();
    int sidebarWidth = 0;

    if (sidebar && sidebar->isVisible()) {
        sidebarWidth = sidebar->width();
    }

    if (previewStack && previewStack->isVisible() && editorStack && editorStack->isVisible()) {
        previewStack->setMaximumWidth((width - sidebarWidth) / 2);
    } else if (previewStack) {
        previewStack->setMaximumWidth(QWIDGETSIZE_MAX);
    }

    if (auto *ed = currentEditor()) {
        ed->setupPaperMargins();
        ed->centerCursor();
    }
}

void MainWindow::adjustTabBarHeight()
{
    if (!tabBar || !menuBar()) {
        return;
    }

    int h = menuBar()->sizeHint().height();
    if (h <= 0) {
        return;
    }

    tabBar->setFixedHeight(h);
    if (newTabButton) {
        newTabButton->setFixedHeight(h);
        newTabButton->setFixedWidth(h);
        const int iconPx = qMax(8, (h * 11) / 20);
        newTabButton->setIconSize(QSize(iconPx, iconPx));
    }
}

QString MainWindow::htmlPreviewStyleSheetForCurrentTheme() const
{
    ColorScheme colorScheme = theme.lightColorScheme();

    if (appSettings->darkModeEnabled()) {
        colorScheme = theme.darkColorScheme();
    }

    ChromeColors chromeColors(colorScheme);
    StyleSheetBuilder styler(chromeColors,
                             secondaryIconTheme,
                             (InterfaceStyleRounded == appSettings->interfaceStyle()),
                             appSettings->editorFont(),
                             appSettings->previewTextFont(),
                             appSettings->previewCodeFont(),
                             appSettings->editorWidth());

    return styler.htmlPreviewStyleSheet();
}

void MainWindow::applyHtmlPreviewStyleSheetToAllTabs(const QString &css)
{
    if (css.isNull()) {
        return;
    }

    for (auto *tab : tabs) {
        if (tab->htmlPreview()) {
            tab->htmlPreview()->setStyleSheet(css);
        }
    }
}

void MainWindow::applyTheme()
{
    if (!theme.name().isNull() && !theme.name().isEmpty()) {
        appSettings->setThemeName(theme.name());
    }

    ColorScheme colorScheme = theme.lightColorScheme();

    if (appSettings->darkModeEnabled()) {
        colorScheme = theme.darkColorScheme();
    }

    ChromeColors chromeColors(colorScheme);

    primaryIconTheme->setColor(QIcon::Normal, chromeColors.color(ChromeColors::SecondaryLabel, ChromeColors::NormalState));
    primaryIconTheme->setColor(QIcon::Active, chromeColors.color(ChromeColors::Label, ChromeColors::NormalState));
    primaryIconTheme->setColor(QIcon::Selected, chromeColors.color(ChromeColors::Label, ChromeColors::NormalState));
    primaryIconTheme->setColor(QIcon::Disabled, chromeColors.color(ChromeColors::SecondaryLabel, ChromeColors::DisabledState));

    secondaryIconTheme->setColor(QIcon::Normal, chromeColors.color(ChromeColors::SecondaryLabel, ChromeColors::NormalState));
    secondaryIconTheme->setColor(QIcon::Active, chromeColors.color(ChromeColors::SecondaryLabel, ChromeColors::ActiveState));
    secondaryIconTheme->setColor(QIcon::Selected, chromeColors.color(ChromeColors::SecondaryLabel, ChromeColors::PressedState));
    secondaryIconTheme->setColor(QIcon::Disabled, chromeColors.color(ChromeColors::SecondaryLabel, ChromeColors::DisabledState));

    StyleSheetBuilder styler(chromeColors,
                             secondaryIconTheme,
                             (InterfaceStyleRounded == appSettings->interfaceStyle()),
                             appSettings->editorFont(),
                             appSettings->previewTextFont(),
                             appSettings->previewCodeFont(),
                             appSettings->editorWidth());

    for (auto *tab : tabs) {
        tab->applyColorScheme(colorScheme);
        if (tab->spelling()) {
            tab->spelling()->setErrorColor(colorScheme.error);
        }
    }

    QString styleSheet = styler.widgetStyleSheet();

    if (styleSheet.isNull()) {
        qCritical() << "Invalid widget style sheet provided.";
    } else {
        qApp->style()->unpolish(qApp);
        qApp->style()->unpolish(this);
        qApp->setStyleSheet(styleSheet);
        qApp->style()->polish(qApp);
        qApp->style()->polish(this);
    }

    QString previewSheet = styler.htmlPreviewStyleSheet();

    if (previewSheet.isNull()) {
        qCritical() << "Invalid HTML preview style sheet provided.";
    } else {
        applyHtmlPreviewStyleSheetToAllTabs(previewSheet);
    }

    adjustTabBarHeight();
    adjustEditor();

    applyDarkModeToWindowFrame(this, appSettings->darkModeEnabled());
}

void MainWindow::runSpellCheck()
{
    auto *editor = currentEditor();
    auto *tab = currentTab();

    if (!editor || !tab) {
        return;
    }

    SpellCheckDialog *dialog = new SpellCheckDialog(editor);
    connect(dialog, &SpellCheckDialog::finished, tab->spelling(), &SpellCheckDecorator::rehighlight);

    dialog->show();
}

} // namespace ghostwriterpp
