#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <deque>

class QTimer;

namespace chatterino {

/// Finds 7TV personal emote sets that the EventAPI did not announce.
///
/// 7TV only sends a personal set when the owner's extension reports presence
/// in the channel, so after a restart (or for a chatter without the
/// extension running) the set is unknown and their emotes stay plain text.
/// Like the 7TV extension, the sets seen are remembered on disk; a chatter
/// with nothing known is looked up once per session, a few per second.
class SeventvPersonalLookup final : public QObject
{
public:
    static SeventvPersonalLookup &instance();

    /// A live chat message from this user: load their personal set if it is
    /// not known yet. GUI thread.
    void noteChatter(const QString &userID, const QString &login);

    /// The EventAPI assigned a personal set: remember it for next time.
    /// Any thread.
    void remember(const QString &userID, const QString &emoteSetID);

    /// For tests: where the remembered sets are kept (read right away).
    void setStorePath(const QString &path);
    void save();
    QString rememberedSet(const QString &userID) const;
    int queued() const;

private:
    SeventvPersonalLookup();

    struct Known {
        QString emoteSetID;  // empty: the user has no personal set
        qint64 at = 0;
    };

    void load();
    void scheduleSave();
    void lookUpNext();
    void store(const QString &userID, const QString &emoteSetID);
    void loadSet(const QString &userID, const QString &login,
                 const QString &emoteSetID);

    QString path_;
    bool loaded_ = false;
    QHash<QString, Known> known_;
    /// asked in this session (a lookup or a set load is on its way or done)
    QSet<QString> asked_;
    std::deque<std::pair<QString, QString>> queue_;  // user id, login
    QTimer *pace_;
    QTimer *saveTimer_;
};

}  // namespace chatterino
