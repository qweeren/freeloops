#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <memory>

// Plays one decoded clip through the plugin's output. play()/stop() run on the message thread and
// hand the clip to the audio thread through a try-locked slot, so render() never blocks or frees memory.
class PreviewPlayer
{
public:
    struct Clip
    {
        juce::AudioBuffer<float> audio;
        double sampleRate = 44100.0;
    };

    // Message thread.
    void play (const juce::String& id, std::shared_ptr<Clip>);
    void stop();
    void releaseRetired();
    const juce::String& getCurrentId() const { return currentId; }
    bool isPlaying() const { return playing.load(); }
    double getProgress() const { return progress.load(); }

    // Audio thread.
    void prepare (double hostSampleRate) { hostRate = hostSampleRate; }
    void render (juce::AudioBuffer<float>& output);

private:
    juce::SpinLock lock;
    std::shared_ptr<Clip> next, retired; // guarded by lock
    bool hasNext = false;                // guarded by lock

    std::shared_ptr<Clip> current;       // audio thread only
    double position = 0.0;
    bool finished = true;
    double hostRate = 44100.0;

    std::atomic<bool> playing { false };
    std::atomic<double> progress { 0.0 };
    juce::String currentId;
};
