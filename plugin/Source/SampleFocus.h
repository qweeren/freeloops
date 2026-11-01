// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>

#include <map>

struct SampleFocusSample
{
    juce::String id;          // Sample Focus's sample number, e.g. "585539"
    juce::String slug;        // its page: https://samplefocus.com/samples/<slug>
    juce::String name, author, key;
    juce::StringArray tags;
    juce::String pageUrl, previewUrl;
    int bpm = 0;
    double duration = 0.0;
    bool isLoop = false;
};

// What freeloops knows about Sample Focus: its public search pages, which carry their results as JSON for
// the page's own player, the samples shown so far, and where their files go. All requests to the site
// (searches, previews and downloads) go through the user's own session page, SampleFocusView, because
// Sample Focus sits behind Cloudflare and serves previews only to its own pages. Message thread only,
// except where noted.
class SampleFocusClient
{
public:
    // The search page for one page of results. filters: { bpm: [[min, max], ...], key: "C#m" }, applied by
    // Sample Focus. It takes one tempo range, so only the first is sent. Any thread.
    static juce::String searchUrl (const juce::String& query, int page, const juce::var& filters);

    // { samples: [...], page, hasMore } for a search already loaded, or void.
    juce::var cachedResults (const juce::String& url, int page) const;
    // Parses a loaded search page, remembers it, and returns the same shape (or { error }).
    juce::var addResults (const juce::String& url, int page, const juce::String& html);

    const SampleFocusSample* find (const juce::String& id) const;

    // Samples the user kept (dragged or downloaded): Music\freeloops\Sample Focus. FL projects point at these.
    static juce::File downloadsFolder();
    // Previews only auditioned: a size-capped cache.
    static juce::File previewCache();
    static void trimPreviewCache();

    juce::File findDownload (const juce::String& id) const { return findIn (downloadsFolder(), id); }
    juce::File findPreview (const juce::String& id) const { return findIn (previewCache(), id); }
    juce::File downloadPathFor (const juce::String& id) const; // without an extension
    juce::File previewPathFor (const juce::String& id) const;

    static juce::var toVar (const SampleFocusSample&, const juce::File& downloaded);

    // Any thread: parses one search results page. Public for testing.
    static std::vector<SampleFocusSample> parseResults (const juce::String& html, bool& hasMore);
    static bool isResultsPage (const juce::String& html);

private:
    static juce::File findIn (const juce::File& folder, const juce::String& id);

    std::map<juce::String, SampleFocusSample> known; // every sample we've shown, so ids from the page are checked
    std::map<juce::String, juce::var> searchCache;
};
