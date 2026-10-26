#pragma once

#include "providers/archive/ArchiveClient.hpp"

class QMenu;
class QWidget;

namespace chatterino {
ArchiveEndpoint configuredArchiveEndpoint();
void openArchiveView(QWidget *owner, const ArchiveEndpoint &endpoint,
                     const QString &view, const QString &userId,
                     const QString &login, const QString &channel,
                     const QString &messageId = {});
void addArchiveActions(QMenu *menu, QWidget *owner, const QString &userId,
                       const QString &login, const QString &channel,
                       const QString &messageId = {});
}  // namespace chatterino
