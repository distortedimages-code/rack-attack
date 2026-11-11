// Renders the simulator playing a pattern to a WAV file, offline: a way to hear the
// software RackAttack without running the plug-in.  SimRender out.wav [bars]

#include <JuceHeader.h>

#include "../Source/app/FakeRackAttack.h"
#include "../Source/core/RaSeq.h"
#include "../Source/sim/RaSynth.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::File out (argc > 1 ? juce::String (argv[1]) : juce::String ("sim-demo.wav"));
    const int bars = argc > 2 ? juce::String (argv[2]).getIntValue() : 8;
    const double sr = 48000.0, tempo = 124.0;

    FakeRackAttack unit;
    unit.useDemoSounds();

    ra::synth::Synth synth;
    synth.prepare (sr);
    synth.setGlobalChannel (unit.channel);
    synth.setTempo (tempo);
    for (int s = 0; s < 24; ++s)
        synth.setSound (s, unit.edit.soundData (s).data());
    for (int e = 0; e < 4; ++e)
    {
        std::uint8_t fx[ra::synth::fxBytes];
        for (int a = 0; a < ra::synth::fxBytes; ++a)
            fx[a] = (std::uint8_t) juce::jlimit (0, 127, unit.edit.effectValue (e, a));
        synth.setEffect (e, fx);
    }

    const bool kitSheet = argc > 3 && juce::String (argv[3]) == "kit";

    ra::seq::Pattern p;
    p.tempo = tempo;
    if (kitSheet)
    {
        // every sound once, a beat apart (the tuned bass as a drum note)
        for (int t = 0; t < 24; ++t)
        {
            p.tracks[(size_t) t].length = 128;
            p.tracks[(size_t) t].steps[(size_t) (t * 4) % 128].on = t * 4 < 128;
            p.tracks[(size_t) t].steps[(size_t) (t * 4) % 128].velocity = 110;
        }
        for (int t = 0; t < 24; ++t)
            if (t * 4 >= 128) p.tracks[(size_t) t].steps[0].on = false;
    }
    else
    {
    p.swing = 54;
    auto on = [&] (int t, std::initializer_list<int> steps, int vel = 110)
    {
        for (int i : steps) { p.tracks[(size_t) t].steps[(size_t) i].on = true; p.tracks[(size_t) t].steps[(size_t) i].velocity = (std::uint8_t) vel; }
    };
    on (0, { 0, 4, 8, 12 }, 120);
    on (0, { 14 }, 70);
    on (2, { 4, 12 });
    on (3, { 12 }, 100);
    for (int i = 0; i < 16; i += 2) on (5, { i }, i % 4 == 0 ? 110 : 60);
    on (6, { 6, 14 }, 90);
    on (13, { 2, 5, 9, 11 }, 70);
    p.tracks[13].length = 12;
    on (8, { 13 }, 90); on (9, { 14 }, 90); on (10, { 15 }, 90);
    p.tracks[8].steps[13].condB = 4; p.tracks[8].steps[13].condA = 4;
    p.tracks[9].steps[14].condB = 4; p.tracks[9].steps[14].condA = 4;
    p.tracks[10].steps[15].condB = 4; p.tracks[10].steps[15].condA = 4;
    on (7, { 0 }, 100);
    p.tracks[7].length = 64;
    p.tracks[2].steps[12].setLock (0x27, 120);           // a brighter second snare
    p.tracks[2].steps[12].setLock (0x40, 75);
    on (11, { 3, 11 }, 80);
    // the bass, as notes on its Tuned Channel
    p.tracks[20].mode = ra::seq::Mode::notes;
    const int bass[][2] = { { 0, 36 }, { 3, 36 }, { 6, 43 }, { 8, 36 }, { 10, 46 }, { 13, 48 } };
    for (auto& b : bass)
    {
        auto& st = p.tracks[20].steps[(size_t) b[0]];
        st.on = true; st.note = (std::uint8_t) b[1]; st.length = 6;
    }
    p.tracks[20].steps[13].setLock (0x27, 90);
    }

    ra::seq::Context ctx;
    ctx.channel = unit.channel;
    ctx.baseNote = 36;
    for (int s = 0; s < 24; ++s)
    {
        ctx.soundKnown[(size_t) s] = true;
        for (int a = 0; a < ra::seq::numSoundParameters; ++a)
            ctx.sound[(size_t) s][(size_t) a] = (std::uint8_t) unit.edit.soundValue (s, a);
        const int tuned = unit.edit.soundValue (s, 0x58);
        ctx.tunedChannel[(size_t) s] = (std::uint8_t) (tuned >= 1 && tuned <= 16 ? tuned : 0);
    }

    ra::seq::Player player;
    const int total = (int) (sr * 60.0 / tempo * 4.0 * bars) + (int) sr;    // plus a second of tails
    juce::AudioBuffer<float> audio (2, total);
    audio.clear();
    const int block = 256;
    const double ppqPerSample = tempo / 60.0 / sr;
    std::vector<ra::seq::Event> events;
    for (int start = 0; start < total; start += block)
    {
        const int n = std::min (block, total - start);
        const double from = start * ppqPerSample, until = (start + n) * ppqPerSample;
        events.clear();
        if (from < 4.0 * bars)
            player.render (p, ctx, from, until, 0.0, events);
        int pos = 0;
        for (auto& e : events)
        {
            const int at = juce::jlimit (0, n, (int) std::lround ((e.ppq - from) / ppqPerSample));
            if (at > pos)
            {
                synth.render (audio.getWritePointer (0, start + pos), audio.getWritePointer (1, start + pos), at - pos);
                pos = at;
            }
            if (e.kind == ra::seq::Event::Kind::parameter)
            {
                auto m = ra::soundParameter (5, e.track, e.address, e.value);
                synth.handleMidi (m.data(), (int) m.size());
            }
            else
            {
                std::uint8_t m[3] = { (std::uint8_t) ((e.kind == ra::seq::Event::Kind::noteOn ? 0x90 : 0x80) | (e.channel - 1)),
                                      (std::uint8_t) e.note, (std::uint8_t) e.velocity };
                synth.handleMidi (m, 3);
            }
        }
        if (pos < n)
            synth.render (audio.getWritePointer (0, start + pos), audio.getWritePointer (1, start + pos), n - pos);
    }

    float peak = audio.getMagnitude (0, total);
    std::printf ("peak %.3f, rms L %.4f\n", peak, audio.getRMSLevel (0, 0, total));
    out.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (out.createOutputStream().release());
    if (auto* writer = wav.createWriterFor (stream.get(), sr, 2, 24, {}, 0))
    {
        stream.release();
        std::unique_ptr<juce::AudioFormatWriter> w (writer);
        w->writeFromAudioSampleBuffer (audio, 0, total);
        std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
        return 0;
    }
    return 1;
}
