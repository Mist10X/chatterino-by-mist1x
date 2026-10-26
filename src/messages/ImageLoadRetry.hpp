// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace chatterino::detail {

/// GUI-owned state for bounded retries of a single network image.
class ImageLoadRetry
{
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    static constexpr unsigned maxAttempts = 3;
    static constexpr auto cooldown = std::chrono::seconds(5);
    static constexpr auto demandLifetime = std::chrono::seconds(60);

    void request()
    {
        if (!this->inFlight_ && !this->pending_)
        {
            this->generation_.reset();
            this->attempts_ = 0;
            this->nextAttempt_ = {};
            this->pending_ = true;
        }
    }

    bool tryStart(uint64_t generation, TimePoint now)
    {
        if (this->inFlight_ || !this->pending_)
        {
            return false;
        }

        if (this->generation_ != generation)
        {
            this->generation_ = generation;
            this->attempts_ = 0;
            this->nextAttempt_ = {};
        }

        if (this->attempts_ >= maxAttempts || now < this->nextAttempt_)
        {
            return false;
        }

        ++this->attempts_;
        this->pending_ = false;
        this->inFlight_ = true;
        return true;
    }

    void failed(TimePoint now)
    {
        this->inFlight_ = false;
        this->pending_ = true;
        this->nextAttempt_ = now + cooldown;
    }

    void finished()
    {
        this->inFlight_ = false;
        this->pending_ = false;
    }

    bool pending() const
    {
        return this->pending_;
    }

    /// Exhausted images wait for renewed demand to observe a new generation.
    bool canSchedule() const
    {
        return this->pending_ && this->attempts_ < maxAttempts;
    }

    std::chrono::milliseconds delay(TimePoint now) const
    {
        return std::chrono::ceil<std::chrono::milliseconds>(
            this->nextAttempt_ > now ? this->nextAttempt_ - now
                                    : Clock::duration::zero());
    }

    static bool recentlyNeeded(TimePoint now, TimePoint lastUsed)
    {
        return now - lastUsed <= demandLifetime;
    }

private:
    std::optional<uint64_t> generation_;
    unsigned attempts_ = 0;
    bool inFlight_ = false;
    bool pending_ = false;
    TimePoint nextAttempt_{};
};

}  // namespace chatterino::detail
