// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QDialog>

class QLabel;
class QTableWidget;

namespace chatterino {

/// Who uses Chatterino by Mist1X, from the users' service (test version).
/// Only for the build's author: the list needs the admin key kept in
/// Settings/mist1x-admin.key on their computer.
class Mist1XUsersDialog : public QDialog
{
public:
    explicit Mist1XUsersDialog(QWidget *parent = nullptr);

    /// Whether this computer has the admin key.
    static bool available();

private:
    void reload();

    QTableWidget *table_;
    QLabel *status_;
};

}  // namespace chatterino
