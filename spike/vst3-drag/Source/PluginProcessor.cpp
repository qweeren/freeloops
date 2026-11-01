// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PluginProcessor.h"
#include "PluginEditor.h"

FreeloopsProcessor::FreeloopsProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

bool FreeloopsProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void FreeloopsProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // Phase 4 needs host tempo for auto-stretch previews; this spike only reads and displays it.
    if (auto* head = getPlayHead())
    {
        if (auto position = head->getPosition())
        {
            if (auto bpm = position->getBpm())
                hostBpm.store (*bpm);

            hostPlaying.store (position->getIsPlaying());
        }
    }
}

juce::AudioProcessorEditor* FreeloopsProcessor::createEditor()
{
    return new FreeloopsEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FreeloopsProcessor();
}
