// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "SampleFocus.h"

#include <deque>

// The user's own Sample Focus session: a WebView2 browser on samplefocus.com with its own cookie store,
// kept 1x1 inside the plugin. Every request to the site is made from this page, the way the site's own
// scripts make them: search pages, previews (its CDN serves them only to its own pages), and downloads with
// the user's signed-in session (each new one costs one of their Sample Focus credits). Requests run one at a
// time, only when the user asks for something, and searches at most one every searchGapMs.
// Signing in happens in a separate popup window that shares the same cookie store; the user types their
// password into Sample Focus's own login page there, and freeloops never sees it. If Cloudflare asks for a
// browser check, the same popup shows it, for the user to answer.
class SampleFocusView : public juce::Component,
                        private juce::Timer
{
public:
    using FileCallback = std::function<void (juce::File, juce::String error)>;
    using SearchCallback = std::function<void (juce::var)>;

    explicit SampleFocusView (SampleFocusClient&);
    ~SampleFocusView() override;

    void showSignIn();
    void showBrowserCheck(); // samplefocus.com in the popup, so the user can pass Cloudflare's check
    void signOut();
    bool isSigningIn() const { return popup != nullptr && ! popupIsCheck; }
    bool isCheckingBrowser() const { return popup != nullptr && popupIsCheck; }
    bool isSignedIn() const { return signedIn; }
    bool needsBrowserCheck() const { return challenged; }
    int getCredits() const { return credits; } // -1 when unknown

    // Callback: { samples: [...], page, hasMore }, { error }, or { superseded } when a newer search replaced
    // it in the queue. An empty query has no results.
    void search (const juce::String& query, int page, const juce::var& filters, SearchCallback);

    // The MP3 preview, into SampleFocusClient::previewCache(). No account needed. A newer preview
    // replaces one still waiting in the queue (its callback gets "superseded").
    void fetchPreview (const juce::String& id, FileCallback);

    // The original file, with the user's account, into SampleFocusClient::downloadsFolder().
    void download (const juce::String& id, FileCallback);

    std::function<void()> onStateChange;

    void resized() override;

private:
    class Browser;
    class Popup;

    enum class Kind { search, preview, download };

    struct Job
    {
        Kind kind;
        juce::String key;  // the sample id, or the search page's URL
        int page = 1;
        std::vector<FileCallback> callbacks;
        std::vector<SearchCallback> replies;
    };

    void pageLoaded (const juce::String& url);
    void openPopup (bool check);
    void popupPageLoaded (const juce::String& url);
    void closePopup();
    void readSession (juce::WebBrowserComponent&, std::function<void (const juce::var&)>);
    void enqueue (Kind, const juce::String& key, int page, FileCallback, SearchCallback);
    void dropQueued (Kind, const juce::String& except);
    void startNextJob();
    void jobFinished (const juce::var&);
    void fetchDirectly (const juce::String& url);
    void save (const juce::MemoryBlock&);
    void finishFront (const juce::File&, const juce::String& error, const juce::var& reply = {});
    void failAll (const juce::String& error);
    void timerCallback() override;
    void notify();

    SampleFocusClient& client;
    std::unique_ptr<Browser> browser;
    std::unique_ptr<Popup> popup;

    bool signedIn = false, challenged = false, pageReady = false, busy = false, waitingForGap = false, popupIsCheck = false;
    int credits = -1;
    int jobNumber = 0;          // tags the running job, so a late reply from a timed-out one is ignored
    double lastSearchMs = -1e9;
    std::deque<Job> queue;

    juce::ThreadPool files { juce::ThreadPoolOptions{}.withThreadName ("freeloops-samplefocus-files").withNumberOfThreads (1) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleFocusView)
};
