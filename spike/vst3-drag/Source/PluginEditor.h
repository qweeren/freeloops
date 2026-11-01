// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

#include <functional>

// One draggable loop. Dragging starts an OS-level file drag via performExternalDragDropOfFiles.
class LoopRow : public juce::Component
{
public:
    LoopRow (juce::File fileToDrag, std::function<void (const juce::String&)> logger);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::File file;
    std::function<void (const juce::String&)> log;
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopRow)
};

class FreeloopsEditor : public juce::AudioProcessorEditor,
                        private juce::Timer
{
public:
    explicit FreeloopsEditor (FreeloopsProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void addLoop (const juce::File&);
    void layoutRows();
    void log (const juce::String&);

    FreeloopsProcessor& processor;

    juce::Label title, tempo;
    juce::TextButton addButton { "Add file..." }, folderButton { "Open folder" };
    juce::Viewport viewport;
    juce::Component rowHolder;
    juce::OwnedArray<LoopRow> rows;
    juce::TextEditor logView;

    juce::File samplesDir;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FreeloopsEditor)
};
