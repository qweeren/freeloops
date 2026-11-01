// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>

// Writes a stereo 140 bpm kick loop into dir (once) using JUCE's own WAV writer, and returns it.
juce::File writeStereoTestLoop (const juce::File& dir);
