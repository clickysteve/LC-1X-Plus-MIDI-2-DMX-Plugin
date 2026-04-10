#include "UserProfileStore.h"
#include "FixtureProfile.h"

namespace UserProfileStore
{
    juce::File getProfilesFile()
    {
        auto dir = juce::File::getSpecialLocation(
                       juce::File::userApplicationDataDirectory)
                       .getChildFile("AMFAS")
                       .getChildFile("LC-1X+ MIDI2DMX");
        if (!dir.exists())
            dir.createDirectory();
        return dir.getChildFile("user_profiles.xml");
    }

    static juce::String layoutToString(const std::vector<char>& layout)
    {
        juce::String s;
        for (char c : layout) s += juce::String::charToString((juce::juce_wchar)c);
        return s;
    }

    static std::vector<char> layoutFromString(const juce::String& s)
    {
        std::vector<char> v;
        for (int i = 0; i < s.length(); ++i) {
            auto c = s[i];
            // Only accept known layout chars; silently drop anything else
            // so a malformed file can't scramble the DMX routing.
            if (c == 'r' || c == 'g' || c == 'b' || c == 'w' || c == 'd')
                v.push_back((char)c);
        }
        return v;
    }

    void loadOnce()
    {
        static bool loaded = false;
        if (loaded) return;
        loaded = true;

        auto f = getProfilesFile();
        if (!f.existsAsFile()) return;

        auto xml = juce::XmlDocument::parse(f);
        if (xml == nullptr || xml->getTagName() != "UserProfiles") return;

        auto& all = getFixtureProfiles();
        for (auto* p : xml->getChildWithTagNameIterator("Profile"))
        {
            FixtureProfile prof;
            prof.name               = p->getStringAttribute("name").toStdString();
            prof.channelLayout      = layoutFromString(p->getStringAttribute("layout"));
            prof.hasMasterDim       = p->getBoolAttribute("hasMasterDim", false);
            prof.channelsPerSegment = p->getIntAttribute("chPerSeg", 3);
            prof.totalDmxChannels   = p->getIntAttribute("totalCh", 0);
            prof.description        = p->getStringAttribute("description").toStdString();
            prof.fixedSegments      = p->getIntAttribute("fixedSegs", 0);
            prof.dimAlwaysMax       = p->getBoolAttribute("dimAlwaysMax", false);
            prof.isBuiltin          = false;

            // Refuse to load anything that would be obviously broken
            if (prof.name.empty() || prof.channelLayout.empty() || prof.channelsPerSegment <= 0)
                continue;

            all.push_back(std::move(prof));
        }
    }

    void save()
    {
        juce::XmlElement root("UserProfiles");

        for (auto& prof : getFixtureProfiles())
        {
            if (prof.isBuiltin) continue;

            auto* p = root.createNewChildElement("Profile");
            p->setAttribute("name",         juce::String(prof.name));
            p->setAttribute("layout",       layoutToString(prof.channelLayout));
            p->setAttribute("hasMasterDim", prof.hasMasterDim);
            p->setAttribute("chPerSeg",     prof.channelsPerSegment);
            p->setAttribute("totalCh",      prof.totalDmxChannels);
            p->setAttribute("description",  juce::String(prof.description));
            p->setAttribute("fixedSegs",    prof.fixedSegments);
            p->setAttribute("dimAlwaysMax", prof.dimAlwaysMax);
        }

        auto f = getProfilesFile();
        f.replaceWithText(root.toString());
    }
}
