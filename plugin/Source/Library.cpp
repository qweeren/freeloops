// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Library.h"

namespace
{
    constexpr auto audioWildcard = "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg";
    constexpr size_t maxSamples = 200000;
    constexpr int numPeaks = 96;

    juce::PropertiesFile::Options settingsOptions()
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "freeloops";
        o.filenameSuffix = ".settings";
        o.folderName = "freeloops";
        o.osxLibrarySubFolder = "Application Support";
        o.storageFormat = juce::PropertiesFile::storeAsXML;
        return o;
    }

    bool shouldExit()
    {
        auto* job = juce::ThreadPoolJob::getCurrentThreadPoolJob();
        return job != nullptr && job->shouldExit();
    }
}

Library::Library() : settings (settingsOptions())
{
    formats.registerBasicFormats();
    loadSettings();
    rescan();
}

Library::~Library()
{
    pool.removeAllJobs (true, 4000);
}

//==============================================================================
void Library::loadSettings()
{
    settings.reload();
    folders = juce::StringArray::fromLines (settings.getValue ("folders"));
    folders.removeEmptyStrings();

    favorites.clear();
    for (const auto& f : juce::StringArray::fromLines (settings.getValue ("favorites")))
        if (f.isNotEmpty())
            favorites.insert (f);
}

void Library::saveSettings()
{
    juce::StringArray favs;
    for (const auto& f : favorites)
        favs.add (f);

    settings.setValue ("folders", folders.joinIntoString ("\n"));
    settings.setValue ("favorites", favs.joinIntoString ("\n"));
    settings.saveIfNeeded();
}

void Library::addFolder (const juce::File& folder)
{
    if (! folder.isDirectory() || folders.contains (folder.getFullPathName()))
        return;

    folders.add (folder.getFullPathName());
    saveSettings();
    rescan();
}

void Library::removeFolder (const juce::String& path)
{
    folders.removeString (path);
    saveSettings();
    rescan();
}

void Library::setFavorite (const juce::String& path, bool favorite)
{
    if (favorite)
        favorites.insert (path);
    else
        favorites.erase (path);

    saveSettings();
}

//==============================================================================
void Library::rescan()
{
    const auto generation = ++scanGeneration;
    scanning = true;
    sendChangeMessage();

    juce::WeakReference<Library> weak (this);
    pool.addJob ([weak, generation, roots = folders]
    {
        std::vector<SampleInfo> found;

        for (const auto& rootPath : roots)
        {
            const juce::File root (rootPath);
            const auto base = root.getParentDirectory();

            for (const auto& entry : juce::RangedDirectoryIterator (root, true, audioWildcard, juce::File::findFiles))
            {
                if (shouldExit())
                    return;

                if (entry.isHidden() || found.size() >= maxSamples)
                    continue;

                const auto& f = entry.getFile();
                found.push_back ({ f.getFullPathName(),
                                   f.getFileNameWithoutExtension(),
                                   f.getParentDirectory().getRelativePathFrom (base),
                                   entry.getFileSize() });
            }
        }

        std::sort (found.begin(), found.end(), [] (const auto& a, const auto& b) { return a.name.compareNatural (b.name) < 0; });

        juce::MessageManager::callAsync ([weak, generation, found = std::move (found)]() mutable
        {
            if (auto* lib = weak.get(); lib != nullptr && lib->scanGeneration == generation)
                lib->setSamples (std::move (found));
        });
    });
}

void Library::setSamples (std::vector<SampleInfo> newSamples)
{
    samples = std::move (newSamples);
    index.clear();
    for (size_t i = 0; i < samples.size(); ++i)
        index[samples[i].path] = i;

    scanning = false;
    sendChangeMessage();
}

//==============================================================================
void Library::getPeaks (const juce::String& path, std::function<void (juce::var)> callback)
{
    if (auto it = peakCache.find (path); it != peakCache.end())
    {
        callback (it->second);
        return;
    }

    juce::WeakReference<Library> weak (this);
    pool.addJob ([this, weak, path, callback = std::move (callback)]
    {
        juce::var result;

        if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (juce::File (path)) })
        {
            const auto length = reader->lengthInSamples;
            const auto channels = juce::jlimit (1, 2, (int) reader->numChannels);
            juce::Array<juce::var> peaks;

            for (int i = 0; i < numPeaks && length > 0; ++i)
            {
                if (shouldExit())
                    return;

                const auto start = length * i / numPeaks;
                const auto end = length * (i + 1) / numPeaks;
                juce::Range<float> ranges[2];
                reader->readMaxLevels (start, juce::jmax ((juce::int64) 1, end - start), ranges, channels);

                float peak = 0.0f;
                for (int c = 0; c < channels; ++c)
                    peak = juce::jmax (peak, std::abs (ranges[c].getStart()), std::abs (ranges[c].getEnd()));

                peaks.add (std::round (juce::jmin (1.0f, peak) * 100.0f) / 100.0f);
            }

            auto* obj = new juce::DynamicObject();
            obj->setProperty ("duration", reader->sampleRate > 0 ? (double) length / reader->sampleRate : 0.0);
            obj->setProperty ("peaks", peaks);
            result = juce::var (obj);
        }

        juce::MessageManager::callAsync ([weak, path, result, callback]
        {
            if (auto* lib = weak.get())
            {
                lib->peakCache[path] = result;
                callback (result);
            }
        });
    });
}

//==============================================================================
juce::var Library::toVar() const
{
    juce::Array<juce::var> folderList;
    for (const auto& f : folders)
        folderList.add (f);

    juce::Array<juce::var> sampleList;
    sampleList.ensureStorageAllocated ((int) samples.size());

    for (const auto& s : samples)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", s.path);
        o->setProperty ("name", s.name);
        o->setProperty ("folder", s.folder);
        o->setProperty ("size", s.size);
        if (isFavorite (s.path))
            o->setProperty ("fav", true);
        sampleList.add (juce::var (o));
    }

    auto* obj = new juce::DynamicObject();
    obj->setProperty ("folders", folderList);
    obj->setProperty ("scanning", scanning);
    obj->setProperty ("samples", sampleList);
    return juce::var (obj);
}
