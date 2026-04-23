/*
 * SPDX-FileCopyrightText: 2014-2024 Megan Conkle <megan.conkle@kdemail.net>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QAction>
#include <QContextMenuEvent>
#include <QFutureWatcher>
#include <QMenu>
#include <QVariant>
#include <QFile>
#include <QTextStream>
#include <QString>
#include <QRegularExpression>
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QStack>
#include <QDir>
#include <QDesktopServices>
#include <QtConcurrentRun>
#include <QFuture>
#include <QWebChannel>
#include <QEventLoop>
#include <QTextCursor>
#include <QTimer>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QWebEngineContextMenuRequest>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#endif

#include <export/cmarkgfmexporter.h>
#include <export/exporter.h>
#include <markdown/cmarkgfmapi.h>
#include <markdown/previeweditmetadata.h>
#include "htmlpreview.h"
#include "previewproxy.h"
#include "sandboxedwebpage.h"

namespace ghostwriterpp
{
class HtmlPreviewPrivate
{
    Q_DECLARE_PUBLIC(HtmlPreview)

public:
    HtmlPreviewPrivate(HtmlPreview *q_ptr)
        : q_ptr(q_ptr)
    {
        proxy = new PreviewProxy(q_ptr);
    }

    ~HtmlPreviewPrivate()
    {
        ;
    }

    HtmlPreview *q_ptr;

    MarkdownDocument *document;
    bool updateInProgress;
    bool updateAgain;
    bool pendingRefresh = false;
    PreviewProxy *proxy;
    QString baseUrl;
    QRegularExpression headingTagExp;
    Exporter *exporter;
    QString wrapperHtml;
    QFutureWatcher<QString> *futureWatcher;

    struct PreviewEditSession {
        bool active = false;
        QString kind;
        int elementSourceStart = 0;
        int elementSourceEnd = 0;
        QChar forbiddenDelimiter;
        int inlineCodeOpenFenceLen = 0;
        QString originalPlain;
        std::vector<TextNodeSlot> textNodes;
        bool allowSoftbreaks = false;
        bool allowParagraphGaps = false;
        bool codeblockWholeSourceReplace = false;
    } previewEditSession;

    bool previewApplying = false;

    void onHtmlReady();
    void onLoadFinished(bool ok);
    void onDocumentContentsChanged();

    /**
     * Sets the base directory path for determining resource
     * paths relative to the web page being previewed.
     * This method is called whenever the file path changes.
     */
    void updateBaseDir();
    /*
    * Sets the HTML contents to display, and creates a backup of the old
    * HTML for diffing to scroll to the first difference whenever
    * updatePreview() is called.
    */
    void setHtmlContent(const QString &html);

    static QString exportToHtml(const QString &text, Exporter *exporter);
};

void HtmlPreviewPrivate::onDocumentContentsChanged()
{
    Q_Q(HtmlPreview);

    if (previewApplying) {
        return;
    }

    if (previewEditSession.active) {
        previewEditSession.active = false;
        q->updatePreview();
    }
}

HtmlPreview::HtmlPreview
(
    MarkdownDocument *document,
    Exporter *exporter,
    QWidget *parent
) : QWebEngineView(parent),
    d_ptr(new HtmlPreviewPrivate(this))
{
    Q_D(HtmlPreview);
    
    d->document = document;
    d->updateInProgress = false;
    d->updateAgain = false;
    d->pendingRefresh = false;
    d->exporter = exporter;
    d->proxy->setMathEnabled(d->exporter->supportsMath());

    d->baseUrl = "";

    this->setPage(new SandboxedWebPage(this));
    this->settings()->setDefaultTextEncoding("utf-8");
    this->settings()->setAttribute(
        QWebEngineSettings::LocalContentCanAccessFileUrls,
        true);
    this->settings()->setAttribute(
        QWebEngineSettings::LocalContentCanAccessRemoteUrls,
        true);
    this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    this->page()->action(QWebEnginePage::Reload)->setVisible(false);
    this->page()->action(QWebEnginePage::ReloadAndBypassCache)
        ->setVisible(false);
    this->page()->action(QWebEnginePage::OpenLinkInThisWindow)
        ->setVisible(false);
    this->page()->action(QWebEnginePage::OpenLinkInNewWindow)
        ->setVisible(false);
    this->page()->action(QWebEnginePage::ViewSource)->setVisible(false);
    this->page()->action(QWebEnginePage::SavePage)->setVisible(false);
    QWebEngineProfile::defaultProfile()
        ->setHttpCacheType(QWebEngineProfile::NoCache);
    QWebEngineProfile::defaultProfile()->clearHttpCache();
    QWebEngineProfile::defaultProfile()->clearAllVisitedLinks();

    this->connect(
        this,
        &QWebEngineView::loadFinished,
        [d](bool ok) {
            d->onLoadFinished(ok);
        }
    );

    d->headingTagExp.setPattern("^[Hh][1-6]$");

    d->futureWatcher = new QFutureWatcher<QString>(this);
    this->connect(
        d->futureWatcher,
        &QFutureWatcher<QString>::finished,
        [d]() {
            d->onHtmlReady();
        }
    );

    this->connect(
        document,
        &MarkdownDocument::filePathChanged,
        [d]() {
            d->updateBaseDir();
        }
    );

    this->connect(document, &QTextDocument::contentsChanged, [d]() {
        d->onDocumentContentsChanged();
    });

    // Set zoom factor for Chromium browser to account for system DPI settings,
    // since Chromium assumes 96 DPI as a fixed resolution.
    //
    qreal horizontalDpi = 96.0;
    if (QScreen *ps = QGuiApplication::primaryScreen()) {
        horizontalDpi = ps->logicalDotsPerInchX();
    } else {
        const QList<QScreen *> screens = QGuiApplication::screens();
        if (!screens.isEmpty()) {
            horizontalDpi = screens.first()->logicalDotsPerInchX();
        }
    }
    setZoomFactor(horizontalDpi / 96.0);

    QWebChannel *channel = new QWebChannel(this);
    channel->registerObject(QStringLiteral("previewProxy"), d->proxy);
    this->page()->setWebChannel(channel);

    QFile wrapperHtmlFile(":/resources/preview.html");

    if (!wrapperHtmlFile.open(QFile::ReadOnly | QFile::Text)) {
        d->wrapperHtml = tr("Error loading resources/preview.html");
    } else {
        QTextStream stream(&wrapperHtmlFile);
        d->wrapperHtml = stream.readAll();
        wrapperHtmlFile.close();
    }

    // Set the base URL and load the preview using wrapperHtml above.
    d->updateBaseDir();
}

HtmlPreview::~HtmlPreview()
{
    shutdownBeforeDestroy();
}

void HtmlPreview::shutdownBeforeDestroy()
{
    Q_D(HtmlPreview);

    d->previewEditSession.active = false;
    d->updateInProgress = false;
    d->updateAgain = false;
    d->pendingRefresh = false;

    if (d->futureWatcher->isRunning()) {
        d->futureWatcher->disconnect();
        d->futureWatcher->cancel();
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(d->futureWatcher, &QFutureWatcherBase::finished, &loop, &QEventLoop::quit);
        timer.start(3000);
        loop.exec(QEventLoop::ExcludeUserInputEvents);
        if (d->futureWatcher->isRunning()) {
            qWarning() << "ghostwriter++: preview export did not finish before shutdown; continuing.";
        }
    }

    d->setHtmlContent("");
}

void HtmlPreview::contextMenuEvent(QContextMenuEvent *event)
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QMenu *menu = page()->createStandardContextMenu(event->pos());
#else
    QMenu *menu = createStandardContextMenu();
#endif

    QUrl linkUrl;
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    linkUrl = page()->contextMenuData().linkUrl();
#else
    if (QWebEngineContextMenuRequest *r = lastContextMenuRequest()) {
        linkUrl = r->linkUrl();
    }
#endif

    if (linkUrl.isValid() && !linkUrl.isEmpty()) {
        auto *openInBrowser = new QAction(tr("Open in browser"), menu);
        QObject::connect(
            openInBrowser,
            &QAction::triggered,
            [u = QUrl(linkUrl)]() { QDesktopServices::openUrl(u); });

        QAction *copyLink = page()->action(QWebEnginePage::CopyLinkToClipboard);
        const QList<QAction *> items = menu->actions();
        int idx = -1;
        if (copyLink) {
            idx = items.indexOf(copyLink);
        }
        if (idx >= 0 && idx + 1 < items.size()) {
            menu->insertAction(items.at(idx + 1), openInBrowser);
        } else {
            menu->addAction(openInBrowser);
        }
    }

    menu->popup(event->globalPos());
}

void HtmlPreview::updatePreview()
{
    Q_D(HtmlPreview);

    if (d->previewEditSession.active) {
        return;
    }
    
    if (d->updateInProgress) {
        d->updateAgain = true;
        return;
    }

    if (!this->isVisible()) {
        d->pendingRefresh = true;
        return;
    }

    d->pendingRefresh = false;

    // Some markdown processors don't handle empty text very well
    // and will err.  Thus, only pass in text from the document
    // into the markdown processor if the text isn't empty or null.
    //
    if (d->document->isEmpty()) {
        d->setHtmlContent("");
    } else if (nullptr != d->exporter) {
        QString text = d->document->toPlainText();

        if (!text.isNull() && !text.isEmpty()) {
            d->updateInProgress = true;
            QFuture<QString> future =
                QtConcurrent::run
                (
                    &HtmlPreviewPrivate::exportToHtml,
                    d->document->toPlainText(),
                    d->exporter
                );
            d->futureWatcher->setFuture(future);
        }
    }
}

void HtmlPreview::navigateToHeading(int headingSequenceNumber)
{
    this->page()->runJavaScript
    (
        QString
        (
            "scrollToHeading(%1);"
        ).arg(headingSequenceNumber)
    );
}

void HtmlPreview::setHtmlExporter(Exporter *exporter)
{
    Q_D(HtmlPreview);
    
    d->exporter = exporter;
    d->previewEditSession.active = false;
    d->setHtmlContent("");
    d->proxy->setMathEnabled(d->exporter->supportsMath());
    updatePreview();
}

void HtmlPreview::setStyleSheet(const QString &css)
{
    Q_D(HtmlPreview);

    d->proxy->setStyleSheet(css);
}

void HtmlPreview::setMathEnabled(bool enabled)
{
    Q_D(HtmlPreview);

    d->proxy->setMathEnabled(enabled);
}

void HtmlPreviewPrivate::onHtmlReady()
{
    Q_Q(HtmlPreview);
    
    setHtmlContent(futureWatcher->result());
    updateInProgress = false;

    if (updateAgain) {
        updateAgain = false;
        q->updatePreview();
    }

}

void HtmlPreviewPrivate::onLoadFinished(bool ok)
{
    Q_UNUSED(ok);
}

void HtmlPreviewPrivate::updateBaseDir()
{
    Q_Q(HtmlPreview);
    
    if (!document->filePath().isNull() && !document->filePath().isEmpty()) {
        // Note that a forward slash ("/") is appended to the path to
        // ensure it works.  If the slash isn't there, then it won't
        // recognize the base URL for some reason.
        //
        baseUrl = QUrl::fromLocalFile(
            QFileInfo(document->filePath()).dir().absolutePath() 
                      + "/").toString();
    } else {
        this->baseUrl = "";
    }

    q->setHtml(wrapperHtml, QUrl(baseUrl));
    q->updatePreview();
}

void HtmlPreview::closeEvent(QCloseEvent *event)
{
    Q_UNUSED(event);
    Q_D(HtmlPreview);

    d->previewEditSession.active = false;
    d->proxy->setHtmlContent("");
}

void HtmlPreview::showEvent(QShowEvent *event)
{
    QWebEngineView::showEvent(event);
    Q_D(HtmlPreview);

    if (d->pendingRefresh && !d->previewEditSession.active) {
        updatePreview();
    }
}

void HtmlPreviewPrivate::setHtmlContent(const QString &html)
{
    if (previewEditSession.active) {
        return;
    }
    this->proxy->setHtmlContent(html);
}

QString HtmlPreviewPrivate::exportToHtml
(
    const QString &text,
    Exporter *exporter
)
{
    QString html;

    // Enable smart typography for preview, if available for the exporter.
    bool smartTypographyEnabled = exporter->smartTypographyEnabled();
    exporter->setSmartTypographyEnabled(true);

    if (dynamic_cast<CmarkGfmExporter *>(exporter) != nullptr) {
        html = CmarkGfmAPI::instance()->renderToHtmlWithPreviewEditMetadata(text, true);
    } else {
        exporter->exportToHtml(text, html);
    }

    // Put smart typography setting back to the way it was before
    // so that the last setting used during document export is remembered.
    //
    exporter->setSmartTypographyEnabled(smartTypographyEnabled);

    return html;
}

void HtmlPreview::beginPreviewEditSession(const QString &kind, int start, int end)
{
    Q_D(HtmlPreview);

    d->previewEditSession.active = false;
    d->previewEditSession.textNodes.clear();
    d->previewEditSession.originalPlain.clear();
    d->previewEditSession.forbiddenDelimiter = QChar();
    d->previewEditSession.inlineCodeOpenFenceLen = 0;
    d->previewEditSession.allowSoftbreaks = false;
    d->previewEditSession.allowParagraphGaps = false;
    d->previewEditSession.codeblockWholeSourceReplace = false;

    const QString plain = d->document->toPlainText();
    const bool isTableCell = (kind == QLatin1String("tablecell"));

    if (kind != QLatin1String("heading") && kind != QLatin1String("link")
        && kind != QLatin1String("text") && kind != QLatin1String("emph")
        && kind != QLatin1String("strong") && kind != QLatin1String("strikethrough")
        && kind != QLatin1String("codespan") && kind != QLatin1String("codeblock")
        && kind != QLatin1String("listitem") && kind != QLatin1String("paragraph")
        && !isTableCell) {
        return;
    }

    if (start < 0 || end > plain.size() || start >= end) {
        return;
    }

    QChar delimForbidden;
    if (kind == QLatin1String("strikethrough")) {
        delimForbidden = u'~';
    } else if (kind == QLatin1String("strong")) {
        if (start >= 2 && plain.mid(start - 2, 2) == QLatin1String("**")) {
            delimForbidden = u'*';
        } else if (start >= 2 && plain.mid(start - 2, 2) == QLatin1String("__")) {
            delimForbidden = u'_';
        } else {
            return;
        }
    } else if (kind == QLatin1String("emph")) {
        if (start >= 1 && end < plain.size() && plain.at(start - 1) == u'*' && plain.at(end) == u'*') {
            delimForbidden = u'*';
        } else if (start >= 1 && end < plain.size() && plain.at(start - 1) == u'_' && plain.at(end) == u'_') {
            delimForbidden = u'_';
        } else {
            return;
        }
    }

    const bool isListItem = (kind == QLatin1String("listitem"));
    const bool isParagraph = (kind == QLatin1String("paragraph"));
    const bool allowSoftbreaks = isListItem || isParagraph;
    const bool allowParagraphGaps = isListItem
        && QStringView(plain).mid(start, end - start).indexOf(QLatin1String("\n\n")) >= 0;

    PreviewEditTextMap map = CmarkGfmAPI::instance()->extractPreviewEditTextMap(
        plain, start, end, allowSoftbreaks, allowParagraphGaps, isTableCell);

    if (!map.valid) {
        return;
    }

    if (kind == QLatin1String("codespan")) {
        int n = 0;
        for (int i = start - 1; i >= 0 && plain.at(i) == u'`'; --i) {
            ++n;
        }
        if (n < 1) {
            return;
        }
        d->previewEditSession.inlineCodeOpenFenceLen = n;
    }

    d->previewEditSession.active = true;
    d->previewEditSession.kind = kind;
    d->previewEditSession.elementSourceStart = start;
    d->previewEditSession.elementSourceEnd = end;
    d->previewEditSession.forbiddenDelimiter = delimForbidden;
    d->previewEditSession.originalPlain = map.plain;
    d->previewEditSession.textNodes = std::move(map.nodes);
    d->previewEditSession.allowSoftbreaks = allowSoftbreaks;
    d->previewEditSession.allowParagraphGaps = allowParagraphGaps;
    d->previewEditSession.codeblockWholeSourceReplace = map.codeblockWholeSourceReplace;
}

void HtmlPreview::applyPreviewEdit(const QString &text)
{
    Q_D(HtmlPreview);

    if (!d->previewEditSession.active) {
        return;
    }

    const QString &k = d->previewEditSession.kind;
    if (k == QLatin1String("link")) {
        if (text.contains(u']') || text.contains(u'\n') || text.contains(u'\r')) {
            return;
        }
    } else if (k == QLatin1String("tablecell")) {
        if (text.contains(u'|') || text.contains(u'\n') || text.contains(u'\r')) {
            return;
        }
    } else if (k == QLatin1String("heading") || k == QLatin1String("text")) {
        if (text.contains(u'\n') || text.contains(u'\r')) {
            return;
        }
    } else if (k == QLatin1String("emph") || k == QLatin1String("strong")
               || k == QLatin1String("strikethrough")) {
        if (text.contains(u'\n') || text.contains(u'\r')) {
            return;
        }
        const QChar fd = d->previewEditSession.forbiddenDelimiter;
        if (!fd.isNull() && text.contains(fd)) {
            return;
        }
    } else if (k == QLatin1String("codeblock")) {
        if (previewCodeblockTextHasFenceLikeLine(text)) {
            return;
        }
    } else if (k == QLatin1String("listitem") || k == QLatin1String("paragraph")) {
        if (text.contains(u'\r')) {
            return;
        }
        if (previewListItemTextHasListStarter(text)) {
            return;
        }
        const bool blockParagraphGapNewlines = (k == QLatin1String("paragraph"))
            || !d->previewEditSession.allowParagraphGaps;
        if (blockParagraphGapNewlines && text.contains(QLatin1String("\n\n"))) {
            return;
        }
    }

    if (k == QLatin1String("codeblock") && d->previewEditSession.codeblockWholeSourceReplace) {
        const QString newMd = serializeIndentedCodeBlock(text);
        const QString plainDoc = d->document->toPlainText();
        const int bs = d->previewEditSession.elementSourceStart;
        const int be = d->previewEditSession.elementSourceEnd;
        if (bs < 0 || be > plainDoc.size() || bs >= be) {
            endPreviewEditSession();
            return;
        }
        d->previewApplying = true;
        QTextCursor c(d->document);
        c.beginEditBlock();
        c.setPosition(bs);
        c.setPosition(be, QTextCursor::KeepAnchor);
        c.insertText(newMd);
        c.endEditBlock();
        d->previewApplying = false;
        const int cbDelta = newMd.size() - (be - bs);
        d->previewEditSession.elementSourceEnd = bs + newMd.size();
        d->previewEditSession.originalPlain = text;
        if (cbDelta != 0 && d->proxy) {
            emit d->proxy->previewEditOffsetsShifted(be, cbDelta);
        }
        return;
    }

    if (k == QLatin1String("codespan")) {
        if (text.contains(u'\n') || text.contains(u'\r')) {
            return;
        }
        if (longestBacktickRun(text) >= d->previewEditSession.inlineCodeOpenFenceLen) {
            return;
        }
        const QString newRaw = serializeInlineCodeRawInner(text, d->previewEditSession.inlineCodeOpenFenceLen);
        if (newRaw.isEmpty() && !text.isEmpty()) {
            return;
        }
        if (d->previewEditSession.textNodes.size() != 1U) {
            return;
        }
        TextNodeSlot &tn = d->previewEditSession.textNodes[0];
        const int srcReplaceStart = tn.sourceStart;
        const int srcReplaceEnd = tn.sourceEnd;
        const QString plainDoc = d->document->toPlainText();
        if (srcReplaceStart < 0 || srcReplaceEnd > plainDoc.size() || srcReplaceStart > srcReplaceEnd) {
            endPreviewEditSession();
            return;
        }
        d->previewApplying = true;
        QTextCursor c(d->document);
        c.beginEditBlock();
        c.setPosition(srcReplaceStart);
        c.setPosition(srcReplaceEnd, QTextCursor::KeepAnchor);
        c.insertText(newRaw);
        c.endEditBlock();
        d->previewApplying = false;
        const int delta = newRaw.size() - (srcReplaceEnd - srcReplaceStart);
        const int oldElementEnd = d->previewEditSession.elementSourceEnd;
        tn.sourceEnd = tn.sourceStart + newRaw.size();
        tn.plainEnd = static_cast<int>(text.size());
        d->previewEditSession.elementSourceEnd += delta;
        d->previewEditSession.originalPlain = text;
        if (delta != 0 && d->proxy) {
            emit d->proxy->previewEditOffsetsShifted(oldElementEnd, delta);
        }
        return;
    }

    const QString &originalPlain = d->previewEditSession.originalPlain;
    const QString &newPlain = text;
    const int oldLen = originalPlain.size();
    const int newLen = newPlain.size();

    int cp = 0;
    while (cp < oldLen && cp < newLen && originalPlain.at(cp) == newPlain.at(cp)) {
        ++cp;
    }

    int cs = 0;
    while (cs < oldLen - cp
           && cs < newLen - cp
           && originalPlain.at(oldLen - 1 - cs) == newPlain.at(newLen - 1 - cs)) {
        ++cs;
    }

    const int oldMidStart = cp;
    const int oldMidEnd = oldLen - cs;
    const int newMidStart = cp;
    const int newMidEnd = newLen - cs;

    if (oldMidStart == oldMidEnd && newMidStart == newMidEnd) {
        return;
    }

    const QString newMiddle = newPlain.mid(newMidStart, newMidEnd - newMidStart);

    const auto &textNodes = d->previewEditSession.textNodes;

    int srcReplaceStart = -1;
    int srcReplaceEnd = -1;
    int editedNodeIdx = -1;

    if (oldMidStart == oldMidEnd) {
        const int insertPos = oldMidStart;
        int idx = -1;
        for (size_t i = 0; i < textNodes.size(); ++i) {
            if (textNodes[i].plainEnd >= insertPos) {
                if (textNodes[i].plainStart <= insertPos) {
                    idx = static_cast<int>(i);
                }
                break;
            }
        }
        if (idx < 0) {
            return;
        }

        const TextNodeSlot &tn = textNodes[idx];
        int srcPos;
        if (insertPos <= tn.plainStart) {
            srcPos = tn.sourceStart;
        } else if (insertPos >= tn.plainEnd) {
            srcPos = tn.sourceEnd;
        } else {
            srcPos = tn.sourceStart + (insertPos - tn.plainStart);
        }
        srcReplaceStart = srcPos;
        srcReplaceEnd = srcPos;
        editedNodeIdx = idx;
    } else {
        int idx = -1;
        for (size_t i = 0; i < textNodes.size(); ++i) {
            if (textNodes[i].plainStart <= oldMidStart
                && oldMidEnd <= textNodes[i].plainEnd) {
                idx = static_cast<int>(i);
                break;
            }
        }
        if (idx < 0) {
            return;
        }

        const TextNodeSlot &tn = textNodes[idx];
        srcReplaceStart = tn.sourceStart + (oldMidStart - tn.plainStart);
        srcReplaceEnd = tn.sourceStart + (oldMidEnd - tn.plainStart);
        editedNodeIdx = idx;
    }

    const QString plainDoc = d->document->toPlainText();
    if (srcReplaceStart < 0
        || srcReplaceEnd > plainDoc.size()
        || srcReplaceStart > srcReplaceEnd) {
        endPreviewEditSession();
        return;
    }

    d->previewApplying = true;
    QTextCursor c(d->document);
    c.beginEditBlock();
    c.setPosition(srcReplaceStart);
    c.setPosition(srcReplaceEnd, QTextCursor::KeepAnchor);
    c.insertText(newMiddle);
    c.endEditBlock();
    d->previewApplying = false;

    const int delta = newMiddle.size() - (srcReplaceEnd - srcReplaceStart);
    const int plainDelta = (newMidEnd - newMidStart) - (oldMidEnd - oldMidStart);
    for (size_t i = 0; i < d->previewEditSession.textNodes.size(); ++i) {
        TextNodeSlot &tn = d->previewEditSession.textNodes[i];
        if (static_cast<int>(i) == editedNodeIdx) {
            tn.sourceEnd += delta;
            tn.plainEnd += plainDelta;
        } else {
            if (tn.sourceStart >= srcReplaceEnd) {
                tn.sourceStart += delta;
                tn.sourceEnd += delta;
            }
            if (tn.plainStart >= oldMidEnd) {
                tn.plainStart += plainDelta;
                tn.plainEnd += plainDelta;
            }
        }
    }
    const int oldElementEnd = d->previewEditSession.elementSourceEnd;
    d->previewEditSession.elementSourceEnd += delta;
    d->previewEditSession.originalPlain = newPlain;
    if (delta != 0 && d->proxy) {
        emit d->proxy->previewEditOffsetsShifted(oldElementEnd, delta);
    }
}

void HtmlPreview::endPreviewEditSession()
{
    Q_D(HtmlPreview);

    const bool was = d->previewEditSession.active;
    d->previewEditSession.active = false;

    if (was) {
        updatePreview();
    }
}

void HtmlPreview::togglePreviewCheckbox(int offset, bool checked)
{
    Q_D(HtmlPreview);

    const QString plain = d->document->toPlainText();
    if (offset < 0 || offset >= plain.size()) {
        return;
    }
    const QChar cur = plain.at(offset);
    if (cur != u' ' && cur != u'x' && cur != u'X') {
        return;
    }
    const QChar target = checked ? QChar(u'x') : QChar(u' ');
    if (cur == target) {
        return;
    }

    d->previewApplying = true;
    QTextCursor c(d->document);
    c.beginEditBlock();
    c.setPosition(offset);
    c.setPosition(offset + 1, QTextCursor::KeepAnchor);
    c.insertText(QString(target));
    c.endEditBlock();
    d->previewApplying = false;

    updatePreview();
}
} // namespace ghostwriterpp
