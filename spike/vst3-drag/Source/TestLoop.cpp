#include "TestLoop.h"

#include <juce_audio_formats/juce_audio_formats.h>

juce::File writeStereoTestLoop (const juce::File& dir)
{
    auto file = dir.getChildFile ("freeloops - Kick Loop (stereo) - 140bpm.wav");
    if (file.existsAsFile())
        return file;

    constexpr double sampleRate = 44100.0;
    constexpr double bpm = 140.0;
    constexpr int beats = 8; // 2 bars
    const auto beatSamples = (int) std::round (60.0 / bpm * sampleRate);

    juce::AudioBuffer<float> buffer (2, beatSamples * beats);
    buffer.clear();

    // Pitch-swept sine with exponential decay on every beat.
    for (int b = 0; b < beats; ++b)
    {
        double phase = 0.0;
        for (int i = 0; i < beatSamples; ++i)
        {
            const auto t = i / sampleRate;
            phase += juce::MathConstants<double>::twoPi * (50.0 + 100.0 * std::exp (-t * 30.0)) / sampleRate;
            const auto s = (float) (0.9 * std::sin (phase) * std::exp (-t * 8.0));
            buffer.setSample (0, b * beatSamples + i, s);
            buffer.setSample (1, b * beatSamples + i, s);
        }
    }

    dir.createDirectory();
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (stream == nullptr)
        return {};

    // On success the writer takes ownership of the stream.
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate)
                                                                              .withNumChannels (2)
                                                                              .withBitsPerSample (16));
    if (writer == nullptr)
        return {};

    writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    return file;
}
