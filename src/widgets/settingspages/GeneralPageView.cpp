// SPDX-FileCopyrightText: 2020 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/settingspages/GeneralPageView.hpp"

#include "Application.hpp"
#include "common/QLogging.hpp"
#include "singletons/Theme.hpp"
#include "util/LayoutHelper.hpp"
#include "util/RapidJsonSerializeQString.hpp"
#include "widgets/helper/Line.hpp"
#include "widgets/settingspages/SettingWidget.hpp"

#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>

#include <algorithm>

namespace {

constexpr int MAX_TOOLTIP_LINE_LENGTH = 50;
const auto MAX_TOOLTIP_LINE_LENGTH_PATTERN =
    QStringLiteral(R"(.{%1}\S*\K(\s+))").arg(MAX_TOOLTIP_LINE_LENGTH);
const QRegularExpression MAX_TOOLTIP_LINE_LENGTH_REGEX(
    MAX_TOOLTIP_LINE_LENGTH_PATTERN);
}  // namespace

namespace chatterino {

GeneralPageView::GeneralPageView(QWidget *parent)
    : QWidget(parent)
    , contentScrollArea_(new QScrollArea)
    , contentLayout_(new QVBoxLayout)
{
    auto *contentWidget = new QWidget;
    contentWidget->setLayout(this->contentLayout_);
    this->contentScrollArea_->setWidget(contentWidget);
    this->contentScrollArea_->setObjectName("generalSettingsScrollContent");
    this->contentScrollArea_->setWidgetResizable(true);
}

GeneralPageView *GeneralPageView::withoutNavigation(QWidget *parent)
{
    auto *view = new GeneralPageView(parent);

    view->setLayout(makeLayout<QHBoxLayout>({
        view->contentScrollArea_,
    }));

    return view;
}

GeneralPageView *GeneralPageView::withNavigation(QWidget *parent)
{
    auto *view = new GeneralPageView(parent);

    auto *navigation =
        wrapLayout(view->navigationLayout_ = makeLayout<QVBoxLayout>({}));
    navigation->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Minimum);
    view->navigationLayout_->setAlignment(Qt::AlignTop);
    view->navigationLayout_->addSpacing(6);

    view->setLayout(makeLayout<QHBoxLayout>({
        view->contentScrollArea_,
        new QSpacerItem(16, 1),
        navigation,
    }));

    QObject::connect(view->contentScrollArea_->verticalScrollBar(),
                     &QScrollBar::valueChanged, view, [view] {
                         view->updateNavigationHighlighting();
                     });

    return view;
}

namespace {

std::function<GeneralPageView *(const QString &)> &activeTopics()
{
    static std::function<GeneralPageView *(const QString &)> lookup;
    return lookup;
}

}  // namespace

GeneralPageView *SettingsTopics::find(const QString &topic)
{
    const auto &lookup = activeTopics();
    return lookup ? lookup(topic) : nullptr;
}

bool SettingsTopics::active()
{
    return static_cast<bool>(activeTopics());
}

void SettingsTopics::setActive(
    std::function<GeneralPageView *(const QString &)> lookup)
{
    activeTopics() = std::move(lookup);
}

void routeSetting(GeneralPageView &from, const QString &topic,
                  const QString &section)
{
    // a page shown on its own (tests, previews) keeps the sections itself
    auto *target = SettingsTopics::find(topic);
    from.routeTo(target != nullptr ? target : &from, section);
}

void GeneralPageView::routeTo(GeneralPageView *target, const QString &section)
{
    this->route_ = target == this ? nullptr : target;
    if (target == nullptr)
    {
        return;
    }
    target->route_ = nullptr;
    const auto name = uiText(section);
    for (size_t i = 0; i < target->groups_.size(); ++i)
    {
        if (target->groups_[i].name == name)
        {
            target->insertGroup_ = static_cast<int>(i);
            return;
        }
    }
    target->addTitle(section);
}

void GeneralPageView::finishTopic()
{
    for (auto &group : this->groups_)
    {
        // a section is its title and the space before the next title
        const bool empty = std::ranges::none_of(group.widgets, [](auto &w) {
            return dynamic_cast<TitleLabel *>(w.element) == nullptr &&
                   dynamic_cast<Space *>(w.element) == nullptr;
        });
        if (empty)
        {
            group.title->hide();
            if (group.space)
            {
                group.space->hide();
            }
            if (group.navigationLink)
            {
                group.navigationLink->hide();
            }
        }
    }
    this->insertGroup_ = -1;
    this->contentLayout_->addStretch();
}

GeneralPageView::Group &GeneralPageView::currentGroup()
{
    if (this->insertGroup_ >= 0 &&
        this->insertGroup_ < static_cast<int>(this->groups_.size()))
    {
        return this->groups_[this->insertGroup_];
    }
    return this->groups_.back();
}

int GeneralPageView::insertIndex() const
{
    const auto next = this->insertGroup_ + 1;
    if (this->insertGroup_ < 0 ||
        next >= static_cast<int>(this->groups_.size()))
    {
        return -1;
    }
    // the space before the next title ends this section
    const auto &group = this->groups_[this->insertGroup_];
    QWidget *end = group.space != nullptr
                       ? static_cast<QWidget *>(group.space)
                       : this->groups_[next].title;
    return this->contentLayout_->indexOf(end);
}

void GeneralPageView::placeWidget(QWidget *widget)
{
    const auto at = this->insertIndex();
    if (at < 0)
    {
        this->contentLayout_->addWidget(widget);
    }
    else
    {
        this->contentLayout_->insertWidget(at, widget);
    }
}

void GeneralPageView::placeLayout(QLayout *layout)
{
    const auto at = this->insertIndex();
    if (at < 0)
    {
        this->contentLayout_->addLayout(layout);
    }
    else
    {
        this->contentLayout_->insertLayout(at, layout);
    }
}

void GeneralPageView::addWidget(QWidget *widget, const QStringList &keywords)
{
    if (this->route_ != nullptr)
    {
        this->route_->addWidget(widget, keywords);
        return;
    }
    this->placeWidget(widget);
    if (!this->groups_.empty())
    {
        this->currentGroup().widgets.push_back({
            .element = widget,
            .keywords = keywords,
        });
    }
}

void GeneralPageView::registerWidget(QWidget *widget,
                                     const QStringList &keywords,
                                     QWidget *parentElement)
{
    if (this->route_ != nullptr)
    {
        this->route_->registerWidget(widget, keywords, parentElement);
        return;
    }
    if (!this->groups_.empty())
    {
        this->currentGroup().widgets.push_back({
            .element = widget,
            .keywords = keywords,
            .parentElement = parentElement,
        });
    }
}

void GeneralPageView::pushWidget(QWidget *widget)
{
    if (this->route_ != nullptr)
    {
        this->route_->pushWidget(widget);
        return;
    }
    this->placeWidget(widget);
}

void GeneralPageView::addLayout(QLayout *layout)
{
    if (this->route_ != nullptr)
    {
        this->route_->addLayout(layout);
        return;
    }
    this->placeLayout(layout);
}

void GeneralPageView::addStretch()
{
    if (SettingsTopics::active())
    {
        return;  // topic pages end with finishTopic()
    }
    this->contentLayout_->addStretch();
}

TitleLabel *GeneralPageView::addTitle(const QString &title)
{
    if (this->route_ != nullptr)
    {
        return this->route_->addTitle(title);
    }
    this->insertGroup_ = -1;
    if (!this->groups_.empty())
    {
        this->addWidget(this->groups_.back().space = new Space);
    }

    auto *label = new TitleLabel(uiText(title));
    this->addWidget(label);

    NavigationLabel *navLabel = nullptr;

    if (this->navigationLayout_ != nullptr)
    {
        navLabel = new NavigationLabel(title);
        navLabel->setCursor(Qt::PointingHandCursor);
        this->navigationLayout_->addWidget(navLabel);

        QObject::connect(
            navLabel, &NavigationLabel::leftMouseUp, label, [this, label] {
                this->contentScrollArea_->verticalScrollBar()->setValue(
                    label->y());
            });
    }

    this->groups_.push_back(Group{uiText(title), label, navLabel, nullptr, {}});

    if (this->groups_.size() == 1)
    {
        this->updateNavigationHighlighting();
    }

    return label;
}

SubtitleLabel *GeneralPageView::addSubtitle(const QString &title)
{
    if (this->route_ != nullptr)
    {
        return this->route_->addSubtitle(title);
    }
    auto *label = new SubtitleLabel(uiText(title));
    this->addWidget(label);

    this->currentGroup().widgets.push_back({label, {title}});

    return label;
}

ComboBox *GeneralPageView::addDropdown(const QString &text,
                                       const QStringList &list,
                                       QString toolTipText)
{
    if (this->route_ != nullptr)
    {
        return this->route_->addDropdown(text, list, toolTipText);
    }
    auto *layout = new QHBoxLayout;
    auto *combo = new ComboBox;
    combo->setFocusPolicy(Qt::StrongFocus);
    for (const auto &item : list)
    {
        combo->addItem(uiText(item));
        combo->setItemData(combo->count() - 1, item, UiComboBox::SourceRole);
    }

    auto *label = new QLabel(uiText(text) + ":");
    layout->addWidget(label);
    layout->addStretch(1);
    layout->addWidget(combo);

    this->addToolTip(*label, toolTipText);
    this->addLayout(layout);

    this->currentGroup().widgets.push_back({combo, {text}});
    this->currentGroup().widgets.push_back({label, {text}});

    return combo;
}

void GeneralPageView::addNavigationSpacing()
{
    if (this->route_ != nullptr || SettingsTopics::active())
    {
        return;
    }
    assert(this->navigationLayout_ != nullptr &&
           "addNavigationSpacing used without navigation");

    this->navigationLayout_->addSpacing(24);
}

DescriptionLabel *GeneralPageView::addDescription(const QString &text)
{
    if (this->route_ != nullptr)
    {
        return this->route_->addDescription(text);
    }
    auto *label = new DescriptionLabel(text);

    label->setTextInteractionFlags(Qt::TextBrowserInteraction |
                                   Qt::LinksAccessibleByKeyboard);
    label->setOpenExternalLinks(true);
    label->setWordWrap(true);

    this->addWidget(label);

    this->currentGroup().widgets.push_back({label, {text}});

    return label;
}

void GeneralPageView::addSeparator()
{
    if (this->route_ != nullptr)
    {
        this->route_->addSeparator();
        return;
    }
    this->addWidget(new Line(false));
}

bool GeneralPageView::filterElements(const QString &query)
{
    bool any{};

    for (auto &&group : this->groups_)
    {
        bool descriptionMatches{};
        for (auto &&widget : group.widgets)
        {
            if (auto *x = dynamic_cast<DescriptionLabel *>(widget.element); x)
            {
                if (x->text().contains(query, Qt::CaseInsensitive))
                {
                    descriptionMatches = true;
                    break;
                }
            }
        }

        if (group.name.contains(query, Qt::CaseInsensitive) ||
            descriptionMatches)
        {
            for (auto &&widget : group.widgets)
            {
                widget.element->show();
                if (widget.parentElement != nullptr)
                {
                    widget.parentElement->show();
                }
            }

            group.title->show();
            if (group.navigationLink != nullptr)
            {
                group.navigationLink->show();
            }
            any = true;
        }

        else
        {
            auto groupAny = false;

            QWidget *currentSubtitle = nullptr;
            bool currentSubtitleVisible = false;
            bool currentSubtitleSearched = false;

            for (auto &&widget : group.widgets)
            {
                if (auto *x = dynamic_cast<SubtitleLabel *>(widget.element))
                {
                    currentSubtitleSearched = false;
                    if (currentSubtitle)
                    {
                        currentSubtitle->setVisible(currentSubtitleVisible);
                    }

                    currentSubtitleVisible = false;
                    currentSubtitle = widget.element;

                    if (x->text().contains(query, Qt::CaseInsensitive))
                    {
                        currentSubtitleSearched = true;
                    }
                    continue;
                }

                if (auto *x = dynamic_cast<Line *>(widget.element))
                {
                    x->hide();
                }

                for (auto &&keyword : widget.keywords)
                {
                    if (keyword.contains(query, Qt::CaseInsensitive) ||
                        uiText(keyword).contains(query, Qt::CaseInsensitive) ||
                        currentSubtitleSearched)
                    {
                        currentSubtitleVisible = true;
                        widget.element->show();
                        if (widget.parentElement != nullptr)
                        {
                            widget.parentElement->show();
                        }
                        groupAny = true;
                        break;
                    }

                    widget.element->hide();
                    if (widget.parentElement != nullptr)
                    {
                        widget.parentElement->hide();
                    }
                }
            }

            if (currentSubtitle)
            {
                currentSubtitle->setVisible(currentSubtitleVisible);
            }

            if (group.space)
            {
                group.space->setVisible(groupAny);
            }

            group.title->setVisible(groupAny);
            if (group.navigationLink != nullptr)
            {
                group.navigationLink->setVisible(groupAny);
            }
            any |= groupAny;
        }
    }

    return any;
}

void GeneralPageView::updateNavigationHighlighting()
{
    if (this->navigationLayout_ == nullptr)
    {
        return;
    }

    auto scrollY = this->contentScrollArea_->verticalScrollBar()->value();
    auto first = true;

    for (auto &&group : this->groups_)
    {
        if (first && (group.title->geometry().bottom() > scrollY ||
                      &group == &this->groups_.back()))
        {
            first = false;
            group.navigationLink->setStyleSheet(
                QStringLiteral("color: %1").arg(getTheme()->accent.name()));
        }
        else
        {
            group.navigationLink->setStyleSheet("");
        }
    }
}

void GeneralPageView::addToolTip(QWidget &widget, QString text) const
{
    if (text.isEmpty())
    {
        return;
    }

    text = uiText(text);

    if (text.length() > MAX_TOOLTIP_LINE_LENGTH)
    {
        text.replace(MAX_TOOLTIP_LINE_LENGTH_REGEX, "\n");
    }

    widget.setToolTip(text);
}

}  // namespace chatterino
