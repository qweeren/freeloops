// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "Looperman.h"

#include <deque>

// The user's own Looperman session: a WebView2 browser on looperman.com with its own cookie store,
// kept 1x1 inside the plugin. It downloads loops the way the user's browser would: one at a time,
// with their session, only when they ask for a loop.
// Signing in happens in a separate popup window that shares the same cookie store; the user types
// their password into Looperman's own login page there, and freeloops never sees it.
class LoopermanView : public juce::Component,
                      private juce::Timer
{
public:
    using DownloadCallback = std::function<void (juce::File, juce::String error)>;

    explicit LoopermanView (LoopermanClient&);
    ~LoopermanView() override;

    void showSignIn();
    void signOut();
    bool isSigningIn() const { return signInWindow != nullptr; }
    bool isSignedIn() const { return signedIn; }

    // Gets the loop's file. keep = the user is dragging or saving it (LoopermanClient::downloadsFolder());
    // otherwise it's an audition and goes to the capped cache. Callback runs on the message thread.
    void download (const juce::String& id, bool keep, DownloadCallback);

    std::function<void()> onStateChange;

    void resized() override;

private:
    class Browser;
    class SignInWindow;

    void pageLoaded (const juce::String& url);
    void signInPageLoaded (const juce::String& url);
    void finishSignIn();
    void startNextDownload();
    void downloadFinished (const juce::var&);
    void finishFront (const juce::File&, const juce::String& error);
    void timerCallback() override;
    void notify();

    LoopermanClient& client;
    std::unique_ptr<Browser> browser;
    std::unique_ptr<SignInWindow> signInWindow;

    bool signedIn = false, pageReady = false, downloading = false;

    struct Pending
    {
        juce::String id;
        bool keep = false;
        DownloadCallback callback;
    };
    std::deque<Pending> queue;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopermanView)
};
