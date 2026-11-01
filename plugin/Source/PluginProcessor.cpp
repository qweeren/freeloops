// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr double maxPreviewSeconds = 60.0;
}

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

    if (auto* head = getPlayHead())
    {
        if (auto position = head->getPosition())
        {
            if (auto bpm = position->getBpm())
                hostBpm.store (*bpm);

            hostPlaying.store (position->getIsPlaying());
        }
    }

    preview.render (buffer);
}

void FreeloopsProcessor::startPreview (const juce::String& path, const juce::String& id)
{
    const auto request = ++previewRequest;
    juce::WeakReference<FreeloopsProcessor> weak (this);

    library.getPool().addJob ([this, weak, path, id, request]
    {
        std::unique_ptr<juce::AudioFormatReader> reader { library.getFormats().createReaderFor (juce::File (path)) };
        if (reader == nullptr || reader->sampleRate <= 0)
            return;

        const auto length = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (reader->sampleRate * maxPreviewSeconds));
        auto clip = std::make_shared<PreviewPlayer::Clip>();
        clip->audio.setSize (juce::jlimit (1, 2, (int) reader->numChannels), juce::jmax (1, length));
        clip->audio.clear();
        reader->read (&clip->audio, 0, length, 0, true, true);
        clip->sampleRate = reader->sampleRate;

        juce::MessageManager::callAsync ([weak, id, request, clip]
        {
            if (auto* p = weak.get(); p != nullptr && p->previewRequest == request)
                p->preview.play (id, clip);
        });
    });
}

void FreeloopsProcessor::stopPreview()
{
    ++previewRequest; // drop any load still in flight
    preview.stop();
}

juce::AudioProcessorEditor* FreeloopsProcessor::createEditor()
{
    return new FreeloopsEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FreeloopsProcessor();
}
