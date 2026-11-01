// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PluginEditor.h"

#include <BinaryData.h>

namespace
{
    using Resource = juce::WebBrowserComponent::Resource;

    const juce::Colour background { 0xff16181b };

    const char* mimeTypeFor (const juce::String& path)
    {
        if (path.endsWithIgnoreCase (".html")) return "text/html";
        if (path.endsWithIgnoreCase (".js"))   return "text/javascript";
        if (path.endsWithIgnoreCase (".css"))  return "text/css";
        if (path.endsWithIgnoreCase (".svg"))  return "image/svg+xml";
        return "application/octet-stream";
    }

    // Serves the files compiled in from ../ui (see juce_add_binary_data in CMakeLists.txt).
    std::optional<Resource> getResource (const juce::String& url)
    {
        auto path = url.upToFirstOccurrenceOf ("?", false, false).trimCharactersAtStart ("/");
        if (path.isEmpty())
            path = "index.html";

        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        {
            if (path != BinaryData::originalFilenames[i])
                continue;

            int size = 0;
            const auto* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
            std::vector<std::byte> bytes ((size_t) size);
            std::memcpy (bytes.data(), data, (size_t) size);
            return Resource { std::move (bytes), mimeTypeFor (path) };
        }

        return std::nullopt;
    }

    juce::File appDataFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("freeloops");
    }

    juce::var errorResult (const juce::String& message)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("error", message);
        return juce::var (o);
    }

    // "lm:435464" -> "435464"
    juce::String loopermanId (const juce::var& id)
    {
        return id.toString().fromFirstOccurrenceOf ("lm:", false, false);
    }
}

//==============================================================================
FreeloopsEditor::FreeloopsEditor (FreeloopsProcessor& p)
    : AudioProcessorEditor (p), owner (p), browser (makeBrowserOptions())
{
    addAndMakeVisible (browser);
    browser.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    owner.library.addChangeListener (this);

    setResizable (true, true);
    setResizeLimits (640, 420, 2400, 1600);
    setSize (1000, 640);
    startTimerHz (15);

    if (owner.library.getSettings().getBoolValue ("looperman.enabled"))
        setLoopermanEnabled (true);
}

FreeloopsEditor::~FreeloopsEditor()
{
    looperman.reset(); // fails any queued downloads while the page can still receive the result
    owner.library.removeChangeListener (this);
}

void FreeloopsEditor::resized()
{
    browser.setBounds (getLocalBounds());

    // The Looperman session page works in the background; signing in happens in its own popup window.
    if (looperman != nullptr)
        looperman->setBounds (0, 0, 1, 1);
}

//==============================================================================
juce::WebBrowserComponent::Options FreeloopsEditor::makeBrowserOptions()
{
    using Options = juce::WebBrowserComponent::Options;
    using Args = const juce::Array<juce::var>&;

    // WebView2's default data folder sits next to the host exe, which a plugin can't write to.
    const auto webViewOptions = Options::WinWebView2{}
                                    .withUserDataFolder (appDataFolder().getChildFile ("webview2"))
                                    .withStatusBarDisabled()
                                    .withBackgroundColour (background);

    return Options{}
        .withBackend (Options::Backend::webview2)
        .withWinWebView2Options (webViewOptions)
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider ([] (const juce::String& url) { return getResource (url); })

        .withNativeFunction ("getState", [this] (Args, NativeFunctionCompletion done)
        {
            done (owner.library.toVar());
        })
        .withNativeFunction ("addFolder", [this] (Args, NativeFunctionCompletion done)
        {
            addFolder (std::move (done));
        })
        .withNativeFunction ("removeFolder", [this] (Args args, NativeFunctionCompletion done)
        {
            owner.library.removeFolder (args[0].toString());
            done (true);
        })
        .withNativeFunction ("rescan", [this] (Args, NativeFunctionCompletion done)
        {
            owner.library.rescan();
            done (true);
        })
        .withNativeFunction ("setFavorite", [this] (Args args, NativeFunctionCompletion done)
        {
            const auto path = args[0].toString();
            if (owner.library.contains (path))
                owner.library.setFavorite (path, (bool) args[1]);
            done (true);
        })
        .withNativeFunction ("preview", [this] (Args args, NativeFunctionCompletion done)
        {
            const auto path = args[0].toString();
            const auto ok = owner.library.contains (path);
            ++loopermanPreviewRequest;
            if (ok)
                owner.startPreview (path, path);
            done (ok);
        })
        .withNativeFunction ("stopPreview", [this] (Args, NativeFunctionCompletion done)
        {
            ++loopermanPreviewRequest;
            owner.stopPreview();
            done (true);
        })
        .withNativeFunction ("getPeaks", [this] (Args args, NativeFunctionCompletion done)
        {
            auto path = args[0].toString();
            if (path.startsWith ("lm:"))
            {
                // A Looperman loop has peaks once it's been auditioned or downloaded.
                auto file = owner.looperman.findDownload (loopermanId (path));
                if (! file.existsAsFile())
                    file = owner.looperman.findCached (loopermanId (path));
                path = file.getFullPathName();
            }
            else if (! owner.library.contains (path))
            {
                path = {};
            }

            if (path.isEmpty())
            {
                done ({});
                return;
            }

            juce::Component::SafePointer<FreeloopsEditor> safe (this);
            owner.library.getPeaks (path, [safe, done] (juce::var result)
            {
                if (safe != nullptr)
                    done (result);
            });
        })
        .withNativeFunction ("startDrag", [this] (Args args, NativeFunctionCompletion done)
        {
            startDrag (args[0].toString(), std::move (done));
        })
        .withNativeFunction ("reveal", [this] (Args args, NativeFunctionCompletion done)
        {
            const auto path = args[0].toString();
            if (owner.library.contains (path))
                juce::File (path).revealToUser();
            done (true);
        })

        // ---- Looperman (opt-in) ----
        .withNativeFunction ("lmState", [this] (Args, NativeFunctionCompletion done)
        {
            done (loopermanState());
        })
        .withNativeFunction ("lmSetEnabled", [this] (Args args, NativeFunctionCompletion done)
        {
            setLoopermanEnabled ((bool) args[0]);
            done (loopermanState());
        })
        .withNativeFunction ("lmSearch", [this] (Args args, NativeFunctionCompletion done)
        {
            if (looperman == nullptr)
            {
                done (errorResult ("Looperman is turned off"));
                return;
            }

            juce::Component::SafePointer<FreeloopsEditor> safe (this);
            owner.looperman.search (args[0].toString(), (int) args[1], [safe, done] (juce::var result)
            {
                if (safe != nullptr)
                    done (result);
            });
        })
        .withNativeFunction ("lmPreview", [this] (Args args, NativeFunctionCompletion done)
        {
            loopermanPreview (args[0].toString(), std::move (done));
        })
        .withNativeFunction ("lmDownload", [this] (Args args, NativeFunctionCompletion done)
        {
            loopermanDownload (args[0].toString(), std::move (done));
        })
        .withNativeFunction ("lmSignIn", [this] (Args, NativeFunctionCompletion done)
        {
            if (looperman != nullptr)
                looperman->showSignIn();
            done (true);
        })
        .withNativeFunction ("lmSignOut", [this] (Args, NativeFunctionCompletion done)
        {
            if (looperman != nullptr)
                looperman->signOut();
            done (true);
        })
        .withNativeFunction ("lmOpen", [this] (Args args, NativeFunctionCompletion done)
        {
            if (const auto* loop = owner.looperman.find (loopermanId (args[0])))
                juce::URL (loop->detailUrl).launchInDefaultBrowser();
            done (true);
        });
}

//==============================================================================
void FreeloopsEditor::setLoopermanEnabled (bool enabled)
{
    auto& settings = owner.library.getSettings();
    settings.setValue ("looperman.enabled", enabled);
    settings.saveIfNeeded();

    if (enabled && looperman == nullptr)
    {
        looperman = std::make_unique<LoopermanView> (owner.looperman);
        looperman->onStateChange = [this] { browser.emitEventIfBrowserIsVisible ("lmState", loopermanState()); };
        addAndMakeVisible (*looperman);
    }
    else if (! enabled)
    {
        looperman.reset();
    }

    resized();
}

juce::var FreeloopsEditor::loopermanState() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("enabled", looperman != nullptr);
    o->setProperty ("signedIn", looperman != nullptr && looperman->isSignedIn());
    o->setProperty ("signingIn", looperman != nullptr && looperman->isSigningIn());
    o->setProperty ("folder", LoopermanClient::downloadsFolder().getFullPathName());
    return juce::var (o);
}

void FreeloopsEditor::loopermanPreview (const juce::String& id, NativeFunctionCompletion done)
{
    if (looperman == nullptr)
    {
        done (errorResult ("Looperman is turned off"));
        return;
    }

    // Looperman refuses direct requests for its MP3 previews, so a loop is auditioned from a download
    // made with the user's own session (kept in the audition cache unless they drag it).
    const auto request = ++loopermanPreviewRequest;
    juce::Component::SafePointer<FreeloopsEditor> safe (this);
    looperman->download (loopermanId (id), false, [safe, id, request, done] (juce::File file, juce::String error)
    {
        if (safe == nullptr)
            return;

        if (! file.existsAsFile())
        {
            done (errorResult (error));
            return;
        }

        if (request == safe->loopermanPreviewRequest) // the user hasn't moved on to another sample
            safe->owner.startPreview (file.getFullPathName(), id);

        auto* o = new juce::DynamicObject();
        o->setProperty ("ok", true);
        done (juce::var (o));
    });
}

void FreeloopsEditor::loopermanDownload (const juce::String& id, NativeFunctionCompletion done)
{
    if (looperman == nullptr)
    {
        done (errorResult ("Looperman is turned off"));
        return;
    }

    juce::Component::SafePointer<FreeloopsEditor> safe (this);
    looperman->download (loopermanId (id), true, [safe, done] (juce::File file, juce::String error)
    {
        if (safe == nullptr)
            return;

        if (! file.existsAsFile())
        {
            done (errorResult (error));
            return;
        }

        auto* o = new juce::DynamicObject();
        o->setProperty ("path", file.getFullPathName());
        done (juce::var (o));
    });
}

//==============================================================================
void FreeloopsEditor::addFolder (NativeFunctionCompletion done)
{
    chooser = std::make_unique<juce::FileChooser> ("Add a sample folder", juce::File(), juce::String(), true, false, this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this, done] (const juce::FileChooser& fc)
                          {
                              const auto folder = fc.getResult();
                              if (folder.isDirectory())
                                  owner.library.addFolder (folder);
                              done (folder.isDirectory());
                          });
}

void FreeloopsEditor::startDrag (const juce::String& path, NativeFunctionCompletion done)
{
    // Only files freeloops knows about: library samples and loops it downloaded from Looperman.
    const juce::File file (path);
    const auto known = owner.library.contains (path) || file.isAChildOf (LoopermanClient::downloadsFolder());
    if (! known || ! file.existsAsFile())
    {
        done (false);
        return;
    }

    // Per the JUCE forum, a WebView-based editor works as the drag source when the browser
    // component itself is passed. On Windows JUCE runs DoDragDrop on its own thread.
    juce::Component::SafePointer<FreeloopsEditor> safe (this);
    const auto started = juce::DragAndDropContainer::performExternalDragDropOfFiles ({ path }, false, &browser, [safe]
    {
        if (safe != nullptr)
            safe->browser.emitEventIfBrowserIsVisible ("dragEnded", juce::var());
    });

    done (started);
}

//==============================================================================
void FreeloopsEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    browser.emitEventIfBrowserIsVisible ("library", owner.library.toVar());
}

void FreeloopsEditor::timerCallback()
{
    owner.preview.releaseRetired();

    auto* status = new juce::DynamicObject();
    status->setProperty ("bpm", owner.hostBpm.load());
    status->setProperty ("hostPlaying", owner.hostPlaying.load());
    status->setProperty ("previewId", owner.preview.getCurrentId());
    status->setProperty ("previewPlaying", owner.preview.isPlaying());
    status->setProperty ("previewProgress", owner.preview.getProgress());
    browser.emitEventIfBrowserIsVisible ("status", juce::var (status));
}
