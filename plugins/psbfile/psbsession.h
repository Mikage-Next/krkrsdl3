// SPDX-License-Identifier: GPL-3.0-or-later
// Written for Mikage (https://github.com/Mikage-Next/krkrsdl3); see MIKAGE-NOTICE.md.
#pragma once

// Released PSBFile trees are freed a few containers per frame. During exit
// nothing may wait for a later frame: the script engine goes away first.
void TVPSetPSBDeferredRelease(bool enabled);
void TVPFlushPSBDeferredRelease();
