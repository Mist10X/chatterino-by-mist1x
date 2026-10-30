// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "common/UiLanguage.hpp"
#include "widgets/settingspages/AboutPage.hpp"
#include "widgets/helper/ArchiveActions.hpp"
#include "singletons/InstallCounter.hpp"

#include "Application.hpp"
#include "common/Common.hpp"
#include "common/QLogging.hpp"
#include "common/Version.hpp"
#include "singletons/Paths.hpp"
#include "util/Expected.hpp"
#include "util/LayoutCreator.hpp"
#include "util/RemoveScrollAreaBackground.hpp"
#include "widgets/BasePopup.hpp"
#include "widgets/layout/FlowLayout.hpp"

#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPointer>
#include <QStringBuilder>
#include <QTextEdit>
#include <QTextStream>
#include <QVBoxLayout>
#include <twitch-eventsub-ws/chrono.hpp>

namespace chatterino {

constexpr int PIXMAP_WIDTH = 500;

constexpr QStringView LINK_CHATTERINO_FEATURES =
    u"https://chatterino.com/#features";

AboutPage::AboutPage()
{
    LayoutCreator<AboutPage> layoutCreator(this);

    auto scroll = layoutCreator.emplace<QScrollArea>();
    auto widget = scroll.emplaceScrollAreaWidget();
    removeScrollAreaBackground(scroll.getElement(), widget.getElement());

    auto layout = widget.setLayoutType<QVBoxLayout>();
    {
        QPixmap pixmap;
        pixmap.load(":/icon.png");

        auto logo = layout.emplace<QLabel>().assign(&this->logo_);
        logo->setPixmap(pixmap);
        if (pixmap.width() != 0)
        {
            logo->setFixedSize(96, 96);
        }
        logo->setScaledContents(true);

        auto brand = layout.emplace<QLabel>(QStringLiteral("<h1>Chatterino by Mist1X</h1>"));
        brand->setObjectName("BrandDisplayName");
        auto attribution = layout.emplace<QLabel>(uiText(QStringLiteral(
            R"(Unofficial build based on <a href="https://github.com/Chatterino/chatterino2">Chatterino 2</a>, <a href="https://github.com/SevenTV/chatterino7">Chatterino7</a>, <a href="https://github.com/Leafyzito/leafyrino">Leafyrino</a>, <a href="https://github.com/2547techno/technorino">Technorino</a> and <a href="https://github.com/EEVA-inc/Moltorino7">Moltorino7</a> (MIT license). It is not affiliated with their authors or with Twitch. Changes by Mist1X: <a href="https://github.com/Mist10X/chatterino-by-mist1x">github.com/Mist10X/chatterino-by-mist1x</a>. Who made what: the CREDITS.md file there.)")));
        attribution->setWordWrap(true);
        attribution->setTextFormat(Qt::RichText);
        attribution->setOpenExternalLinks(true);
        if (archiveConnected())
        {
            auto installs = layout.emplace<QLabel>(uiText("Installations: …"));
            QPointer<QLabel> target = installs.getElement();
            fetchInstallCount([target](int count) {
                if (target)
                {
                    target->setText(count < 0
                                        ? uiText("Installations: unknown")
                                        : uiText("Installations: %1").arg(count));
                }
            });
        }
        attribution->setObjectName("BrandAttribution");

        auto versionInfo = layout.emplace<QGroupBox>(uiText("Version"));
        {
            auto vbox = versionInfo.emplace<QVBoxLayout>();
            const auto &version = Version::instance();

            QString string =
                version.buildString() % "<br>" % version.runningString();

            if (!version.extraString().isEmpty())
            {
                string += " " % version.extraString();
            }

            string +=
                uiText("<br><br>Your settings directory is located at <a href=\"");
            string +=
                QUrl::fromLocalFile(getApp()->getPaths().settingsDirectory)
                    .toString(QUrl::FullyEncoded);
            string += "\">";
            string += getApp()->getPaths().settingsDirectory.toHtmlEscaped();
            string += "</a>.";

            auto label = vbox.emplace<QLabel>(string);
            label->setWordWrap(true);
            label->setOpenExternalLinks(true);
            label->setTextInteractionFlags(Qt::TextBrowserInteraction);
        }

        auto aboutChatterino = layout.emplace<QGroupBox>(uiText("About Chatterino..."));
        {
            auto l = aboutChatterino.emplace<QVBoxLayout>();

            // clang-format off
            l.emplace<QLabel>(uiText("Chatterino Wiki can be found <a href=\"") % LINK_CHATTERINO_WIKI % uiText("\">here</a>"))->setOpenExternalLinks(true);
            l.emplace<QLabel>(uiText("All about Chatterino's <a href=\"") % LINK_CHATTERINO_FEATURES % uiText("\">features</a>"))->setOpenExternalLinks(true);
            // Problems with this build go to its own page, not to the
            // communities of the projects it is based on.
            l.emplace<QLabel>(uiText("Report a problem with this build <a href=\"https://github.com/Mist10X/chatterino-by-mist1x/issues\">on GitHub</a>"))->setOpenExternalLinks(true);
            // clang-format on
        }

        auto licenses =
            layout.emplace<QGroupBox>(uiText("Open source software used..."));
        {
            auto form = licenses.emplace<QFormLayout>();

            addLicense(form.getElement(), "Montserrat",
                       "https://github.com/google/fonts/tree/main/ofl/montserrat",
                       ":/licenses/montserrat-OFL.txt");
            addLicense(form.getElement(), "Qt Framework", "https://www.qt.io",
                       ":/licenses/qt_lgpl-3.0.txt");
            addLicense(form.getElement(), "Boost", "https://www.boost.org/",
                       ":/licenses/boost_boost.txt");
            addLicense(form.getElement(), "LibCommuni",
                       "https://github.com/communi/libcommuni",
                       ":/licenses/libcommuni_BSD3.txt");
            addLicense(form.getElement(), "OpenSSL", "https://www.openssl.org/",
                       ":/licenses/openssl.txt");
            addLicense(form.getElement(), "RapidJson", "https://rapidjson.org/",
                       ":/licenses/rapidjson.txt");
            addLicense(form.getElement(), "Pajlada/Settings",
                       "https://github.com/pajlada/settings",
                       ":/licenses/pajlada_settings.txt");
            addLicense(form.getElement(), "Pajlada/Signals",
                       "https://github.com/pajlada/signals",
                       ":/licenses/pajlada_signals.txt");
            addLicense(form.getElement(), "Pajlada/Serialize",
                       "https://github.com/pajlada/serialize",
                       ":/licenses/pajlada_serialize.txt");
            addLicense(form.getElement(), "lrucache",
                       "https://github.com/lamerman/cpp-lru-cache",
                       ":/licenses/lrucache.txt");
            addLicense(form.getElement(), "magic_enum",
                       "https://github.com/Neargye/magic_enum",
                       ":/licenses/magic_enum.txt");
            addLicense(form.getElement(), "semver",
                       "https://github.com/Neargye/semver",
                       ":/licenses/semver.txt");
            addLicense(form.getElement(), "miniaudio",
                       "https://github.com/mackron/miniaudio",
                       ":/licenses/miniaudio.txt");
#ifdef CHATTERINO_HAVE_PLUGINS
            addLicense(form.getElement(), "lua", "https://lua.org",
                       ":/licenses/lua.txt");
#endif
#ifdef CHATTERINO_WITH_CRASHPAD
            addLicense(form.getElement(), "sentry-crashpad",
                       "https://github.com/getsentry/crashpad",
                       ":/licenses/crashpad.txt");
#endif
            addLicense(form.getElement(), "Fluent icons",
                       "https://github.com/microsoft/fluentui-system-icons",
                       ":/licenses/fluenticons.txt");
            addLicense(form.getElement(), "KImageFormats",
                       "https://invent.kde.org/frameworks/kimageformats",
                       ":/licenses/kimageformats.txt");
#ifdef CHATTERINO_USING_NONSTD_EXPECTED
            addLicense(form.getElement(), "expected-lite",
                       "https://github.com/martinmoene/expected-lite",
                       ":/licenses/expected-lite.txt");
#endif
            addLicense(form.getElement(), "certify",
                       "https://github.com/djarek/certify",
                       ":/licenses/certify.txt");
#ifdef CHATTERINO_USING_HOWARD_HINNANTS_DATE
            addLicense(form.getElement(), "Howard Hinnant's date.h",
                       "https://github.com/HowardHinnant/date",
                       ":/licenses/howard-hinnant-date.txt");
#endif
            addLicense(form.getElement(), "{fmt}", "https://fmt.dev",
                       ":/licenses/fmtlib.txt");
            addLicense(form.getElement(), "Unicode",
                       "https://www.unicode.org/copyright.html",
                       ":/licenses/unicode.txt");
#ifdef CHATTERINO_WITH_SPELLCHECK
            addLicense(form.getElement(), "Hunspell",
                       "https://hunspell.github.io", ":/licenses/hunspell.txt");
#endif
        }

        auto attributions = layout.emplace<QGroupBox>(uiText("Attributions..."));
        {
            auto l = attributions.emplace<QVBoxLayout>();

            l.emplace<QLabel>(uiText("Twemoji emojis provided by <a "
                              "href=\"https://github.com/twitter/"
                              "twemoji\">Twitter's Twemoji</a>"))
                ->setOpenExternalLinks(true);
            l.emplace<QLabel>(uiText("Facebook emojis provided by <a "
                              "href=\"https://facebook.com\">Facebook</a>"))
                ->setOpenExternalLinks(true);
            l.emplace<QLabel>(uiText("Apple emojis provided by <a "
                              "href=\"https://apple.com\">Apple</a>"))
                ->setOpenExternalLinks(true);
            l.emplace<QLabel>(uiText("Google emojis provided by <a "
                              "href=\"https://google.com\">Google</a>"))
                ->setOpenExternalLinks(true);
            l.emplace<QLabel>(
                 uiText("Emoji datasource provided by <a "
                 "href=\"https://www.iamcal.com/\">Cal Henderson</a> "
                 "(<a "
                 "href=\"https://github.com/iamcal/emoji-data/blob/master/"
                 "LICENSE\">show license</a>)"))
                ->setOpenExternalLinks(true);
            l.emplace<QLabel>(uiText("Twitch emote data provided by <a "
                              "href=\"https://chatvau.lt/\">Chat Vault</a>"))
                ->setOpenExternalLinks(true);
            // clang-format on
        }

        auto contributors = layout.emplace<QGroupBox>(uiText("People"));
        {
            auto l = contributors.emplace<FlowLayout>();

            QFile contributorsFile(":/contributors.txt");
            if (!contributorsFile.open(QFile::ReadOnly))
            {
                assert(false && "Resources not loaded");
                qCWarning(chatterinoWidget) << "Resources not loaded";
            }

            QTextStream stream(&contributorsFile);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)

#else
            stream.setCodec("UTF-8");
#endif

            QString line;

            while (stream.readLineInto(&line))
            {
                if (line.isEmpty() || line.startsWith('#'))
                {
                    continue;
                }

                if (line.startsWith(u"@header"))
                {
                    if (l->count() != 0)
                    {
                        l->addLinebreak(20);
                    }
                    auto *label = new QLabel(QStringLiteral("<h1>%1</h1>")
                                                 .arg(uiText(line.mid(8).trimmed())));
                    l->addWidget(label);
                    l->addLinebreak(8);
                    continue;
                }

                QStringList contributorParts = line.split("|");

                if (contributorParts.size() != 3)
                {
                    qCWarning(chatterinoWidget)
                        << "Missing parts in line" << line;
                    continue;
                }

                QString username = contributorParts[0].trimmed();
                QString url = contributorParts[1].trimmed();
                QString avatarUrl = contributorParts[2].trimmed();

                auto *usernameLabel =
                    new QLabel(uiText("<a href=\"") + url + "\">" + username + "</a>");
                usernameLabel->setOpenExternalLinks(true);
                usernameLabel->setToolTip(url);

                auto contributorBox2 = l.emplace<QVBoxLayout>();

                const auto addAvatar = [&] {
                    auto *avatar = new QLabel();
                    QPixmap avatarPixmap;
                    if (avatarUrl.isEmpty())
                    {
                        avatarPixmap.load(":/avatars/anon.png");
                    }
                    else
                    {
                        avatarPixmap.load(avatarUrl);
                    }

                    avatar->setPixmap(avatarPixmap);
                    avatar->setFixedSize(64, 64);
                    avatar->setScaledContents(true);
                    contributorBox2->addWidget(avatar, 0, Qt::AlignCenter);
                };

                const auto addLabels = [&] {
                    auto *labelBox = new QVBoxLayout();
                    contributorBox2->addLayout(labelBox);

                    labelBox->addWidget(usernameLabel, 0, Qt::AlignCenter);
                };

                addAvatar();
                addLabels();
            }
        }
    }

    layout->addStretch(1);
}

void AboutPage::addLicense(QFormLayout *form, const QString &name,
                           const QString &website, const QString &licenseLink)
{
    auto *a = new QLabel(uiText("<a href=\"") + website + "\">" + name + "</a>");
    a->setOpenExternalLinks(true);
    auto *b = new QLabel(uiText("<a href=\"") + licenseLink + uiText("\">show license</a>"));
    QObject::connect(
        b, &QLabel::linkActivated, [parent = this, name, licenseLink] {
            auto *window = new BasePopup(
                {
                    BaseWindow::EnableCustomFrame,
                    BaseWindow::DisableLayoutSave,
                    BaseWindow::BoundsCheckOnShow,
                },
                parent);
            window->setWindowTitle(uiText("Leafyrino - License for ") + name);
            window->setAttribute(Qt::WA_DeleteOnClose);
            auto *layout = new QVBoxLayout();
            auto *edit = new QTextEdit;

            QFile file(licenseLink);
            if (!file.open(QIODevice::ReadOnly))
            {
                assert(false && "License not found");
                qCWarning(chatterinoWidget)
                    << "License not found" << licenseLink;
            }
            edit->setText(file.readAll());
            edit->setReadOnly(true);

            layout->addWidget(edit);

            window->getLayoutContainer()->setLayout(layout);
            window->show();
        });

    form->addRow(a, b);
}

}  // namespace chatterino
