#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

class FreeloopsProcessor : public juce::AudioProcessor
{
public:
    FreeloopsProcessor();

    void prepareToPlay (double, int) override {}
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

    // Written on the audio thread, polled by the editor. 0 means the host reported no tempo.
    std::atomic<double> hostBpm { 0.0 };
    std::atomic<bool> hostPlaying { false };

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FreeloopsProcessor)
};
