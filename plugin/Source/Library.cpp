// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Library.h"

namespace
{
    constexpr auto audioWildcard = "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg";
    constexpr size_t maxSamples = 200000;
    constexpr int unsavedPeaksLimit = 500; // new waveforms read before the disk cache is written again
    constexpr juce::uint32 peakCacheMagic = 0x6b706c66; // "flpk"
    constexpr int peakCacheVersion = 1;

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
    loadPeakCache();
    rescan();
}

Library::~Library()
{
    pool.removeAllJobs (true, 4000);
    if (unsavedPeaks > 0)
        savePeakCache();
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
    ++samplesVersion;
    index.clear();
    for (size_t i = 0; i < samples.size(); ++i)
        index[samples[i].path] = i;

    scanning = false;
    sendChangeMessage();
}

//==============================================================================
void Library::getPeaks (const juce::String& path, std::function<void (juce::var)> callback)
{
    const juce::File file (path);
    const auto size = file.getSize();
    const auto modified = file.getLastModificationTime().toMilliseconds();

    if (auto it = peakCache.find (path); it != peakCache.end() && it->second.size == size && it->second.modified == modified)
    {
        callback (it->second.toVar());
        return;
    }

    juce::WeakReference<Library> weak (this);
    pool.addJob ([this, weak, path, size, modified, callback = std::move (callback)]
    {
        Peaks result;
        result.size = size;
        result.modified = modified;

        if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (juce::File (path)) })
        {
            const auto length = reader->lengthInSamples;
            const auto channels = juce::jlimit (1, 2, (int) reader->numChannels);

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

                result.levels[(size_t) i] = (juce::uint8) juce::roundToInt (juce::jmin (1.0f, peak) * 100.0f);
            }

            result.duration = reader->sampleRate > 0 ? (float) ((double) length / reader->sampleRate) : 0.0f;
            result.readable = true;
        }

        juce::MessageManager::callAsync ([weak, path, result, callback]
        {
            if (auto* lib = weak.get())
            {
                lib->peakCache[path] = result;
                if (result.readable && ++lib->unsavedPeaks >= unsavedPeaksLimit)
                    lib->savePeakCache();
                callback (result.toVar());
            }
        });
    });
}

juce::var Library::Peaks::toVar() const
{
    if (! readable)
        return {};

    juce::Array<juce::var> peaks;
    peaks.ensureStorageAllocated (numPeaks);
    for (auto level : levels)
        peaks.add (level / 100.0);

    auto* obj = new juce::DynamicObject();
    obj->setProperty ("duration", duration);
    obj->setProperty ("peaks", peaks);
    return juce::var (obj);
}

juce::File Library::peakCacheFile() const
{
    return settings.getFile().getSiblingFile ("cache").getChildFile ("peaks.bin");
}

// Read in the background: a big library's cache takes a moment, and waveforms just load as usual until then.
void Library::loadPeakCache()
{
    juce::WeakReference<Library> weak (this);
    pool.addJob ([weak, file = peakCacheFile()]
    {
        auto loaded = std::make_shared<std::map<juce::String, Peaks>>();
        juce::MemoryBlock data;

        if (file.loadFileAsData (data))
        {
            juce::MemoryInputStream in (data, false);
            if ((juce::uint32) in.readInt() == peakCacheMagic && in.readInt() == peakCacheVersion)
            {
                const auto count = in.readInt();
                for (int i = 0; i < count && ! in.isExhausted(); ++i)
                {
                    if (shouldExit())
                        return;

                    const auto path = in.readString();
                    Peaks p;
                    p.size = in.readInt64();
                    p.modified = in.readInt64();
                    p.duration = in.readFloat();
                    if (in.read (p.levels.data(), numPeaks) != numPeaks)
                        break;
                    p.readable = true;
                    loaded->emplace (path, p);
                }
            }
        }

        juce::MessageManager::callAsync ([weak, loaded]
        {
            if (auto* lib = weak.get())
            {
                for (auto& [path, p] : *loaded)
                    lib->peakCache.emplace (path, p); // anything read since is newer
                lib->peakCacheLoaded = true;
            }
        });
    });
}

void Library::savePeakCache()
{
    if (! peakCacheLoaded) // saving now would drop what's on disk
        return;

    unsavedPeaks = 0;

    juce::MemoryOutputStream out;
    out.writeInt ((int) peakCacheMagic);
    out.writeInt (peakCacheVersion);

    int count = 0;
    for (const auto& [path, p] : peakCache)
        count += p.readable ? 1 : 0;
    out.writeInt (count);

    for (const auto& [path, p] : peakCache)
    {
        if (! p.readable)
            continue;

        out.writeString (path);
        out.writeInt64 (p.size);
        out.writeInt64 (p.modified);
        out.writeFloat (p.duration);
        out.write (p.levels.data(), numPeaks);
    }

    // Write a temporary file and swap it in, so another plugin instance never reads half a cache.
    const auto file = peakCacheFile();
    file.getParentDirectory().createDirectory();
    juce::TemporaryFile temp (file);
    if (temp.getFile().replaceWithData (out.getData(), out.getDataSize()))
        temp.overwriteTargetFileWithTemporary();
}

//==============================================================================
juce::var Library::toVar (bool withSamples) const
{
    juce::Array<juce::var> folderList;
    for (const auto& f : folders)
        folderList.add (f);

    auto* obj = new juce::DynamicObject();
    obj->setProperty ("folders", folderList);
    obj->setProperty ("scanning", scanning);
    if (! withSamples)
        return juce::var (obj);

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

    obj->setProperty ("samples", sampleList);
    return juce::var (obj);
}
