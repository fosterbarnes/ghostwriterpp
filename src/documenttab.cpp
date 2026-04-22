/*
 * SPDX-FileCopyrightText: 2026 Nate Peterson
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "documenttab.h"

#include "editor/markdowndocument.h"
#include "editor/markdowneditor.h"
#include "preview/htmlpreview.h"
#include "settings/appsettings.h"
#include "spelling/spellcheckdecorator.h"
#include "statistics/documentstatistics.h"
#include "documentmanager.h"

namespace ghostwriterpp
{

DocumentTab::DocumentTab(const ColorScheme &colors, QObject *parent)
    : QObject(parent)
{
    AppSettings *appSettings = AppSettings::instance();

    m_document = new MarkdownDocument();

    m_editor = new MarkdownEditor(m_document, colors, nullptr);
    m_editor->setFont(appSettings->editorFont().family(), appSettings->editorFont().pointSize());
    m_editor->setUseUnderlineForEmphasis(appSettings->useUnderlineForEmphasis());
    m_editor->setEnableLargeHeadingSizes(appSettings->largeHeadingSizesEnabled());
    m_editor->setAutoMatchEnabled(appSettings->autoMatchEnabled());
    m_editor->setBulletPointCyclingEnabled(appSettings->bulletPointCyclingEnabled());
    m_editor->setPlainText("");
    m_editor->setEditorWidth((EditorWidth)appSettings->editorWidth());
    m_editor->setEditorCorners((InterfaceStyle)appSettings->interfaceStyle());
    m_editor->setItalicizeBlockquotes(appSettings->italicizeBlockquotes());
    m_editor->setTabulationWidth(appSettings->tabWidth());
    m_editor->setInsertSpacesForTabs(appSettings->insertSpacesForTabsEnabled());
    m_editor->setShowUnbreakableSpaces(appSettings->showUnbreakableSpaceEnabled());

    m_editor->setAutoMatchEnabled('\"', appSettings->autoMatchCharEnabled('\"'));
    m_editor->setAutoMatchEnabled('\'', appSettings->autoMatchCharEnabled('\''));
    m_editor->setAutoMatchEnabled('(', appSettings->autoMatchCharEnabled('('));
    m_editor->setAutoMatchEnabled('[', appSettings->autoMatchCharEnabled('['));
    m_editor->setAutoMatchEnabled('{', appSettings->autoMatchCharEnabled('{'));
    m_editor->setAutoMatchEnabled('*', appSettings->autoMatchCharEnabled('*'));
    m_editor->setAutoMatchEnabled('_', appSettings->autoMatchCharEnabled('_'));
    m_editor->setAutoMatchEnabled('`', appSettings->autoMatchCharEnabled('`'));
    m_editor->setAutoMatchEnabled('<', appSettings->autoMatchCharEnabled('<'));

    connect(appSettings, &AppSettings::tabWidthChanged, m_editor, &MarkdownEditor::setTabulationWidth);
    connect(appSettings, &AppSettings::insertSpacesForTabsChanged, m_editor, &MarkdownEditor::setInsertSpacesForTabs);
    connect(appSettings, &AppSettings::showUnbreakableSpaceEnabledChanged, m_editor, &MarkdownEditor::setShowUnbreakableSpaces);
    connect(appSettings, &AppSettings::useUnderlineForEmphasisChanged, m_editor, &MarkdownEditor::setUseUnderlineForEmphasis);
    connect(appSettings, &AppSettings::italicizeBlockquotesChanged, m_editor, &MarkdownEditor::setItalicizeBlockquotes);
    connect(appSettings, &AppSettings::largeHeadingSizesChanged, m_editor, &MarkdownEditor::setEnableLargeHeadingSizes);
    connect(appSettings, &AppSettings::autoMatchChanged, m_editor, QOverload<bool>::of(&MarkdownEditor::setAutoMatchEnabled));
    connect(appSettings, &AppSettings::autoMatchCharChanged, m_editor, QOverload<QChar, bool>::of(&MarkdownEditor::setAutoMatchEnabled));
    connect(appSettings, &AppSettings::bulletPointCyclingChanged, m_editor, &MarkdownEditor::setBulletPointCyclingEnabled);

    m_documentManager = new DocumentManager(m_editor, this);
    m_documentManager->setAutoSaveEnabled(appSettings->autoSaveEnabled());
    m_documentManager->setFileBackupEnabled(appSettings->backupFileEnabled());
    m_documentManager->setDraftLocation(appSettings->draftLocation());
    m_documentManager->setBackupLocation(appSettings->backupLocation());
    m_documentManager->setFileHistoryEnabled(appSettings->fileHistoryEnabled());
    m_documentManager->setRestoreSessionEnabled(appSettings->restoreSessionEnabled());

    connect(appSettings, &AppSettings::autoSaveChanged, m_documentManager, &DocumentManager::setAutoSaveEnabled);
    connect(appSettings, &AppSettings::backupFileChanged, m_documentManager, &DocumentManager::setFileBackupEnabled);
    connect(appSettings, &AppSettings::backupLocationChanged, m_documentManager, &DocumentManager::setBackupLocation);

    m_spelling = new SpellCheckDecorator(m_editor);
    connect(appSettings, &AppSettings::spellCheckSettingsChanged, m_spelling, &SpellCheckDecorator::settingsChanged);

    auto *exp = appSettings->currentHtmlExporter();
    m_htmlPreview = new HtmlPreview(m_document, exp, nullptr);
    m_htmlPreview->setMinimumWidth(100);
    m_htmlPreview->setObjectName("htmlpreview");

    connect(m_editor, &MarkdownEditor::typingPausedScaled, m_htmlPreview, &HtmlPreview::updatePreview);
    connect(m_documentManager, &DocumentManager::documentLoaded, m_htmlPreview, &HtmlPreview::updatePreview);
    connect(m_documentManager, &DocumentManager::documentClosed, m_htmlPreview, &HtmlPreview::updatePreview);
    connect(appSettings, &AppSettings::currentHtmlExporterChanged, m_htmlPreview, &HtmlPreview::setHtmlExporter);

    m_documentStats = new DocumentStatistics(m_document, this);
    connect(m_editor, &MarkdownEditor::textSelected, m_documentStats, &DocumentStatistics::onTextSelected);
    connect(m_editor, &MarkdownEditor::textDeselected, m_documentStats, &DocumentStatistics::onTextDeselected);
}

DocumentTab::~DocumentTab()
{
    AppSettings *appSettings = AppSettings::instance();
    if (appSettings) {
        QObject::disconnect(appSettings, nullptr, m_editor.data(), nullptr);
        QObject::disconnect(appSettings, nullptr, m_documentManager.data(), nullptr);
        QObject::disconnect(appSettings, nullptr, m_spelling.data(), nullptr);
        QObject::disconnect(appSettings, nullptr, m_htmlPreview.data(), nullptr);
    }

    if (m_editor && m_htmlPreview) {
        QObject::disconnect(m_editor.data(), nullptr, m_htmlPreview.data(), nullptr);
    }
    if (m_documentManager && m_htmlPreview) {
        QObject::disconnect(m_documentManager.data(), nullptr, m_htmlPreview.data(), nullptr);
    }

    // DocumentManager and DocumentStatistics must go away while the editor
    // and document are still valid — their private state keeps raw pointers.
    delete m_documentManager.data();
    delete m_documentStats.data();

    if (m_htmlPreview) {
        m_htmlPreview->disconnect();
        delete m_htmlPreview.data();
    }

    if (m_editor) {
        m_editor->disconnect();
        delete m_editor.data();
    }

    delete m_document.data();
}

MarkdownEditor *DocumentTab::editor() const { return m_editor; }
MarkdownDocument *DocumentTab::document() const { return m_document; }
DocumentManager *DocumentTab::documentManager() const { return m_documentManager; }
HtmlPreview *DocumentTab::htmlPreview() const { return m_htmlPreview; }
SpellCheckDecorator *DocumentTab::spelling() const { return m_spelling; }
DocumentStatistics *DocumentTab::documentStats() const { return m_documentStats; }

void DocumentTab::applyColorScheme(const ColorScheme &colors)
{
    if (m_editor) {
        m_editor->setColorScheme(colors);
    }
}

void DocumentTab::releaseHtmlPreview()
{
    if (!m_htmlPreview) {
        return;
    }

    m_htmlPreview->shutdownBeforeDestroy();
    delete m_htmlPreview.data();
}

} // namespace ghostwriterpp
