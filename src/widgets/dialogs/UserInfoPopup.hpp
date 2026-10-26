// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "providers/twitch/TwitchNameHistory.hpp"
#include "singletons/Paths.hpp"
#include "widgets/BaseWindow.hpp"
#include "widgets/DraggablePopup.hpp"

#include <boost/signals2/connection.hpp>
#include <pajlada/signals/scoped-connection.hpp>
#include <pajlada/signals/signal.hpp>
#include <pajlada/signals/signalholder.hpp>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QMovie>
#include <QPixmap>
#include <QPointer>
#include <QString>

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

class QCheckBox;
class QKeyEvent;
class QLabel;
class QMenu;
class QMovie;
class QResizeEvent;
class QScrollArea;
class QSplitter;
class QTabWidget;
class QToolButton;
class QWidget;

namespace chatterino {

class Channel;
class Button;
using ChannelPtr = std::shared_ptr<Channel>;
struct Message;
struct Emote;
using MessagePtr = std::shared_ptr<const Message>;
enum class MessagePlatform : uint8_t;
class Label;
class MarkdownLabel;
class EditUserNotesDialog;
class ChannelView;
class Split;
struct HelixUser;
struct IvrSubage;
struct IvrUserProfile;
class LabelButton;
class LiveIndicator;
class Paint;
class PixmapButton;
class SvgButton;
class ArchiveClient;

/// Synthetic, already-resolved data used by the standalone offline UI preview.
struct UserInfoPreviewData {
    QString login;
    QString userId;
    QString displayName;
    QString createdText;
    QString followersText;
    QString pronouns;
    ChannelPtr messages;
    /// Optional unfiltered buffer for exercising the real identity filter.
    ChannelPtr sourceMessages;
    QPixmap avatar;
    QPixmap banner;
    std::vector<std::shared_ptr<const Emote>> earnedBadges;
    bool allowPublicHistory = false;
};

class UserInfoPopup final : public DraggablePopup
{
    Q_OBJECT

public:
    /**
     * @param closeAutomatically Decides whether the popup should close when it loses focus
     * @param split Will be used as the popup's parent. Must not be null
     */
    UserInfoPopup(bool closeAutomatically, Split *split,
                  bool offlinePreview = false);
    ~UserInfoPopup() override;

    void setData(const QString &name, const ChannelPtr &channel);
    void setData(const QString &name, const ChannelPtr &contextChannel,
                 const ChannelPtr &openingChannel,
                 const QString &expectedUserId = {});

    void setYouTubeContext();

    /// Installs a read-only local Archive view. Ownership transfers to this card.
    void setArchivePanel(QWidget *panel);
    void setOfflinePreviewData(const UserInfoPreviewData &data);
    /// Offline preview only: show the moderation strip as for a moderator.
    void previewModerationStrip();

Q_SIGNALS:
    /// Emitted immediately on a new target and again after its Twitch UID resolves.
    /// An empty UID must never be interpreted as proof of a login's identity.
    void archiveIdentityChanged(const QString &userId, const QString &login,
                                const QString &channelName);
    void archiveIdentityUnavailable();

public:
    static void openUserChannelAction(const QString &userName,
                                      MessagePlatform platform,
                                      const QString &channelName,
                                      const QString &channelId);

    static void notifyFollowMutation(const QString &targetId,
                                     const QString &requestUserId,
                                     const QString &requestLogin,
                                     bool unfollow);

protected:
    void themeChangedEvent() override;
    void scaleChangedEvent(float scale) override;
    void windowDeactivationEvent() override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void registerMnemonicButton(LabelButton *button, int key,
                                std::function<void()> action);

    void installEvents();
    void updateUserData();
    void updateLatestMessages();
    void updateUsercardMessagesVisibility();
    void resetUsercardMessageLoader();
    void updateLoadMoreMessagesButton();
    bool canLoadMoreUsercardMessages() const;
    void maybeStartUsercardMessageAutoLoad();
    void requestMoreUsercardMessages(bool enableLazyLoadOnSuccess);
    void maybeLoadMoreUsercardMessagesFromScroll();
    void fetchMoreUsercardMessages(int emptyPageSkipsLeft,
                                   bool enableLazyLoadOnSuccess);
    void updateNotes();
    void refreshSevenTVUserButtonVisibility();
    void refreshSeventvPaint();
    /// Show / hide the moderation strip; the card grows / shrinks by it.
    void setModerationStripVisible(bool visible);
    void updateSeventvPaintPixmap();
    void resetNameHistory();
    bool applyCachedNameHistory();
    void updateNameHistoryButton();
    void updateBadgesButton();
    void openBadgesDialog();
    void showNameHistoryMenu();
    void openNameHistoryMenu(const QString &statusText = {});
    void requestNameHistory();
    void updateUsercardStatusIcons();
    void setUsercardFollowage(const std::optional<QDateTime> &followedAt);
    void updateUsercardSubGiftRow(const IvrSubage &subageInfo);
    void hideUsercardSubGiftRow();
    void resetUsercardInfoRows();
    void applyIvrUserProfile(const IvrUserProfile &profile);
    void updateLiveIndicatorDisplay();
    void updateResponsiveLayout();
    /// Header line above messages/Archive: "Сообщения в чате #channel",
    /// page switch and history (one line, like Chatterino+).
    void updateMessagesHeader();
    void setMessagesHeading(const QString &channel);
    void showActionsMenu();
    void loadProfileBanner(const QString &url);
    void updateProfileBanner();
    void updateObservedBadges();
    void requestEarnedBadges();
    void updateNamePresentation();
    void showProfileDetails(QMenu *menu);
    bool canUseArchiveHistory() const;
    void resetArchiveHistory();
    void requestArchiveHistory(bool userInitiated,
                               bool localContinuation = false);
    void applyArchiveHistory(const QJsonObject &data);
    bool archiveConfigured() const;
    void rebuildArchiveHistoryView(bool preserveAnchor);
    void updateArchiveHistoryButton();
    bool hasCachedOlderHistory() const;

    bool isFollowing() const;
    bool isFollowingStatusKnown() const;
    void setFollowingStatus(bool following,
                            std::optional<QDateTime> followedAt = std::nullopt);
    void refreshFollowingStatus(bool force = false);
    void resetFollowingStatus();
    void updateUsercardFollowButton();
    void toggleUsercardFollow();

    void loadAvatar(const QString &userID, const QString &pictureURL,
                    bool isKick);
    void loadCurrentAvatar();

    void loadSevenTVAvatar(const QString &userID, bool isKick,
                           bool allowAvatarDownload = true);
    bool setSevenTVAvatar(const QString &filename, const QByteArray &format);

    bool saveCacheAvatar(const QByteArray &avatar,
                         const QString &filename) const;

    void updateAvatarUrl();

    void updateKickUserData();
    void updateYouTubeUserData();
    void updateYouTubeLatestMessages();
    void loadYouTubeAvatar(const QString &url);
    void onKickProfilePictureClick(Qt::MouseButton button);
    bool canShowRoleManagementMenu() const;
    void showRoleManagementMenu();
    void runRoleManagementCommand(const QString &command,
                                  const QString &actionText);

    /// Shows the profile picture context menu (avatar right-click menu).
    /// Returns an error message if the menu could not be shown, empty on success.
    QString showProfilePictureContextMenu();

    QStringView platformName() const;

    void appendCommonProfileActions(QMenu *menu);
    void refreshTargetModerationStatus();
    bool updateTargetModerationStatusFromMessage(const MessagePtr &message);
    bool shouldShowModerationActions() const;
    bool crossActionAvailable() const;
    void runCrossAction(const QString &command);

    enum class UsercardModerationAction { Ban, Unban, Timeout };

    struct UsercardModerationRequest {
        UsercardModerationAction action{};
        int durationSeconds = -1;
        QString reason;
        bool promptForReason = false;
    };

    void executeUsercardModerationAction(
        const UsercardModerationRequest &request);
    void showUsercardModerationReasonPopup(
        const UsercardModerationRequest &request);

    bool isMod_{};
    bool isBroadcaster_{};

    Split *split_;

    QString userName_;
    QString userId_;
    QString archiveLoginHint_;
    uint64_t userDataRequestGeneration_ = 0;
    QString avatarUrl_;
    QString helixAvatarUrl_;
    QString seventvAvatarUrl_;
    QString seventvUserID_;
    int seventvUserRequestGeneration_ = 0;
    bool seventvUserLookupInFlight_ = false;
    bool seventvPaintAsked_ = false;
    bool seventvUserLookupFinished_ = false;
    QString nameHistoryLogin_;
    std::vector<TwitchNameHistoryEntry> nameHistoryEntries_;
    QPointer<QMenu> nameHistoryMenu_;
    uint64_t nameHistoryRequestGeneration_ = 0;
    bool nameHistoryLoading_ = false;
    bool nameHistoryLoaded_ = false;
    ChannelPtr usercardMessagesChannel_;
    QString usercardMessagesCursor_;
    QString usercardMessagesError_;
    uint64_t usercardMessagesRequestGeneration_ = 0;
    bool usercardMessagesLoading_ = false;
    bool usercardMessagesHasNextPage_ = true;
    bool usercardMessagesLazyLoadEnabled_ = false;
    ArchiveClient *archiveHistoryClient_ = nullptr;
    QJsonObject archiveLocalCursor_;
    QJsonObject archivePublicCursor_;
    std::vector<MessagePtr> archiveHistoryMessages_;
    QHash<QString, QJsonObject> archiveHistoryRowMetadata_;
    QString archiveHistoryStatus_;
    bool archiveHistoryStarted_ = false;
    bool archiveHistoryLoading_ = false;
    bool archiveHistoryPublic_ = false;
    bool archiveLocalComplete_ = false;
    bool archivePublicComplete_ = false;
    bool archiveHistoryPreserveAnchor_ = false;
    bool archiveHistorySyntheticEndpoint_ = false;
    bool offlinePublicHistoryAllowed_ = false;
    int archiveLocalDrainRemaining_ = 0;
    bool archiveHistoryWindowLimited_ = false;
    std::chrono::steady_clock::time_point archiveLocalDrainDeadline_;

    QString kickUserSlug_;
    QPointer<QWidget> moderationReasonPopup_;

    // The channel the popup was opened from (e.g. /mentions or #forsen). Can be a special channel.
    ChannelPtr channel_;

    // The channel the messages are rendered from (e.g. #forsen). Can be a special channel, but will try to not be where possible.
    ChannelPtr underlyingChannel_;

    pajlada::Signals::NoArgSignal userStateChanged_;
    pajlada::Signals::NoArgSignal followingStatusChanged_;

    bool following_ = false;
    bool followingStatusKnown_ = false;
    std::optional<QDateTime> followedAt_;
    QString followingStatusUserId_;
    QDateTime lastFollowingStatusRefreshAt_;
    std::atomic<bool> followingStatusFetchInFlight_{false};
    std::unique_ptr<pajlada::Signals::ScopedConnection>
        followingStatusChangedConnection_;

    std::unique_ptr<pajlada::Signals::ScopedConnection> refreshConnection_;
    std::unique_ptr<pajlada::Signals::ScopedConnection>
        twitchUserStateConnection_;
    std::unique_ptr<pajlada::Signals::ScopedConnection>
        userDataUpdatedConnection_;
    std::unique_ptr<pajlada::Signals::ScopedConnection>
        usercardScrollConnection_;
    boost::signals2::scoped_connection currentUserChangedConnection_;

    // If we should close the dialog automatically if the user clicks out
    // Set based on the "Automatically close usercard when it loses focus" setting
    // Pinned status is tracked in DraggablePopup::isPinned_.
    const bool closeAutomatically_;
    const bool offlinePreview_;

    pajlada::Signals::SignalHolder signalHolder_;

    class TimeoutWidget;
    struct {
        QSplitter *columns = nullptr;
        QWidget *moderationStrip = nullptr;
        QScrollArea *profileScroll = nullptr;
        QWidget *profile = nullptr;
        QWidget *profileDetails = nullptr;
        QWidget *profileHeader = nullptr;
        QWidget *identityHeader = nullptr;
        QWidget *profileFacts = nullptr;
        QLabel *banner = nullptr;
        QWidget *badgeStrip = nullptr;
        QToolButton *actions = nullptr;
        QWidget *actionControls = nullptr;
        QWidget *rightColumn = nullptr;
        LabelButton *archiveToggle = nullptr;
        LabelButton *messagesToggle = nullptr;
        QTabWidget *contentTabs = nullptr;
        QWidget *messagesPage = nullptr;
        QWidget *archivePanel = nullptr;
        QLabel *messagesHeading = nullptr;
        LabelButton *historyButton = nullptr;
        PixmapButton *avatarButton = nullptr;
        PixmapButton *localizedNameCopyButton = nullptr;

        Label *nameLabel = nullptr;
        SvgButton *followButton = nullptr;
        LabelButton *nameHistoryButton = nullptr;
        Label *localizedNameLabel = nullptr;
        Label *pronounsLabel = nullptr;
        Label *followerCountLabel = nullptr;
        Label *createdDateLabel = nullptr;
        Label *lastLiveLabel = nullptr;
        Label *userIDLabel = nullptr;
        QLabel *bannedAvatarLabel = nullptr;
        QWidget *followageRow = nullptr;
        QLabel *followageIcon = nullptr;
        Label *followageLabel = nullptr;
        QWidget *subageRow = nullptr;
        QLabel *subageIcon = nullptr;
        Label *subageLabel = nullptr;
        QWidget *subGiftRow = nullptr;
        QLabel *subGiftIcon = nullptr;
        Label *subGiftLabel = nullptr;
        Label *chatterCountLabel = nullptr;
        QWidget *userColorRow = nullptr;
        QWidget *userColorSwatch = nullptr;
        Label *userColorLabel = nullptr;
        Label *statusLabel = nullptr;

        LiveIndicator *liveIndicator = nullptr;

        QCheckBox *block = nullptr;
        QCheckBox *ignoreHighlights = nullptr;
        MarkdownLabel *notesPreview = nullptr;
        LabelButton *notesAdd = nullptr;

        Label *noMessagesLabel = nullptr;
        ChannelView *latestMessages = nullptr;
        LabelButton *loadMoreMessages = nullptr;

        LabelButton *usercardLabel = nullptr;
        LabelButton *userlogsLabel = nullptr;
        LabelButton *badgesLabel = nullptr;
        LabelButton *sevenTVUserLabel = nullptr;
        QLabel *seventvPaintPixmapLabel = nullptr;
        QWidget *seventvPaintRow = nullptr;
        LabelButton *rolesLabel = nullptr;
        LabelButton *switchAvatars = nullptr;

        TimeoutWidget *timeoutWidget = nullptr;
    } ui_;

    QMovie *seventvAvatar_ = nullptr;
    std::shared_ptr<Paint> seventvPaint_;
    bool isTwitchAvatarShown_ = true;
    bool columnsBalanced_ = false;
    QString headingChannel_;
    QPixmap avatarPixmap_;
    QPixmap bannerPixmap_;
    QString bannerUrl_;
    std::vector<std::shared_ptr<const Emote>> earnedBadges_;
    bool earnedBadgesReady_ = false;
    QString earnedBadgesError_;
    std::vector<std::pair<QString, QPointer<Button>>> actionMenuButtons_;
    QPointer<EditUserNotesDialog> editUserNotesDialog_;

    bool isKick_ = false;
    bool isYouTube_ = false;
    bool forceYouTube_ = false;
    QString youtubeChannelId_;
    uint64_t kickUserID_ = 0;
    bool isUserLive_ = false;
    int liveViewerCount_ = 0;

    std::map<int, std::pair<std::function<void()>, std::function<bool()>>>
        mnemonicActions_;

    class TimeoutWidget : public BaseWidget
    {
    public:
        TimeoutWidget();

        pajlada::Signals::Signal<UsercardModerationRequest> buttonClicked;
        pajlada::Signals::Signal<bool> crossActionClicked;

        void setMinTimeout(int minSecs);
        void setModerationVisible(bool visible);
        void setCrossVisible(bool visible);

    protected:
        void paintEvent(QPaintEvent *event) override;

    private:
        void applyModerationVisibility();

        QWidget *unbanButton_ = nullptr;
        QWidget *banButton_ = nullptr;
        QWidget *timeoutsColumn_ = nullptr;
        QWidget *crossUnbanButton_ = nullptr;
        QWidget *crossBanButton_ = nullptr;
        bool moderationVisible_ = true;
        int minTimeoutSecs_ = 0;
        std::vector<std::pair<QWidget *, int>> timeoutButtons;
    };
};

}  // namespace chatterino
