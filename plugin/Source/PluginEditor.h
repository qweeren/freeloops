// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "FreesoundSignIn.h"
#include "LoopermanView.h"
#include "PluginProcessor.h"
#include "SampleFocusView.h"

// Hosts the web UI (../ui) in a WebView2 browser. The page calls the native functions registered in
// makeBrowserOptions() through window.fl (ui/bridge.js); the editor pushes "library", "status" and
// "dragEnded" events back. File drags are started here, natively, never with HTML5 drag events.
class FreeloopsEditor : public juce::AudioProcessorEditor,
                        private juce::ChangeListener,
                        private juce::Timer
{
public:
    explicit FreeloopsEditor (FreeloopsProcessor&);
    ~FreeloopsEditor() override;

    void resized() override;

private:
    using NativeFunctionCompletion = juce::WebBrowserComponent::NativeFunctionCompletion;

    juce::WebBrowserComponent::Options makeBrowserOptions();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void addFolder (NativeFunctionCompletion);
    void startDrag (const juce::String& path, NativeFunctionCompletion);

    // Looperman is opt-in: its session view only exists (and only loads looperman.com) when enabled.
    void setLoopermanEnabled (bool);
    juce::var loopermanState() const;
    void loopermanPreview (const juce::String& id, NativeFunctionCompletion);
    void loopermanDownload (const juce::String& id, NativeFunctionCompletion);

    // Freesound: search and previews need an API key, downloads the user's own account (OAuth2).
    juce::var freesoundState() const;
    void freesoundPreview (const juce::String& id, NativeFunctionCompletion);
    void freesoundSignIn();

    // Sample Focus is opt-in too. Every request goes through its session view; downloads spend the user's credits.
    void setSampleFocusEnabled (bool);
    juce::var sampleFocusState() const;
    void sampleFocusPreview (const juce::String& id, NativeFunctionCompletion);
    void sampleFocusDownload (const juce::String& id, NativeFunctionCompletion);

    FreeloopsProcessor& owner;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::WebBrowserComponent browser; // after owner: its options capture this editor
    std::unique_ptr<LoopermanView> looperman;
    std::unique_ptr<SampleFocusView> samplefocus;
    std::unique_ptr<FreesoundSignIn> freesoundSignInWindow;
    int onlinePreviewRequest = 0; // an online audition still loading plays only if it's the latest
    int sentSamplesVersion = -1;  // the library's sample list as the page last got it

    // The "status" event, sent statusHz times a second while something changes.
    static constexpr int statusHz = 20;
    struct LastStatus
    {
        double bpm = -1.0;
        bool hostPlaying = false;
        juce::String previewId;
        bool previewPlaying = false;
        double previewProgress = -1.0;
    };
    LastStatus lastStatus;
    int quietTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FreeloopsEditor)
};
