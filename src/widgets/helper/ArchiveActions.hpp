#pragma once

#include "providers/archive/ArchiveClient.hpp"

class QMenu;
class QWidget;

namespace chatterino {
ArchiveEndpoint configuredArchiveEndpoint();
/// The Archive is the owner's own program: everywhere else (a copy from the
/// public release) it does not exist, so nothing about it is shown.
bool archiveConnected();
void openArchiveView(QWidget *owner, const ArchiveEndpoint &endpoint,
                     const QString &view, const QString &userId,
                     const QString &login, const QString &channel,
                     const QString &messageId = {});
void addArchiveActions(QMenu *menu, QWidget *owner, const QString &userId,
                       const QString &login, const QString &channel,
                       const QString &messageId = {});
}  // namespace chatterino
