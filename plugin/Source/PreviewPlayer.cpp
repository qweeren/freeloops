// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PreviewPlayer.h"

namespace
{
    constexpr float previewGain = 0.8f;
}

void PreviewPlayer::play (const juce::String& id, std::shared_ptr<Clip> clip)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    next = std::move (clip);
    hasNext = true;
    currentId = id;
    playing = true;
    progress = 0.0;
}

void PreviewPlayer::stop()
{
    const juce::SpinLock::ScopedLockType sl (lock);
    next = nullptr;
    hasNext = true;
    playing = false;
}

void PreviewPlayer::releaseRetired()
{
    std::shared_ptr<Clip> old;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        old = std::move (retired);
    }
    // old is freed here, on the message thread
}

void PreviewPlayer::render (juce::AudioBuffer<float>& output)
{
    {
        const juce::SpinLock::ScopedTryLockType sl (lock);

        // Only swap when the retired slot is empty, so the old clip is never freed on this thread.
        if (sl.isLocked() && hasNext && retired == nullptr)
        {
            retired = std::move (current);
            current = std::move (next);
            hasNext = false;
            position = 0.0;
            finished = current == nullptr;
            playing = ! finished;
        }
    }

    if (current == nullptr || finished)
        return;

    const auto& audio = current->audio;
    const int srcLength = audio.getNumSamples();
    const int srcChannels = audio.getNumChannels();
    const double step = current->sampleRate / hostRate;

    for (int i = 0; i < output.getNumSamples(); ++i)
    {
        const auto i0 = (int) position;
        if (i0 + 1 >= srcLength)
        {
            finished = true;
            break;
        }

        const auto frac = (float) (position - i0);
        for (int ch = 0; ch < output.getNumChannels(); ++ch)
        {
            const auto* src = audio.getReadPointer (ch % srcChannels);
            output.addSample (ch, i, previewGain * (src[i0] + frac * (src[i0 + 1] - src[i0])));
        }

        position += step;
    }

    progress = srcLength > 0 ? juce::jmin (1.0, position / srcLength) : 1.0;
    if (finished)
    {
        progress = 1.0;
        playing = false;
    }
}
