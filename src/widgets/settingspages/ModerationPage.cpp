// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "common/UiLanguage.hpp"
#include "widgets/settingspages/ModerationPage.hpp"

#include "Application.hpp"
#include "controllers/logging/ChannelLoggingModel.hpp"
#include "controllers/moderationactions/ModerationAction.hpp"
#include "controllers/moderationactions/ModerationActionModel.hpp"
#include "singletons/Logging.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"
#include "util/Helpers.hpp"
#include "util/LayoutCreator.hpp"
#include "util/LoadPixmap.hpp"
#include "util/PostToThread.hpp"
#include "widgets/helper/EditableModelView.hpp"
#include "widgets/helper/IconDelegate.hpp"
#include "widgets/settingspages/SettingWidget.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QTableView>
#include <QtConcurrent/QtConcurrent>

namespace chatterino {

qint64 dirSize(QString &dirPath)
{
    QDirIterator it(dirPath, QDirIterator::Subdirectories);
    qint64 size = 0;

    while (it.hasNext())
    {
        size += it.fileInfo().size();
        it.next();
    }

    return size;
}

QString formatSize(qint64 size)
{
    QStringList units = {"Bytes", "KB", "MB", "GB", "TB", "PB"};
    int i;
    double outputSize = size;
    for (i = 0; i < units.size() - 1; i++)
    {
        if (outputSize < 1024)
        {
            break;
        }
        outputSize = outputSize / 1024;
    }
    return QString("%0 %1").arg(outputSize, 0, 'f', 2).arg(units[i]);
}

QString fetchLogDirectorySize()
{
    QString logsDirectoryPath =
        getApp()->getPaths().messageLogsIn(getSettings()->logPath.getValue());

    auto logsSize = dirSize(logsDirectoryPath);

    return uiText(QString("Your logs currently take up %1 of space"))
        .arg(formatSize(logsSize));
}

ModerationPage::ModerationPage()
{
    LayoutCreator<ModerationPage> layoutCreator(this);

    auto tabs = layoutCreator.emplace<QTabWidget>();
    this->tabWidget_ = tabs.getElement();

    auto logs = tabs.appendTab(new QVBoxLayout, uiText("Logs"));
    {
        QCheckBox *enableLogging = this->createCheckBox(
            uiText("Enable logging"), getSettings()->enableLogging);
        logs.append(enableLogging);

        auto logsPathLabel = logs.emplace<QLabel>();

        QString logExplanation = uiText(
            "<span style=\"color:#bbb\"> They are saved as plain "
            "text files per channel, containing the messages with "
            "timestamps.</span>");

        getSettings()->logPath.connect(
            [logsPathLabel, logExplanation](const QString &logPath,
                                            auto) mutable {
                QString pathOriginal =
                    getApp()->getPaths().messageLogsIn(logPath);

                QString pathShortened =
                    uiText("Logs are saved at <a href=\"file:///") + pathOriginal +
                    R"("><span style="color: white;">)" +
                    shortenString(pathOriginal, 50) + ".</span></a>";

                logsPathLabel->setText(pathShortened + logExplanation);
                logsPathLabel->setToolTip(pathOriginal);
                logsPathLabel->setWordWrap(true);
            });

        logsPathLabel->setTextFormat(Qt::RichText);
        logsPathLabel->setTextInteractionFlags(Qt::TextBrowserInteraction |
                                               Qt::LinksAccessibleByKeyboard);
        logsPathLabel->setOpenExternalLinks(true);

        auto buttons = logs.emplace<QHBoxLayout>().withoutMargin();

        auto selectDir = buttons.emplace<QPushButton>(uiText("Select log directory "));
        auto resetDir = buttons.emplace<QPushButton>(uiText("Reset"));

        getSettings()->logPath.connect(
            [element = resetDir.getElement()](const QString &path) {
                element->setEnabled(!path.isEmpty());
            });

        buttons->addStretch();

        auto logsPathSizeLabel = logs.emplace<QLabel>();
        logsPathSizeLabel->setText(QtConcurrent::run([] {
                                       return fetchLogDirectorySize();
                                   }).result());

        QObject::connect(
            selectDir.getElement(), &QPushButton::clicked, this,
            [this, logsPathSizeLabel]() mutable {
                auto dirName = QFileDialog::getExistingDirectory(this);

                getSettings()->logPath = dirName;

                logsPathSizeLabel->setText(QtConcurrent::run([] {
                                               return fetchLogDirectorySize();
                                           }).result());
            });

        buttons->addSpacing(16);

        QObject::connect(
            resetDir.getElement(), &QPushButton::clicked, this,
            [logsPathSizeLabel]() mutable {
                getSettings()->logPath = "";

                logsPathSizeLabel->setText(QtConcurrent::run([] {
                                               return fetchLogDirectorySize();
                                           }).result());
            });

        auto logsTimestampFormatLayout =
            logs.emplace<QHBoxLayout>().withoutMargin();
        auto logsTimestampFormatLabel =
            logsTimestampFormatLayout.emplace<QLabel>();
        logsTimestampFormatLabel->setText(
            uiText(QString("Log file timestamp format: ")));

        QComboBox *logTimestampFormat = this->createComboBox(
            {uiText("Disable"), uiText("h:mm"), uiText("hh:mm"), uiText("h:mm a"), uiText("hh:mm a"), uiText("h:mm:ss"),
             uiText("hh:mm:ss"), uiText("h:mm:ss a"), uiText("hh:mm:ss a"), uiText("h:mm:ss.zzz"),
             uiText("h:mm:ss.zzz a"), uiText("hh:mm:ss.zzz"), uiText("hh:mm:ss.zzz a")},
            getSettings()->logTimestampFormat);
        logTimestampFormat->setToolTip(uiText("a = am/pm, zzz = milliseconds"));
        logsTimestampFormatLayout.append(logTimestampFormat);

        SettingWidget::checkbox(uiText("Use Twitch's timestamps"),
                                getSettings()->tryUseTwitchTimestamps)
            ->setTooltip(
                uiText("Try to use Twitch's timestamp (the time when the message was "
                "received by Twitch's chat server), rather than your "
                "computer's local timestamp.\nNote that using this setting can "
                "result in out-of-order timestamps in the log files, and that "
                "if Twitch's timestamp was unavailable for a message, it will "
                "fall back to your computer's local timestamp."))
            ->conditionallyEnabledBy(getSettings()->enableLogging)
            ->addToLayout(logs->layout());

        QCheckBox *onlyLogListedChannels =
            this->createCheckBox(uiText("Only log channels listed below"),
                                 getSettings()->onlyLogListedChannels);

        onlyLogListedChannels->setEnabled(getSettings()->enableLogging);
        logs.append(onlyLogListedChannels);

        auto *separatelyStoreStreamLogs =
            this->createCheckBox(uiText("Store live stream logs as separate files"),
                                 getSettings()->separatelyStoreStreamLogs);

        separatelyStoreStreamLogs->setEnabled(getSettings()->enableLogging);
        logs.append(separatelyStoreStreamLogs);

        QObject::connect(
            enableLogging, &QCheckBox::stateChanged, this,
            [enableLogging, onlyLogListedChannels,
             separatelyStoreStreamLogs]() mutable {
                onlyLogListedChannels->setEnabled(enableLogging->isChecked());
                separatelyStoreStreamLogs->setEnabled(
                    getSettings()->enableLogging);
            });

        EditableModelView *view =
            logs.emplace<EditableModelView>(
                    (new ChannelLoggingModel(nullptr))
                        ->initialized(&getSettings()->loggedChannels))
                .getElement();

        view->setTitles({uiText("Twitch channels")});
        view->getTableView()->horizontalHeader()->setSectionResizeMode(
            QHeaderView::Fixed);
        view->getTableView()->horizontalHeader()->setSectionResizeMode(
            0, QHeaderView::Stretch);

        std::ignore = view->addButtonPressed.connect([] {
            getSettings()->loggedChannels.append(ChannelLog("channel"));
        });
    }

    auto modMode = tabs.appendTab(new QVBoxLayout, uiText("Moderation buttons"));
    {
        auto label = modMode.emplace<QLabel>(
            uiText("Moderation mode is enabled by clicking <img width='18' "
            "height='18' src=':/buttons/moderationDisabledDarkMode18x18.png'> "
            "in a channel that you moderate.<br><br>"
            "Moderation buttons can be bound to chat commands such as \"/ban "
            "{user.name}\", \"/timeout {user.name} 1000\", \"/w someusername "
            "!report {user.name} was bad in channel {channel.name}\" or any "
            "other custom text commands.<br>"
            "For deleting messages use /delete {msg.id}.<br><br>"
            "More information can be found <a "
            "href='https://wiki.chatterino.com/Moderation/"
            "#moderation-mode'>here</a>."));
        label->setOpenExternalLinks(true);
        label->setWordWrap(true);
        label->setStyleSheet("color: #bbb");

        EditableModelView *view =
            modMode
                .emplace<EditableModelView>(
                    (new ModerationActionModel(nullptr))
                        ->initialized(&getSettings()->moderationActions))
                .getElement();

        view->setTitles({uiText("Action"), uiText("Icon")});
        view->getTableView()->horizontalHeader()->setSectionResizeMode(
            QHeaderView::Fixed);
        view->getTableView()->horizontalHeader()->setSectionResizeMode(
            0, QHeaderView::Stretch);
        view->getTableView()->setItemDelegateForColumn(
            ModerationActionModel::Column::Icon, new IconDelegate(view));
        QObject::connect(
            view->getTableView(), &QTableView::clicked,
            [this, view](const QModelIndex &clicked) {
                if (clicked.column() == ModerationActionModel::Column::Icon)
                {
                    auto fileUrl = QFileDialog::getOpenFileUrl(
                        this, uiText("Open Image"), QUrl(),
                        "Image Files (*.png *.jpg *.jpeg)");
                    view->getModel()->setData(clicked, fileUrl, Qt::UserRole);
                    view->getModel()->setData(clicked, fileUrl.fileName(),
                                              Qt::DisplayRole);

                    if (fileUrl.isEmpty())
                    {
                        view->getModel()->setData(clicked, QVariant(),
                                                  Qt::DecorationRole);
                    }
                    else
                    {
                        QPointer<EditableModelView> viewtemp = view;

                        loadPixmapFromUrl(
                            {fileUrl.toString()},
                            [clicked, view = viewtemp](const QPixmap &pixmap) {
                                postToThread([clicked, view, pixmap]() {
                                    if (view.isNull())
                                    {
                                        return;
                                    }

                                    view->getModel()->setData(
                                        clicked, pixmap, Qt::DecorationRole);
                                });
                            });
                    }
                }
            });

        std::ignore = view->addButtonPressed.connect([] {
            getSettings()->moderationActions.append(
                ModerationAction("/timeout {user.name} 300"));
        });

        auto *addPin = new QPushButton(uiText("Add Pin"));
        view->addCustomButton(addPin);
        QObject::connect(addPin, &QPushButton::clicked, [] {
            getSettings()->moderationActions.append(
                ModerationAction("/pin {msg.id}"));
        });
    }

    this->addModerationButtonSettings(tabs.getElement());

    this->itemsChangedTimer_.setSingleShot(true);
}

void ModerationPage::addModerationButtonSettings(QTabWidget *tabs)
{
    auto timeoutLayout =
        LayoutCreator{tabs}.appendTab(new QVBoxLayout, uiText("User Timeout Buttons"));
    auto texts = timeoutLayout.emplace<QVBoxLayout>().withoutMargin();
    {
        auto infoLabel = texts.emplace<QLabel>();
        infoLabel->setText(
            uiText("Customize the timeout buttons in the user popup (accessible "
            "through clicking a username).\nUse seconds (s), "
            "minutes (m), hours (h), days (d) or weeks (w)."));

        infoLabel->setAlignment(Qt::AlignCenter);

        auto maxLabel = texts.emplace<QLabel>();
        maxLabel->setText(uiText("(maximum timeout duration = 2 w)"));
        maxLabel->setAlignment(Qt::AlignCenter);
    }
    texts->setContentsMargins(0, 0, 0, 15);
    texts->setSizeConstraint(QLayout::SetMaximumSize);

    const auto valueChanged = [=, this] {
        bool ok = false;
        const auto index = QObject::sender()->objectName().toInt(&ok);
        if (!ok || index < 0 ||
            index >= static_cast<int>(this->durationInputs_.size()))
        {
            return;
        }

        auto *const line = this->durationInputs_[index];
        const auto duration = line->text().toInt();
        const auto unit = this->unitInputs_[index]->currentText();
        if (duration <= 0)
        {
            return;
        }

        if (unit == "d" && duration > 14)
        {
            line->setText("14");
            return;
        }
        else if (unit == "w" && duration > 2)
        {
            line->setText("2");
            return;
        }

        auto timeouts = getSettings()->timeoutButtons.getValue();
        if (index >= static_cast<int>(timeouts.size()))
        {
            return;
        }
        timeouts[index] = TimeoutButton{unit, duration};
        getSettings()->timeoutButtons.setValue(timeouts);
    };

    const auto reasonChanged = [=, this] {
        bool ok = false;
        const auto index = QObject::sender()->objectName().toInt(&ok);
        if (!ok || index < 0 ||
            index >= static_cast<int>(this->reasonInputs_.size()))
        {
            return;
        }

        auto reasons = getSettings()->timeoutButtonReasons.getValue();
        const auto timeoutCount =
            getSettings()->timeoutButtons.getValue().size();
        if (reasons.size() < timeoutCount)
        {
            reasons.resize(timeoutCount);
        }

        reasons[index] = this->reasonInputs_[index]->text();
        while (!reasons.empty() && reasons.back().trimmed().isEmpty())
        {
            reasons.pop_back();
        }
        getSettings()->timeoutButtonReasons.setValue(reasons);
    };

    auto i = 0;
    const auto reasons = getSettings()->timeoutButtonReasons.getValue();
    for (const auto &tButton : getSettings()->timeoutButtons.getValue())
    {
        const auto buttonNumber = QString::number(i);
        const auto index = i;
        auto timeout = timeoutLayout.emplace<QHBoxLayout>().withoutMargin();

        auto buttonLabel = timeout.emplace<QLabel>();
        buttonLabel->setText(uiText(QString("Button %1: ")).arg(++i));

        auto *lineEditDurationInput = new QLineEdit();
        lineEditDurationInput->setObjectName(buttonNumber);
        lineEditDurationInput->setValidator(new QIntValidator(1, 99, this));
        lineEditDurationInput->setText(QString::number(tButton.second));
        lineEditDurationInput->setAlignment(Qt::AlignRight);
        lineEditDurationInput->setMaximumWidth(30);
        timeout.append(lineEditDurationInput);

        auto *timeoutDurationUnit = new QComboBox();
        timeoutDurationUnit->setObjectName(buttonNumber);
        timeoutDurationUnit->addItems({"s", "m", "h", "d", "w"});
        timeoutDurationUnit->setCurrentText(tButton.first);
        timeout.append(timeoutDurationUnit);

        auto reasonLabel = timeout.emplace<QLabel>();
        reasonLabel->setText(uiText("Reason:"));

        auto *lineEditReasonInput = new QLineEdit();
        lineEditReasonInput->setObjectName(buttonNumber);
        lineEditReasonInput->setPlaceholderText(uiText("optional timeout reason"));
        if (index < static_cast<int>(reasons.size()))
        {
            lineEditReasonInput->setText(reasons[index]);
        }
        lineEditReasonInput->setMinimumWidth(220);
        lineEditReasonInput->setMaximumWidth(360);
        timeout.append(lineEditReasonInput);

        QObject::connect(lineEditDurationInput, &QLineEdit::textChanged, this,
                         valueChanged);

        QObject::connect(timeoutDurationUnit, &QComboBox::currentTextChanged,
                         this, valueChanged);

        QObject::connect(lineEditReasonInput, &QLineEdit::textChanged, this,
                         reasonChanged);

        timeout->addStretch();

        this->durationInputs_.push_back(lineEditDurationInput);
        this->unitInputs_.push_back(timeoutDurationUnit);
        this->reasonInputs_.push_back(lineEditReasonInput);

        timeout->setContentsMargins(40, 0, 0, 0);
        timeout->setSizeConstraint(QLayout::SetMaximumSize);
    }

    auto banReason = timeoutLayout.emplace<QHBoxLayout>().withoutMargin();
    {
        auto label = banReason.emplace<QLabel>();
        label->setText(uiText("Ban reason:"));

        auto *input = new QLineEdit();
        input->setPlaceholderText(uiText("optional ban reason"));
        input->setText(getSettings()->timeoutBanReason.getValue());
        input->setMinimumWidth(220);
        input->setMaximumWidth(360);
        banReason.append(input);
        banReason->addStretch();
        banReason->setContentsMargins(40, 8, 0, 0);
        banReason->setSizeConstraint(QLayout::SetMaximumSize);

        QObject::connect(input, &QLineEdit::textChanged, this,
                         [](const QString &text) {
                             getSettings()->timeoutBanReason = text;
                         });
    }

    auto promptOptions = timeoutLayout.emplace<QVBoxLayout>().withoutMargin();
    {
        auto promptLabel = promptOptions.emplace<QLabel>();
        promptLabel->setText(
            uiText("Saved reasons are sent with normal timeout and ban clicks. Use "
            "the reason prompt when you want to edit the reason first."));
        promptLabel->setWordWrap(true);
        promptLabel->setStyleSheet("color: #bbb");

        auto *rightClickPrompt = this->createCheckBox(
            uiText("Open the reason prompt on right-click"),
            getSettings()->timeoutReasonPromptOnRightClick,
            "Right-click a timeout or ban button in a usercard to edit the "
            "reason before sending.");
        promptOptions.append(rightClickPrompt);

        auto modifierRow = promptOptions.emplace<QHBoxLayout>().withoutMargin();
        auto *modifierPrompt = this->createCheckBox(
            uiText("Open the reason prompt while holding"),
            getSettings()->timeoutReasonPromptOnModifier,
            "Hold this modifier while clicking a timeout or ban button to edit "
            "the reason before sending.");
        modifierRow.append(modifierPrompt);

        auto *modifierKey =
            this->createComboBox({uiText("Shift"), uiText("Ctrl"), uiText("Alt")},
                                 getSettings()->timeoutReasonPromptModifier);
        modifierKey->setMaximumWidth(120);
        modifierRow.append(modifierKey);
        modifierRow->addStretch();

        auto updateModifierEnabled = [modifierPrompt, modifierKey] {
            modifierKey->setEnabled(modifierPrompt->isChecked());
        };
        QObject::connect(modifierPrompt, &QCheckBox::toggled, this,
                         [updateModifierEnabled](bool) {
                             updateModifierEnabled();
                         });
        updateModifierEnabled();

        auto *showSendButton = this->createCheckBox(
            uiText("Show a Send button in the reason prompt"),
            getSettings()->timeoutReasonPromptShowSendButton,
            "When this is off, press Enter to send the prompt. Esc or clicking "
            "away cancels it.");
        promptOptions.append(showSendButton);

        auto *prefillSavedReason = this->createCheckBox(
            uiText("Prefill the reason prompt with the saved reason"),
            getSettings()->timeoutReasonPromptPrefillSavedReason,
            "When this is on, right-click and modifier prompts start with the "
            "saved timeout or ban reason selected for quick editing.");
        promptOptions.append(prefillSavedReason);
    }
    promptOptions->setContentsMargins(40, 12, 0, 15);
    promptOptions->setSizeConstraint(QLayout::SetMaximumSize);

    timeoutLayout->addStretch();
}

void ModerationPage::selectModerationActions()
{
    this->tabWidget_->setCurrentIndex(1);
}

}  // namespace chatterino
