#pragma once

#include <IrcMessage>
#include <pajlada/signals/signal.hpp>
#include <QObject>

namespace chatterino {

class TwitchChannel;
class TwitchIrcServer;

class TwitchReadConnectionPool : public QObject
{
public:
    TwitchReadConnectionPool(TwitchIrcServer *parent);

    virtual void onChannelCreated(TwitchChannel &channel) = 0;
    virtual void reconnect() = 0;
    virtual void close() = 0;
    /// Leave and join one channel again (after a ban of the account is lifted).
    virtual void rejoin(const QString &channelName) = 0;

    pajlada::Signals::Signal<Communi::IrcMessage *> messageReceived;
    pajlada::Signals::Signal<Communi::IrcPrivateMessage *>
        privateMessageReceived;
};

/// True while this channel's connection trouble is recent (the status line
/// is being updated in place); the "joined channel" line is skipped then.
bool connectionTroubleRecent(const QString &channelName);

class TwitchReadConnectionPoolSingle;
class TwitchReadConnectionPoolMulti;

TwitchReadConnectionPool *createTwitchConnectionPool(TwitchIrcServer *parent,
                                                     bool multi);

}  // namespace chatterino
