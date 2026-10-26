// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "messages/ImageLoadRetry.hpp"

#include <chrono>
#include <iostream>

using chatterino::detail::ImageLoadRetry;
using namespace std::chrono_literals;

namespace {

int checks = 0;
int failures = 0;

void check(const char *name, bool passed)
{
    ++checks;
    if (!passed)
    {
        ++failures;
        std::cerr << "FAILED: " << name << '\n';
    }
}

void retryTests()
{
    ImageLoadRetry retry;
    const ImageLoadRetry::TimePoint start{};

    check("undemanded image cannot start", !retry.tryStart(1, start));
    retry.request();
    check("initial demanded load starts", retry.tryStart(1, start));
    check("in-flight load does not schedule", !retry.canSchedule());
    check("paint cannot duplicate in-flight request",
          !retry.tryStart(1, start + 1s));
    check("network transition cannot duplicate in-flight request",
          !retry.tryStart(2, start + 1s));

    retry.failed(start + 2s);
    check("transient failure stays pending", retry.pending());
    check("transient failure permits finite timer", retry.canSchedule());
    check("cooldown is measured from failure", retry.delay(start + 2s) == 5s);
    bool blockedPaints = true;
    for (int n = 0; n < 1000; ++n)
    {
        blockedPaints &= !retry.tryStart(1, start + 2s + n * 1ms);
    }
    check("repeated paint calls cannot bypass cooldown", blockedPaints);
    check("second request starts at cooldown", retry.tryStart(1, start + 7s));
    retry.failed(start + 8s);
    check("third request starts at cooldown", retry.tryStart(1, start + 13s));
    retry.failed(start + 14s);
    check("exhausted image remains eligible for future network transition",
          retry.pending());
    check("exhausted image does not schedule polling timer",
          !retry.canSchedule());
    check("elapsed time cannot refill exhausted generation",
          !retry.tryStart(1, start + 24h));

    check("new generation refills an exhausted image immediately",
          retry.tryStart(2, start + 15s));
    retry.failed(start + 16s);
    check("new generation has its own remaining timer budget",
          retry.canSchedule());
    check("same generation retains cooldown", !retry.tryStart(2, start + 17s));
    check("another network transition releases stale cooldown",
          retry.tryStart(3, start + 17s));
    retry.finished();
    check("successful or terminal completion clears pending", !retry.pending());
    check("completed request never schedules", !retry.canSchedule());
    check("terminal state is not resurrected by network transition",
          !retry.tryStart(4, start + 1h));

    // A deliberate new load (e.g. after successful frame expiration) gets a
    // fresh budget. Paint calls on a failed image do not call request().
    retry.request();
    check("new frame-cache load can start", retry.tryStart(3, start + 20s));
    retry.failed(start + 21s);
    retry.request();
    check("renewed demand does not erase a pending cooldown",
          !retry.tryStart(3, start + 22s));
    check("renewed demand retains its scheduled delay",
          retry.delay(start + 22s) == 4s);
    check("fresh demand permits weak scheduled retry",
          ImageLoadRetry::recentlyNeeded(start + 30s, start));
    check("demand window includes its boundary",
          ImageLoadRetry::recentlyNeeded(start + 60s, start));
    check("dormant image does not retry autonomously",
          !ImageLoadRetry::recentlyNeeded(start + 60s + 1ms, start));
    check("sub-millisecond cooldown is rounded up",
          retry.delay(start + 26s - 1us) == 1ms);
}

}  // namespace

int main()
{
    retryTests();
    std::cout << "ImageLoadRetry: " << checks - failures << '/' << checks
              << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
