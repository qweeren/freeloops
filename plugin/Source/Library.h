// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

#include <array>
#include <functional>
#include <map>
#include <set>

struct SampleInfo
{
    juce::String path;   // also the sample's id in the UI
    juce::String name;   // file name without extension
    juce::String folder; // parent folder, relative to the library folder's parent
    juce::int64 size = 0;
};

// The user's sample folders: settings (shared by every plugin instance), background scanning,
// favorites and waveform peaks. Message thread only, except where noted.
class Library : public juce::ChangeBroadcaster
{
public:
    Library();
    ~Library() override;

    const juce::StringArray& getFolders() const { return folders; }
    void addFolder (const juce::File&);
    void removeFolder (const juce::String& path);
    void rescan();
    bool isScanning() const { return scanning; }

    const std::vector<SampleInfo>& getSamples() const { return samples; }
    bool contains (const juce::String& path) const { return index.count (path) > 0; }

    bool isFavorite (const juce::String& path) const { return favorites.count (path) > 0; }
    void setFavorite (const juce::String& path, bool favorite);

    // { duration, peaks[] }, or void if the file can't be read. The callback runs on the message thread.
    // Peaks are kept on disk too (%APPDATA%\freeloops\cache\peaks.bin), so waveforms show at once next time.
    void getPeaks (const juce::String& path, std::function<void (juce::var)> callback);

    // Folders, favorites flags and (unless withSamples is false) every sample, in the shape ui/app.js expects.
    juce::var toVar (bool withSamples = true) const;

    // Changes whenever the sample list does, so the page is only sent the list again when it changed.
    int getSamplesVersion() const { return samplesVersion; }

    // Shared settings file (%APPDATA%\freeloops\freeloops.settings) for other features' options.
    juce::PropertiesFile& getSettings() { return settings; }

    juce::AudioFormatManager& getFormats() { return formats; }
    juce::ThreadPool& getPool() { return pool; }

    static constexpr int numPeaks = 96;

    struct Peaks
    {
        juce::int64 size = 0, modified = 0; // of the file they were read from; a change means read it again
        float duration = 0.0f;
        std::array<juce::uint8, numPeaks> levels {}; // 0 to 100
        bool readable = false;

        juce::var toVar() const;
    };

private:
    void loadSettings();
    void saveSettings();
    void setSamples (std::vector<SampleInfo>);

    juce::File peakCacheFile() const;
    void loadPeakCache();
    void savePeakCache();

    juce::PropertiesFile settings;
    juce::StringArray folders;
    std::set<juce::String> favorites;

    std::vector<SampleInfo> samples;
    std::map<juce::String, size_t> index;
    int samplesVersion = 0;
    bool scanning = false;
    int scanGeneration = 0;

    std::map<juce::String, Peaks> peakCache;
    bool peakCacheLoaded = false;
    int unsavedPeaks = 0;

    juce::AudioFormatManager formats;
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withThreadName ("freeloops")
                                .withNumberOfThreads (juce::jlimit (2, 4, juce::SystemStats::getNumCpus() / 2)) }; // last: drained first

    JUCE_DECLARE_WEAK_REFERENCEABLE (Library)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Library)
};
