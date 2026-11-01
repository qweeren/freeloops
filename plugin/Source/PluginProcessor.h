// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Freesound.h"
#include "Library.h"
#include "Looperman.h"
#include "PreviewPlayer.h"

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

    // Message thread: decodes the file in the background, then starts it. Later requests win.
    // id is what the UI calls the sample (a library path, "lm:<loop>" for Looperman, "fs:<sound>" for Freesound).
    void startPreview (const juce::String& path, const juce::String& id);
    void stopPreview();

    // Written on the audio thread, polled by the editor. 0 means the host reported no tempo.
    std::atomic<double> hostBpm { 0.0 };
    std::atomic<bool> hostPlaying { false };

    PreviewPlayer preview;
    LoopermanClient looperman;
    FreesoundClient freesound;
    Library library; // last member: its thread pool is drained before anything above is destroyed

private:
    int previewRequest = 0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (FreeloopsProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FreeloopsProcessor)
};
