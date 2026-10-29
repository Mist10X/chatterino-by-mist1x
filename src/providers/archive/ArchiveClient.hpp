#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>

class QLocalSocket;

namespace chatterino {

struct ArchiveEndpoint {
    QString socketName;
    QString datasetId;
    int timeoutMs = 1800;
};

/// Reads only the explicitly named, nonsecret sidecar. Missing or invalid
/// configuration is disabled; no sockets, profiles, discovery or writes occur.
ArchiveEndpoint readArchiveEndpointFile(const QString &path);
/// Both parts present and well-formed.
bool isValidArchiveEndpoint(const ArchiveEndpoint &endpoint);

// A local, read-only bridge. No HTTP, credentials, discovery, process launch or
// database access belongs here. Each operation verifies the dataset first.
class ArchiveClient final : public QObject
{
    Q_OBJECT
public:
    static constexpr int ProtocolVersion = 2;
    static constexpr qsizetype MaxResponseBytes = 256 * 1024;
    static constexpr qsizetype MaxRequestBytes = 8192;

    explicit ArchiveClient(ArchiveEndpoint endpoint, QObject *parent = nullptr);
    ~ArchiveClient() override;

    bool configured() const;
    void requestCard(const QString &userId, const QString &login,
                     const QString &channel);
    void requestHistory(const QString &userId, const QString &login,
                        const QString &channel,
                        const QJsonObject &before = {}, bool publicLogs = false);
    void openView(const QString &view, const QString &userId,
                  const QString &login, const QString &channel,
                  const QString &messageId = {});
    void cancel();

    static QString userFacingError(const QString &code);

Q_SIGNALS:
    void cardReady(const QJsonObject &data);
    void historyReady(const QJsonObject &data);
    void historyLoading();
    void viewOpened(const QString &view);
    void failed(const QString &code, const QString &message);

private:
    enum class Phase { Idle, Hello, Operation };
    void begin(QJsonObject request);
    void send(QJsonObject request);
    void read(quint64 generation);
    void frame(const QByteArray &line);
    void fail(const QString &code);
    bool validTarget(const QJsonObject &request) const;

    ArchiveEndpoint endpoint_;
    QPointer<QLocalSocket> socket_;
    QTimer deadline_;
    QTimer historyPoll_;
    QByteArray buffer_;
    QJsonObject operation_;
    Phase phase_ = Phase::Idle;
    quint64 generation_ = 0;
    int sequence_ = 0;
    int expectedId_ = 0;
    bool active_ = false;
    static int activeRequests_;
};

}  // namespace chatterino
