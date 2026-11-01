// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PluginEditor.h"
#include "TestLoop.h"

namespace
{
    const juce::Colour background { 0xff15161a }, panel { 0xff1e2026 }, line { 0xff2c2f37 },
                       text { 0xffe8e9ec }, dim { 0xff8b8f99 }, accent { 0xffff8c10 };

    constexpr int rowHeight = 36;
    constexpr int dragThreshold = 4; // px of movement before a press counts as a drag
}

//==============================================================================
LoopRow::LoopRow (juce::File fileToDrag, std::function<void (const juce::String&)> logger)
    : file (std::move (fileToDrag)), log (std::move (logger))
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void LoopRow::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (0, 2).toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (isMouseOver() || dragging ? accent : line);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

    auto area = getLocalBounds().reduced (12, 0);
    g.setColour (dim);
    g.setFont (13.0f);
    g.drawText (juce::String (file.getSize() / 1024) + " KB", area.removeFromRight (70), juce::Justification::centredRight);
    g.setColour (text);
    g.setFont (14.0f);
    g.drawText (file.getFileName(), area, juce::Justification::centredLeft, true);
}

void LoopRow::mouseDown (const juce::MouseEvent&)
{
    dragging = false;
}

void LoopRow::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < dragThreshold)
        return;

    if (! file.existsAsFile())
    {
        log ("missing file: " + file.getFullPathName());
        return;
    }

    dragging = true;
    repaint();
    log ("drag started: " + file.getFileName());

    juce::Component::SafePointer<LoopRow> safeThis (this);
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
        [safeThis]
        {
            if (safeThis == nullptr)
                return;
            safeThis->dragging = false;
            safeThis->log ("drag finished: " + safeThis->file.getFileName());
            safeThis->repaint();
        });
}

void LoopRow::mouseUp (const juce::MouseEvent&)
{
    repaint();
}

//==============================================================================
FreeloopsEditor::FreeloopsEditor (FreeloopsProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    const auto appData = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
    samplesDir = appData.getChildFile ("freeloops").getChildFile ("samples");

    title.setText ("freeloops - VST3 drag spike", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
    title.setColour (juce::Label::textColourId, text);
    addAndMakeVisible (title);

    tempo.setColour (juce::Label::textColourId, dim);
    tempo.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (tempo);

    for (auto* b : { &addButton, &folderButton })
    {
        b->setColour (juce::TextButton::buttonColourId, panel);
        b->setColour (juce::TextButton::textColourOffId, text);
        addAndMakeVisible (b);
    }

    addButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Add audio files", juce::File(), "*.wav;*.mp3;*.ogg;*.flac;*.aif;*.aiff");
        chooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& fc)
                              {
                                  for (const auto& f : fc.getResults())
                                      addLoop (f);
                              });
    };
    folderButton.onClick = [this] { samplesDir.revealToUser(); };

    viewport.setViewedComponent (&rowHolder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    logView.setMultiLine (true);
    logView.setReadOnly (true);
    logView.setCaretVisible (false);
    logView.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain)));
    logView.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0f1013));
    logView.setColour (juce::TextEditor::outlineColourId, line);
    logView.setColour (juce::TextEditor::textColourId, dim);
    addAndMakeVisible (logView);

    if (auto loop = writeStereoTestLoop (samplesDir); loop.existsAsFile())
        addLoop (loop);
    else
        log ("could not write test loop to " + samplesDir.getFullPathName());

    log ("drag a row into FL Studio's Playlist or Channel Rack");

    setResizable (true, true);
    setResizeLimits (380, 320, 1200, 1000);
    setSize (520, 460);
    startTimerHz (4);
}

void FreeloopsEditor::addLoop (const juce::File& f)
{
    auto* row = rows.add (new LoopRow (f, [this] (const juce::String& m) { log (m); }));
    rowHolder.addAndMakeVisible (row);
    layoutRows();
}

void FreeloopsEditor::layoutRows()
{
    const auto w = viewport.getMaximumVisibleWidth();
    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * rowHeight, w, rowHeight);
    rowHolder.setSize (w, rows.size() * rowHeight);
}

void FreeloopsEditor::log (const juce::String& message)
{
    logView.moveCaretToEnd();
    logView.insertTextAtCaret (juce::Time::getCurrentTime().toString (false, true, true, true) + "  " + message + "\n");
}

void FreeloopsEditor::timerCallback()
{
    const auto bpm = processor.hostBpm.load();
    const auto playing = processor.hostPlaying.load();
    tempo.setText (bpm > 0.0 ? "host " + juce::String (bpm, 1) + " bpm" + (playing ? "  (playing)" : "") : "host tempo: n/a",
                   juce::dontSendNotification);
}

void FreeloopsEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
}

void FreeloopsEditor::resized()
{
    auto area = getLocalBounds().reduced (14);

    auto header = area.removeFromTop (28);
    tempo.setBounds (header.removeFromRight (160));
    title.setBounds (header);

    area.removeFromTop (8);
    auto buttons = area.removeFromTop (28);
    addButton.setBounds (buttons.removeFromLeft (100));
    buttons.removeFromLeft (8);
    folderButton.setBounds (buttons.removeFromLeft (100));

    area.removeFromTop (10);
    logView.setBounds (area.removeFromBottom (120));
    area.removeFromBottom (10);
    viewport.setBounds (area);
    layoutRows();
}
