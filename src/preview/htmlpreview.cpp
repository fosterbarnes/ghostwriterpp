/*
 * SPDX-FileCopyrightText: 2014-2024 Megan Conkle <megan.conkle@kdemail.net>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QFutureWatcher>
#include <QMenu>
#include <QVariant>
#include <QFile>
#include <QTextStream>
#include <QString>
#include <QApplication>
#include <QStack>
#include <QDir>
#include <QDesktopServices>
#include <QtConcurrentRun>
#include <QFuture>
#include <QWebChannel>
#include <QEventLoop>
#include <QMetaObject>
#include <QTextCursor>
#include <QTimer>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#endif

#include <export/exporter.h>
#include "htmlpreview.h"
#include "previewproxy.h"
#include "sandboxedwebpage.h"

namespace {

void computeDiffReplacement(
    const QString &oldPlain,
    const QString &newPlain,
    QString *oldChunk,
    QString *newChunk,
    int *prefixLen)
{
    *oldChunk = QString();
    *newChunk = QString();
    *prefixLen = 0;
    if (oldPlain == newPlain) {
        return;
    }

    int i = 0;
    const int oldLen = oldPlain.size();
    const int newLen = newPlain.size();
    while (i < oldLen && i < newLen && oldPlain.at(i) == newPlain.at(i)) {
        ++i;
    }
    int j = 0;
    while (j < (oldLen - i) && j < (newLen - i)
           && oldPlain.at(oldLen - 1 - j) == newPlain.at(newLen - 1 - j)) {
        ++j;
    }
    *prefixLen = i;
    *oldChunk = oldPlain.mid(i, oldLen - i - j);
    *newChunk = newPlain.mid(i, newLen - i - j);
}

} // namespace

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
    PreviewProxy *proxy;
    QString baseUrl;
    QRegularExpression headingTagExp;
    Exporter *exporter;
    QString wrapperHtml;
    QFutureWatcher<QString> *futureWatcher;

    QString previewPlainBaseline;
    bool previewDirty;
    bool inPlaceEditingEnabled;

    void onHtmlReady();
    void onLoadFinished(bool ok);
    void flushPreviewEditsToMarkdown();
    bool applyPreviewPlainDiffToDocument(const QString &editedPlain);
    void onPreviewEditedFromJs();

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
    d->exporter = exporter;
    d->proxy->setMathEnabled(d->exporter->supportsMath());
    d->previewDirty = false;
    d->inPlaceEditingEnabled = false;

    connect(
        d->proxy,
        &PreviewProxy::previewPlainBaselineChanged,
        this,
        [d](const QString &plain) {
            d->previewPlainBaseline = plain;
        });
    connect(
        d->proxy,
        &PreviewProxy::previewEdited,
        this,
        [d]() {
            d->onPreviewEditedFromJs();
        });

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

    // Set zoom factor for Chromium browser to account for system DPI settings,
    // since Chromium assumes 96 DPI as a fixed resolution.
    //
    qreal horizontalDpi =
        QGuiApplication::primaryScreen()->logicalDotsPerInchX();
    this->setZoomFactor((horizontalDpi / 96.0));

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

    d->updateInProgress = false;
    d->updateAgain = false;

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
    QMenu *menu = page()->createStandardContextMenu();
#else
    QMenu *menu = createStandardContextMenu();
#endif

    menu->popup(event->globalPos());
}

void HtmlPreview::updatePreview()
{
    Q_D(HtmlPreview);
    
    if (d->updateInProgress) {
        d->updateAgain = true;
        return;
    }

    if (this->isVisible()) {
        d->flushPreviewEditsToMarkdown();

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

void HtmlPreview::setInPlaceEditingEnabled(bool enabled)
{
    Q_D(HtmlPreview);

    if (!enabled && d->previewDirty) {
        d->flushPreviewEditsToMarkdown();
    }
    d->inPlaceEditingEnabled = enabled;
    this->page()->runJavaScript(
        QStringLiteral("window.__gwSetPreviewEditingEnabled && window.__gwSetPreviewEditingEnabled(%1);")
            .arg(enabled ? QLatin1String("true") : QLatin1String("false")));
}

void HtmlPreview::flushPreviewEditsToDocumentSync()
{
    Q_D(HtmlPreview);
    d->flushPreviewEditsToMarkdown();
}

void HtmlPreviewPrivate::onPreviewEditedFromJs()
{
    if (!inPlaceEditingEnabled || document->isReadOnly()) {
        return;
    }
    previewDirty = true;
    document->setModified(true);
}

void HtmlPreviewPrivate::flushPreviewEditsToMarkdown()
{
    if (!previewDirty) {
        return;
    }

    Q_Q(HtmlPreview);
    QEventLoop loop;
    QString capturedPlain;
    bool received = false;
    QTimer failSafe;
    failSafe.setSingleShot(true);
    QObject::connect(&failSafe, &QTimer::timeout, &loop, &QEventLoop::quit);
    failSafe.start(8000);
    q->page()->runJavaScript(
        QStringLiteral("(function(){ return (window.__gwPreviewPlain ? window.__gwPreviewPlain() : ''); })()"),
        [&](const QVariant &v) {
            capturedPlain = v.toString();
            received = true;
            loop.quit();
        });
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    failSafe.stop();
    if (!received) {
        qWarning() << "ghostwriter++: timed out merging in-preview edits; save continues without that merge.";
        return;
    }

    if (!applyPreviewPlainDiffToDocument(capturedPlain)) {
        qWarning(
            "ghostwriter++: could not apply preview edit to markdown source "
            "(edited text may not appear verbatim in the file). "
            "Try the same change in the editor, or simplify the edit.");
        previewDirty = false;
        QMetaObject::invokeMethod(q, "updatePreview", Qt::QueuedConnection);
        return;
    }
    previewDirty = false;
}

bool HtmlPreviewPrivate::applyPreviewPlainDiffToDocument(const QString &editedPlain)
{
    if (document->isReadOnly()) {
        return true;
    }
    if (editedPlain == previewPlainBaseline) {
        return true;
    }

    QString oldChunk;
    QString newChunk;
    int prefixLen = 0;
    computeDiffReplacement(previewPlainBaseline, editedPlain, &oldChunk, &newChunk, &prefixLen);

    if (oldChunk.isEmpty() && newChunk.isEmpty()) {
        previewPlainBaseline = editedPlain;
        return true;
    }

    const QString md = document->toPlainText();
    QTextCursor cursor(document);

    if (!oldChunk.isEmpty()) {
        const int pos = md.indexOf(oldChunk);
        if (pos >= 0) {
            cursor.beginEditBlock();
            cursor.setPosition(pos);
            cursor.setPosition(pos + oldChunk.size(), QTextCursor::KeepAnchor);
            cursor.insertText(newChunk);
            cursor.endEditBlock();
            previewPlainBaseline = editedPlain;
            return true;
        }
    }

    if (oldChunk.isEmpty() && !newChunk.isEmpty() && prefixLen > 0) {
        const QString prefix = previewPlainBaseline.left(prefixLen);
        const int pos = md.indexOf(prefix);
        if (pos >= 0) {
            const int ins = pos + prefix.size();
            cursor.beginEditBlock();
            cursor.setPosition(ins);
            cursor.insertText(newChunk);
            cursor.endEditBlock();
            previewPlainBaseline = editedPlain;
            return true;
        }
    }

    return false;
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
    Q_Q(HtmlPreview);
    
    if (ok) {
        q->page()->runJavaScript(
            QStringLiteral(
                "window.__gwSetPreviewEditingEnabled && window.__gwSetPreviewEditingEnabled(%1);")
                .arg(inPlaceEditingEnabled ? QLatin1String("true")
                                            : QLatin1String("false")));
    }
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
    
    d->setHtmlContent("");
}

void HtmlPreviewPrivate::setHtmlContent(const QString &html)
{
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

    // Export to HTML.
    exporter->exportToHtml(text, html);

    // Put smart typography setting back to the way it was before
    // so that the last setting used during document export is remembered.
    //
    exporter->setSmartTypographyEnabled(smartTypographyEnabled);

    return html;
}
} // namespace ghostwriterpp
