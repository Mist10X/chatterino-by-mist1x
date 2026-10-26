#include "common/UiLanguage.hpp"
#include "widgets/helper/ArchiveActions.hpp"

#include "Application.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>

namespace chatterino {
ArchiveEndpoint configuredArchiveEndpoint()
{
    const ArchiveEndpoint endpoint{getSettings()->archiveSocketName.getValue(),
                                   getSettings()->archiveDatasetId.getValue()};
    // Explicit settings always win, including incomplete/invalid values. Never
    // silently bypass a broken explicit configuration with the sidecar.
    if (!endpoint.socketName.isEmpty() || !endpoint.datasetId.isEmpty())
        return endpoint;
    return readArchiveEndpointFile(
        QDir(getApp()->getPaths().settingsDirectory)
            .filePath(QStringLiteral("archive-bridge.json")));
}

void openArchiveView(QWidget *owner, const ArchiveEndpoint &endpoint,
                     const QString &view, const QString &userId,
                     const QString &login, const QString &channel,
                     const QString &messageId)
{
    // Explicit navigation survives closing an auto-close usercard or its menu.
    // It still has a bounded deadline and deletes itself on either outcome.
    // Card *queries* remain children of the card and cancel on card destruction.
    auto *client = new ArchiveClient(endpoint, QCoreApplication::instance());
    const QPointer<QWidget> target(owner);
    QObject::connect(client, &ArchiveClient::viewOpened, client, [client] {
        client->deleteLater();
    });
    QObject::connect(client, &ArchiveClient::failed, client,
                     [client, target](const QString &, const QString &message) {
                         if (target)
                         {
                             auto *box = new QMessageBox(
                                 QMessageBox::Information,
                                 uiText(QStringLiteral("Архив")), message,
                                 QMessageBox::Ok, target);
                             box->setTextFormat(Qt::PlainText);
                             box->setAttribute(Qt::WA_DeleteOnClose);
                             box->open();
                         }
                         client->deleteLater();
                     });
    client->openView(view, userId, login, channel, messageId);
}

void addArchiveActions(QMenu *menu, QWidget *owner, const QString &userId,
                       const QString &login, const QString &channel,
                       const QString &messageId)
{
    auto *archive = menu->addMenu(uiText(QStringLiteral("Архив")));
    const auto endpoint = configuredArchiveEndpoint();
    auto add = [&](const QString &label, const QString &view,
                   bool needsMessage = false) {
        auto *action = archive->addAction(label);
        action->setEnabled(!userId.isEmpty() && !login.isEmpty() &&
                           (!needsMessage || !messageId.isEmpty()));
        if (!action->isEnabled())
            action->setToolTip(
                uiText(QStringLiteral("Нет точного Twitch ID или ID сообщения")));
        QObject::connect(action, &QAction::triggered, owner, [=] {
            openArchiveView(owner, endpoint, view, userId, login, channel,
                            messageId);
        });
    };
    add(uiText(QStringLiteral("История пользователя")), "user");
    add(uiText(QStringLiteral("Наказания пользователя")), "moderation");
    add(uiText(QStringLiteral("Найти это сообщение")), "message", true);
    add(uiText(QStringLiteral("Контекст сообщения")), "context", true);
    archive->addSeparator();
    add(uiText(QStringLiteral("Открыть Архив")), "archive");
}
}  // namespace chatterino
