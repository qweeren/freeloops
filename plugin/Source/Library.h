#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

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
    void getPeaks (const juce::String& path, std::function<void (juce::var)> callback);

    // Folders, favorites flags and every sample, in the shape ui/app.js expects.
    juce::var toVar() const;

    juce::AudioFormatManager& getFormats() { return formats; }
    juce::ThreadPool& getPool() { return pool; }

private:
    void loadSettings();
    void saveSettings();
    void setSamples (std::vector<SampleInfo>);

    juce::PropertiesFile settings;
    juce::StringArray folders;
    std::set<juce::String> favorites;

    std::vector<SampleInfo> samples;
    std::map<juce::String, size_t> index;
    std::map<juce::String, juce::var> peakCache;
    bool scanning = false;
    int scanGeneration = 0;

    juce::AudioFormatManager formats;
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withThreadName ("freeloops").withNumberOfThreads (2) }; // last: drained first

    JUCE_DECLARE_WEAK_REFERENCEABLE (Library)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Library)
};
