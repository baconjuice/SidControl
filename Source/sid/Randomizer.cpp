#include "Randomizer.h"
#include "Params.h"

#include <algorithm>
#include <cmath>
#include <cctype>

namespace sid
{
namespace
{
using Rng = std::mt19937;

int rnd (Rng& r, int lo, int hi) { return std::uniform_int_distribution<int> (lo, hi) (r); }
bool chance (Rng& r, double p) { return std::uniform_real_distribution<double> (0.0, 1.0) (r) < p; }

bool startsWith (const std::string& s, const std::string& p) { return s.rfind (p, 0) == 0; }
bool contains (const std::string& s, const std::string& p) { return s.find (p) != std::string::npos; }
bool endsWith (const std::string& s, const std::string& p)
{
    return s.size() >= p.size() && s.compare (s.size() - p.size(), p.size(), p) == 0;
}

// "v3.attack" -> true for prefix "v" followed by a digit.
bool numberedPrefix (const std::string& id, const std::string& p)
{
    return startsWith (id, p) && id.size() > p.size() && std::isdigit ((unsigned char) id[p.size()]);
}

// Random raw value covering the parameter's valid range. Choices skip the
// unnamed placeholder entries.
int randomRaw (const ParamDef& d, Rng& r)
{
    if (! d.choices.empty())
    {
        std::vector<int> valid;
        for (size_t i = 0; i < d.choices.size() && (int) i <= d.maxRaw(); ++i)
            if (! d.choices[i].empty() && d.choices[i][0] != '(')
                valid.push_back ((int) i);
        if (! valid.empty())
            return valid[(size_t) rnd (r, 0, (int) valid.size() - 1)];
    }
    return rnd (r, 0, d.maxRaw());
}

void set (Patch& p, const ParamTable& t, const std::string& id, int raw)
{
    if (const auto* d = t.find (id))
        p.setRaw (*d, std::clamp (raw, 0, d->maxRaw()));
}
int get (const Patch& p, const ParamTable& t, const std::string& id)
{
    const auto* d = t.find (id);
    return d != nullptr ? p.getRaw (*d) : 0;
}

// SID oscillator waveform as the box picks it: one of 0..8 (none, tri, saw,
// tri+saw, pulse, ..., noise) plus random sync / ring.
void randomWave (Patch& p, const ParamTable& t, const std::string& v, Rng& r)
{
    const int w = rnd (r, 0, 8);
    const bool noise = w == 8;
    set (p, t, v + ".tri", ! noise && (w & 1));
    set (p, t, v + ".saw", ! noise && (w & 2));
    set (p, t, v + ".pulse", ! noise && (w & 4));
    set (p, t, v + ".noise", noise);
    set (p, t, v + ".off", 0);
    set (p, t, v + ".sync", rnd (r, 0, 1));
    set (p, t, v + ".ring", rnd (r, 0, 1));
}

// Voice rules shared by Lead and Multi (SID_RND_GenerateOsc_Gen).
void voiceRules (Patch& p, const ParamTable& t, const std::string& v, Rng& r)
{
    set (p, t, v + ".delay", 0);
    set (p, t, v + ".transpose", 0x40 + 12 * rnd (r, -1, 2));
    set (p, t, v + ".finetune", 0x80 + rnd (r, -10, 10));
    set (p, t, v + ".pitchrange", 2);
    set (p, t, v + ".porta", rnd (r, 0, 15));
    set (p, t, v + ".arp.on", 0);
    set (p, t, v + ".gae", 0);
    randomWave (p, t, v, r);
}

void lfoRules (Patch& p, const ParamTable& t, const std::string& l, Rng& r)
{
    set (p, t, l + ".wave", rnd (r, 0, 8));
    set (p, t, l + ".delay", rnd (r, 0, 31));
}

// Wavetable sequencer setup (SID_RND_GenerateWTC_Gen): slow-ish speed, one
// 32 step block of the table.
void wtRules (Patch& p, const ParamTable& t, const std::string& w, bool lead, Rng& r)
{
    const int start = 0x20 * rnd (r, 0, 3);
    set (p, t, w + ".div", rnd (r, 0, 15));
    set (p, t, w + ".start", start);
    set (p, t, w + ".end", start + 0x1f);
    set (p, t, w + ".loop", start + rnd (r, 0, 0x1f));
    if (lead)
    {
        set (p, t, w + ".left", 1);
        set (p, t, w + ".right", 1);
        set (p, t, w + ".startmod", 0);
        set (p, t, w + ".endkey", 0);
    }
    else
    {
        set (p, t, w + ".w16", 0);
    }
}

// Which parameters belong to a section.
bool inSection (Engine e, const std::string& s, const std::string& id)
{
    const bool all = s == "all";
    const bool knobId = numberedPrefix (id, "k") || startsWith (id, "kV.") || startsWith (id, "kP.") || startsWith (id, "kA.");
    if ((all || s == "knb") && knobId) return true;
    if ((all || s == "ext") && (numberedPrefix (id, "ext") || startsWith (id, "extsw."))) return true;
    if ((all || s == "fil") && (startsWith (id, "fL.") || startsWith (id, "fR."))) return true;

    switch (e)
    {
        case Engine::Lead:
            if ((all || s == "osc") && numberedPrefix (id, "v"))
                return ! contains (id, ".arp.") && ! contains (id, ".sw.") && id.find (".swphase") == std::string::npos;
            if ((all || s == "osc") && (id == "lead.detune" || id == "lead.phase")) return true;
            if ((all || s == "lfo") && numberedPrefix (id, "lfo")) return true;
            if ((all || s == "env") && numberedPrefix (id, "env")) return true;
            if ((all || s == "mod") && numberedPrefix (id, "mp")) return true;
            if ((all || s == "trg") && startsWith (id, "trg.") && ! startsWith (id, "trg.0.")) return true; // keep Note On
            if ((all || s == "wtc") && numberedPrefix (id, "wt")) return true;
            if ((all || s == "wte") && startsWith (id, "wtdata.")) return true;
            return false;

        case Engine::Bassline:
        {
            const bool b = startsWith (id, "bL.") || startsWith (id, "bR.");
            if (! b) return false;
            const auto f = id.substr (3);
            if ((all || s == "o23") && startsWith (f, "o")) return std::isdigit ((unsigned char) f[1]) != 0;
            if ((all || s == "lfo") && startsWith (f, "lfo")) return true;
            if ((all || s == "env") && startsWith (f, "env.")) return true;
            if ((all || s == "osc"))
            {
                static const std::vector<std::string> voice { "portamode", "gae", "tri", "saw", "pulse", "noise", "off", "sync",
                                                              "ring", "attack", "decay", "sustain", "release", "pw", "accent",
                                                              "delay", "transpose", "finetune", "pitchrange", "porta" };
                return std::find (voice.begin(), voice.end(), f) != voice.end() || f == "arp.on";
            }
            return false;
        }

        case Engine::Drum:
            if ((all || s == "drm") && id.size() > 1 && id[0] == 'd' && std::isdigit ((unsigned char) id[1])) return true;
            return false;

        case Engine::Multi:
        {
            if (! numberedPrefix (id, "m")) return false;
            const auto f = id.substr (id.find ('.') + 1);
            if ((all || s == "lfo") && startsWith (f, "lfo")) return true;
            if ((all || s == "env") && startsWith (f, "env.")) return true;
            if ((all || s == "wtc") && startsWith (f, "wt.")) return true;
            if ((all || s == "osc"))
                return ! startsWith (f, "lfo") && ! startsWith (f, "env.") && ! startsWith (f, "wt.") && ! startsWith (f, "arp.")
                       && ! startsWith (f, "sw.") && f != "swphase" && f != "voice" && f != "velpar" && f != "pbpar"
                       && f != "poly" && f != "legato" && f != "wto" && f != "suskey";
            if ((all || s == "wte") && startsWith (id, "wtdata.")) return true;
            return false;
        }
    }
    return false;
}

// Rules applied after the plain random values (the firmware's special cases).
void applyRules (Engine e, const std::string& s, Patch& p, Rng& r)
{
    const auto& t = paramsFor (e);
    const bool all = s == "all";

    if (all || s == "fil")
    {
        // CutOff in the lower quarter (better for modulation), both channels
        // with the same routing, no OSC3 off / external input.
        for (auto f : { "fL", "fR" })
        {
            set (p, t, std::string (f) + ".cutoff", 0x400 + rnd (r, 0, 0xff));
            set (p, t, std::string (f) + ".ext", 0);
            set (p, t, std::string (f) + ".o3off", 0);
        }
        for (auto field : { ".o1", ".o2", ".o3", ".lp", ".bp", ".hp" })
            set (p, t, std::string ("fR") + field, get (p, t, std::string ("fL") + field));
    }

    switch (e)
    {
        case Engine::Lead:
            if (all || s == "osc")
            {
                set (p, t, "lead.phase", 0);
                set (p, t, "lead.detune", rnd (r, 0, 31));
                for (int v = 1; v <= 3; ++v)
                    voiceRules (p, t, "v" + std::to_string (v), r);
                // Right SID = copy of the left one (nice for stereo modulation).
                for (auto& d : t.all())
                    if (numberedPrefix (d.id, "v") && d.id[1] >= '4' && d.id[1] <= '6')
                        if (const auto* src = t.find (std::string ("v") + char (d.id[1] - 3) + d.id.substr (2)))
                            p.setRaw (d, p.getRaw (*src));
            }
            if (all || s == "lfo")
                for (int l = 1; l <= 6; ++l)
                    lfoRules (p, t, "lfo" + std::to_string (l), r);
            if (all || s == "env")
                for (int n = 1; n <= 2; ++n)
                    set (p, t, "env" + std::to_string (n) + ".delay", rnd (r, 0, 31));
            if (all || s == "mod")
                for (int m = 1; m <= 8; ++m)
                {
                    const auto mp = "mp" + std::to_string (m);
                    set (p, t, mp + ".src1", rnd (r, 1, 30));
                    set (p, t, mp + ".op", rnd (r, 1, 12));
                    // Skip "off" and the pitch targets; half of the slots get one target only.
                    set (p, t, mp + ".tgt1", rnd (r, 7, (int) modTargets().size() - 1));
                    set (p, t, mp + ".tgt2", chance (r, 0.5) ? 0 : rnd (r, 7, (int) modTargets().size() - 1));
                }
            if (all || s == "wtc")
                for (int w = 1; w <= 4; ++w)
                    wtRules (p, t, "wt" + std::to_string (w), true, r);
            break;

        case Engine::Bassline:
            for (auto b : { "bL", "bR" })
            {
                const std::string v = b;
                if (all || s == "osc")
                {
                    voiceRules (p, t, v, r);
                    // Bassline flavour: 2 octaves down, more slide, plucky envelope.
                    set (p, t, v + ".transpose", get (p, t, v + ".transpose") - 24);
                    set (p, t, v + ".porta", get (p, t, v + ".porta") + 30);
                    set (p, t, v + ".attack", 0);
                    set (p, t, v + ".decay", 0);
                    set (p, t, v + ".sustain", 12);
                    set (p, t, v + ".release", 0);
                }
                if (all || s == "o23")
                    for (int o = 2; o <= 3; ++o)
                    {
                        const auto on = v + ".o" + std::to_string (o);
                        randomWave (p, t, on, r);
                        set (p, t, on + ".note", 0); // no static notes
                    }
                if (all || s == "lfo")
                    for (int l = 1; l <= 2; ++l)
                        lfoRules (p, t, v + ".lfo" + std::to_string (l), r);
            }
            break;

        case Engine::Drum:
            if (all || s == "drm")
                for (int i = 1; i <= 16; ++i)
                {
                    const auto d = "d" + std::to_string (i);
                    set (p, t, d + ".voice", 0);
                    set (p, t, d + ".attack", 0);
                    set (p, t, d + ".model", rnd (r, 0, (int) drumModels().size() - 1));
                    set (p, t, d + ".velpar", 0);
                }
            break;

        case Engine::Multi:
            for (int i = 1; i <= 6; ++i)
            {
                const auto m = "m" + std::to_string (i);
                if (all || s == "osc") voiceRules (p, t, m, r);
                if (all || s == "lfo")
                    for (int l = 1; l <= 2; ++l)
                        lfoRules (p, t, m + ".lfo" + std::to_string (l), r);
                if (all || s == "wtc") wtRules (p, t, m + ".wt", false, r);
            }
            break;
    }
}

// Sequencer bytes (not in the parameter tables). Fills `out` with
// (address, mask) pairs of the bits a section may change, and writes random
// values for them into p.
struct SeqBits { uint16_t addr; uint8_t mask; };

std::vector<SeqBits> randomizeSequences (Engine e, const std::string& s, Patch& p, Rng& r)
{
    std::vector<SeqBits> bits;
    const bool all = s == "all";
    const auto& t = paramsFor (e);

    if (e == Engine::Bassline)
    {
        // The sequence each bassline plays. Low byte: note [3:0], octave
        // [5:4], slide [6], gate [7]. High byte: parameter [6:0], accent [7].
        std::vector<int> seqs { get (p, t, "bL.seq.num") & 7, get (p, t, "bR.seq.num") & 7 };
        std::sort (seqs.begin(), seqs.end());
        seqs.erase (std::unique (seqs.begin(), seqs.end()), seqs.end());
        for (int seq : seqs)
            for (int step = 0; step < 16; ++step)
            {
                const auto lo = (uint16_t) (0x100 + seq * 16 + step), hi = (uint16_t) (0x180 + seq * 16 + step);
                uint8_t loMask = 0, hiMask = 0;
                auto lb = p.data[lo], hb = p.data[hi];
                if (all || s == "nte") { lb = (uint8_t) ((lb & 0xc0) | rnd (r, 0, 12) | (rnd (r, 0, 3) << 4)); loMask |= 0x3f; }
                if (all || s == "gte") { lb = (uint8_t) ((lb & 0x7f) | (chance (r, 0.75) ? 0x80 : 0)); loMask |= 0x80; }
                if (all || s == "sld") { lb = (uint8_t) ((lb & 0xbf) | (chance (r, 0.25) ? 0x40 : 0)); loMask |= 0x40; }
                if (all || s == "acc") { hb = (uint8_t) ((hb & 0x7f) | (chance (r, 0.3) ? 0x80 : 0)); hiMask |= 0x80; }
                if (all || s == "par") { hb = (uint8_t) ((hb & 0x80) | rnd (r, 0, 127)); hiMask |= 0x7f; }
                p.data[lo] = lb;
                p.data[hi] = hb;
                if (loMask) bits.push_back ({ lo, loMask });
                if (hiMask) bits.push_back ({ hi, hiMask });
            }
    }
    else if (e == Engine::Drum && (all || s == "seq"))
    {
        // The selected sequence: 8 tracks x (gate 1-8, accent 1-8, gate 9-16, accent 9-16).
        const int seq = get (p, t, "dr.seq.num") & 7;
        for (int track = 0; track < 8; ++track)
            for (int b = 0; b < 4; ++b)
            {
                const auto a = (uint16_t) (0x100 + seq * 0x20 + track * 4 + b);
                const bool accent = (b & 1) != 0;
                uint8_t v = 0;
                for (int bit = 0; bit < 8; ++bit)
                    if (chance (r, accent ? 0.2 : 0.35))
                        v |= (uint8_t) (1 << bit);
                p.data[a] = v;
                bits.push_back ({ a, 0xff });
            }
    }
    return bits;
}
} // namespace

const std::vector<RandomSection>& randomSections (Engine e)
{
    static const std::vector<RandomSection> lead {
        { "all", "All", "A completely new random patch" },
        { "osc", "Osc", "Waveforms, ADSR, pulsewidth, tuning of the 6 voices (right SID copies the left)" },
        { "fil", "Filter", "Filter mode, routing, cutoff and resonance" },
        { "lfo", "LFO", "The 6 LFOs" },
        { "env", "ENV", "The 2 modulation envelopes" },
        { "mod", "Mod", "The modulation matrix" },
        { "trg", "Trigger", "The trigger matrix (Note On row is kept)" },
        { "wtc", "WT Cfg", "Wavetable sequencer setup" },
        { "wte", "WT Data", "Wavetable contents" },
        { "knb", "Knobs", "Knob assignments and ranges" },
        { "ext", "Ext", "External CV values and switches" },
    };
    static const std::vector<RandomSection> bass {
        { "all", "All", "A completely new random patch" },
        { "nte", "Notes", "Notes of the playing sequences" },
        { "gte", "Gates", "Gates of the playing sequences" },
        { "sld", "Slides", "Slides of the playing sequences" },
        { "acc", "Accents", "Accents of the playing sequences" },
        { "par", "Par Track", "Parameter track of the playing sequences" },
        { "osc", "Osc", "OSC1 of both basslines" },
        { "o23", "Osc 2/3", "OSC2 and OSC3 of both basslines" },
        { "fil", "Filter", "Filter mode, routing, cutoff and resonance" },
        { "lfo", "LFO", "The LFOs of both basslines" },
        { "env", "ENV", "The envelopes of both basslines" },
        { "knb", "Knobs", "Knob assignments and ranges" },
        { "ext", "Ext", "External CV values and switches" },
    };
    static const std::vector<RandomSection> drum {
        { "all", "All", "A completely new random patch" },
        { "seq", "Pattern", "The playing drum sequence" },
        { "drm", "Drums", "The 16 drum instruments" },
        { "fil", "Filter", "Filter mode, routing, cutoff and resonance" },
        { "knb", "Knobs", "Knob assignments and ranges" },
        { "ext", "Ext", "External CV values and switches" },
    };
    static const std::vector<RandomSection> multi {
        { "all", "All", "A completely new random patch" },
        { "osc", "Osc", "Voice settings of the 6 instruments" },
        { "fil", "Filter", "Filter mode, routing, cutoff and resonance" },
        { "lfo", "LFO", "The LFOs of the 6 instruments" },
        { "env", "ENV", "The envelopes of the 6 instruments" },
        { "wtc", "WT Cfg", "Wavetable sequencer setup of the 6 instruments" },
        { "wte", "WT Data", "Wavetable contents" },
        { "knb", "Knobs", "Knob assignments and ranges" },
        { "ext", "Ext", "External CV values and switches" },
    };
    switch (e)
    {
        case Engine::Bassline: return bass;
        case Engine::Drum: return drum;
        case Engine::Multi: return multi;
        case Engine::Lead: break;
    }
    return lead;
}

void randomize (Patch& patch, const std::string& section, float amount, Rng& rng)
{
    amount = std::clamp (amount, 0.0f, 1.0f);
    const auto e = patch.engine();
    const auto& t = paramsFor (e);
    const Patch before = patch;

    // A fully random "All" starts from the init patch like on the box, so
    // nothing of the old sound (arpeggiator, odd settings) is left over.
    Patch target = (section == "all" && amount >= 1.0f) ? Patch::makeInit (e) : patch;
    if (section == "all" && amount >= 1.0f)
        target.setName (patch.name());

    std::vector<const ParamDef*> defs;
    for (auto& d : t.all())
        if (inSection (e, section, d.id))
        {
            defs.push_back (&d);
            target.setRaw (d, randomRaw (d, rng));
        }
    applyRules (e, section, target, rng);
    const auto seqBits = randomizeSequences (e, section, target, rng);

    if (amount >= 1.0f)
    {
        patch = target;
        return;
    }

    // Partial: move each value part of the way; switches and choices flip
    // with a probability of `amount`.
    for (auto* d : defs)
    {
        const int a = before.getRaw (*d), b = target.getRaw (*d);
        if (a == b) continue;
        const bool discrete = d->kind == ParamDef::Kind::Bool || d->kind == ParamDef::Kind::Choice || d->maxRaw() < 16;
        const int v = discrete ? (chance (rng, amount) ? b : a) : (int) std::lround (a + (b - a) * (double) amount);
        patch.setRaw (*d, v);
    }
    for (auto& sb : seqBits)
        if (chance (rng, amount))
            patch.data[sb.addr] = (uint8_t) ((patch.data[sb.addr] & ~sb.mask) | (target.data[sb.addr] & sb.mask));
}
} // namespace sid
