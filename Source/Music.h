#pragma once
#include <array>
#include <vector>

// Key, scale and harmony helpers. All notes are MIDI note numbers.
namespace music
{
    inline int clampIdx (int v, int hi) { return v < 0 ? 0 : (v > hi ? hi : v); }

    inline const char* keyNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    struct Scale { const char* name; std::vector<int> steps; };

    inline const std::vector<Scale>& scales()
    {
        static const std::vector<Scale> s {
            { "Chromatic",        { 0,1,2,3,4,5,6,7,8,9,10,11 } },
            { "Major",            { 0,2,4,5,7,9,11 } },
            { "Natural minor",    { 0,2,3,5,7,8,10 } },
            { "Dorian",           { 0,2,3,5,7,9,10 } },
            { "Phrygian",         { 0,1,3,5,7,8,10 } },
            { "Lydian",           { 0,2,4,6,7,9,11 } },
            { "Mixolydian",       { 0,2,4,5,7,9,10 } },
            { "Harmonic minor",   { 0,2,3,5,7,8,11 } },
            { "Major pentatonic", { 0,2,4,7,9 } },
            { "Minor pentatonic", { 0,3,5,7,10 } },
            { "Hirajoshi",        { 0,2,3,7,8 } },
            { "Whole tone",       { 0,2,4,6,8,10 } } };
        return s;
    }

    inline const char* harmonyNames[9] = { "Single", "Octaves", "Fifths", "Triad", "Seventh", "Sus2", "Sus4", "Ninth", "Open" };

    inline int floorDiv (int a, int b) { int q = a / b; if ((a % b) != 0 && ((a < 0) != (b < 0))) --q; return q; }
    inline int mod (int a, int b) { int r = a % b; return r < 0 ? r + b : r; }

    inline bool inScale (int note, int root, const std::vector<int>& st)
    {
        int pc = mod (note - root, 12);
        for (int s : st) if (s == pc) return true;
        return false;
    }

    // Nearest note in the scale; a tie goes down.
    inline int quantize (int note, int root, const std::vector<int>& st)
    {
        for (int d = 0; d < 12; ++d)
        {
            if (inScale (note - d, root, st)) return note - d;
            if (inScale (note + d, root, st)) return note + d;
        }
        return note;
    }

    // Fills out[] with the chord tones for a played note. Returns the number of tones (1..5).
    inline int chord (int note, int root, int scaleIndex, int harmony, bool doQuantize, int* out)
    {
        const auto& sc = scales();
        const auto& st = sc[(size_t) clampIdx (scaleIndex, (int) sc.size() - 1)].steps;
        const int base = doQuantize ? quantize (note, root, st) : note;

        static const std::vector<int> stepOffsets[9] = { {0}, {0}, {0,4}, {0,2,4}, {0,2,4,6}, {0,1,4}, {0,3,4}, {0,2,4,6,8}, {0,4,9} };
        static const std::vector<int> semiOffsets[9] = { {0}, {0,12}, {0,7}, {0,4,7}, {0,4,7,10}, {0,2,7}, {0,5,7}, {0,4,7,10,14}, {0,7,16} };

        const int h = clampIdx (harmony, 8);
        const bool diatonic = st.size() == 7 && h != 1 && inScale (base, root, st);

        int n = 0;
        if (diatonic)
        {
            const int len = (int) st.size();
            const int rel = base - root;
            const int oct = floorDiv (rel, 12);
            const int pc  = rel - oct * 12;
            int idx = 0;
            for (int i = 0; i < len; ++i) if (st[(size_t) i] == pc) idx = i;
            const int deg = oct * len + idx;
            for (int off : stepOffsets[h])
            {
                const int d = deg + off;
                out[n++] = root + floorDiv (d, len) * 12 + st[(size_t) mod (d, len)];
            }
        }
        else
        {
            for (int off : semiOffsets[h])
            {
                int t = base + off;
                if (doQuantize && off != 12) t = quantize (t, root, st);
                out[n++] = t;
            }
        }
        return n;
    }

}
