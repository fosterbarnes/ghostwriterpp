/*
 * SPDX-FileCopyrightText: 2020-2022 Megan Conkle <megan.conkle@kdemail.net>
 * SPDX-FileCopyrightText: 2026 Nate Peterson
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QApplication>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMenu>
#include <QPushButton>
#include <QStackedWidget>
#include <QString>
#include <QButtonGroup>
#include <QStyle>
#include <QWidget>

#include "sidebar.h"

namespace ghostwriterpp
{
namespace
{
constexpr int kActivityButtonSizeTopPx = 28;
}

class SidebarPrivate
{
public:
    SidebarPrivate()
        : stack(nullptr)
        , topBar(nullptr)
        , leftBar(nullptr)
        , topItemsLayout(nullptr)
        , leftTabsLayout(nullptr)
        , leftButtonsLayout(nullptr)
        , overflowButton(nullptr)
        , tabGroup(nullptr)
        , autoHideEnabled(false)
        , activityBarLocation(ActivityBarLocationTop)
    {
    }

    QStackedWidget *stack;
    QWidget *topBar;
    QWidget *leftBar;
    QHBoxLayout *topItemsLayout;
    QVBoxLayout *leftTabsLayout;
    QVBoxLayout *leftButtonsLayout;
    QPushButton *overflowButton;
    QButtonGroup *tabGroup;
    QList<QPushButton *> tabButtons;
    QList<QPushButton *> actionButtons;
    bool autoHideEnabled;
    ActivityBarLocation activityBarLocation;

    void placeActivityWidgets();
    void updateActivityBarOverflow();
    void showOverflowMenu();
    void refreshActivityBarStyle(Sidebar *q);
    int activityItemCount() const;
    QPushButton *activityItemAt(int index) const;
};

int SidebarPrivate::activityItemCount() const
{
    return tabButtons.size() + actionButtons.size();
}

QPushButton *SidebarPrivate::activityItemAt(int index) const
{
    if (index < 0) {
        return nullptr;
    }

    if (index < tabButtons.size()) {
        return tabButtons.at(index);
    }

    index -= tabButtons.size();

    if (index < actionButtons.size()) {
        return actionButtons.at(index);
    }

    return nullptr;
}

void SidebarPrivate::refreshActivityBarStyle(Sidebar *q)
{
    const char *value = (activityBarLocation == ActivityBarLocationTop) ? "top" : "left";
    q->setProperty("activityBarLocation", QString::fromLatin1(value));
    q->style()->unpolish(q);
    q->style()->polish(q);

    for (QPushButton *tab : tabButtons) {
        tab->style()->unpolish(tab);
        tab->style()->polish(tab);
        tab->update();
    }

    q->update();
}

void SidebarPrivate::placeActivityWidgets()
{
    auto detach = [](QWidget *w) {
        if (w == nullptr) {
            return;
        }

        QWidget *parent = w->parentWidget();

        if ((parent != nullptr) && (parent->layout() != nullptr)) {
            parent->layout()->removeWidget(w);
        }
    };

    for (QPushButton *tab : tabButtons) {
        detach(tab);
    }

    for (QPushButton *button : actionButtons) {
        detach(button);
    }

    detach(overflowButton);

    if (activityBarLocation == ActivityBarLocationTop) {
        for (QPushButton *tab : tabButtons) {
            topItemsLayout->addWidget(tab);
        }

        for (QPushButton *button : actionButtons) {
            topItemsLayout->addWidget(button);
        }

        topItemsLayout->addWidget(overflowButton);
        topBar->setVisible(true);
        leftBar->setVisible(false);
    } else {
        for (QPushButton *tab : tabButtons) {
            leftTabsLayout->addWidget(tab);
            tab->setVisible(true);
        }

        for (QPushButton *button : actionButtons) {
            leftButtonsLayout->addWidget(button);
            button->setVisible(true);
        }

        overflowButton->setVisible(false);
        topBar->setVisible(false);
        leftBar->setVisible(true);
    }

    updateActivityBarOverflow();
}

void SidebarPrivate::updateActivityBarOverflow()
{
    if (activityBarLocation != ActivityBarLocationTop) {
        overflowButton->setVisible(false);

        for (QPushButton *tab : tabButtons) {
            tab->setVisible(true);
        }

        for (QPushButton *button : actionButtons) {
            button->setVisible(true);
        }

        return;
    }

    const int itemCount = activityItemCount();
    const int sidebarWidth = topBar->parentWidget() != nullptr
        ? topBar->parentWidget()->width()
        : topBar->width();

    if (sidebarWidth <= 0) {
        for (int i = 0; i < itemCount; ++i) {
            activityItemAt(i)->setVisible(true);
        }

        overflowButton->setVisible(false);
        return;
    }

    int visibleCount = itemCount;
    bool needOverflow = false;
    const int buttonSize = kActivityButtonSizeTopPx;

    if (itemCount > 0) {
        const int withoutOverflow = itemCount * buttonSize;

        if (withoutOverflow > sidebarWidth) {
            needOverflow = true;
            const int available = sidebarWidth - buttonSize;
            visibleCount = qMax(0, available / buttonSize);

            if (visibleCount >= itemCount) {
                visibleCount = itemCount;
                needOverflow = false;
            }
        }
    }

    for (int i = 0; i < itemCount; ++i) {
        activityItemAt(i)->setVisible(i < visibleCount);
    }

    overflowButton->setVisible(needOverflow && (visibleCount < itemCount));
}

void SidebarPrivate::showOverflowMenu()
{
    QMenu menu(overflowButton);
    const int current = stack->currentIndex();
    const int itemCount = activityItemCount();

    for (int i = 0; i < itemCount; ++i) {
        QPushButton *item = activityItemAt(i);

        if (item->isVisible()) {
            continue;
        }

        const bool isTab = i < tabButtons.size();
        QAction *action = menu.addAction(item->icon(), item->toolTip());

        if (isTab) {
            action->setCheckable(true);
            action->setChecked(i == current);
            QObject::connect(action, &QAction::triggered, overflowButton, [this, i]() {
                if ((i < 0) || (i >= tabButtons.size())) {
                    return;
                }

                QPushButton *tab = tabButtons.at(i);
                tab->setChecked(true);
                stack->setCurrentIndex(i);

                if (stack->currentWidget() != nullptr) {
                    stack->currentWidget()->setFocus();
                }
            });
        } else {
            QObject::connect(action, &QAction::triggered, item, &QPushButton::click);
        }
    }

    if (menu.isEmpty()) {
        return;
    }

    menu.exec(overflowButton->mapToGlobal(QPoint(0, overflowButton->height())));
}

Sidebar::Sidebar(QWidget *parent)
    : QFrame(parent),
      d_ptr(new SidebarPrivate())
{
    Q_D(Sidebar);

    d->stack = new QStackedWidget(this);
    d->autoHideEnabled = false;
    d->activityBarLocation = ActivityBarLocationTop;

    this->setObjectName("sidebar");
    this->setContentsMargins(0, 0, 0, 0);
    this->connect(qApp,
        &QApplication::focusChanged,
        this,
        &Sidebar::onFocusChanged);

    d->tabGroup = new QButtonGroup(this);
    d->tabGroup->setExclusive(true);

    this->connect(
        d->tabGroup,
        static_cast<void (QButtonGroup::*)(QAbstractButton *)>(&QButtonGroup::buttonClicked),
        [d](QAbstractButton *tab) {
            const int index = d->tabButtons.indexOf(static_cast<QPushButton *>(tab));

            if (index < 0) {
                return;
            }

            d->stack->setCurrentIndex(index);
            d->stack->currentWidget()->setFocus();
        });

    d->topBar = new QWidget(this);
    d->topBar->setObjectName("sidebarTopBar");
    d->topBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    QHBoxLayout *topBarLayout = new QHBoxLayout(d->topBar);
    topBarLayout->setSpacing(0);
    topBarLayout->setContentsMargins(0, 0, 0, 0);

    d->topItemsLayout = new QHBoxLayout();
    d->topItemsLayout->setObjectName("sidebarTopItems");
    d->topItemsLayout->setSpacing(0);
    d->topItemsLayout->setContentsMargins(0, 0, 0, 0);
    d->topItemsLayout->setAlignment(Qt::AlignCenter | Qt::AlignVCenter);

    topBarLayout->addStretch(1);
    topBarLayout->addLayout(d->topItemsLayout, 0);
    topBarLayout->addStretch(1);

    d->overflowButton = new QPushButton(this);
    d->overflowButton->setObjectName("sidebarOverflowButton");
    d->overflowButton->setCheckable(false);
    d->overflowButton->setFocusPolicy(Qt::NoFocus);
    d->overflowButton->setToolTip(tr("More"));
    d->overflowButton->setVisible(false);
    this->connect(d->overflowButton, &QPushButton::clicked, this, [d]() {
        d->showOverflowMenu();
    });

    d->leftBar = new QWidget(this);
    d->leftBar->setObjectName("sidebarLeftBar");
    d->leftBar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    QVBoxLayout *leftBarLayout = new QVBoxLayout(d->leftBar);
    leftBarLayout->setAlignment(Qt::AlignCenter | Qt::AlignTop);
    leftBarLayout->setSpacing(0);
    leftBarLayout->setContentsMargins(0, 0, 0, 0);

    d->leftTabsLayout = new QVBoxLayout();
    d->leftTabsLayout->setObjectName("sidebarTabs");
    d->leftTabsLayout->setAlignment(Qt::AlignTop | Qt::AlignCenter);
    d->leftTabsLayout->setSpacing(0);
    d->leftTabsLayout->setContentsMargins(0, 0, 0, 0);

    d->leftButtonsLayout = new QVBoxLayout();
    d->leftButtonsLayout->setObjectName("sidebarButtons");
    d->leftButtonsLayout->setAlignment(Qt::AlignBottom | Qt::AlignCenter);
    d->leftButtonsLayout->setSpacing(0);
    d->leftButtonsLayout->setContentsMargins(0, 0, 0, 0);

    leftBarLayout->addLayout(d->leftTabsLayout);
    leftBarLayout->addStretch(1);
    leftBarLayout->addLayout(d->leftButtonsLayout);

    QHBoxLayout *bodyLayout = new QHBoxLayout();
    bodyLayout->setSpacing(0);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->addWidget(d->leftBar, 0);
    bodyLayout->addWidget(d->stack, 1);

    QVBoxLayout *rootLayout = new QVBoxLayout();
    rootLayout->setSpacing(0);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->addWidget(d->topBar, 0);
    rootLayout->addLayout(bodyLayout, 1);
    rootLayout->setSizeConstraint(QLayout::SetNoConstraint);
    this->setLayout(rootLayout);

    d->placeActivityWidgets();
    d->refreshActivityBarStyle(this);
}

Sidebar::~Sidebar()
{
}

void Sidebar::addTab(const QIcon &icon, QWidget *widget, const QString &tooltip, const QString &objectName)
{
    Q_D(Sidebar);

    this->insertTab(d->tabButtons.count(), icon, widget, tooltip, objectName);
}

void Sidebar::insertTab(int index, const QIcon &icon, QWidget *widget, const QString &tooltip, const QString &objectName)
{
    Q_D(Sidebar);

    if (index < 0) {
        index = 0;
    } else if (index > d->tabButtons.count()) {
        index = d->tabButtons.count();
    }

    QPushButton *button = new QPushButton(this);
    button->setIcon(icon);
    button->setCheckable(true);
    button->setToolTip(tooltip);

    if (d->tabButtons.count() < 1) {
        button->setChecked(true);
    }

    if (!objectName.isNull() && !objectName.isEmpty()) {
        button->setObjectName(objectName);
    }

    d->stack->insertWidget(index, widget);
    d->tabButtons.insert(index, button);
    d->tabGroup->addButton(button);
    d->placeActivityWidgets();
}

void Sidebar::removeTab(int index)
{
    Q_D(Sidebar);

    if (d->tabButtons.count() <= 0) {
        return;
    }

    if (index < 0) {
        index = 0;
    } else if (index >= d->tabButtons.count()) {
        index = d->tabButtons.count() - 1;
    }

    int activeTabIndex = d->stack->currentIndex();

    if (activeTabIndex == index) {
        if (activeTabIndex > 0) {
            activeTabIndex--;
        } else {
            activeTabIndex = 0;
        }
    } else if (activeTabIndex > index) {
        activeTabIndex--;
    }

    if (d->stack->widget(index) != nullptr) {
        d->stack->removeWidget(d->stack->widget(index));
    }

    QPushButton *tab = d->tabButtons.takeAt(index);
    d->tabGroup->removeButton(tab);

    if (tab->parentWidget() != nullptr && tab->parentWidget()->layout() != nullptr) {
        tab->parentWidget()->layout()->removeWidget(tab);
    }

    tab->deleteLater();

    if (d->stack->count() > 0) {
        d->stack->setCurrentIndex(activeTabIndex);
        QPushButton *active = d->tabButtons.at(activeTabIndex);
        active->setChecked(true);
    }

    d->placeActivityWidgets();
}

void Sidebar::setCurrentTabIndex(int index)
{
    Q_D(Sidebar);

    if (d->tabButtons.isEmpty()) {
        return;
    }

    if (index < 0) {
        index = 0;
    } else if (index >= d->tabButtons.count()) {
        index = d->tabButtons.count() - 1;
    }

    QPushButton *tab = d->tabButtons.at(index);
    tab->setChecked(true);

    d->stack->setCurrentIndex(index);
    d->stack->currentWidget()->setFocus();
}

int Sidebar::tabCount() const
{
    Q_D(const Sidebar);

    return d->tabButtons.count();
}

int Sidebar::buttonCount() const
{
    Q_D(const Sidebar);

    return d->actionButtons.count();
}

void Sidebar::setAutoHideEnabled(bool enabled)
{
    Q_D(Sidebar);

    d->autoHideEnabled = enabled;

    if (enabled && !this->hasFocus() && this->isVisible()) {
        QFrame::setVisible(false);
    }
}

bool Sidebar::autoHideEnabled() const
{
    Q_D(const Sidebar);

    return d->autoHideEnabled;
}

void Sidebar::setActivityBarLocation(ActivityBarLocation location)
{
    Q_D(Sidebar);

    if ((location < ActivityBarLocationFirst) || (location > ActivityBarLocationLast)) {
        return;
    }

    if (d->activityBarLocation == location) {
        d->placeActivityWidgets();
        return;
    }

    d->activityBarLocation = location;
    d->refreshActivityBarStyle(this);
    d->placeActivityWidgets();
}

ActivityBarLocation Sidebar::activityBarLocation() const
{
    Q_D(const Sidebar);

    return d->activityBarLocation;
}

void Sidebar::setOverflowIcon(const QIcon &icon)
{
    Q_D(Sidebar);

    d->overflowButton->setIcon(icon);
}

QPushButton *Sidebar::addButton(const QIcon &icon, const QString &tooltip, const QString &objectName)
{
    Q_D(Sidebar);

    return this->insertButton(d->actionButtons.count(), icon, tooltip, objectName);
}

QPushButton *Sidebar::insertButton(int index, const QIcon &icon, const QString &tooltip, const QString &objectName)
{
    Q_D(Sidebar);

    if (index < 0) {
        index = 0;
    } else if (index > d->actionButtons.count()) {
        index = d->actionButtons.count();
    }

    QPushButton *button = new QPushButton(this);
    button->setIcon(icon);
    button->setFocusPolicy(Qt::NoFocus);
    button->setCheckable(false);
    button->setToolTip(tooltip);

    if (!objectName.isNull() && !objectName.isEmpty()) {
        button->setObjectName(objectName);
    }

    d->actionButtons.insert(index, button);
    d->placeActivityWidgets();
    return button;
}

void Sidebar::removeButton(int index)
{
    Q_D(Sidebar);

    if (d->actionButtons.isEmpty()) {
        return;
    }

    if (index < 0) {
        index = 0;
    } else if (index >= d->actionButtons.count()) {
        index = d->actionButtons.count() - 1;
    }

    QPushButton *button = d->actionButtons.takeAt(index);

    if (button->parentWidget() != nullptr && button->parentWidget()->layout() != nullptr) {
        button->parentWidget()->layout()->removeWidget(button);
    }

    button->deleteLater();
    d->placeActivityWidgets();
}

void Sidebar::hideEvent(QHideEvent *event)
{
    Q_UNUSED(event)
    emit visibilityChanged(false);
}

void Sidebar::showEvent(QShowEvent *event)
{
    Q_UNUSED(event)
    emit visibilityChanged(true);
    Q_D(Sidebar);
    d->updateActivityBarOverflow();
}

void Sidebar::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    Q_D(Sidebar);
    d->updateActivityBarOverflow();
}

void Sidebar::onFocusChanged(QWidget *old, QWidget *now) {
    bool focusLost = ((old != nullptr)
        && this->isAncestorOf(old)
        && ((now == nullptr) || !this->isAncestorOf(now)));

    if (this->autoHideEnabled() && focusLost && this->isVisible()) {
        this->setVisible(false);
    }
}

} // namespace ghostwriterpp
