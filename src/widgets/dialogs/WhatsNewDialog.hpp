// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>

class QWidget;

namespace chatterino {

/// After an update: a short list of what the new version brings, read from
/// "Что нового.txt" next to the program (the installer puts it there). Shown
/// once per version; not on a fresh install (that gets "Getting started").
void showWhatsNewIfUpdated(QWidget *parent);

/// The list of changes made after `sinceVersion` (empty: the current
/// version's only).
void showWhatsNewDialog(QWidget *parent, const QString &sinceVersion);

}  // namespace chatterino
