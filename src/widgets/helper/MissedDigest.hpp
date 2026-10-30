// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

namespace chatterino {

class Split;

/// "While you were away": when the user comes back to a split after a while
/// (another tab, another window, the program in the background), a card at
/// the top of the chat sums up what they missed - mentions, replies to them,
/// bans, raids, announcements, new chatters. A click on an item scrolls to
/// it (again: to the next one).
void attachMissedDigest(Split *split);

}  // namespace chatterino
