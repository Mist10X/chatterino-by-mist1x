#pragma once

#include "providers/archive/ArchiveClient.hpp"

#include <QWidget>

class QLabel;
class QPushButton;
class QTextBrowser;

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
    void render(const QJsonObject &data);
    void status(const QString &text, const QString &state);
    void open(const QString &view, const QString &messageId = {});

    ArchiveEndpoint endpoint_;
    ArchiveClient *client_;
    QLabel *status_;
    QTextBrowser *body_;
    QPushButton *history_;
    QPushButton *moderation_;
    QPushButton *open_;
    QString userId_;
    QString login_;
    QString channel_;
};
}  // namespace chatterino
