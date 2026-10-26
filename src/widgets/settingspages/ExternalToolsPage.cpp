// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "common/UiLanguage.hpp"
#include "widgets/settingspages/ExternalToolsPage.hpp"

#include "controllers/spellcheck/SpellChecker.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"
#include "util/Clipboard.hpp"
#include "util/Helpers.hpp"
#include "util/ImageUploader.hpp"
#include "util/StreamLink.hpp"
#include "widgets/settingspages/SettingWidget.hpp"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>

#include <algorithm>

namespace chatterino {

namespace {

inline const QStringList STREAMLINK_QUALITY = {
    "Choose", "Source", "High", "Medium", "Low", "Audio only",
};

void exportImageUploaderSettings(QWidget *parent)
{
    const auto &s = *getSettings();

    QJsonObject settingsObj = imageuploader::detail::exportSettings(s);
    QJsonDocument doc(settingsObj);
    crossPlatformCopy(doc.toJson(QJsonDocument::Indented));

    QMessageBox::information(
        parent, uiText("Settings Exported"),
        uiText("Image uploader settings have been copied to clipboard as JSON."));
}

void importImageUploaderSettings(QWidget *parent)
{
    QString clipboardText = getClipboardText().trimmed();

    auto res = imageuploader::detail::validateImportJson(clipboardText);
    if (!res)
    {
        QMessageBox::warning(
            parent, uiText("Import Failed"),
            uiText(QString("Error validating image uploader import: %1."))
                .arg(res.error()));
        return;
    }
    const auto &settingsObj = *res;

    int ret = QMessageBox::question(
        parent, uiText("Import Settings"),
        uiText("This will overwrite your current image uploader settings. Continue?"),
        QMessageBox::Yes | QMessageBox::No);

    if (ret != QMessageBox::Yes)
    {
        return;
    }

    auto &s = *getSettings();
    if (imageuploader::detail::importSettings(settingsObj, s))
    {
        QMessageBox::information(
            parent, uiText("Import Successful"),
            uiText("Image uploader settings have been imported successfully!"));
    }
    else
    {
        QMessageBox::warning(
            parent, uiText("Import Failed"),
            uiText("No valid image uploader settings found in the JSON."));
    }
}

}  // namespace

ExternalToolsPage::ExternalToolsPage()
    : view(GeneralPageView::withoutNavigation(this))
{
    auto *y = new QVBoxLayout;
    auto *x = new QHBoxLayout;
    x->addWidget(this->view);
    auto *z = new QFrame;
    z->setLayout(x);
    y->addWidget(z);
    this->setLayout(y);

    this->initLayout(*this->view);
}

bool ExternalToolsPage::filterElements(const QString &query)
{
    if (this->view)
    {
        return this->view->filterElements(query) || query.isEmpty();
    }

    return false;
}

void ExternalToolsPage::initLayout(GeneralPageView &layout)
{
    auto &s = *getSettings();

    {
        auto *form = new QFormLayout;
        layout.addTitle(uiText("Streamlink"));
        layout.addDescription(uiText("Streamlink is a command-line utility that pipes "
                              "video streams from "
                              "various services into a video player, such as "
                              "VLC. Make sure to edit "
                              "the configuration file before you use it!"));
        layout.addDescription(
            formatRichNamedLink("https://streamlink.github.io/", uiText("Website")) +
            " " +
            formatRichNamedLink(
                "https://github.com/streamlink/streamlink/releases/latest",
                uiText("Download")) +
            " " +
            formatRichNamedLink("https://streamlink.github.io/cli.html#twitch",
                                uiText("Documentation")));

        SettingWidget::checkbox(uiText("Use custom path (Enable if using non-standard "
                                "streamlink installation path)"),
                                s.streamlinkUseCustomPath)
            ->addTo(layout);

        layout.addDescription(
            uiText(QStringLiteral(
                "Chatterino expects the executable to be called \"%1\"."))
                .arg(STREAMLINK_BINARY_NAME));

        layout.addLayout(form);

        SettingWidget::lineEdit(
            uiText("Custom streamlink path"), s.streamlinkPath,
            uiText("Path to folder where Streamlink executable can be found"))
            ->conditionallyEnabledBy(s.streamlinkUseCustomPath)
            ->addTo(layout, form);

        SettingWidget::dropdown(uiText("Preferred quality"), s.preferredQuality)
            ->addTo(layout, form);

        SettingWidget::lineEdit(uiText("Additional options"), s.streamlinkOpts, "")
            ->addTo(layout, form);
    }

    {
        layout.addTitle(uiText("Custom stream player"));
        layout.addDescription(
            uiText("You can open Twitch streams directly in any video player that has "
            "built-in Twitch support and its own URI Scheme.\nE.g.: IINA for "
            "macOS and PotPlayer (with extension) for Windows.\n\nWith this "
            "value set, you will get the option to \"Open in custom player\" "
            "when right-clicking a channel header."));

        SettingWidget::lineEdit(uiText("Custom stream player URI Scheme"),
                                s.customURIScheme, "custom-player-scheme://")
            ->addTo(layout);
    }

    {
        auto *form = new QFormLayout;
        layout.addTitle(uiText("Image Uploader"));

        layout.addDescription(
            uiText("You can set custom host for uploading images, like imgur.com or "
            "s-ul.eu.<br>Check ") +
            formatRichNamedLink("https://chatterino.com/help/image-uploader",
                                "this guide") +
            uiText(" for help."));

        SettingWidget::checkbox(uiText("Enable image uploader"), s.imageUploaderEnabled)
            ->addTo(layout);

        SettingWidget::checkbox(uiText("Ask for confirmation when uploading an image"),
                                s.askOnImageUpload)
            ->addTo(layout);

        layout.addLayout(form);

        SettingWidget::lineEdit(uiText("Request URL"), s.imageUploaderUrl)
            ->addTo(layout, form);

        SettingWidget::lineEdit(uiText("Form field"), s.imageUploaderFormField)
            ->addTo(layout, form);

        SettingWidget::lineEdit(uiText("Extra Headers"), s.imageUploaderHeaders)
            ->addTo(layout, form);

        SettingWidget::lineEdit(uiText("Image link"), s.imageUploaderLink)
            ->addTo(layout, form);

        SettingWidget::lineEdit(uiText("Deletion link"), s.imageUploaderDeletionLink)
            ->addTo(layout, form);

        layout.addDescription(
            uiText("Export your current image uploader settings as JSON to share with "
            "others, or import settings from clipboard (compatible with ShareX "
            ".sxcu format)."));

        auto *buttonLayout = new QHBoxLayout;

        auto *importButton = new QPushButton(uiText("Import Settings from Clipboard"));
        importButton->setToolTip(
            uiText("Import image uploader settings from clipboard JSON"));
        QObject::connect(importButton, &QPushButton::clicked, [this]() {
            importImageUploaderSettings(this);
        });
        buttonLayout->addWidget(importButton);

        auto *exportButton = new QPushButton(uiText("Export Settings to Clipboard"));
        exportButton->setToolTip(
            uiText("Copy current image uploader settings to clipboard as JSON"));
        QObject::connect(exportButton, &QPushButton::clicked, [this]() {
            exportImageUploaderSettings(this);
        });
        buttonLayout->addWidget(exportButton);

        buttonLayout->addStretch();
        layout.addLayout(buttonLayout);
    }

#ifdef CHATTERINO_WITH_SPELLCHECK
    {
        layout.addTitle(uiText("Spell checker (experimental)"));

        layout.addDescription(
            uiText(u"Check the spelling of words in the input box of splits."
            " Chatterino does not include dictionaries - they have to "
            "be downloaded or created manually. Chatterino expects "
            "Hunspell "
            "dictionaries in ") %
            formatRichNamedLink(getApp()->getPaths().dictionariesDirectory,
                                getApp()->getPaths().dictionariesDirectory) %
            uiText(u". Dictionaries are pairs of .aff (affixes) and .dic (dictionary) "
            u"files."));

        SettingWidget::checkbox(uiText("Check spelling by default"),
                                s.enableSpellChecking)
            ->setTooltip(uiText("Check the spelling of words in the input box of all "
                         "splits by default."))
            ->addTo(layout);
        SettingWidget::intInput(uiText("Number of suggestions in context menu"),
                                s.nSpellCheckingSuggestions,
                                {
                                    .min = -1,
                                    .max = std::numeric_limits<int>::max(),
                                })
            ->setTooltip(
                uiText("When right-clicking any word, show this many suggestions. If "
                "this is 0, no suggestions will be shown and if it's -1, no "
                "limit is set."))
            ->addTo(layout);

        auto toItem =
            [](const DictionaryInfo &dict) -> std::pair<QString, QVariant> {
            return {
                dict.name,
                dict.path,
            };
        };
        std::vector<std::pair<QString, QVariant>> dictList{{"None", ""}};

        std::ranges::transform(
            getApp()->getSpellChecker()->getAvailableDictionaries(),
            std::back_inserter(dictList), toItem);

        if (dictList.size() > 1)
        {
            SettingWidget::dropdown(uiText("Default dictionary (requires restart)"),
                                    s.spellCheckingDefaultDictionary, dictList)
                ->addTo(layout);
        }
    }
#endif

    layout.addStretch();
}

}  // namespace chatterino
