#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "PluginProcessor.h"

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

    FreeloopsProcessor& owner;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::WebBrowserComponent browser; // after owner: its options capture this editor

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FreeloopsEditor)
};
