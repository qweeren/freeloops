// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Freesound.h"
#include "Library.h"
#include "Looperman.h"
#include "PreviewPlayer.h"
#include "SampleFocus.h"

#include <atomic>

class FreeloopsProcessor : public juce::AudioProcessor
{
public:
    FreeloopsProcessor();

    void prepareToPlay (double sampleRate, int) override { preview.prepare (sampleRate); }
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    // Message thread: decodes the file in the background and starts it once the first moments are decoded,
    // or right away if it was auditioned recently. Later requests win.
    // id is what the UI calls the sample (a library path, "lm:<loop>" for Looperman, "fs:<sound>" for Freesound,
    // "sf:<sample>" for Sample Focus).
    void startPreview (const juce::String& path, const juce::String& id);
    void stopPreview();

    // Written on the audio thread, polled by the editor. 0 means the host reported no tempo.
    std::atomic<double> hostBpm { 0.0 };
    std::atomic<bool> hostPlaying { false };

    PreviewPlayer preview;
    LoopermanClient looperman;
    FreesoundClient freesound;
    SampleFocusClient samplefocus;
    Library library; // its thread pool is drained before anything above is destroyed

private:
    // Recently auditioned clips, newest first, so going back to one plays it instantly. Message thread only.
    struct CachedClip
    {
        juce::String path;
        juce::int64 modified = 0;
        std::shared_ptr<PreviewPlayer::Clip> clip;
    };
    std::shared_ptr<PreviewPlayer::Clip> findCachedClip (const juce::String& path);
    void cacheClip (const juce::String& path, juce::int64 modified, std::shared_ptr<PreviewPlayer::Clip>);
    std::vector<CachedClip> clipCache;

    std::atomic<int> previewRequest { 0 }; // also read by the decoder, to stop when the user moves on

    // Its own thread, so an audition never waits behind waveform or scan jobs. Last member: drained first,
    // since its jobs use library's formats.
    juce::ThreadPool previewPool { juce::ThreadPoolOptions{}.withThreadName ("freeloops preview").withNumberOfThreads (1) };

    JUCE_DECLARE_WEAK_REFERENCEABLE (FreeloopsProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FreeloopsProcessor)
};
