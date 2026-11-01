// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr double maxPreviewSeconds = 60.0;
    constexpr int decodeChunk = 1 << 14;             // samples decoded per step; playback starts after the first
    constexpr size_t clipCacheBytes = 96 * 1024 * 1024;
    constexpr size_t clipCacheEntries = 48;

    size_t bytesOf (const PreviewPlayer::Clip& clip)
    {
        return (size_t) clip.audio.getNumChannels() * (size_t) clip.audio.getNumSamples() * sizeof (float);
    }

    juce::int64 modifiedTime (const juce::String& path)
    {
        return juce::File (path).getLastModificationTime().toMilliseconds();
    }
}

FreeloopsProcessor::FreeloopsProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    freesound.attachSettings (library.getSettings());
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

    if (auto clip = findCachedClip (path))
    {
        preview.play (id, std::move (clip));
        return;
    }

    juce::WeakReference<FreeloopsProcessor> weak (this);
    previewPool.addJob ([this, weak, path, id, request]
    {
        std::unique_ptr<juce::AudioFormatReader> reader { library.getFormats().createReaderFor (juce::File (path)) };
        if (reader == nullptr || reader->sampleRate <= 0)
            return;

        const auto modified = modifiedTime (path);
        const auto length = juce::jmax (1, (int) juce::jmin (reader->lengthInSamples, (juce::int64) (reader->sampleRate * maxPreviewSeconds)));
        const auto channels = juce::jlimit (1, 2, (int) reader->numChannels);

        auto clip = std::make_shared<PreviewPlayer::Clip>();
        clip->audio.setSize (channels, length);
        clip->sampleRate = reader->sampleRate;
        float* const dest[] = { clip->audio.getWritePointer (0), clip->audio.getWritePointer (channels - 1) };

        // Decode a step at a time and start playing after the first, so a long loop doesn't keep the user waiting.
        for (int pos = 0; pos < length; pos += decodeChunk)
        {
            const auto* job = juce::ThreadPoolJob::getCurrentThreadPoolJob();
            if (previewRequest.load() != request || (job != nullptr && job->shouldExit()))
                return; // the user moved on: this clip is no longer playing

            const auto n = juce::jmin (decodeChunk, length - pos);
            float* const chunk[] = { dest[0] + pos, dest[1] + pos };
            reader->read (chunk, channels, pos, n);
            clip->ready.store (pos + n, std::memory_order_release);

            if (pos == 0)
            {
                juce::MessageManager::callAsync ([weak, id, request, clip]
                {
                    if (auto* p = weak.get(); p != nullptr && p->previewRequest == request)
                        p->preview.play (id, clip);
                });
            }
        }

        juce::MessageManager::callAsync ([weak, path, modified, clip]
        {
            if (auto* p = weak.get())
                p->cacheClip (path, modified, clip);
        });
    });
}

void FreeloopsProcessor::stopPreview()
{
    ++previewRequest; // drop any load still in flight
    preview.stop();
}

std::shared_ptr<PreviewPlayer::Clip> FreeloopsProcessor::findCachedClip (const juce::String& path)
{
    const auto it = std::find_if (clipCache.begin(), clipCache.end(), [&] (const auto& c) { return c.path == path; });
    if (it == clipCache.end())
        return nullptr;

    if (it->modified != modifiedTime (path)) // the file changed since: decode it again
    {
        clipCache.erase (it);
        return nullptr;
    }

    std::rotate (clipCache.begin(), it, it + 1);
    return clipCache.front().clip;
}

void FreeloopsProcessor::cacheClip (const juce::String& path, juce::int64 modified, std::shared_ptr<PreviewPlayer::Clip> clip)
{
    if (! clip->isComplete() || bytesOf (*clip) > clipCacheBytes / 4)
        return;

    clipCache.erase (std::remove_if (clipCache.begin(), clipCache.end(), [&] (const auto& c) { return c.path == path; }), clipCache.end());
    clipCache.insert (clipCache.begin(), { path, modified, std::move (clip) });

    // Evict the oldest. A clip still playing stays alive in the player and is freed later, on this thread.
    size_t total = 0;
    for (size_t i = 0; i < clipCache.size(); ++i)
    {
        total += bytesOf (*clipCache[i].clip);
        if (total > clipCacheBytes || i >= clipCacheEntries)
        {
            clipCache.resize (i);
            break;
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
