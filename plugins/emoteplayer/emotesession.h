// SPDX-License-Identifier: GPL-3.0-or-later
// Written for Mikage (https://github.com/Mikage-Next/krkrsdl3); see MIKAGE-NOTICE.md.
#pragma once

namespace emoteplayer {
// Call before the TJS VM and renderer are destroyed. Safe to repeat and safe
// when Emote was never loaded during a session.
void ResetEmotePlayerSession();
}
