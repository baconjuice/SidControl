#include "Params.h"

#include <stdexcept>

namespace sid
{
// ---------------------------------------------------------------------------
// ParamTable
// ---------------------------------------------------------------------------
void ParamTable::add (ParamDef d)
{
    lookup[d.id] = defs.size();
    defs.push_back (std::move (d));
}

const ParamDef* ParamTable::find (const std::string& id) const
{
    auto it = lookup.find (id);
    return it == lookup.end() ? nullptr : &defs[it->second];
}

const ParamDef& ParamTable::get (const std::string& id) const
{
    if (auto* p = find (id))
        return *p;
    throw std::out_of_range ("unknown parameter id: " + id);
}

// ---------------------------------------------------------------------------
// Choice lists
// ---------------------------------------------------------------------------
const std::vector<std::string>& lfoWaveforms()
{
    static const std::vector<std::string> v { "Sine", "Triangle", "Saw", "Pulse", "Random",
                                              "Pos. Sine", "Pos. Triangle", "Pos. Saw", "Pos. Pulse",
                                              "(9)", "(10)", "(11)", "(12)", "(13)", "(14)", "(15)" };
    return v;
}

const std::vector<std::string>& modSources()
{
    static const std::vector<std::string> v = []
    {
        std::vector<std::string> s { "Off", "ENV1", "ENV2", "LFO1", "LFO2", "LFO3", "LFO4", "LFO5", "LFO6",
                                     "MP1", "MP2", "MP3", "MP4", "MP5", "MP6", "MP7", "MP8",
                                     "ModWheel", "Key", "Knob 1", "Knob 2", "Knob 3", "Knob 4", "Knob 5",
                                     "Knob V", "Knob P", "Knob A", "WT1", "WT2", "WT3", "WT4" };
        for (int i = 31; i < 128; ++i) s.push_back ("(" + std::to_string (i) + ")");
        for (int i = 0; i < 128; ++i) s.push_back ("Const " + std::to_string (i));
        return s;
    }();
    return v;
}

const std::vector<std::string>& modTargets()
{
    static const std::vector<std::string> v = []
    {
        std::vector<std::string> s { "Off" };
        for (int i = 1; i <= 6; ++i) s.push_back ("Pitch OSC" + std::to_string (i));
        for (int i = 1; i <= 6; ++i) s.push_back ("PW OSC" + std::to_string (i));
        s.insert (s.end(), { "CutOff L", "CutOff R", "Volume L", "Volume R" });
        for (int i = 1; i <= 6; ++i) s.push_back ("LFO" + std::to_string (i) + " Depth");
        for (int i = 1; i <= 6; ++i) s.push_back ("LFO" + std::to_string (i) + " Rate");
        for (int i = 1; i <= 8; ++i) s.push_back ("Ext" + std::to_string (i));
        for (int i = 1; i <= 4; ++i) s.push_back ("WT" + std::to_string (i) + " Pos");
        for (int i = 41; i < 256; ++i) s.push_back ("(" + std::to_string (i) + ")");
        return s;
    }();
    return v;
}

const std::vector<std::string>& modOperators()
{
    static const std::vector<std::string> v { "Off", "Src1", "Src2", "1+2", "1-2", "1*2", "XOR", "OR",
                                              "AND", "MIN", "MAX", "1<2", "1>2", "1=2", "S&H", "(15)" };
    return v;
}

const std::vector<std::string>& envLoopPoints()
{
    static const std::vector<std::string> v { "Off", "Attack1", "Attack2", "Decay1", "Decay2",
                                              "Sustain", "Release1", "Release2" };
    return v;
}

const std::vector<std::string>& arpDirections()
{
    static const std::vector<std::string> v { "Up", "Down", "Up&Down", "Down&Up",
                                              "Up&Down 2", "Down&Up 2", "Random", "(7)" };
    return v;
}

const std::vector<std::string>& lfoClockRates()
{
    static const std::vector<std::string> v { "Off", "16 bars", "14 bars", "12 bars", "10 bars", "9 bars",
                                              "8 bars", "7 bars", "6 bars", "5 bars", "4 bars", "3.5 bars",
                                              "3 bars", "2.5 bars", "2 bars", "1.5 bars", "1 bar", "1/2.",
                                              "1/1T", "1/2", "1/4.", "1/2T", "1/4", "1/8.", "1/4T", "1/8",
                                              "1/16.", "1/8T", "1/16", "1/32.", "1/16T", "1/32" };
    return v;
}

const std::vector<std::string>& drumModels()
{
    static const std::vector<std::string> v { "BD1", "BD2", "BD3", "SD1", "SD2", "SD3", "HH1", "HH2", "TOM1", "TOM2",
                                              "CLAP", "FX1", "FX2", "FX3", "FX4", "FX5", "FX6", "FX7", "FX8", "FX9" };
    return v;
}

const std::vector<std::string>& voiceAssignments()
{
    static const std::vector<std::string> v { "L+R", "Left", "Right", "OSC1", "OSC2", "OSC3", "OSC4", "OSC5", "OSC6" };
    return v;
}

std::string noteName (int n)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (n < 0 || n > 127) return "---";
    return std::string (names[n % 12]) + std::to_string (n / 12 - 2);
}

// ---------------------------------------------------------------------------
// Table builders
// ---------------------------------------------------------------------------
namespace
{
    struct Builder
    {
        ParamTable& t;

        void num (std::string id, std::string name, int addr, int bits = 8, int shift = 0, int offset = 0)
        {
            ParamDef d;
            d.id = std::move (id); d.name = std::move (name);
            d.addr = (uint16_t) addr; d.bits = (uint8_t) bits; d.shift = (uint8_t) shift;
            d.kind = ParamDef::Kind::Int; d.offset = offset;
            t.add (std::move (d));
        }
        // signed 8 bit, stored as offset binary (0x80 = 0)
        void s8 (std::string id, std::string name, int addr) { num (std::move (id), std::move (name), addr, 8, 0, 128); }

        void flag (std::string id, std::string name, int addr, int bit)
        {
            ParamDef d;
            d.id = std::move (id); d.name = std::move (name);
            d.addr = (uint16_t) addr; d.bits = 1; d.shift = (uint8_t) bit;
            d.kind = ParamDef::Kind::Bool;
            t.add (std::move (d));
        }

        void choice (std::string id, std::string name, int addr, int bits, int shift, std::vector<std::string> c)
        {
            ParamDef d;
            d.id = std::move (id); d.name = std::move (name);
            d.addr = (uint16_t) addr; d.bits = (uint8_t) bits; d.shift = (uint8_t) shift;
            d.kind = ParamDef::Kind::Choice;
            c.resize ((size_t) 1 << bits);
            for (size_t i = 0; i < c.size(); ++i)
                if (c[i].empty()) c[i] = "(" + std::to_string (i) + ")";
            d.choices = std::move (c);
            t.add (std::move (d));
        }

        void word12 (std::string id, std::string name, int addr, ParamDef::Kind k = ParamDef::Kind::Word12)
        {
            ParamDef d;
            d.id = std::move (id); d.name = std::move (name);
            d.addr = (uint16_t) addr; d.kind = k;
            t.add (std::move (d));
        }
    };

    // 0x000..0x04f and the filters (0x054..0x05f): identical for all engines.
    void addCommon (Builder& b)
    {
        b.choice ("engine", "Engine", 0x10, 2, 0, { "Lead", "Bassline", "Drum", "Multi" });
        b.choice ("hw.sid", "SID Type", 0x11, 2, 0, { "Any", "6581", "6582/8580", "SwinSID" });
        b.flag ("hw.stereo", "Stereo", 0x11, 3);
        b.choice ("hw.caps", "Filter Caps", 0x11, 4, 4,
                  { "470pF", "1nF", "2.2nF", "4.7nF", "6.8nF", "10nF", "22nF", "47nF", "100nF" });
        b.flag ("opt.abw", "ADSR Bug Workaround", 0x12, 0);
        for (int i = 0; i < 8; ++i)
            b.flag ("extsw." + std::to_string (i + 1), "Sw" + std::to_string (i + 1), 0x14, i);

        static const char* knobIds[] = { "k1", "k2", "k3", "k4", "k5", "kV", "kP", "kA" };
        for (int i = 0; i < 8; ++i)
        {
            const int base = 0x18 + i * 5;
            const std::string k = knobIds[i];
            b.num (k + ".par1", "Assign 1", base + 0);
            b.num (k + ".par2", "Assign 2", base + 1);
            b.num (k + ".init", "Init", base + 2);
            b.num (k + ".min", "Min", base + 3);
            b.num (k + ".max", "Max", base + 4);
        }

        for (int i = 0; i < 8; ++i)
            b.word12 ("ext" + std::to_string (i + 1), "Ext " + std::to_string (i + 1), 0x40 + 2 * i, ParamDef::Kind::Ext12);

        for (int f = 0; f < 2; ++f)
        {
            const int base = f == 0 ? 0x54 : 0x5a;
            const std::string p = f == 0 ? "fL" : "fR";
            b.flag (p + ".o1", "OSC1", base, 0);
            b.flag (p + ".o2", "OSC2", base, 1);
            b.flag (p + ".o3", "OSC3", base, 2);
            b.flag (p + ".ext", "Ext In", base, 3);
            b.flag (p + ".lp", "LP", base, 4);
            b.flag (p + ".bp", "BP", base, 5);
            b.flag (p + ".hp", "HP", base, 6);
            b.flag (p + ".o3off", "OSC3 Off", base, 7);
            b.word12 (p + ".cutoff", "CutOff", base + 1);
            b.flag (p + ".fip", "Interpolate", base + 2, 7);
            b.num (p + ".res", "Resonance", base + 3);
            b.num (p + ".keytrack", "Keytrack", base + 4);
        }
    }

    // Voice structure (0x10 bytes): Lead (x6), Bassline OSC1 (x2), Multi (x6).
    void addVoice (Builder& b, const std::string& p, int base)
    {
        b.choice (p + ".portamode", "Porta Mode", base, 2, 0, { "Portamento", "Const. Time", "Glissando", "" });
        b.flag (p + ".gae", "Gate Always", base, 2);

        b.flag (p + ".tri", "Tri", base + 1, 0);
        b.flag (p + ".saw", "Saw", base + 1, 1);
        b.flag (p + ".pulse", "Pulse", base + 1, 2);
        b.flag (p + ".noise", "Noise", base + 1, 3);
        b.flag (p + ".off", "Off", base + 1, 4);
        b.flag (p + ".sync", "Sync", base + 1, 5);
        b.flag (p + ".ring", "Ring", base + 1, 6);

        b.num (p + ".attack", "Attack", base + 2, 4, 4);
        b.num (p + ".decay", "Decay", base + 2, 4, 0);
        b.num (p + ".sustain", "Sustain", base + 3, 4, 4);
        b.num (p + ".release", "Release", base + 3, 4, 0);

        b.word12 (p + ".pw", "Pulsewidth", base + 4);
        b.num (p + ".swphase", "SwinSID Phase", base + 6);
        b.num (p + ".accent", "Accent", base + 6); // same byte, used by the Bassline engine
        b.num (p + ".delay", "Env Delay", base + 7);
        b.num (p + ".transpose", "Transpose", base + 8, 7, 0, 64);
        b.s8 (p + ".finetune", "Finetune", base + 9);
        b.num (p + ".pitchrange", "Pitch Range", base + 10, 7);
        b.num (p + ".porta", "Portamento", base + 11);

        b.flag (p + ".arp.on", "Arp", base + 12, 0);
        b.choice (p + ".arp.dir", "Direction", base + 12, 3, 1, arpDirections());
        b.flag (p + ".arp.sorted", "Sorted", base + 12, 4);
        b.flag (p + ".arp.hold", "Hold", base + 12, 5);
        b.flag (p + ".arp.keysync", "Key Sync", base + 12, 6);
        b.flag (p + ".arp.cac", "Const Cycle", base + 12, 7);
        b.num (p + ".arp.speed", "Speed Div", base + 13, 6);
        b.flag (p + ".arp.easychord", "Easy Chord", base + 13, 6);
        b.flag (p + ".arp.oneshot", "Oneshot", base + 13, 7);
        b.num (p + ".arp.gate", "Gatelength", base + 14, 5);
        b.num (p + ".arp.range", "Octaves", base + 14, 3, 5, -1); // 0..7 shown as 1..8

        b.choice (p + ".sw.wave", "2nd OSC Wave", base + 15, 4, 0, {});
        b.flag (p + ".sw.on", "2nd OSC", base + 15, 4);
        b.flag (p + ".sw.x2", "x2 Pitch", base + 15, 5);
        b.flag (p + ".sw.rev", "Reverse", base + 15, 6);
    }

    // LFO of Bassline and Multi engines (7 bytes).
    void addLfo7 (Builder& b, const std::string& p, int base)
    {
        b.flag (p + ".on", "On", base, 0);
        b.flag (p + ".notesync", "Note Sync", base, 1);
        b.flag (p + ".clksync", "Clock Sync", base, 2);
        b.flag (p + ".oneshot", "Oneshot", base, 3);
        b.choice (p + ".wave", "Waveform", base, 4, 4, lfoWaveforms());
        b.s8 (p + ".depthp", "Pitch", base + 1);
        b.num (p + ".rate", "Rate", base + 2);
        b.num (p + ".delay", "Delay", base + 3);
        b.num (p + ".phase", "Phase", base + 4);
        b.s8 (p + ".depthpw", "PW", base + 5);
        b.s8 (p + ".depthf", "CutOff", base + 6);
    }

    // Envelope of Bassline and Multi engines (9 bytes).
    void addEnv9 (Builder& b, const std::string& p, int base)
    {
        b.flag (p + ".curveatk", "Curve Atk", base, 4);
        b.flag (p + ".curvedec", "Curve Dec", base, 5);
        b.flag (p + ".curverel", "Curve Rel", base, 6);
        b.flag (p + ".clksync", "Clock Sync", base, 7);
        b.s8 (p + ".depthp", "Pitch", base + 1);
        b.s8 (p + ".depthpw", "PW", base + 2);
        b.s8 (p + ".depthf", "CutOff", base + 3);
        b.num (p + ".attack", "Attack", base + 4);
        b.num (p + ".decay", "Decay", base + 5);
        b.num (p + ".sustain", "Sustain", base + 6);
        b.num (p + ".release", "Release", base + 7);
        b.s8 (p + ".curve", "Curve", base + 8);
    }

    void addWaveFlags (Builder& b, const std::string& p, int addr)
    {
        b.flag (p + ".tri", "Tri", addr, 0);
        b.flag (p + ".saw", "Saw", addr, 1);
        b.flag (p + ".pulse", "Pulse", addr, 2);
        b.flag (p + ".noise", "Noise", addr, 3);
        b.flag (p + ".off", "Off", addr, 4);
        b.flag (p + ".sync", "Sync", addr, 5);
        b.flag (p + ".ring", "Ring", addr, 6);
    }

    std::vector<std::string> sequenceChoices()
    {
        return { "Seq 1", "Seq 2", "Seq 3", "Seq 4", "Seq 5", "Seq 6", "Seq 7", "Seq 8",
                 "Off", "Off (9)", "Off (10)", "Off (11)", "Off (12)", "Off (13)", "Off (14)", "Off (15)" };
    }

    void addWavetableData (Builder& b)
    {
        for (int i = 0; i < 128; ++i)
            b.num ("wtdata." + std::to_string (i), "Step " + std::to_string (i), 0x180 + i);
    }

    ParamTable buildBassline()
    {
        ParamTable t;
        Builder b { t };
        addCommon (b);
        b.num ("eng.detune", "Detune", 0x51);
        b.num ("eng.volume", "Volume", 0x52, 7);

        for (int side = 0; side < 2; ++side)
        {
            const int base = side == 0 ? 0x60 : 0xb0;
            const std::string p = side == 0 ? "bL" : "bR";
            addVoice (b, p, base);
            b.choice (p + ".legato", "Play Mode", base + 0x10, 1, 0, { "Mono", "Legato" });
            b.flag (p + ".seqmode", "Sequencer", base + 0x10, 1);
            b.flag (p + ".suskey", "SusKey", base + 0x10, 2);
            b.flag (p + ".oscsync", "OSC Phase Sync", base + 0x10, 3);
            addLfo7 (b, p + ".lfo1", base + 0x14);
            addLfo7 (b, p + ".lfo2", base + 0x1b);
            addEnv9 (b, p + ".env", base + 0x22);
            b.num (p + ".seq.div", "Clock Div", base + 0x2b, 6);
            b.flag (p + ".seq.measure", "Sync to Measure", base + 0x2b, 7);
            b.choice (p + ".seq.num", "Sequence", base + 0x2c, 4, 0, sequenceChoices());
            b.num (p + ".seq.steps", "Steps", base + 0x2d, 4, 0, -1);
            b.num (p + ".seq.par", "Par. Track", base + 0x2e);
            b.num (p + ".accentdecay", "Accent Decay", base + 0x30);

            for (int o = 0; o < 2; ++o)
            {
                const int ob = base + (o == 0 ? 0x40 : 0x48);
                const std::string q = p + (o == 0 ? ".o2" : ".o3");
                addWaveFlags (b, q, ob);
                b.word12 (q + ".pw", "Pulsewidth", ob + 1);
                b.choice (q + ".oct", "Octave", ob + 3, 3, 0, { "0", "+1", "+2", "+3", "-4", "-3", "-2", "-1" });
                b.num (q + ".note", "Static Note", ob + 4, 7);
            }
        }
        return t;
    }

    ParamTable buildDrum()
    {
        ParamTable t;
        Builder b { t };
        addCommon (b);
        b.num ("dr.seq.div", "Clock Div", 0x50, 6);
        b.flag ("dr.seq.on", "Sequencer", 0x50, 6);
        b.flag ("dr.seq.measure", "Sync to Measure", 0x50, 7);
        b.choice ("dr.seq.num", "Sequence", 0x51, 4, 0, sequenceChoices());
        b.num ("eng.volume", "Volume", 0x52, 7);
        b.num ("dr.seq.steps", "Steps", 0x53, 4, 0, -1);

        for (int i = 0; i < 16; ++i)
        {
            const int base = 0x60 + 10 * i;
            const std::string p = "d" + std::to_string (i + 1);
            b.choice (p + ".voice", "Voice", base, 4, 4, voiceAssignments());
            b.choice (p + ".model", "Model", base + 1, 8, 0, drumModels());
            b.num (p + ".attack", "Attack", base + 2, 4, 4);
            b.num (p + ".decay", "Decay", base + 2, 4, 0);
            b.num (p + ".sustain", "Sustain", base + 3, 4, 4);
            b.num (p + ".release", "Release", base + 3, 4, 0);
            b.s8 (p + ".tune", "Tune", base + 4);
            b.s8 (p + ".gatelength", "Gatelength", base + 5);
            b.s8 (p + ".speed", "Speed", base + 6);
            b.s8 (p + ".par3", "Param 3", base + 7);
            b.num (p + ".velpar", "Velocity To", base + 8);
        }
        return t;
    }

    ParamTable buildMulti()
    {
        ParamTable t;
        Builder b { t };
        addCommon (b);
        b.num ("eng.volume", "Volume", 0x52, 7);

        for (int i = 0; i < 6; ++i)
        {
            const int base = 0x60 + 0x30 * i;
            const std::string p = "m" + std::to_string (i + 1);
            addVoice (b, p, base);
            b.choice (p + ".legato", "Play Mode", base + 0x10, 1, 0, { "Mono", "Legato" });
            b.flag (p + ".wto", "WT Only", base + 0x10, 1);
            b.flag (p + ".suskey", "SusKey", base + 0x10, 2);
            b.flag (p + ".poly", "Poly", base + 0x10, 3);
            b.choice (p + ".voice", "Voice", base + 0x11, 4, 0, voiceAssignments());
            b.num (p + ".velpar", "Velocity To", base + 0x12);
            b.num (p + ".pbpar", "PitchBend To", base + 0x13);
            addLfo7 (b, p + ".lfo1", base + 0x14);
            addLfo7 (b, p + ".lfo2", base + 0x1b);
            addEnv9 (b, p + ".env", base + 0x22);
            b.num (p + ".wt.div", "Clock Div", base + 0x2b, 6);
            b.num (p + ".wt.par", "Parameter", base + 0x2c);
            b.num (p + ".wt.start", "Start", base + 0x2d, 7);
            b.flag (p + ".wt.w16", "W+16", base + 0x2d, 7);
            b.num (p + ".wt.end", "End", base + 0x2e, 7);
            b.num (p + ".wt.loop", "Loop", base + 0x2f, 7);
            b.flag (p + ".wt.oneshot", "Oneshot", base + 0x2f, 7);
        }
        addWavetableData (b);
        return t;
    }

    ParamTable buildEnsemble()
    {
        ParamTable t;
        Builder b { t };
        for (int s = 0; s < 4; ++s)
        {
            const int base = 0x40 * s;
            const std::string p = "s" + std::to_string (s + 1);
            b.choice (p + ".bank", "Bank", base, 3, 0, { "A", "B", "C", "D", "E", "F", "G", "H" });
            b.num (p + ".patch", "Patch", base + 1, 7, 0, -1);
            b.choice (p + ".clock", "Clock", base + 2, 2, 0, { "Master", "Slave", "Auto", "" });
            b.flag (p + ".clkout", "Clock Out", base + 2, 3);
            b.flag (p + ".fillog", "FilLog", base + 2, 4);
            b.flag (p + ".mono", "Mono", base + 2, 6);
            b.flag (p + ".dor", "No OSC Reset", base + 2, 7);
            b.flag (p + ".f2a", "F2A", base + 3, 0);
            b.flag (p + ".v2a", "V2A", base + 3, 1);
            b.flag (p + ".p2a", "P2A", base + 3, 2);
            b.flag (p + ".k2a", "K2A", base + 3, 3);
            b.flag (p + ".o2a", "O2A", base + 3, 4);
            b.num (p + ".bpm", "BPM", base + 5);
            b.num (p + ".superpoly", "SuperPoly", base + 6, 3);
            for (int a = 0; a < 8; ++a)
                b.flag (p + ".ainv." + std::to_string (a + 1), "Inv" + std::to_string (a + 1), base + 7, a);
            b.word12 (p + ".cal.lmin", "Left Min", base + 8, ParamDef::Kind::Ext12);
            b.word12 (p + ".cal.lmax", "Left Max", base + 10, ParamDef::Kind::Ext12);
            b.word12 (p + ".cal.rmin", "Right Min", base + 12, ParamDef::Kind::Ext12);
            b.word12 (p + ".cal.rmax", "Right Max", base + 14, ParamDef::Kind::Ext12);
            for (int i = 0; i < 6; ++i)
            {
                const int ib = base + 0x10 + 8 * i;
                const std::string q = p + ".i" + std::to_string (i + 1);
                b.num (q + ".chn", "Channel", ib, 4, 0, -1);
                b.flag (q + ".off", "Disabled", ib, 4);
                b.num (q + ".splitlo", "Split Low", ib + 1, 7);
                b.num (q + ".splithi", "Split High", ib + 2, 7);
                b.num (q + ".transpose", "Transpose", ib + 3, 7, 0, 64);
            }
        }
        return t;
    }

    ParamTable buildLead()
    {
        ParamTable t;
        Builder b { t };
        addCommon (b);

        b.choice ("lead.legato", "Play Mode", 0x50, 1, 0, { "Mono", "Legato" });
        b.flag ("lead.wto", "WT Only", 0x50, 1);
        b.flag ("lead.suskey", "SusKey", 0x50, 2);
        b.num ("lead.detune", "Detune", 0x51);
        b.num ("lead.volume", "Volume", 0x52, 7);
        b.num ("lead.phase", "Phase Offset", 0x53);

        for (int v = 0; v < 6; ++v)
            addVoice (b, "v" + std::to_string (v + 1), 0x60 + 0x10 * v);

        for (int l = 0; l < 6; ++l)
        {
            const int base = 0xc0 + 5 * l;
            const std::string p = "lfo" + std::to_string (l + 1);
            b.flag (p + ".on", "On", base, 0);
            b.flag (p + ".clksync", "Clock Sync", base, 2);
            b.flag (p + ".oneshot", "Oneshot", base, 3);
            b.choice (p + ".wave", "Waveform", base, 4, 4, lfoWaveforms());
            b.s8 (p + ".depth", "Depth", base + 1);
            b.num (p + ".rate", "Rate", base + 2);
            b.num (p + ".delay", "Delay", base + 3);
            b.num (p + ".phase", "Phase", base + 4);
        }

        for (int e = 0; e < 2; ++e)
        {
            const int base = 0xe0 + 0x10 * e;
            const std::string p = "env" + std::to_string (e + 1);
            b.choice (p + ".loopbegin", "Loop Begin", base, 3, 0, envLoopPoints());
            b.choice (p + ".loopend", "Loop End", base, 3, 4, envLoopPoints());
            b.flag (p + ".clksync", "Clock Sync", base, 7);
            b.s8 (p + ".depth", "Depth", base + 1);
            b.num (p + ".delay", "Delay", base + 2);
            b.num (p + ".attack1", "Attack 1", base + 3);
            b.num (p + ".attacklvl", "Attack Lvl", base + 4);
            b.num (p + ".attack2", "Attack 2", base + 5);
            b.num (p + ".decay1", "Decay 1", base + 6);
            b.num (p + ".decaylvl", "Decay Lvl", base + 7);
            b.num (p + ".decay2", "Decay 2", base + 8);
            b.num (p + ".sustain", "Sustain", base + 9);
            b.num (p + ".release1", "Release 1", base + 10);
            b.num (p + ".releaselvl", "Release Lvl", base + 11);
            b.num (p + ".release2", "Release 2", base + 12);
            b.s8 (p + ".acurve", "Attack Curve", base + 13);
            b.s8 (p + ".dcurve", "Decay Curve", base + 14);
            b.s8 (p + ".rcurve", "Release Curve", base + 15);
        }

        static const char* directTargets[] = { "OSC1 Pitch", "OSC2 Pitch", "OSC3 Pitch", "OSC1 PW",
                                                "OSC2 PW", "OSC3 PW", "CutOff", "Volume" };
        for (int m = 0; m < 8; ++m)
        {
            const int base = 0x100 + 8 * m;
            const std::string p = "mp" + std::to_string (m + 1);
            b.choice (p + ".src1", "Source 1", base, 8, 0, modSources());
            b.choice (p + ".src2", "Source 2", base + 1, 8, 0, modSources());
            b.choice (p + ".op", "Operator", base + 2, 4, 0, modOperators());
            b.flag (p + ".inv1", "Invert 1/L", base + 2, 6);
            b.flag (p + ".inv2", "Invert 2/R", base + 2, 7);
            b.s8 (p + ".depth", "Depth", base + 3);
            for (int i = 0; i < 8; ++i)
            {
                b.flag (p + ".l" + std::to_string (i), directTargets[i], base + 4, i);
                b.flag (p + ".r" + std::to_string (i), directTargets[i], base + 5, i);
            }
            b.choice (p + ".tgt1", "Target 1", base + 6, 8, 0, modTargets());
            b.choice (p + ".tgt2", "Target 2", base + 7, 8, 0, modTargets());
        }

        // Trigger matrix: 14 source nodes x 24 destinations
        for (int n = 0; n < 14; ++n)
            for (int bit = 0; bit < 24; ++bit)
                b.flag ("trg." + std::to_string (n) + "." + std::to_string (bit), "",
                        0x140 + 3 * n + bit / 8, bit % 8);

        for (int w = 0; w < 4; ++w)
        {
            const int base = 0x16c + 5 * w;
            const std::string p = "wt" + std::to_string (w + 1);
            b.num (p + ".div", "Clock Div", base, 6);
            b.flag (p + ".left", "Left", base, 6);
            b.flag (p + ".right", "Right", base, 7);
            b.num (p + ".par", "Parameter", base + 1);
            b.num (p + ".start", "Start", base + 2, 7);
            b.flag (p + ".startmod", "Pos by Mod", base + 2, 7);
            b.num (p + ".end", "End", base + 3, 7);
            b.flag (p + ".endkey", "Pos by Key", base + 3, 7);
            b.num (p + ".loop", "Loop", base + 4, 7);
            b.flag (p + ".oneshot", "Oneshot", base + 4, 7);
        }

        addWavetableData (b);
        return t;
    }
} // namespace

const ParamTable& leadParams()     { static const ParamTable t = buildLead();     return t; }
const ParamTable& basslineParams() { static const ParamTable t = buildBassline(); return t; }
const ParamTable& drumParams()     { static const ParamTable t = buildDrum();     return t; }
const ParamTable& multiParams()    { static const ParamTable t = buildMulti();    return t; }
const ParamTable& ensembleParams() { static const ParamTable t = buildEnsemble(); return t; }

const ParamTable& paramsFor (Engine e)
{
    switch (e)
    {
        case Engine::Bassline: return basslineParams();
        case Engine::Drum:     return drumParams();
        case Engine::Multi:    return multiParams();
        default:               return leadParams();
    }
}

} // namespace sid
