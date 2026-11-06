#pragma once

// Renders the prediction banner and the betting dialog with synthetic
// predictions, for looking at them (no network, no Twitch).

#include "OwnTimeoutChecks.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "widgets/dialogs/PredictionDialog.hpp"
#include "widgets/splits/Split.hpp"

#include <QDateTime>

namespace predictiondemo {

inline chatterino::TwitchChannel::PredictionEvent samplePrediction(
    int outcomes, const QString &status, bool ownBet)
{
    using chatterino::TwitchChannel;
    TwitchChannel::PredictionEvent p;
    p.id = QStringLiteral("demo-%1-%2").arg(outcomes).arg(status);
    p.title = outcomes == 2 ? QStringLiteral("Пройдёт босса с первой попытки?")
                            : QStringLiteral("Какое место займёт в турнире?");
    p.status = status;
    p.predictionWindowSeconds = 300;
    p.createdAt = QDateTime::currentDateTimeUtc().addSecs(-75);
    p.createdByName = QStringLiteral("archive_room");
    const QStringList titles2{QStringLiteral("Да"), QStringLiteral("Нет")};
    const QStringList titlesN{QStringLiteral("Первое"), QStringLiteral("Топ-3"),
                              QStringLiteral("Топ-10"),
                              QStringLiteral("Ниже десятого")};
    const qlonglong points[] = {184'250, 61'700, 12'400, 3'050};
    const int users[] = {231, 97, 40, 12};
    const char *names[] = {"big_better", "lucky_one", "gambler", "small"};
    for (int i = 0; i < outcomes; ++i)
    {
        TwitchChannel::PredictionOutcome o;
        o.id = QStringLiteral("o%1").arg(i);
        o.title = outcomes == 2 ? titles2[i] : titlesN[i];
        o.color = outcomes == 2 && i == 1 ? QStringLiteral("PINK")
                                          : QStringLiteral("BLUE");
        o.totalPoints = points[i];
        o.totalUsers = users[i];
        o.topPoints = points[i] / 6;
        o.topPredictorName = QString::fromLatin1(names[i]);
        p.outcomes.push_back(o);
    }
    if (status == QStringLiteral("LOCKED") ||
        status == QStringLiteral("RESOLVED"))
    {
        p.lockedAt = p.createdAt.addSecs(300);
        p.lockedByName = QStringLiteral("archive_room");
    }
    if (status == QStringLiteral("RESOLVED"))
    {
        p.winningOutcomeId = QStringLiteral("o1");
        p.endedByName = QStringLiteral("archive_room");
    }
    if (ownBet)
    {
        p.selfOutcomeId = QStringLiteral("o0");
        p.selfPoints = 2500;
    }
    return p;
}

inline int runPredictionDemo(chatterino::Split &split,
                             const QString &screenshotBase)
{
    using namespace chatterino;
    using owntimeout::pump;
    auto channel =
        std::make_shared<TwitchChannel>(QStringLiteral("archive_room"));
    split.setChannel(
        IndirectChannel(std::static_pointer_cast<Channel>(channel)));
    split.resize(520, 420);
    split.show();
    pump(200);

    struct Case {
        const char *name;
        int outcomes;
        const char *status;
        bool ownBet;
    };
    const Case cases[] = {
        {"active2", 2, "ACTIVE", false},
        {"active2-bet", 2, "ACTIVE", true},
        {"active4", 4, "ACTIVE", false},
        {"locked", 2, "LOCKED", true},
        {"resolved", 2, "RESOLVED", true},
        {"resolved-won", 2, "RESOLVED-WON", true},
        {"resolved4", 4, "RESOLVED", true},
    };
    for (const auto &c : cases)
    {
        auto status = QString::fromLatin1(c.status);
        const bool wonCase = status == QStringLiteral("RESOLVED-WON");
        if (wonCase)
        {
            status = QStringLiteral("RESOLVED");
        }
        auto prediction = samplePrediction(c.outcomes, status, c.ownBet);
        if (wonCase)
        {
            prediction.winningOutcomeId = QStringLiteral("o0");
        }
        channel->setActivePrediction(prediction);
        pump(300);
        split.grab().save(
            screenshotBase + "-banner-" + QString::fromLatin1(c.name) + ".png",
            "PNG");

        auto *dialog = new PredictionDialog(channel.get(), &split);
        dialog->setPrediction(prediction);
        dialog->show();
        pump(400);
        dialog->grab().save(
            screenshotBase + "-dialog-" + QString::fromLatin1(c.name) + ".png",
            "PNG");
        std::fprintf(stderr, "prediction demo %s: dialog %dx%d\n", c.name,
                     dialog->width(), dialog->height());
        dialog->close();
        delete dialog;
        pump(50);
    }
    // the own result as Twitch sends it (predictions-user-v1)
    int failed = 0;
    auto check = [&](const char *name, bool ok) {
        failed += ok ? 0 : 1;
        std::fprintf(stderr, "prediction check: %s %s\n", ok ? "ok" : "FAIL",
                     name);
    };
    auto lastText = [&] {
        const auto snapshot = channel->getMessageSnapshot();
        return snapshot.size() > 0
                   ? snapshot[snapshot.size() - 1]->messageText
                   : QString();
    };
    auto userEvent = [&](const QString &type, const QString &eventId,
                         const QJsonObject &result) {
        QJsonObject bet{{"event_id", eventId},
                        {"outcome_id", "o0"},
                        {"channel_id", "1"},
                        {"points", 2500}};
        if (!result.isEmpty())
        {
            bet.insert("result", result);
        }
        channel->handlePredictionUserEvent(QJsonObject{
            {"type", type},
            {"data", QJsonObject{{"prediction", bet}}}});
    };

    auto open = samplePrediction(2, QStringLiteral("LOCKED"), false);
    channel->setActivePrediction(open);
    userEvent(QStringLiteral("prediction-made"), open.id, {});
    check("a bet made elsewhere shows as the own bet",
          channel->accessPrediction()->has_value() &&
              (*channel->accessPrediction())->selfPoints == 2500 &&
              (*channel->accessPrediction())->selfOutcomeId == "o0");
    userEvent(QStringLiteral("prediction-result"), open.id,
              QJsonObject{{"type", "WIN"}, {"points_won", 3337}});
    const auto won = lastText();
    std::fprintf(stderr, "won message: %s\n", qPrintable(won));
    check("a won bet says how much came back",
          won.contains(QStringLiteral("3,337")) ||
              won.contains(QStringLiteral("3 337")) ||
              won.contains(QStringLiteral("3337")));
    userEvent(QStringLiteral("prediction-result"), open.id,
              QJsonObject{{"type", "WIN"}, {"points_won", 3337}});
    check("the result is said once", lastText() == won &&
                                         channel->getMessageSnapshot().size() >= 1);

    auto second = samplePrediction(2, QStringLiteral("LOCKED"), false);
    second.id = QStringLiteral("second");
    channel->setActivePrediction(second);
    userEvent(QStringLiteral("prediction-result"), second.id,
              QJsonObject{{"type", "LOSE"}, {"points_won", QJsonValue()}});
    const auto lost = lastText();
    std::fprintf(stderr, "lost message: %s\n", qPrintable(lost));
    check("a lost bet says so",
          lost != won && (lost.contains(QStringLiteral("2,500")) ||
                          lost.contains(QStringLiteral("2 500"))));

    // computed from the pool when Twitch's own result has not come
    auto pool = samplePrediction(2, QStringLiteral("RESOLVED"), true);
    pool.winningOutcomeId = QStringLiteral("o0");
    const auto payout = TwitchChannel::predictionPayout(pool);
    check("payout from the pool share",
          payout && *payout == qlonglong(2500.0 * 245950 / 184250));
    pool.winningOutcomeId = QStringLiteral("o1");
    check("a losing side pays nothing",
          TwitchChannel::predictionPayout(pool) == std::optional<qlonglong>(0));
    check("no bet, no payout",
          !TwitchChannel::predictionPayout(
               samplePrediction(2, QStringLiteral("RESOLVED"), false))
               .has_value());

    channel->setActivePrediction(std::nullopt);
    pump(100);
    return failed;
}

}  // namespace predictiondemo
