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
}

FreeloopsEditor::~FreeloopsEditor()
{
    owner.library.removeChangeListener (this);
}

void FreeloopsEditor::resized()
{
    browser.setBounds (getLocalBounds());
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
            if (ok)
                owner.startPreview (path);
            done (ok);
        })
        .withNativeFunction ("stopPreview", [this] (Args, NativeFunctionCompletion done)
        {
            owner.stopPreview();
            done (true);
        })
        .withNativeFunction ("getPeaks", [this] (Args args, NativeFunctionCompletion done)
        {
            const auto path = args[0].toString();
            if (! owner.library.contains (path))
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
    if (! owner.library.contains (path) || ! juce::File (path).existsAsFile())
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
