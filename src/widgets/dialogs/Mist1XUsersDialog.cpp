// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/dialogs/Mist1XUsersDialog.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/UiLanguage.hpp"
#include "providers/twitch/Mist1XMark.hpp"
#include "singletons/Paths.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace chatterino {

namespace {

QString keyPath()
{
    return QDir(getApp()->getPaths().settingsDirectory)
        .filePath(QStringLiteral("mist1x-admin.key"));
}

QByteArray adminKey()
{
    QFile file(keyPath());
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    return file.readAll().trimmed();
}

QString when(double ms)
{
    if (ms <= 0)
    {
        return {};
    }
    return QDateTime::fromMSecsSinceEpoch(qint64(ms))
        .toLocalTime()
        .toString(QStringLiteral("dd.MM.yyyy HH:mm"));
}

}  // namespace

bool Mist1XUsersDialog::available()
{
    return tryGetApp() != nullptr && !adminKey().isEmpty();
}

Mist1XUsersDialog::Mist1XUsersDialog(QWidget *parent)
    : QDialog(parent)
    , table_(new QTableWidget(0, 4, this))
    , status_(new QLabel(this))
{
    this->setWindowTitle(uiText("Chatterino by Mist1X users"));
    this->resize(620, 460);
    auto *layout = new QVBoxLayout(this);
    this->table_->setHorizontalHeaderLabels(
        {uiText("Name"), uiText("Version"), uiText("First seen"),
         uiText("Last seen")});
    this->table_->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::Stretch);
    this->table_->verticalHeader()->hide();
    this->table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    this->table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    this->table_->setSortingEnabled(true);
    layout->addWidget(this->table_, 1);
    auto *row = new QHBoxLayout;
    row->addWidget(this->status_, 1);
    auto *refresh = new QPushButton(uiText("Refresh"), this);
    QObject::connect(refresh, &QPushButton::clicked, this, [this] {
        this->reload();
    });
    row->addWidget(refresh);
    layout->addLayout(row);
    this->reload();
}

void Mist1XUsersDialog::reload()
{
    this->status_->setText(uiText("Loading..."));
    QPointer<Mist1XUsersDialog> self(this);
    NetworkRequest(mist1xmark::serviceUrl() + QStringLiteral("/v1/users"))
        .header("X-Admin-Key", adminKey())
        .timeout(20000)
        .onSuccess([self](const NetworkResult &result) {
            if (!self)
            {
                return;
            }
            const auto users = result.parseJson().value("users").toArray();
            self->table_->setSortingEnabled(false);
            self->table_->setRowCount(int(users.size()));
            for (int i = 0; i < users.size(); ++i)
            {
                const auto user = users[i].toObject();
                const auto login = user.value("login").toString();
                auto *name = new QTableWidgetItem(login);
                name->setToolTip(QStringLiteral("Twitch ID ") +
                                 user.value("id").toString());
                self->table_->setItem(i, 0, name);
                self->table_->setItem(
                    i, 1, new QTableWidgetItem(user.value("version").toString()));
                // sortable as text: yyyy-MM-dd under the shown day
                auto *first =
                    new QTableWidgetItem(when(user.value("first").toDouble()));
                auto *last =
                    new QTableWidgetItem(when(user.value("last").toDouble()));
                first->setData(Qt::UserRole, user.value("first").toDouble());
                last->setData(Qt::UserRole, user.value("last").toDouble());
                self->table_->setItem(i, 2, first);
                self->table_->setItem(i, 3, last);
            }
            self->table_->setSortingEnabled(true);
            self->status_->setText(
                uiText("Users: %1").arg(QString::number(users.size())));
        })
        .onError([self](const NetworkResult &result) {
            if (self)
            {
                self->status_->setText(uiText("Could not load the list (%1)")
                                           .arg(result.formatError()));
            }
        })
        .execute();
}

}  // namespace chatterino
