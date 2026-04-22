/*
 * SPDX-FileCopyrightText: 2021-2023 Megan Conkle <megan.conkle@kdemail.net>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef STATISTICSINDICATOR_H
#define STATISTICSINDICATOR_H

#include <QComboBox>
#include <QList>
#include <QMetaObject>
#include <QPointer>

#include "documentstatistics.h"
#include "sessionstatistics.h"

namespace ghostwriterpp
{
/**
 * Combo box widget for use with the status bar as a status indicator.  This
 * widget allows the user to select one document/session statistics to display
 * at a time in the status bar.
 */
class StatisticsIndicator : public QComboBox
{
    Q_OBJECT

public:
    /**
     * Constructor.
     */
    StatisticsIndicator(DocumentStatistics *documentStats,
        SessionStatistics *sessionStats,
        QWidget *parent = nullptr);

    /**
     * Destructor.
     */
    ~StatisticsIndicator();

    /**
     * Retargets the document statistics source. The session statistics
     * source is unchanged.
     */
    void setDocumentStats(DocumentStatistics *documentStats);

    void showPopup() override;

private:
    QPointer<DocumentStatistics> m_documentStats;
    QPointer<SessionStatistics> m_sessionStats;
    QList<QMetaObject::Connection> m_documentStatsConnections;

    void connectDocumentStats(DocumentStatistics *stats);
};
}

#endif // STATISTICSINDICATOR_H
