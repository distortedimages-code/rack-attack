#include "RaSysex.h"

#include <cctype>
#include <cstdio>
#include <sstream>

namespace ra
{
//==============================================================================
std::uint8_t checksum (const std::uint8_t* data, std::size_t size)
{
    unsigned sum = 0;
    for (std::size_t i = 0; i < size; ++i)
        sum += data[i];
    return (std::uint8_t) (sum & 0x7F);
}

Bytes build (std::uint8_t device, const Bytes& payload)
{
    Bytes m { kSox, kWaldorf, kRackAttack, (std::uint8_t) (device & 0x7F) };
    for (auto b : payload)
        m.push_back ((std::uint8_t) (b & 0x7F));
    m.push_back (checksum (payload));
    m.push_back (kEox);
    return m;
}

static std::uint8_t b7 (int v)   { return (std::uint8_t) (v & 0x7F); }

Bytes globalRequest (std::uint8_t device)
{
    return build (device, { cmd::GLBR, 0x00 });
}

Bytes programRequest (std::uint8_t device, std::uint8_t program, std::uint8_t buffer)
{
    return build (device, { cmd::PRGR, b7 (program), b7 (buffer) });
}

Bytes soundRequest (std::uint8_t device, int sound, std::uint8_t buffer)
{
    return build (device, { cmd::SNDR, b7 (buffer), b7 (sound) });
}

Bytes effectRequest (std::uint8_t device, int effect, std::uint8_t buffer)
{
    return build (device, { cmd::EFXR, b7 (buffer), b7 (effect) });
}

Bytes soundParameter (std::uint8_t device, int sound, int address, int value, std::uint8_t buffer)
{
    return build (device, { cmd::SNDP, b7 (buffer), b7 (sound), b7 (address >> 7), b7 (address), b7 (value) });
}

//==============================================================================
Parsed parse (const Bytes& m)
{
    Parsed p;
    p.isSysex = ! m.empty() && m.front() == kSox;
    p.complete = p.isSysex && m.size() >= 2 && m.back() == kEox;

    if (! p.complete || m.size() < 7 || m[1] != kWaldorf || m[2] != kRackAttack)
        return p;

    p.isRackAttack = true;
    p.device = m[3];
    p.command = m[4];
    p.payload.assign (m.begin() + 4, m.end() - 2);   // CMD .. before checksum
    p.sentChecksum = m[m.size() - 2];
    p.expectedChecksum = checksum (p.payload);

    if (p.sentChecksum == p.expectedChecksum)  p.check = Parsed::Check::ok;
    else if (p.sentChecksum == 0x7F)           p.check = Parsed::Check::joker;
    else                                       p.check = Parsed::Check::bad;
    return p;
}

Object objectOf (std::uint8_t c)
{
    if (c >= 0x40) return Object::unknown;
    switch (c & 0x0F)
    {
        case 0x00: return Object::sound;
        case 0x01: return Object::program;
        case 0x03: return Object::pattern;
        case 0x04: return Object::global;
        case 0x05: return Object::effect;
        case 0x07: return Object::mode;
        default:   return Object::unknown;
    }
}

Action actionOf (std::uint8_t c)
{
    switch (c >> 4)
    {
        case 0:  return Action::request;
        case 1:  return Action::dump;
        case 2:  return Action::parameter;
        case 3:  return Action::inquiry;
        default: return Action::unknown;
    }
}

const char* commandName (std::uint8_t c)
{
    static const std::vector<std::string> names = []
    {
        const char* objects[8] = { "SND", "PRG", nullptr, "PAT", "GLB", "EFX", nullptr, "MOD" };
        const char actions[4] = { 'R', 'D', 'P', 'Q' };
        std::vector<std::string> t (64);
        for (int i = 0; i < 64; ++i)
            if ((i & 0x0F) < 8 && objects[i & 0x0F] != nullptr)
                t[(size_t) i] = std::string (objects[i & 0x0F]) + actions[i >> 4];
        return t;
    }();

    if (c >= 0x40 || names[c].empty())
        return nullptr;
    return names[c].c_str();
}

int locationSize (std::uint8_t c)
{
    switch (objectOf (c))
    {
        case Object::sound:
        case Object::effect:   return 2;                                     // BUFN, SNDN / EFXN
        case Object::program:  return actionOf (c) == Action::parameter ? 1 : 2;
        case Object::global:   return actionOf (c) == Action::parameter ? 0 : 1;
        case Object::pattern:
        case Object::mode:
        case Object::unknown:  return -1;
    }
    return -1;
}

int dumpDataSize (std::uint8_t c)
{
    switch (c)
    {
        case cmd::SNDD: return kSoundDataSize;
        case cmd::PRGD: return kProgramDataSize;
        case cmd::EFXD: return kEffectDataSize;
        case cmd::GLBD: return kGlobalDataSize;
        default:        return -1;
    }
}

//==============================================================================
// Parameter tables (docs/SYSEX.md)

namespace
{
    struct Param
    {
        int address;
        const char* name;
        const char* const* values;   // nullptr = plain number
        int numValues;
    };

    #define RA_VALUES(name, ...) const char* const name[] = { __VA_ARGS__ }; \
                                 constexpr int name##Count = (int) (sizeof (name) / sizeof (name[0]));

    RA_VALUES (kShapes, "Triangle", "Sine", "Square", "Saw", "S&H", "Noise", "HH Closed", "HH Open", "Crash", "Ext L", "Ext R", "Ext Mono")
    RA_VALUES (kStart, "Peak+", "Peak-", "Zero", "Free")
    RA_VALUES (kModSrc, "Env1", "Env2", "LFO1", "LFO2")
    RA_VALUES (kCtrlSrc, "Velocity", "Modwheel", "Pressure", "Breath", "Foot", "Keytrack")
    RA_VALUES (kFilterTypes, "Bypass", "Low", "Band", "High", "Notch", "Shelf", "Bell", "S&H")
    RA_VALUES (kLfoShapes, "Sine", "Triangle", "Square", "Saw", "Random", "S&H")
    RA_VALUES (kLfoSync, "Off", "Key", "8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64",
               "1/2.", "1/4.", "1/8.", "1/16.", "1/32.", "1/64.", "1/2t", "1/4t", "1/8t", "1/16t", "1/32t", "1/64t")
    RA_VALUES (kOffOn, "Off", "On")
    RA_VALUES (kDryOut, "Mono 1", "Stereo 1/2", "Mono 2", "Mono 3", "Stereo 3/4", "Mono 4", "Mono 5", "Stereo 5/6", "Mono 6")
    RA_VALUES (kFxSelect, "FX1", "FX2", "FX3", "FX4")
    RA_VALUES (kTunedChannel, "Off", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16", "Global")
    RA_VALUES (kXor, "Off", "1", "2", "3")
    RA_VALUES (kPatternMode, "Off", "On", "Latch", "Toggle", "One Shot")
    RA_VALUES (kMidiMode, "Native", "Plug-in", "GlobalChn")
    RA_VALUES (kSignature, "4/4", "3/4", "5/4")
    RA_VALUES (kFxTypes, "Bypass", "Chorus", "Flanger", "Phaser", "Overdrive", "Delay", "ModDelay", "Reverb", "Lo-Fi")
    RA_VALUES (kFxOut, "Out1", "Out1+2", "Out2", "Out3", "Out3+4", "Out4", "Out5", "Out5+6", "Out6")
    RA_VALUES (kGlobalChannel, "Omni", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16")
    RA_VALUES (kCtlSend, "Off", "SysEx")
    RA_VALUES (kCtlReceive, "Off", "On", "MP-Bundle")

    #undef RA_VALUES
    #define V(list) list, list##Count
    #define N nullptr, 0

    const Param kSoundParams[] = {
        { 0x01, "Osc1 Shape", V (kShapes) },          { 0x02, "Osc1 Pitch", N },
        { 0x03, "Osc1 Detune", N },                    { 0x04, "Osc1 Start", V (kStart) },
        { 0x05, "Osc1 Pitch Mod", N },                 { 0x06, "Osc1 Pitch Ctrl", N },
        { 0x07, "Osc1 Pitch Mod Src", V (kModSrc) },   { 0x08, "Osc1 Pitch Ctrl Src", V (kCtrlSrc) },
        { 0x09, "Osc1 FM", N },                        { 0x0A, "Osc1 FM Mod", N },
        { 0x0B, "Osc1 FM Ctrl", N },                   { 0x0C, "Osc1 FM Mod Src", V (kModSrc) },
        { 0x0D, "Osc1 FM Ctrl Src", V (kCtrlSrc) },
        { 0x10, "Osc2 Shape", V (kShapes) },          { 0x11, "Osc2 Pitch", N },
        { 0x12, "Osc2 Detune", N },                    { 0x13, "Osc2 Start", V (kStart) },
        { 0x14, "Osc2 Pitch Mod", N },                 { 0x15, "Osc2 Pitch Ctrl", N },
        { 0x16, "Osc2 Pitch Mod Src", V (kModSrc) },   { 0x17, "Osc2 Pitch Ctrl Src", V (kCtrlSrc) },
        { 0x1A, "Osc1 Mix", N },                       { 0x1B, "Osc2 Mix", N },
        { 0x1C, "Ringmod", N },                        { 0x1D, "Crack Mix", N },
        { 0x1E, "Osc2 Mix Mod", N },                   { 0x1F, "Osc2 Mix Ctrl", N },
        { 0x20, "Osc2 Mix Mod Src", V (kModSrc) },     { 0x21, "Osc2 Mix Ctrl Src", V (kCtrlSrc) },
        { 0x24, "Crack Speed", N },                    { 0x25, "Crack Length", N },
        { 0x26, "Filter Type", V (kFilterTypes) },    { 0x27, "Cutoff", N },
        { 0x28, "Resonance", N },                      { 0x29, "Drive", N },
        { 0x2A, "Filter Keytrack", N },                { 0x2B, "Cutoff Mod 1", N },
        { 0x2C, "Cutoff Ctrl 1", N },                  { 0x2D, "Cutoff Mod 1 Src", V (kModSrc) },
        { 0x2E, "Cutoff Ctrl 1 Src", V (kCtrlSrc) },   { 0x2F, "Cutoff Mod 2", N },
        { 0x30, "Cutoff Ctrl 2", N },                  { 0x31, "Cutoff Mod 2 Src", V (kModSrc) },
        { 0x32, "Cutoff Ctrl 2 Src", V (kCtrlSrc) },
        { 0x35, "Volume", N },                         { 0x36, "Volume Mod", N },
        { 0x37, "Volume Ctrl", N },                    { 0x38, "Volume Mod Src", V (kModSrc) },
        { 0x39, "Volume Ctrl Src", V (kCtrlSrc) },     { 0x3A, "Pan", N },
        { 0x3B, "Pan Mod", N },                        { 0x3C, "Pan Ctrl", N },
        { 0x3D, "Pan Mod Src", V (kModSrc) },          { 0x3E, "Pan Ctrl Src", V (kCtrlSrc) },
        { 0x3F, "Env1 Attack", N },                    { 0x40, "Env1 Decay", N },
        { 0x41, "Env1 Sustain", N },                   { 0x42, "Env1 Release", N },
        { 0x43, "Env1 Shape", N },
        { 0x45, "Env2 Attack", N },                    { 0x46, "Env2 Decay", N },
        { 0x47, "Env2 Sustain", N },                   { 0x48, "Env2 Release", N },
        { 0x49, "Env2 Shape", N },
        { 0x4B, "LFO1 Speed", N },                     { 0x4C, "LFO1 Shape", V (kLfoShapes) },
        { 0x4D, "LFO1 Sync", V (kLfoSync) },           { 0x4E, "LFO1 OneShot", V (kOffOn) },
        { 0x4F, "LFO2 Speed", N },                     { 0x50, "LFO2 Shape", V (kLfoShapes) },
        { 0x51, "LFO2 Sync", V (kLfoSync) },           { 0x52, "LFO2 Mod", N },
        { 0x53, "LFO2 Mod Src", N },                   // MOD or CTRL list: to check
        { 0x55, "Dry Out", V (kDryOut) },              { 0x56, "FX Select", V (kFxSelect) },
        { 0x57, "FX Send Mix", N },                    { 0x58, "Tuned Channel", V (kTunedChannel) },
        { 0x59, "Transpose", N },                      { 0x5A, "Low Key", N },
        { 0x5B, "High Key", N },                       { 0x5E, "XOR Group", V (kXor) },
        { 0x6C, "Pattern Mode", V (kPatternMode) },
    };

    const Param kProgramParams[] = {
        { 0x01, "Name 1", N }, { 0x02, "Name 2", N }, { 0x03, "Name 3", N }, { 0x04, "Name 4", N },
        { 0x05, "Name 5", N }, { 0x06, "Name 6", N }, { 0x07, "Name 7", N }, { 0x08, "Name 8", N },
        { 0x09, "Name 9", N }, { 0x0A, "Name 10", N }, { 0x0B, "Name 11", N }, { 0x0C, "Name 12", N },
        { 0x0D, "Name 13", N }, { 0x0E, "Name 14", N }, { 0x0F, "Name 15", N }, { 0x10, "Name 16", N },
        { 0x11, "MIDI Mode", V (kMidiMode) },
        { 0x12, "Tempo (high)", N },                   { 0x13, "Tempo (low)", N },
        { 0x14, "Trigger A Sound", N },                { 0x15, "Trigger A Vel", N },
        { 0x16, "Trigger B Sound", N },                { 0x17, "Trigger B Vel", N },
        { 0x18, "Trigger C Sound", N },                { 0x19, "Trigger C Vel", N },
        // 1A..1D: Trigger S sound / vel and Signature, ra-editor contradicts itself (docs/SYSEX.md)
    };

    const Param kEffectParams[] = {
        { 0x00, "FX Type", V (kFxTypes) },             { 0x01, "FX Output", V (kFxOut) },
        { 0x02, "FX Param 1", N }, { 0x03, "FX Param 2", N }, { 0x04, "FX Param 3", N },
        { 0x05, "FX Param 4", N }, { 0x06, "FX Param 5", N }, { 0x07, "FX Param 6", N },
        { 0x08, "FX Param 7", N }, { 0x09, "FX Param 8", N }, { 0x0A, "FX Param 9", N },
        { 0x0B, "FX Param 10", N }, { 0x0C, "FX Param 11", N },
    };

    const Param kGlobalParams[] = {
        { 0x00, "Version", N },                        { 0x02, "Popup Time", N },
        { 0x03, "Label Time", N },                     { 0x04, "LCD Contrast", N },
        { 0x05, "Input Gain", N },                     { 0x06, "Master Tune", N },
        { 0x07, "Global Channel", V (kGlobalChannel) }, { 0x08, "Sysex ID", N },
        { 0x09, "Global Volume", N },                  { 0x0C, "Active Program", N },
        { 0x0D, "Map Base Key", N },                   { 0x0E, "Global Tempo (high)", N },
        { 0x0F, "Global Tempo (low)", N },             { 0x10, "Tx Clock", N },
        { 0x11, "Ctl Send", V (kCtlSend) },            { 0x12, "Ctl Receive", V (kCtlReceive) },
        { 0x13, "Tx PrgChange", N },                   { 0x14, "Rx PrgChange", N },
        { 0x16, "Screen Saver", N },                   { 0x18, "Global Pattern Mode", N },
    };

    #undef V
    #undef N

    template <std::size_t n>
    const Param* findIn (const Param (&table)[n], int address)
    {
        for (auto& p : table)
            if (p.address == address)
                return &p;
        return nullptr;
    }

    const Param* findParam (Object o, int address)
    {
        switch (o)
        {
            case Object::sound:   return findIn (kSoundParams, address);
            case Object::program: return findIn (kProgramParams, address);
            case Object::effect:  return findIn (kEffectParams, address);
            case Object::global:  return findIn (kGlobalParams, address);
            default:              return nullptr;
        }
    }

    const char* objectWord (Object o)
    {
        switch (o)
        {
            case Object::sound:   return "son";
            case Object::program: return "programme";
            case Object::pattern: return "pattern";
            case Object::global:  return "globaux";
            case Object::effect:  return "effet";
            case Object::mode:    return "commande";
            default:              return "?";
        }
    }
}

const char* parameterName (Object object, int address)
{
    auto* p = findParam (object, address);
    return p != nullptr ? p->name : nullptr;
}

std::vector<std::string> valueNames (Object object, int address)
{
    std::vector<std::string> names;
    if (auto* p = findParam (object, address))
        if (p->values != nullptr)
            names.assign (p->values, p->values + p->numValues);
    return names;
}

std::string valueText (Object object, int address, int value)
{
    if (auto* p = findParam (object, address))
        if (p->values != nullptr && value >= 0 && value < p->numValues)
            return std::to_string (value) + " (" + p->values[value] + ")";

    if (object == Object::program && address >= 0x01 && address <= 0x10 && value >= 32 && value < 127)
        return std::to_string (value) + " '" + (char) value + "'";

    return std::to_string (value);
}

std::optional<ParameterChange> parameterChange (const Parsed& p)
{
    if (! p.isRackAttack || actionOf (p.command) != Action::parameter || p.payload.size() < 4)
        return std::nullopt;

    ParameterChange c;
    c.command = p.command;
    auto n = p.payload.size();
    c.location.assign (p.payload.begin() + 1, p.payload.end() - 3);
    c.address = p.payload[n - 3] * 128 + p.payload[n - 2];
    c.value = p.payload[n - 1];
    return c;
}

//==============================================================================
std::string hex2 (int v)
{
    char s[4];
    std::snprintf (s, sizeof (s), "%02X", v & 0xFF);
    return s;
}

std::string hex (const Bytes& bytes)
{
    std::string s;
    s.reserve (bytes.size() * 3);
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        if (i > 0) s += ' ';
        s += hex2 (bytes[i]);
    }
    return s;
}

std::optional<Bytes> parseHex (const std::string& text)
{
    Bytes out;
    int nibble = -1;

    for (std::size_t i = 0; i < text.size(); ++i)
    {
        char ch = text[i];

        if (ch == '0' && i + 1 < text.size() && (text[i + 1] == 'x' || text[i + 1] == 'X'))
        {
            if (nibble >= 0) return std::nullopt;   // "0x" in the middle of a byte
            ++i;
            continue;
        }

        int v = -1;
        if (ch >= '0' && ch <= '9')      v = ch - '0';
        else if (ch >= 'a' && ch <= 'f') v = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') v = ch - 'A' + 10;
        else if (std::isspace ((unsigned char) ch) || ch == ',' || ch == ';' || ch == '-')
        {
            if (nibble >= 0) { out.push_back ((std::uint8_t) nibble); nibble = -1; }   // single digit byte
            continue;
        }
        else
            return std::nullopt;

        if (nibble < 0) nibble = v;
        else            { out.push_back ((std::uint8_t) (nibble * 16 + v)); nibble = -1; }
    }

    if (nibble >= 0)
        out.push_back ((std::uint8_t) nibble);
    return out;
}

//==============================================================================
std::string describe (const Bytes& m)
{
    auto p = parse (m);
    std::ostringstream s;

    if (! p.isSysex)   return "pas un SysEx";
    if (! p.complete)  return "SysEx incomplet (" + std::to_string (m.size()) + " octets)";

    if (! p.isRackAttack)
    {
        if (m.size() >= 2 && m[1] == kWaldorf)
            return "SysEx Waldorf, autre machine (modèle " + hex2 (m.size() > 2 ? m[2] : 0) + ")";
        return "SysEx d'un autre fabricant (" + std::to_string (m.size()) + " octets)";
    }

    auto c = p.command;
    auto obj = objectOf (c);
    auto act = actionOf (c);
    auto* name = commandName (c);

    s << (name != nullptr ? name : ("CMD " + hex2 (c)).c_str());
    if (p.device != 0) s << " [ID " << (int) p.device << "]";

    int loc = locationSize (c);
    const auto& pl = p.payload;

    auto locationText = [&] (int count) -> std::string
    {
        std::ostringstream t;
        if (obj == Object::sound && count == 2 && pl.size() >= 3)
            t << " son " << (pl[2] + 1 < 10 ? "0" : "") << pl[2] + 1 << (pl[1] == kEditBuffer ? " (tampon)" : " (emplacement " + hex2 (pl[1]) + ")");
        else if (obj == Object::effect && count == 2 && pl.size() >= 3)
            t << " FX" << pl[2] + 1 << (pl[1] == kEditBuffer ? " (tampon)" : " (emplacement " + hex2 (pl[1]) + ")");
        else if (count > 0 && (int) pl.size() > count)
            t << " emplacement " << hex (Bytes (pl.begin() + 1, pl.begin() + 1 + count));
        return t.str();
    };

    if (act == Action::parameter)
    {
        if (auto pc = parameterChange (p))
        {
            if (loc >= 0) s << locationText (loc);
            else if (! pc->location.empty()) s << " emplacement " << hex (pc->location);
            s << " · " << hex2 (pc->address);
            if (auto* pn = parameterName (obj, pc->address)) s << ' ' << pn;
            else                                             s << " ???";
            s << " = " << valueText (obj, pc->address, pc->value);
        }
        else
            s << " (trop court : " << hex (pl) << ")";
    }
    else if (act == Action::dump)
    {
        int expected = dumpDataSize (c);
        if (loc >= 0)
        {
            s << locationText (loc);
            int dataSize = (int) pl.size() - 1 - loc;
            s << " · " << dataSize << " octets de données";
            if (expected >= 0 && dataSize != expected)
                s << " (attendu " << expected << ")";
        }
        else
            s << " · " << (int) pl.size() - 1 << " octets après la commande";
    }
    else
    {
        s << ' ' << objectWord (obj);
        if (pl.size() > 1) s << " · " << hex (Bytes (pl.begin() + 1, pl.end()));
    }

    switch (p.check)
    {
        case Parsed::Check::ok:    s << " · checksum OK"; break;
        case Parsed::Check::joker: s << " · checksum 7F (joker)"; break;
        case Parsed::Check::bad:   s << " · checksum FAUX (reçu " << hex2 (p.sentChecksum) << ", calculé " << hex2 (p.expectedChecksum) << ")"; break;
        case Parsed::Check::none:  break;
    }
    return s.str();
}

//==============================================================================
void AddressMap::add (const ParameterChange& c)
{
    auto& e = entries[{ c.command, c.address }];
    e.minValue = std::min (e.minValue, c.value);
    e.maxValue = std::max (e.maxValue, c.value);
    e.lastValue = c.value;
    e.lastLocation = c.location;
    ++e.count;
}

const AddressMap::Entry* AddressMap::find (std::uint8_t command, int address) const
{
    auto it = entries.find ({ command, address });
    return it != entries.end() ? &it->second : nullptr;
}

std::string AddressMap::table() const
{
    std::ostringstream s;
    char line[160];
    std::snprintf (line, sizeof (line), "%-5s %-4s %-24s %4s %4s %5s %7s  %s\n",
                   "Cmd", "Adr", "Nom", "min", "max", "nb", "dernier", "emplacement");
    s << line;

    for (auto& [key, e] : entries)
    {
        auto obj = objectOf (key.first);
        auto* cn = commandName (key.first);
        auto* pn = parameterName (obj, key.second);
        std::snprintf (line, sizeof (line), "%-5s %-4s %-24s %4d %4d %5d %7d  %s\n",
                       cn != nullptr ? cn : hex2 (key.first).c_str(),
                       hex2 (key.second).c_str(),
                       pn != nullptr ? pn : "???",
                       e.minValue, e.maxValue, e.count, e.lastValue,
                       hex (e.lastLocation).c_str());
        s << line;
    }
    return s.str();
}
}
