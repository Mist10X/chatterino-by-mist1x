#pragma once

#include "providers/archive/ArchiveClient.hpp"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QPointer>
#include <QWidget>

class QLabel;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QTextBrowser;
class QVBoxLayout;

namespace chatterino {
class ArchiveUserCard final : public QWidget
{
    Q_OBJECT
public:
    explicit ArchiveUserCard(QWidget *parent = nullptr);
    ArchiveUserCard(ArchiveEndpoint endpoint, QWidget *parent = nullptr);
    void setTarget(const QString &userId, const QString &login,
                   const QString &channel);
    void setIdentityUnavailable();

    QString statusText() const;
    QString plainText() const;

private:
    enum class Tab { Punishments, Messages };

    void render(const QJsonObject &data);
    void status(const QString &text, const QString &state);
    void open(const QString &view, const QString &messageId = {});
    void clearData();
    void showTab(Tab tab);
    /// One punishment as a rounded box, coloured like its notice in the chat.
    void addPunishment(const QJsonObject &event, bool fromDossier);
    QLabel *avatarLabel(const QString &channel);

    ArchiveEndpoint endpoint_;
    ArchiveClient *client_;
    QLabel *status_;
    /// "1 бан · 50 мутов": small chips, as the badge counter of the card
    QWidget *summary_;
    QWidget *tabs_;
    QPushButton *punishmentsTab_;
    QPushButton *messagesTab_;
    QStackedWidget *pages_;
    QScrollArea *punishments_;
    QVBoxLayout *punishmentsList_;
    QTextBrowser *body_;
    QPushButton *history_;
    QPushButton *moderation_;
    QPushButton *open_;
    QString userId_;
    QString login_;
    QString channel_;
    QString punishmentsText_;
    /// channel login -> the avatar labels waiting for its picture
    QHash<QString, QList<QPointer<QLabel>>> avatars_;
};
}  // namespace chatterino
