// Tests of the sequencer core: patterns, Euclidean rhythms, timing, locks, saving.

#include "../Source/core/RaSeq.h"

#include <cmath>
#include <cstdio>
#include <string>

static int failures = 0;

#define CHECK(cond) do { if (! (cond)) { std::printf ("FAILED line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

using namespace ra::seq;

static std::string bits (const std::vector<bool>& v)
{
    std::string s;
    for (bool b : v)
        s += b ? 'x' : '.';
    return s;
}

static bool near (double a, double b)   { return std::abs (a - b) < 1e-9; }

static std::vector<Event> notesOn (const std::vector<Event>& ev)
{
    std::vector<Event> out;
    for (auto& e : ev)
        if (e.kind == Event::Kind::noteOn)
            out.push_back (e);
    return out;
}

static std::string describe (const std::vector<Event>& ev)
{
    std::string s;
    for (auto& e : ev)
    {
        char line[96];
        const char* k = e.kind == Event::Kind::noteOn ? "on" : e.kind == Event::Kind::noteOff ? "off" : "par";
        std::snprintf (line, sizeof line, "%s@%.4f t%d ch%d n%d v%d a%d=%d\n", k, e.ppq, e.track, e.channel, e.note,
                       e.velocity, e.address, e.value);
        s += line;
    }
    return s;
}

int main()
{
    // Euclidean rhythms
    CHECK (bits (euclidean (16, 4, 0)) == "x...x...x...x...");
    CHECK (bits (euclidean (8, 3, 0)) == "x..x..x.");
    CHECK (bits (euclidean (8, 3, 1)) == ".x..x..x");
    CHECK (bits (euclidean (8, 3, -1)) == "..x..x.x");
    CHECK (bits (euclidean (5, 0, 0)) == ".....");
    CHECK (bits (euclidean (4, 9, 0)) == "xxxx");

    Context ctx;
    ctx.channel = 1;
    ctx.baseNote = 36;
    ctx.soundKnown.fill (true);
    ctx.sound[0][0x27] = 64;

    // A four-on-the-floor kick: one note per beat, gate half a step
    {
        Pattern p;
        for (int i : { 0, 4, 8, 12 })
            p.tracks[0].steps[(size_t) i].on = true;
        Player pl;
        std::vector<Event> ev;
        pl.render (p, ctx, 0.0, 1.0, 0.0, ev);
        CHECK (ev.size() == 2);
        CHECK (ev[0].kind == Event::Kind::noteOn && near (ev[0].ppq, 0.0) && ev[0].note == 36 && ev[0].channel == 1);
        CHECK (ev[1].kind == Event::Kind::noteOff && near (ev[1].ppq, 0.125));
        ev.clear();
        pl.render (p, ctx, 1.0, 4.0, 0.0, ev);
        auto on = notesOn (ev);
        CHECK (on.size() == 3 && near (on[0].ppq, 1.0) && near (on[2].ppq, 3.0));
        ev.clear();
        pl.render (p, ctx, 4.0, 5.0, 0.0, ev);   // the pattern loops after 16 steps
        CHECK (notesOn (ev).size() == 1 && near (notesOn (ev)[0].ppq, 4.0));
    }

    // Small blocks give the same events as one big one
    {
        Pattern p;
        for (int i = 0; i < 16; i += 3)
            p.tracks[2].steps[(size_t) i].on = true;
        p.tracks[5].length = 7;
        p.tracks[5].steps[1].on = true;
        Player a, b;
        std::vector<Event> big, small;
        a.render (p, ctx, 0.0, 8.0, 0.0, big);
        for (double t = 0.0; t < 8.0 - 1e-9; t += 0.0123)
            b.render (p, ctx, t, std::min (8.0, t + 0.0123), 0.0, small);
        CHECK (notesOn (big).size() == notesOn (small).size());
        bool same = notesOn (big).size() == notesOn (small).size();
        if (same)
        {
            auto x = notesOn (big), y = notesOn (small);
            for (size_t i = 0; i < x.size(); ++i)
                same = same && near (x[i].ppq, y[i].ppq) && x[i].track == y[i].track;
        }
        CHECK (same);
    }

    // Polymeter: a 3-step track against the 16-step ones
    {
        Pattern p;
        p.tracks[1].length = 3;
        p.tracks[1].steps[0].on = true;
        Player pl;
        std::vector<Event> ev;
        pl.render (p, ctx, 0.0, 2.0, 0.0, ev);
        auto on = notesOn (ev);
        CHECK (on.size() == 3 && near (on[1].ppq, 0.75) && near (on[2].ppq, 1.5) && on[0].note == 37);
    }

    // Locks: sent just before the note, put back at the next trig that doesn't lock
    {
        Pattern p;
        for (int i : { 0, 4, 8 })
            p.tracks[0].steps[(size_t) i].on = true;
        p.tracks[0].steps[4].setLock (0x27, 100);
        p.tracks[0].steps[4].setLock (0x30, 5);
        const double lead = 0.01;
        Player pl;
        std::vector<Event> ev;
        pl.render (p, ctx, 0.0, 1.5, lead, ev);
        // step 0: just the note; step 4 (ppq 1.0): two locks then the note
        CHECK (ev.size() == 6);
        if (ev.size() == 6)
        {
            CHECK (ev[2].kind == Event::Kind::parameter && near (ev[2].ppq, 1.0 - 2 * lead) && ev[2].address == 0x27 && ev[2].value == 100);
            CHECK (ev[3].kind == Event::Kind::parameter && near (ev[3].ppq, 1.0 - lead) && ev[3].address == 0x30 && ev[3].value == 5);
            CHECK (ev[4].kind == Event::Kind::noteOn && near (ev[4].ppq, 1.0));
        }
        ev.clear();
        pl.render (p, ctx, 1.5, 2.5, lead, ev);
        CHECK (ev.size() >= 3);
        if (ev.size() >= 3)
        {
            CHECK (ev[0].kind == Event::Kind::parameter && ev[0].address == 0x27 && ev[0].value == 64);
            CHECK (ev[1].kind == Event::Kind::parameter && ev[1].address == 0x30 && ev[1].value == 0);
            CHECK (ev[2].kind == Event::Kind::noteOn && near (ev[2].ppq, 2.0));
        }

        // a lock planned ahead: the step at 1.0 is planned once its first event is in the window
        Player early;
        ev.clear();
        early.render (p, ctx, 0.0, 1.0 - 1.5 * lead, lead, ev);
        CHECK (ev.size() == 5 && ev[2].kind == Event::Kind::parameter && ev[3].kind == Event::Kind::parameter);
        CHECK (notesOn (ev).size() == 2 && near (notesOn (ev)[1].ppq, 1.0));
        ev.clear();
        early.render (p, ctx, 1.0 - 1.5 * lead, 1.2, lead, ev);
        CHECK (notesOn (ev).empty() && ev.size() == 1 && ev[0].kind == Event::Kind::noteOff);

        // stopping puts the locked values back
        Player st;
        ev.clear();
        st.render (p, ctx, 0.0, 1.1, lead, ev);
        ev.clear();
        st.stop (ctx, 1.1, ev);
        int puts = 0, offs = 0;
        for (auto& e : ev)
        {
            puts += e.kind == Event::Kind::parameter ? 1 : 0;
            offs += e.kind == Event::Kind::noteOff ? 1 : 0;
        }
        CHECK (puts == 2 && offs >= 1);
        CHECK (! st.isRunning());

        // stopped on the locked step, while the next step (which puts the sound back) is
        // already planned: the putting back is dropped with the plan, so the stop must
        // put the locked values back itself
        Player late;
        ev.clear();
        late.render (p, ctx, 0.0, 2.0 - 1.5 * lead, lead, ev);     // step 8 (ppq 2.0) planned, its put-backs included
        ev.clear();
        late.stop (ctx, 1.5, ev);
        int back = 0;
        for (auto& e : ev)
            if (e.kind == Event::Kind::parameter && ((e.address == 0x27 && e.value == 64) || (e.address == 0x30 && e.value == 0)))
                ++back;
        CHECK (back == 2);

        // and the next start begins with the sound's own values
        ev.clear();
        late.render (p, ctx, 0.0, 0.5, lead, ev);
        bool params = false;
        for (auto& e : ev)
            params = params || e.kind == Event::Kind::parameter;
        CHECK (notesOn (ev).size() == 1 && ! params);
    }

    // Conditions and probability
    {
        Pattern p;
        auto& s = p.tracks[3].steps[0];
        s.on = true;
        s.condA = 1;
        s.condB = 2;
        Player pl;
        std::vector<Event> ev;
        pl.render (p, ctx, 0.0, 16.0, 0.0, ev);   // four loops of 16 steps
        auto on = notesOn (ev);
        CHECK (on.size() == 2 && near (on[0].ppq, 0.0) && near (on[1].ppq, 8.0));

        Pattern q;
        q.tracks[3].steps[0].on = true;
        q.tracks[3].steps[0].probability = 0;
        Player pq;
        ev.clear();
        pq.render (q, ctx, 0.0, 16.0, 0.0, ev);
        CHECK (ev.empty());
    }

    // Notes mode on the sound's Tuned Channel, retrigs, swing, micro timing, mute
    {
        Pattern p;
        auto& tr = p.tracks[20];
        tr.mode = Mode::notes;
        tr.steps[0].on = true;
        tr.steps[0].note = 48;
        Context c2 = ctx;
        c2.tunedChannel[20] = 3;
        Player pl;
        std::vector<Event> ev;
        pl.render (p, c2, 0.0, 0.25, 0.0, ev);
        CHECK (! ev.empty() && ev[0].channel == 3 && ev[0].note == 48);

        Player fallback;   // no Tuned Channel: plays the drum note
        ev.clear();
        fallback.render (p, ctx, 0.0, 0.25, 0.0, ev);
        CHECK (! ev.empty() && ev[0].channel == 1 && ev[0].note == 36 + 20);

        Pattern r;
        r.tracks[0].steps[0].on = true;
        r.tracks[0].steps[0].retrig = 4;
        Player pr;
        ev.clear();
        pr.render (r, ctx, 0.0, 0.25, 0.0, ev);
        auto on = notesOn (ev);
        CHECK (on.size() == 4 && near (on[1].ppq, 0.0625) && near (on[3].ppq, 0.1875));

        Pattern sw;
        sw.swing = 75;
        sw.tracks[0].steps[1].on = true;
        sw.tracks[0].steps[2].on = true;
        sw.tracks[0].steps[2].micro = 6;
        Player ps;
        ev.clear();
        ps.render (sw, ctx, 0.0, 1.0, 0.0, ev);
        on = notesOn (ev);
        CHECK (on.size() == 2 && near (on[0].ppq, 0.375) && near (on[1].ppq, 0.5625));

        Pattern m;
        m.tracks[0].steps[0].on = true;
        m.tracks[0].mute = true;
        Player pm;
        ev.clear();
        pm.render (m, ctx, 0.0, 4.0, 0.0, ev);
        CHECK (ev.empty());
    }

    // Euclid mode keeps the steps' settings (here a lock on the 2nd hit)
    {
        Pattern p;
        auto& tr = p.tracks[5];
        tr.mode = Mode::euclid;
        tr.euclid = { 8, 3, 0 };
        tr.steps[3].setLock (0x27, 1);
        Context c2 = ctx;
        c2.sound[5][0x27] = 90;
        Player pl;
        std::vector<Event> ev;
        pl.render (p, c2, 0.0, 2.0, 0.0, ev);
        auto on = notesOn (ev);
        CHECK (on.size() == 3 && near (on[1].ppq, 0.75) && near (on[2].ppq, 1.5));
        int locks = 0;
        for (auto& e : ev)
            if (e.kind == Event::Kind::parameter)
                locks += (e.value == 1 && near (e.ppq, 0.75)) || (e.value == 90 && near (e.ppq, 1.5)) ? 1 : 0;
        CHECK (locks == 2);

        tr.printEuclid();
        CHECK (tr.mode == Mode::steps && tr.length == 8 && tr.steps[6].on && ! tr.steps[7].on);
    }

    // The same note held longer than the gap: let go just before it plays again
    {
        Pattern p;
        p.tracks[0].steps[0].on = true;
        p.tracks[0].steps[0].length = 64;   // 8 steps
        p.tracks[0].steps[2].on = true;
        Player pl;
        std::vector<Event> ev;
        pl.render (p, ctx, 0.0, 1.0, 0.0, ev);
        bool offBeforeSecond = false;
        for (size_t i = 0; i + 1 < ev.size(); ++i)
            if (ev[i].kind == Event::Kind::noteOff && ev[i + 1].kind == Event::Kind::noteOn && near (ev[i + 1].ppq, 0.5))
                offBeforeSecond = true;
        CHECK (offBeforeSecond);
    }

    // Saving and loading
    {
        Pattern p;
        p.tempo = 124.0;
        p.swing = 58;
        p.tracks[2].length = 64;
        p.tracks[2].division = Division::sixteenthTriplet;
        p.tracks[2].steps[13].on = true;
        p.tracks[2].steps[13].velocity = 77;
        p.tracks[2].steps[13].micro = -3;
        p.tracks[2].steps[13].condA = 2;
        p.tracks[2].steps[13].condB = 4;
        p.tracks[2].steps[13].setLock (0x27, 98);
        p.tracks[7].mode = Mode::euclid;
        p.tracks[7].euclid = { 12, 5, -2 };
        p.tracks[16].mute = true;
        p.tracks[3].name = "Caisse claire";
        p.tracks[21].steps[127].on = true;
        auto data = p.save();
        Pattern q;
        CHECK (q.load (data));
        CHECK (q.save() == data);
        CHECK (q.tracks[2].steps[13].findLock (0x27) != nullptr && q.tracks[2].steps[13].findLock (0x27)->value == 98);
        CHECK (q.tracks[7].euclid.rotation == -2 && q.tracks[2].division == Division::sixteenthTriplet);
        CHECK (near (q.tempo, 124.0) && q.swing == 58);
        CHECK (q.tracks[3].name == "Caisse claire" && q.tracks[16].mute);

        Pattern r;
        r.tracks[0].length = 9;
        CHECK (! r.load ({ 1, 2, 3 }));
        auto cut = data;
        cut.resize (cut.size() / 2);
        CHECK (! r.load (cut));
        CHECK (r.tracks[0].length == 9);   // unchanged
    }

    if (failures == 0)
        std::printf ("SeqTests: all passed\n");
    else
        std::printf ("%d failure(s)\n", failures);
    (void) describe;
    return failures == 0 ? 0 : 1;
}
