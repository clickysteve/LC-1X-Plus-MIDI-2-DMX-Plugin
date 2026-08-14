#include "QuickLightApp.h"
#include "UserProfileStore.h"

// ============================================================================
// DMX over MIDI
//
// Same encoding the plugin uses: one note per DMX channel, note number =
// channel index (0..127), velocity = the 7-bit DMX value. This is what the
// LC-1X+ expects, and keeping the two implementations identical is the point —
// a rig addressed here lights the same way it does from the plugin.
// ============================================================================
static int dmxToVelocity(int dmx) {
    if (dmx <= 0) return 0;
    return std::min(127, (dmx + 1) / 2);
}

QuickLightEngine::QuickLightEngine() {
    std::fill(std::begin(lastSent_), std::end(lastSent_), -1);
    UserProfileStore::loadOnce();     // user profiles are shared with the plugin
    load();
}

QuickLightEngine::~QuickLightEngine() {
    // Never leave the rig lit by a process that no longer exists.
    blackout();
    save();
}

// ----------------------------------------------------------------------------
// Output device
// ----------------------------------------------------------------------------
juce::StringArray QuickLightEngine::getOutputDeviceNames() const {
    juce::StringArray names;
    for (const auto& d : juce::MidiOutput::getAvailableDevices())
        names.add(d.name);
    return names;
}

void QuickLightEngine::setOutputDevice(const juce::String& name) {
    // Dark the OLD device before dropping it, without touching on_ — the
    // user asked to change interface, not to turn the lights off, and the
    // new device should come up holding the same look.
    if (out_) {
        for (int ch = 0; ch < 128; ++ch)
            out_->sendMessageNow(juce::MidiMessage::noteOff(1, ch));
    }

    out_.reset();
    deviceName_ = name;

    for (const auto& d : juce::MidiOutput::getAvailableDevices()) {
        if (d.name == name) {
            out_ = juce::MidiOutput::openDevice(d.identifier);
            break;
        }
    }

    std::fill(std::begin(lastSent_), std::end(lastSent_), -1);   // force a full send
    resend();
    save();
}

// ----------------------------------------------------------------------------
// Look
// ----------------------------------------------------------------------------
void QuickLightEngine::setColour(RGBColor c) {
    colour_ = c;
    // Picking a colour means "I want this on" — it would be a poor menu that
    // made you choose a colour and then separately switch the lights on.
    on_ = !(c.r == 0 && c.g == 0 && c.b == 0);
    resend();
    save();
}

void QuickLightEngine::setBrightness(float b) {
    brightness_ = juce::jlimit(0.0f, 1.0f, b);
    resend();
    save();
}

void QuickLightEngine::setOn(bool shouldBeOn) {
    on_ = shouldBeOn;
    resend();
    save();
}

void QuickLightEngine::blackout() {
    on_ = false;
    resend();
}

void QuickLightEngine::rigChanged() {
    // A moved, resized or deleted fixture orphans the channels it used to
    // own. Those channels are still lit on the hardware and `lastSent_`
    // still agrees with them, so a plain resend() would skip them. Clear
    // everything first, then re-render.
    for (int ch = 0; ch < 128; ++ch) sendChannel(ch, 0);
    resend();
    save();
}

// ----------------------------------------------------------------------------
// Rendering
// ----------------------------------------------------------------------------
void QuickLightEngine::resend() {
    uint8_t state[128] = {};

    if (on_) {
        const RGBColor lit {
            (uint8_t)std::lround(colour_.r * brightness_),
            (uint8_t)std::lround(colour_.g * brightness_),
            (uint8_t)std::lround(colour_.b * brightness_)
        };

        std::vector<RGBColor>            colours;
        std::vector<std::pair<int, int>> pairs;

        for (const auto& qf : fixtures_) {
            const auto cfg = qf.toConfig();
            colours.assign((size_t)std::max(1, qf.segments), lit);

            // Route through the shared profile mapping so par cans with a dim
            // channel, RGBW fixtures and plain RGB bars all land correctly.
            pairs = cfg.mapColorsToDmx(colours);
            for (const auto& p : pairs) {
                const int ch = qf.dmxStart + p.first;
                if (ch >= 0 && ch < 128)
                    state[ch] = (uint8_t)juce::jlimit(0, 255, p.second);
            }
        }
    }

    for (int ch = 0; ch < 128; ++ch)
        sendChannel(ch, state[ch]);
}

void QuickLightEngine::sendChannel(int channel, int dmxValue) {
    if (!out_ || channel < 0 || channel >= 128) return;

    const int vel = dmxToVelocity(dmxValue);
    if (vel == lastSent_[channel]) return;
    lastSent_[channel] = vel;

    // This is a menu-driven app with no audio thread, so sending directly is
    // fine — there is no realtime deadline to miss.
    out_->sendMessageNow(vel > 0
        ? juce::MidiMessage::noteOn (1, channel, (juce::uint8)vel)
        : juce::MidiMessage::noteOff(1, channel));
}

// ----------------------------------------------------------------------------
// Persistence
// ----------------------------------------------------------------------------
juce::File QuickLightEngine::settingsFile() const {
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("AMFAS")
                   .getChildFile("LC-1X+ MIDI2DMX");
    dir.createDirectory();
    return dir.getChildFile("quicklight.xml");
}

void QuickLightEngine::save() const {
    juce::XmlElement xml("QuickLight");
    xml.setAttribute("device",     deviceName_);
    xml.setAttribute("r",          (int)colour_.r);
    xml.setAttribute("g",          (int)colour_.g);
    xml.setAttribute("b",          (int)colour_.b);
    xml.setAttribute("brightness", (double)brightness_);
    // Deliberately NOT saving `on_`: launching the app should never turn the
    // rig on by itself.

    for (const auto& f : fixtures_) {
        auto* e = xml.createNewChildElement("Fixture");
        e->setAttribute("name",     f.name);
        e->setAttribute("profile",  f.profileIdx);
        e->setAttribute("dmxStart", f.dmxStart);
        e->setAttribute("segments", f.segments);
    }

    xml.writeTo(settingsFile());
}

void QuickLightEngine::load() {
    auto file = settingsFile();
    if (!file.existsAsFile()) return;

    auto xml = juce::XmlDocument::parse(file);
    if (!xml || !xml->hasTagName("QuickLight")) return;

    deviceName_ = xml->getStringAttribute("device");
    colour_     = { (uint8_t)xml->getIntAttribute("r", 255),
                    (uint8_t)xml->getIntAttribute("g", 255),
                    (uint8_t)xml->getIntAttribute("b", 255) };
    brightness_ = (float)xml->getDoubleAttribute("brightness", 1.0);

    std::vector<QuickFixture> loaded;
    for (auto* e : xml->getChildWithTagNameIterator("Fixture")) {
        QuickFixture f;
        f.name       = e->getStringAttribute("name", "Fixture");
        f.profileIdx = e->getIntAttribute("profile", 0);
        f.dmxStart   = e->getIntAttribute("dmxStart", 0);
        f.segments   = juce::jlimit(1, 64, e->getIntAttribute("segments", 8));
        // A settings file written against a longer profile list (a user
        // profile since deleted) must not index out of bounds.
        if (f.profileIdx < 0 || f.profileIdx >= (int)getFixtureProfiles().size())
            f.profileIdx = 0;
        loaded.push_back(f);
    }
    if (!loaded.empty()) fixtures_ = std::move(loaded);

    if (deviceName_.isNotEmpty()) {
        for (const auto& d : juce::MidiOutput::getAvailableDevices()) {
            if (d.name == deviceName_) {
                out_ = juce::MidiOutput::openDevice(d.identifier);
                break;
            }
        }
    }
}
