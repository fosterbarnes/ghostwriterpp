/*
 * SPDX-FileCopyrightText: 2021-2023 Megan Conkle <megan.conkle@kdemail.net>
 * SPDX-FileCopyrightText: 2026 Nate Peterson
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QListView>

#include "statisticsindicator.h"

namespace ghostwriterpp
{
//~ singular %Ln word
//~ plural %Ln words
static QString wordCountText(int value) { return StatisticsIndicator::tr("%Ln word(s)", "", value); }

//~ singular %Ln character
//~ plural %Ln characters
static QString characterCountText(int value) { return StatisticsIndicator::tr("%Ln character(s)", "", value); }

//~ singular %Ln sentence
//~ plural %Ln sentences
static QString sentenceCountText(int value) { return StatisticsIndicator::tr("%Ln sentence(s)", "", value); }

//~ singular %Ln paragraph
//~ plural %Ln paragraphs
static QString paragraphCountText(int value) { return StatisticsIndicator::tr("%Ln paragraph(s)", "", value); }

//~ singular %Ln page
//~ plural %Ln pages
static QString pageCountText(int value) { return StatisticsIndicator::tr("%Ln page(s)", "", value); }

//~ singular %Ln word added
//~ plural %Ln words added
static QString wordsAddedText(int value) { return StatisticsIndicator::tr("%Ln word(s) added", "", value); }

static QString wpmText(int value) { return StatisticsIndicator::tr("%1 wpm").arg(value); }
static QString readTimeText(int minutes) { return StatisticsIndicator::tr("%1:%2 read time")
                                                        .arg((int) (minutes / 60), 2, 10, QChar('0'))
                                                        .arg((int) (minutes % 60), 2, 10, QChar('0')); }
static QString writeTimeText(int minutes) { return StatisticsIndicator::tr("%1:%2 write time")
                                                        .arg((int) (minutes / 60), 2, 10, QChar('0'))
                                                        .arg((int) (minutes % 60), 2, 10, QChar('0')); }


StatisticsIndicator::StatisticsIndicator(DocumentStatistics *documentStats,
        SessionStatistics *sessionStats,
        QWidget *parent)
    : QComboBox(parent),
      m_sessionStats(sessionStats)
{
    this->setView(new QListView());
    this->view()->setTextElideMode(Qt::ElideNone);
    this->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);

    connect(this,
        &QComboBox::currentTextChanged,
        [this](QString text) {
            int max = this->fontMetrics().averageCharWidth() * (text.length() + 2);
            this->setMaximumWidth(max + 20);
            this->setMinimumContentsLength(text.length());
        });

    // Seed items with placeholder values; they will update once a document
    // stats source is connected via connectDocumentStats().
    int index = 0;
    this->addItem(wordCountText(0));
    this->setItemData(index++, Qt::AlignCenter, Qt::TextAlignmentRole);
    this->addItem(characterCountText(0));
    this->setItemData(index++, Qt::AlignCenter, Qt::TextAlignmentRole);
    this->addItem(sentenceCountText(0));
    this->setItemData(index++, Qt::AlignCenter, Qt::TextAlignmentRole);
    this->addItem(paragraphCountText(0));
    this->setItemData(index++, Qt::AlignCenter, Qt::TextAlignmentRole);
    this->addItem(pageCountText(0));
    this->setItemData(index++, Qt::AlignCenter, Qt::TextAlignmentRole);
    this->addItem(readTimeText(0));
    this->setItemData(index++, Qt::AlignCenter, Qt::TextAlignmentRole);

    const int wordsAddedIndex = index;
    this->addItem(wordsAddedText(0));
    this->setItemData(index++, Qt::AlignCenter, Qt::TextAlignmentRole);
    const int wpmIndex = index;
    this->addItem(wpmText(0));
    this->setItemData(index++, Qt::AlignCenter, Qt::TextAlignmentRole);
    const int writeTimeIndex = index;
    this->addItem(writeTimeText(0));
    this->setItemData(index++, Qt::AlignCenter, Qt::TextAlignmentRole);

    if (sessionStats) {
        this->connect(sessionStats,
            &SessionStatistics::wordCountChanged,
            this,
            [this, wordsAddedIndex](int value) {
                this->setItemText(wordsAddedIndex, wordsAddedText(value));
                if (wordsAddedIndex == this->currentIndex()) {
                    this->setMinimumContentsLength(this->itemText(wordsAddedIndex).length());
                }
            });

        this->connect(sessionStats,
            &SessionStatistics::wordsPerMinuteChanged,
            this,
            [this, wpmIndex](int value) {
                this->setItemText(wpmIndex, wpmText(value));
                if (wpmIndex == this->currentIndex()) {
                    this->setMinimumContentsLength(this->itemText(wpmIndex).length());
                }
            });

        this->connect(sessionStats,
            &SessionStatistics::writingTimeChanged,
            this,
            [this, writeTimeIndex](int minutes) {
                this->setItemText(writeTimeIndex, writeTimeText(minutes));
                if (writeTimeIndex == this->currentIndex()) {
                    this->setMinimumContentsLength(this->itemText(writeTimeIndex).length());
                }
            });
    }

    connectDocumentStats(documentStats);
}

StatisticsIndicator::~StatisticsIndicator()
{
    ;
}

void StatisticsIndicator::setDocumentStats(DocumentStatistics *documentStats)
{
    if (m_documentStats.data() == documentStats) {
        return;
    }

    for (const auto &c : std::as_const(m_documentStatsConnections)) {
        QObject::disconnect(c);
    }
    m_documentStatsConnections.clear();

    connectDocumentStats(documentStats);
}

void StatisticsIndicator::connectDocumentStats(DocumentStatistics *documentStats)
{
    m_documentStats = documentStats;

    if (!documentStats) {
        auto z = [this](int itemIndex, const QString &text) {
            this->setItemText(itemIndex, text);
            if (itemIndex == this->currentIndex()) {
                this->setMinimumContentsLength(this->itemText(itemIndex).length());
            }
        };
        z(0, wordCountText(0));
        z(1, characterCountText(0));
        z(2, sentenceCountText(0));
        z(3, paragraphCountText(0));
        z(4, pageCountText(0));
        z(5, readTimeText(0));
        return;
    }

    auto updateItem = [this](int itemIndex, const QString &text) {
        this->setItemText(itemIndex, text);
        if (itemIndex == this->currentIndex()) {
            this->setMinimumContentsLength(this->itemText(itemIndex).length());
        }
    };

    // Seed items with the new source's current values so switching tabs
    // reflects the stats immediately rather than waiting for the next edit.
    updateItem(0, wordCountText(documentStats->wordCount()));
    updateItem(1, characterCountText(documentStats->characterCount()));
    updateItem(2, sentenceCountText(documentStats->sentenceCount()));
    updateItem(3, paragraphCountText(documentStats->paragraphCount()));
    updateItem(4, pageCountText(documentStats->pageCount()));
    updateItem(5, readTimeText(documentStats->readingTime()));

    m_documentStatsConnections << this->connect(documentStats,
        &DocumentStatistics::wordCountChanged,
        this,
        [updateItem](int value) { updateItem(0, wordCountText(value)); });

    m_documentStatsConnections << this->connect(documentStats,
        &DocumentStatistics::characterCountChanged,
        this,
        [updateItem](int value) { updateItem(1, characterCountText(value)); });

    m_documentStatsConnections << this->connect(documentStats,
        &DocumentStatistics::sentenceCountChanged,
        this,
        [updateItem](int value) { updateItem(2, sentenceCountText(value)); });

    m_documentStatsConnections << this->connect(documentStats,
        &DocumentStatistics::paragraphCountChanged,
        this,
        [updateItem](int value) { updateItem(3, paragraphCountText(value)); });

    m_documentStatsConnections << this->connect(documentStats,
        &DocumentStatistics::pageCountChanged,
        this,
        [updateItem](int value) { updateItem(4, pageCountText(value)); });

    m_documentStatsConnections << this->connect(documentStats,
        &DocumentStatistics::readingTimeChanged,
        this,
        [updateItem](int minutes) { updateItem(5, readTimeText(minutes)); });
}

void StatisticsIndicator::showPopup()
{
    int max = 0;

    for (int i = 0; i < this->count(); i++) {
#if (QT_VERSION >= QT_VERSION_CHECK(5, 11, 0))
        int itemWidth = this->fontMetrics().horizontalAdvance(this->itemText(i));
#else
        int itemWidth = this->fontMetrics().boundingRect(this->itemText(i)).width();
#endif

        if (itemWidth > max) {
            max = itemWidth;
        }
    }

    this->view()->setMinimumWidth(max + 20);
    QComboBox::showPopup();
}

}
