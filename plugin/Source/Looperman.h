// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <functional>
#include <map>

struct LoopermanLoop
{
    juce::String id;          // Looperman's loop number, e.g. "435464"
    juce::String title, author, genre, category, key;
    juce::String detailUrl, previewUrl;
    int bpm = 0;
    double duration = 0.0;
};

// Public Looperman search pages. Requests run one at a time on their own thread, at most one every
// requestGapMs, and identify freeloops in the User-Agent. Looperman refuses direct requests for its MP3
// previews (HTTP 403), so loops are previewed from a download instead. Downloads need the user's own
// signed-in session and are done by LoopermanView. Message thread only, except where noted.
class LoopermanClient
{
public:
    LoopermanClient();
    ~LoopermanClient();

    // Callback (message thread): { loops: [...], page, hasMore } or { error }.
    void search (const juce::String& query, int page, std::function<void (juce::var)> callback);

    const LoopermanLoop* find (const juce::String& id) const;

    // Loops the user kept (dragged or downloaded): Music\freeloops\Looperman. FL projects point at these.
    static juce::File downloadsFolder();
    // Loops only auditioned: a size-capped cache, moved into downloadsFolder() when kept.
    static juce::File auditionCache();
    static void trimAuditionCache();

    juce::File findDownload (const juce::String& id) const { return findIn (downloadsFolder(), id); }
    juce::File findCached (const juce::String& id) const { return findIn (auditionCache(), id); }
    juce::File pathFor (const juce::String& id, bool keep) const; // where a new download goes

    static juce::var toVar (const LoopermanLoop&, const juce::File& downloaded);

    // Background thread: parses one search results page. Public for testing.
    static std::vector<LoopermanLoop> parseResults (const juce::String& html, bool& hasMore, int page);

private:
    static juce::File findIn (const juce::File& folder, const juce::String& id);
    juce::String fetchText (const juce::String& url, juce::String& error);
    void throttle();

    std::map<juce::String, LoopermanLoop> known; // every loop we've shown, so ids from the page are checked
    std::map<juce::String, juce::var> searchCache;
    double lastRequestMs = 0.0;                  // request thread only

    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withThreadName ("freeloops-looperman").withNumberOfThreads (1) };

    JUCE_DECLARE_WEAK_REFERENCEABLE (LoopermanClient)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopermanClient)
};
