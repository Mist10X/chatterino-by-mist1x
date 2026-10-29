// SPDX-FileCopyrightText: 2017 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "widgets/BaseWindow.hpp"

#include <pajlada/settings/setting.hpp>
#include <QFrame>
#include <QPushButton>
#include <QStackedLayout>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>

class QLineEdit;
class QScrollArea;

namespace chatterino {

class SettingsPage;
class SettingsDialogTab;
class ModerationPage;
enum class SettingsTabId;

class PageHeader : public QFrame
{
    Q_OBJECT
};

enum class SettingsDialogPreference {
    NoPreference,
    StreamerMode,
    Accounts,
    ModerationActions,
    About,
    Moltorino,
};

class SettingsDialog : public BaseWindow
{
    SettingsDialog(QWidget *parent);

public:
    static void showDialog(QWidget *parent,
                           SettingsDialogPreference preferredTab =
                               SettingsDialogPreference::NoPreference);

    /// For --ui-snapshot-settings: opens the settings on the tab whose name
    /// contains <tab> (scrolled down by <scroll px>) and saves a picture.
    static void saveSnapshot(QWidget *parent, const QString &spec,
                             const QString &file);

protected:
    void scaleChangedEvent(float newDpi) override;
    void showEvent(QShowEvent *) override;

private:
    void refresh();

    void initUi();
    SettingsDialogTab *tab(SettingsTabId id);
    void addTabs();
    void addTab(std::function<SettingsPage *()> page, const QString &name,
                const QString &iconPath, SettingsTabId id = {},
                Qt::Alignment alignment = Qt::AlignTop);
    /// A small heading above a group of tabs in the sidebar.
    void addCaption(const QString &text);
    void updateSidebarWidth();
    void selectTab(SettingsDialogTab *tab, const bool byUser = true);
    void selectTab(SettingsTabId id);
    void filterElements(const QString &query);
    void setElementFilter(const QString &query);
    bool eventFilter(QObject *object, QEvent *event) override;

    void onOkClicked();
    void onCancelClicked();
    void addShortcuts() override;
    void setSearchPlaceholderText();

    struct {
        QScrollArea *sidebar{};
        QWidget *tabContainerContainer{};
        QVBoxLayout *tabContainer{};
        QStackedLayout *pageStack{};
        QPushButton *okButton{};
        QPushButton *cancelButton{};
        QLineEdit *search{};
    } ui_;
    std::vector<SettingsDialogTab *> tabs_;
    std::vector<QWidget *> captions_;
    /// The pages whose settings were spread over the topic pages; they stay
    /// alive (hidden) because their settings still use them.
    std::vector<SettingsPage *> sourcePages_;
    SettingsDialogTab *selectedTab_{};
    SettingsDialogTab *lastSelectedByUser_{};
    float dpi_ = 1.0F;

    friend class SettingsDialogTab;
};

}  // namespace chatterino
