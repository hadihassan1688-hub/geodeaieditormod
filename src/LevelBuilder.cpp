#include "LevelBuilder.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <set>

namespace lb {

namespace {

// ---- object ids -----------------------------------------------------------
constexpr int ID_BLOCK = 1;
constexpr int ID_SPIKE = 8;
constexpr int ID_GRAVITY_NORMAL = 10;
constexpr int ID_YELLOW_ORB = 36;
constexpr int ID_SAW_SMALL = 98;
constexpr int ID_COLOR_TRIGGER = 899;
constexpr int ID_PULSE_TRIGGER = 1006;

constexpr int MODE_PORTAL[] = {12, 13, 47, 111, 660, 745, 1331, 1933};
constexpr int SPEED_PORTAL[] = {200, 201, 202, 203, 1334};
constexpr float SPEED_BPS[] = {8.37f, 10.39f, 12.91f, 15.6f, 19.2f};

// colour channels
constexpr int CH_BLOCK = 1;
constexpr int CH_HAZARD = 2;
constexpr int CH_BG = 1000;
constexpr int CH_GROUND = 1001;
constexpr int CH_GROUND2 = 1009;

constexpr int CORRIDOR_TOP = 10;  // flying modes are 10 blocks tall
constexpr int TRANSITION_CEIL = 4; // every section starts and ends inside y 0..4

bool isCorridor(Mode m) {
    return m != Mode::Cube && m != Mode::Robot;
}

std::string num(float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", v);
    std::string s = buf;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s == "-0") s = "0";
    return s;
}

struct Gen {
    std::mt19937 rng;
    Theme theme;
    std::vector<Obj> out;

    int ri(int a, int b) {
        if (b < a) b = a;
        return std::uniform_int_distribution<int>(a, b)(rng);
    }
    float rf() { return std::uniform_real_distribution<float>(0.f, 1.f)(rng); }
    bool chance(float p) { return rf() < p; }

    Obj& add(int id, float bx, float by) {
        Obj o;
        o.id = id;
        o.x = bx * BLOCK;
        o.y = by * BLOCK;
        out.push_back(std::move(o));
        return out.back();
    }
    void block(int gx, int gy) {
        add(ID_BLOCK, gx + 0.5f, gy + 0.5f).color = CH_BLOCK;
    }
    void spike(int gx, int gy, bool ceiling = false) {
        Obj& o = add(ID_SPIKE, gx + 0.5f, gy + 0.5f);
        o.color = CH_HAZARD;
        o.flipY = ceiling;
    }
    void colorTrigger(float bx, float by, int channel, Color c, float duration) {
        Obj& o = add(ID_COLOR_TRIGGER, bx, by);
        o.extra = {
            {7, std::to_string(c.r)}, {8, std::to_string(c.g)}, {9, std::to_string(c.b)},
            {10, num(duration)}, {35, "1"}, {23, std::to_string(channel)},
        };
    }
    void pulseTrigger(float bx, float by, int channel, Color c) {
        Obj& o = add(ID_PULSE_TRIGGER, bx, by);
        o.extra = {
            {7, std::to_string(c.r)}, {8, std::to_string(c.g)}, {9, std::to_string(c.b)},
            {45, "0.05"}, {46, "0.05"}, {47, "0.4"}, {48, "0"},
            {51, std::to_string(channel)}, {52, "0"},
        };
    }

    // Fills the floor (0..top-1) and ceiling (bot..9) of a corridor as a thin
    // skin that is thick enough to close every step between columns.
    void corridorSkin(int x0, std::vector<int> const& top, std::vector<int> const& bot) {
        int n = (int)top.size();
        for (int i = 0; i < n; i++) {
            bool cap = i == 0 || i == n - 1;
            int lowT = top[i], highB = bot[i];
            if (i > 0) { lowT = std::min(lowT, top[i - 1]); highB = std::max(highB, bot[i - 1]); }
            if (i + 1 < n) { lowT = std::min(lowT, top[i + 1]); highB = std::max(highB, bot[i + 1]); }
            int from = cap ? 0 : std::max(0, lowT - 1);
            for (int gy = from; gy < top[i]; gy++) block(x0 + i, gy);
            int to = cap ? CORRIDOR_TOP - 1 : std::min(CORRIDOR_TOP - 1, highB);
            for (int gy = bot[i]; gy <= to; gy++) block(x0 + i, gy);
        }
    }

    // ---- sections ---------------------------------------------------------

    void cubeSection(int x0, int n, Section const& s) {
        int d = s.difficulty;
        float jump = SPEED_BPS[s.speed] * 0.42f; // blocks covered by one jump
        int maxSpikes = std::clamp((int)(jump - 1.3f), 1, 6);
        int cap = d >= 8 ? maxSpikes : d >= 5 ? std::max(1, maxSpikes - 1) : d >= 3 ? std::min(2, maxSpikes) : 1;
        int gap = std::max(2, (int)std::lround(jump * (0.6f + (10 - d) * 0.22f)) + 1);
        int stepW = std::max(2, (int)std::ceil(jump * 0.5f));
        int orbLen = std::clamp((int)(1.5f * jump - 2.f), maxSpikes + 1, 9);

        int pos = 3;
        while (true) {
            int roll = ri(0, 99);
            int kind = 0; // spikes
            if (d >= 6 && roll < 15) kind = 3;      // orb gap
            else if (d >= 4 && roll < 35) kind = 2; // stairs
            else if (d >= 3 && roll < 60) kind = 1; // block + spikes

            int steps = ri(2, 3);
            int need = kind == 3 ? orbLen : kind == 2 ? steps * stepW + stepW : 8;
            if (pos + need + 3 > n) break;

            int gx = x0 + pos;
            if (kind == 0) {
                int k = ri(1, cap);
                for (int i = 0; i < k; i++) spike(gx + i, 0);
                pos += k;
            } else if (kind == 1) {
                int w = ri(1, 3);
                int k = std::max(1, ri(1, cap) - 1);
                for (int i = 0; i < w; i++) block(gx + i, 0);
                for (int i = 0; i < k; i++) spike(gx + w + i, 0);
                pos += w + k;
            } else if (kind == 2) {
                for (int st = 0; st < steps; st++)
                    for (int i = 0; i < stepW; i++)
                        for (int gy = 0; gy <= st; gy++) block(gx + st * stepW + i, gy);
                pos += steps * stepW + stepW;
            } else {
                for (int i = 0; i < orbLen; i++) spike(gx + i, 0);
                add(ID_YELLOW_ORB, gx + 0.5f * jump - 0.2f, 2.3f);
                pos += orbLen;
            }
            pos += gap + ri(0, (10 - d) / 2);
        }
    }

    void shipSection(int x0, int n, Section const& s, bool swing) {
        int d = s.difficulty;
        float maxSlope = 0.35f + 0.04f * d;
        int tail = (int)std::ceil(8.f / maxSlope) + 4;
        float gapTarget = std::clamp(std::round(7.2f - 0.42f * d), 3.f, 7.f) + (swing ? 1.f : 0.f);

        std::vector<int> top(n), bot(n);
        float c = 2, h = 4, target = 2;
        int hold = 0;
        for (int i = 0; i < n; i++) {
            bool entry = i < 3, leaving = i >= n - tail;
            float wantC = target, wantH = gapTarget;
            if (entry || leaving) { wantC = 2; wantH = 4; }
            else if (hold-- <= 0) {
                float lo = gapTarget / 2 + 0.5f, hi = CORRIDOR_TOP - gapTarget / 2 - 0.5f;
                target = lo + rf() * std::max(0.f, hi - lo);
                hold = ri(4, 10);
                wantC = target;
            }
            c += std::clamp(wantC - c, -maxSlope, maxSlope);
            h += std::clamp(wantH - h, -0.5f, 0.5f);
            int t = std::max(0, (int)std::lround(c - h / 2));
            int b = std::min(CORRIDOR_TOP, t + (int)std::lround(h));
            top[i] = t;
            bot[i] = b;
        }
        // the last columns are exactly the 0..4 exit window
        for (int i = std::max(0, n - 3); i < n; i++) { top[i] = 0; bot[i] = TRANSITION_CEIL; }
        corridorSkin(x0, top, bot);

        int sinceFloat = 0;
        for (int i = 5; i < n - 5; i++) {
            int gapH = bot[i] - top[i];
            bool flat = top[i - 1] == top[i] && top[i + 1] == top[i] && bot[i - 1] == bot[i] && bot[i + 1] == bot[i];
            sinceFloat++;
            if (!flat) continue;
            if (gapH >= 6 && sinceFloat > 7 && chance(0.04f + 0.012f * d)) {
                int mid = (top[i] + bot[i]) / 2;
                if (theme.saws) add(ID_SAW_SMALL, x0 + i + 0.5f, (float)mid).color = CH_HAZARD;
                else block(x0 + i, mid);
                sinceFloat = 0;
            } else if (gapH >= 4 && chance(0.025f * d)) {
                if (chance(0.5f)) spike(x0 + i, top[i]);
                else spike(x0 + i, bot[i] - 1, true);
            }
        }
    }

    void ufoSection(int x0, int n, Section const& s) {
        int d = s.difficulty;
        float jump = SPEED_BPS[s.speed] * 0.42f;
        int spacing = std::max(5, (int)std::lround(jump * 1.5f + (10 - d) * 0.4f));
        int gate = std::clamp((int)std::lround(6.f - 0.3f * d), 3, 6);

        auto pillar = [&](int gx, int from, int to) {
            for (int gy = 0; gy < from; gy++) block(gx, gy);
            for (int gy = to; gy < CORRIDOR_TOP; gy++) block(gx, gy);
        };
        pillar(x0, 0, TRANSITION_CEIL);
        pillar(x0 + n - 1, 0, TRANSITION_CEIL);

        int count = (n - 2 * spacing) / spacing;
        int bottom = 0;
        for (int k = 0; k < count; k++) {
            int left = count - 1 - k; // gates still to come after this one
            int hi = std::min(CORRIDOR_TOP - gate, 1 + left * 3);
            bottom = std::clamp(bottom + ri(-3, 3), 0, std::max(0, hi));
            int gx = x0 + spacing * (k + 1);
            pillar(gx, bottom, bottom + gate);
            if (theme.saws && gate >= 5 && chance(0.3f))
                add(ID_SAW_SMALL, gx + 0.5f + spacing / 2, bottom + gate / 2.f).color = CH_HAZARD;
        }
    }

    void waveSection(int x0, int n, Section const& s) {
        int d = s.difficulty;
        float halfTarget = std::clamp(3.4f - 0.23f * d, 1.2f, 3.5f);
        int segLo = d >= 7 ? 2 : 4;
        int segHi = std::max(segLo + 1, 12 - d);

        std::vector<int> top(n), bot(n);
        float c = 2, w = 2;
        int dir = 1, seg = ri(segLo, segHi);
        bool returning = false;
        for (int i = 0; i < n; i++) {
            int remaining = n - 1 - i;
            if (i >= 3 && !returning && remaining <= (int)(c - 2) + 6) returning = true;
            if (i < 3 || remaining < 3) {
                c = 2; w = 2;
            } else if (returning) {
                w += std::clamp(2.f - w, -0.25f, 0.25f);
                if (c > 2) c -= 1;
            } else {
                w += std::clamp(halfTarget - w, -0.25f, 0.25f);
                if (--seg <= 0) { dir = -dir; seg = ri(segLo, segHi); }
                if (c + dir + w + 1 > CORRIDOR_TOP) dir = -1;
                if (c + dir - w - 1 < 0) dir = 1;
                c += dir;
            }
            top[i] = std::max(0, (int)std::floor(c - w));
            bot[i] = std::min(CORRIDOR_TOP, (int)std::ceil(c + w));
        }
        corridorSkin(x0, top, bot);
    }

    void ballSection(int x0, int n, Section const& s) {
        int d = s.difficulty;
        for (int i = 0; i < n; i++) block(x0 + i, TRANSITION_CEIL);
        int safe = (int)std::ceil(SPEED_BPS[s.speed] * 0.32f) + std::max(1, (10 - d) / 2);
        int pos = 3;
        bool onFloor = true;
        while (true) {
            int z = ri(2, 2 + d / 2);
            if (pos + safe + z + safe > n) break;
            pos += safe;
            for (int i = 0; i < z; i++) {
                if (onFloor) spike(x0 + pos + i, 0);
                else spike(x0 + pos + i, TRANSITION_CEIL - 1, true);
            }
            pos += z;
            onFloor = !onFloor;
        }
    }

    void section(int x0, int n, Section const& s) {
        switch (s.mode) {
            case Mode::Cube:
            case Mode::Robot: cubeSection(x0, n, s); break;
            case Mode::Ship: shipSection(x0, n, s, false); break;
            case Mode::Swing: shipSection(x0, n, s, true); break;
            case Mode::Ufo: ufoSection(x0, n, s); break;
            case Mode::Wave: waveSection(x0, n, s); break;
            case Mode::Ball:
            case Mode::Spider: ballSection(x0, n, s); break;
        }
    }

    // Portals between two sections. Returns how many blocks it used.
    int transition(int x, bool first, Mode prev, int prevSpeed, Section const& next) {
        bool prevFly = !first && isCorridor(prev);
        bool nextFly = isCorridor(next.mode);
        bool changed = first || prev != next.mode || prevSpeed != next.speed;
        if (!changed && !prevFly) return 2;

        const int len = 6;
        if (prevFly || nextFly) {
            // keep the player inside y 0..4 so the portals cannot be skipped
            for (int i = prevFly ? 0 : 3; i < len; i++) block(x + i, TRANSITION_CEIL);
        }
        const float portalY = 2.f;
        if (first || prevFly) add(ID_GRAVITY_NORMAL, x + 1.f, portalY);
        if (first || prev != next.mode) add(MODE_PORTAL[(int)next.mode], x + 2.5f, portalY);
        if (first || prevSpeed != next.speed) add(SPEED_PORTAL[next.speed], x + 4.f, portalY);
        return len;
    }
};

Color scaled(Color c, float f) {
    return Color{(int)(c.r * f), (int)(c.g * f), (int)(c.b * f)};
}

// Rotates the colour channels, used to change the hue between sections.
Color rotated(Color c, int k) {
    switch (((k % 3) + 3) % 3) {
        case 1: return Color{c.b, c.r, c.g};
        case 2: return Color{c.g, c.b, c.r};
        default: return c;
    }
}

// Safety pass: one block per cell, and no spike buried inside a block.
void sanitize(std::vector<Obj>& objs) {
    auto key = [](Obj const& o) {
        long long cx = (long long)std::floor(o.x / BLOCK), cy = (long long)std::floor(o.y / BLOCK);
        return cx * 100000 + cy;
    };
    std::set<long long> blocks;
    std::vector<Obj> kept;
    kept.reserve(objs.size());
    for (auto& o : objs) {
        if (o.id == ID_BLOCK && !blocks.insert(key(o)).second) continue;
        kept.push_back(std::move(o));
    }
    objs.clear();
    for (auto& o : kept) {
        if (o.id == ID_SPIKE && blocks.count(key(o))) continue;
        objs.push_back(std::move(o));
    }
}

std::string lower(std::string s) {
    for (auto& ch : s) ch = (char)std::tolower((unsigned char)ch);
    return s;
}

} // namespace

float blocksPerSecond(int speed) {
    return SPEED_BPS[std::clamp(speed, 0, 4)];
}

char const* styleName(Style s) {
    switch (s) {
        case Style::Classic: return "Classic";
        case Style::HellDemon: return "Hell Demon";
        case Style::NeonMix: return "Neon Mix";
        case Style::WaveRush: return "Wave Rush";
        case Style::SkyFlight: return "Sky Flight";
        case Style::Toxic: return "Toxic";
        case Style::Space: return "Space";
        case Style::Sunset: return "Sunset";
        case Style::Ice: return "Ice";
        case Style::Candy: return "Candy";
    }
    return "Classic";
}

char const* modeName(Mode m) {
    constexpr char const* names[] = {"cube", "ship", "ball", "ufo", "wave", "robot", "spider", "swing"};
    return names[(int)m];
}

bool parseMode(std::string const& name, Mode& out) {
    std::string n = lower(name);
    for (int i = 0; i < 8; i++) {
        if (n == modeName((Mode)i)) { out = (Mode)i; return true; }
    }
    return false;
}

bool parseHexColor(std::string const& hex, Color& out) {
    std::string h = hex;
    if (!h.empty() && h[0] == '#') h.erase(0, 1);
    if (h.size() != 6) return false;
    for (char ch : h) if (!std::isxdigit((unsigned char)ch)) return false;
    long v = std::strtol(h.c_str(), nullptr, 16);
    out = Color{(int)((v >> 16) & 255), (int)((v >> 8) & 255), (int)(v & 255)};
    return true;
}

std::string serialize(std::vector<Obj> const& objs) {
    std::string s;
    s.reserve(objs.size() * 28);
    for (auto const& o : objs) {
        s += "1," + std::to_string(o.id) + ",2," + num(o.x) + ",3," + num(o.y);
        if (o.flipX) s += ",4,1";
        if (o.flipY) s += ",5,1";
        if (o.rot != 0) s += ",6," + num(o.rot);
        if (o.scale != 1 && o.scale > 0) s += ",32," + num(o.scale);
        if (o.color > 0) s += ",21," + std::to_string(o.color);
        for (auto const& [k, v] : o.extra) s += "," + std::to_string(k) + "," + v;
        s += ";";
    }
    return s;
}

Theme presetTheme(Style style) {
    Theme t;
    switch (style) {
        case Style::Classic:
            break;
        case Style::HellDemon:
            t.bg = {45, 0, 0}; t.ground = {25, 0, 0};
            t.block = {10, 10, 10}; t.hazard = {255, 40, 40};
            t.pulses = true; t.saws = true;
            break;
        case Style::NeonMix:
            t.bg = {20, 0, 60}; t.ground = {60, 0, 140};
            t.block = {0, 255, 220}; t.hazard = {255, 0, 200};
            t.pulses = true; t.cycles = true;
            break;
        case Style::WaveRush:
            t.bg = {0, 30, 60}; t.ground = {0, 60, 110};
            t.block = {120, 220, 255}; t.hazard = {255, 255, 255};
            break;
        case Style::SkyFlight:
            t.bg = {90, 170, 255}; t.ground = {40, 110, 210};
            t.block = {255, 255, 255}; t.hazard = {255, 220, 120};
            break;
        case Style::Toxic:
            t.bg = {10, 40, 5}; t.ground = {20, 70, 10};
            t.block = {120, 255, 40}; t.hazard = {230, 255, 0};
            t.pulses = true; t.saws = true;
            break;
        case Style::Space:
            t.bg = {8, 5, 30}; t.ground = {25, 15, 70};
            t.block = {170, 170, 255}; t.hazard = {255, 120, 255};
            t.pulses = true;
            break;
        case Style::Sunset:
            t.bg = {255, 110, 40}; t.ground = {120, 30, 60};
            t.block = {255, 235, 190}; t.hazard = {90, 10, 60};
            break;
        case Style::Ice:
            t.bg = {170, 225, 255}; t.ground = {80, 150, 215};
            t.block = {255, 255, 255}; t.hazard = {40, 90, 190};
            break;
        case Style::Candy:
            t.bg = {255, 140, 200}; t.ground = {200, 80, 160};
            t.block = {255, 255, 255}; t.hazard = {120, 60, 255};
            t.pulses = true; t.cycles = true;
            break;
    }
    return t;
}

std::vector<Section> presetSections(Settings const& s) {
    // weights: cube, ship, ball, ufo, wave, robot, spider, swing
    static const int weights[STYLE_COUNT][8] = {
        {5, 3, 2, 1, 1, 1, 0, 0},
        {2, 3, 1, 2, 5, 0, 1, 0},
        {2, 2, 2, 2, 2, 2, 2, 2},
        {1, 1, 0, 0, 8, 0, 0, 0},
        {1, 5, 0, 3, 1, 0, 0, 3},
        {3, 2, 2, 1, 3, 1, 1, 0},
        {1, 4, 0, 4, 2, 0, 0, 2},
        {4, 3, 1, 1, 1, 1, 0, 1},
        {3, 2, 1, 1, 2, 1, 1, 0},
        {3, 1, 3, 2, 1, 2, 1, 1},
    };
    std::mt19937 rng(s.seed * 2654435761u + 17);
    auto ri = [&](int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); };

    int d = std::clamp(s.difficulty, 1, 10);
    int speedLo = d <= 6 ? 1 : 2;
    int speedHi = d <= 2 ? 1 : d <= 4 ? 2 : d <= 8 ? 3 : 4;
    auto const& w = weights[(int)s.style];
    int total = 0;
    for (int v : w) total += v;

    std::vector<Section> out;
    float left = std::clamp(s.seconds, 5.f, 600.f);
    int prev = -1;
    while (left > 1.f) {
        int mode = 0;
        for (int attempt = 0; attempt < 4; attempt++) {
            int r = ri(0, total - 1);
            for (mode = 0; mode < 8; mode++) {
                if (r < w[mode]) break;
                r -= w[mode];
            }
            if (mode != prev) break;
        }
        Section sec;
        sec.mode = (Mode)mode;
        sec.speed = ri(speedLo, speedHi);
        sec.difficulty = std::clamp(d + ri(-1, 1), 1, 10);
        sec.seconds = std::min(left, (float)ri(5, 9));
        if (left - sec.seconds < 4.f) sec.seconds = left;
        left -= sec.seconds;
        prev = mode;
        out.push_back(sec);
    }
    return out;
}

namespace {

// Whole-word match (an optional plural "s" is allowed), so "ice" does not match "service".
bool hasWord(std::string const& text, std::string const& word) {
    size_t pos = 0;
    while ((pos = text.find(word, pos)) != std::string::npos) {
        size_t end = pos + word.size();
        bool startOk = pos == 0 || !std::isalnum((unsigned char)text[pos - 1]);
        bool endOk = end == text.size() || !std::isalnum((unsigned char)text[end]) ||
            (text[end] == 's' && (end + 1 == text.size() || !std::isalnum((unsigned char)text[end + 1])));
        if (startOk && endOk) return true;
        pos = end;
    }
    return false;
}

struct Tok {
    std::string w;
    bool num = false;
    double v = 0;
};

std::vector<Tok> tokenize(std::string const& p) {
    std::vector<Tok> out;
    size_t i = 0;
    while (i < p.size()) {
        unsigned char c = p[i];
        if (std::isdigit(c)) {
            size_t j = i;
            while (j < p.size() && (std::isdigit((unsigned char)p[j]) ||
                   (p[j] == '.' && j + 1 < p.size() && std::isdigit((unsigned char)p[j + 1])))) j++;
            Tok t;
            t.num = true;
            t.w = p.substr(i, j - i);
            t.v = std::atof(t.w.c_str());
            out.push_back(std::move(t));
            i = j;
        } else if (std::isalpha(c)) {
            size_t j = i;
            while (j < p.size() && std::isalpha((unsigned char)p[j])) j++;
            Tok t;
            t.w = p.substr(i, j - i);
            out.push_back(std::move(t));
            i = j;
        } else {
            i++;
        }
    }
    return out;
}

bool modeFromWord(std::string w, Mode& out) {
    if (w.size() > 3 && w.back() == 's') w.pop_back();
    if (w == "cube" || w == "cubes") { out = Mode::Cube; return true; }
    if (w == "ship" || w == "rocket") { out = Mode::Ship; return true; }
    if (w == "ball" || w == "roll") { out = Mode::Ball; return true; }
    if (w == "ufo") { out = Mode::Ufo; return true; }
    if (w == "wave" || w == "dart") { out = Mode::Wave; return true; }
    if (w == "robot") { out = Mode::Robot; return true; }
    if (w == "spider") { out = Mode::Spider; return true; }
    if (w == "swing" || w == "copter") { out = Mode::Swing; return true; }
    return false;
}

int speedFromWord(std::string const& w) {
    if (w == "slow" || w == "slower" || w == "half") return 0;
    if (w == "normal") return 1;
    if (w == "fast" || w == "faster" || w == "double") return 2;
    if (w == "triple") return 3;
    if (w == "quad" || w == "quadruple" || w == "fastest") return 4;
    return -1;
}

int speedFromMultiplier(double v) {
    if (v < 0.75) return 0;
    if (v < 1.5) return 1;
    if (v < 2.5) return 2;
    if (v < 3.5) return 3;
    return 4;
}

// "s", "sec", "seconds" -> 1, "min", "minutes" -> 60, anything else -> 0
int unitSeconds(std::string const& w) {
    if (w == "s" || w == "sec" || w == "secs" || w == "second" || w == "seconds") return 1;
    if (w == "m" || w == "min" || w == "mins" || w == "minute" || w == "minutes") return 60;
    return 0;
}

struct NamedColor { char const* name; Color c; };
const NamedColor COLORS[] = {
    {"red", {200, 20, 20}}, {"blue", {30, 90, 255}}, {"green", {30, 200, 60}},
    {"purple", {140, 50, 220}}, {"pink", {255, 110, 190}}, {"orange", {255, 140, 30}},
    {"yellow", {255, 220, 40}}, {"black", {10, 10, 10}}, {"white", {240, 240, 240}},
    {"cyan", {0, 220, 230}},
};

int luminance(Color c) { return (c.r * 3 + c.g * 6 + c.b) / 10; }

std::string fmt_plan_header(Settings const& s) {
    char buf[160];
    std::snprintf(buf, sizeof buf, "%s style, difficulty %d/10, seed %u. Plan: ",
        styleName(s.style), s.difficulty, s.seed);
    return buf;
}

} // namespace

std::string applyPromptKeywords(std::string const& prompt, Settings& s) {
    std::string p = lower(prompt);
    bool gotDifficulty = true;

    if (hasWord(p, "extreme")) s.difficulty = 10;
    else if (hasWord(p, "insane demon")) s.difficulty = 9;
    else if (hasWord(p, "easy demon")) s.difficulty = 7;
    else if (hasWord(p, "demon")) s.difficulty = 8;
    else if (hasWord(p, "insane")) s.difficulty = 6;
    else if (hasWord(p, "harder")) s.difficulty = 5;
    else if (hasWord(p, "hard")) s.difficulty = 4;
    else if (hasWord(p, "normal")) s.difficulty = 3;
    else if (hasWord(p, "easy")) s.difficulty = 2;
    else if (hasWord(p, "auto")) s.difficulty = 1;
    else gotDifficulty = false;

    if (hasWord(p, "hell") || hasWord(p, "blood") || hasWord(p, "bloodbath") || hasWord(p, "dark") || hasWord(p, "lava")) {
        s.style = Style::HellDemon;
        if (hasWord(p, "bloodbath")) s.difficulty = gotDifficulty ? std::max(s.difficulty, 9) : 10;
    }
    else if (hasWord(p, "toxic") || hasWord(p, "acid") || hasWord(p, "poison") || hasWord(p, "slime")) s.style = Style::Toxic;
    else if (hasWord(p, "space") || hasWord(p, "galaxy") || hasWord(p, "star") || hasWord(p, "cosmic")) s.style = Style::Space;
    else if (hasWord(p, "sunset") || hasWord(p, "sunrise") || hasWord(p, "desert")) s.style = Style::Sunset;
    else if (hasWord(p, "ice") || hasWord(p, "snow") || hasWord(p, "frozen") || hasWord(p, "winter")) s.style = Style::Ice;
    else if (hasWord(p, "candy") || hasWord(p, "sweet") || hasWord(p, "cute")) s.style = Style::Candy;
    else if (hasWord(p, "neon") || hasWord(p, "mix") || hasWord(p, "rainbow") || hasWord(p, "colorful")) s.style = Style::NeonMix;
    else if (hasWord(p, "classic") || hasWord(p, "simple")) s.style = Style::Classic;

    if (hasWord(p, "tiny")) s.seconds = 15;
    else if (hasWord(p, "very long") || hasWord(p, "extra long") || hasWord(p, "xl")) s.seconds = 120;
    else if (hasWord(p, "short")) s.seconds = 25;
    else if (hasWord(p, "medium")) s.seconds = 45;
    else if (hasWord(p, "long")) s.seconds = 75;

    auto toks = tokenize(p);
    for (size_t i = 0; i + 1 < toks.size(); i++) {
        if (toks[i].w == "seed" && toks[i + 1].num) s.seed = (uint32_t)toks[i + 1].v;
    }
    s.seconds = std::clamp(s.seconds, 5.f, 600.f);

    char buf[160];
    std::snprintf(buf, sizeof buf, "%s, difficulty %d/10, about %ds, seed %u",
        styleName(s.style), s.difficulty, (int)s.seconds, s.seed);
    return buf;
}

PromptPlan planFromPrompt(std::string const& prompt, Settings base) {
    PromptPlan plan;
    Settings& s = plan.settings;
    s = base;
    std::string p = lower(prompt);
    applyPromptKeywords(prompt, s);

    auto toks = tokenize(p);
    int n = (int)toks.size();

    // 1. modes, in the order they are written
    std::vector<int> modePos;
    std::vector<Section> secs;
    for (int i = 0; i < n; i++) {
        Mode m;
        if (!toks[i].num && modeFromWord(toks[i].w, m)) {
            Section sec;
            sec.mode = m;
            sec.speed = -1;
            sec.seconds = -1;
            sec.difficulty = s.difficulty;
            secs.push_back(sec);
            modePos.push_back(i);
        }
    }

    // nearest mode within `reach` tokens that still lacks the property (prefers the one after)
    auto nearest = [&](int pos, int reach, auto lacks) -> int {
        int best = -1, bestDist = reach + 1;
        for (int k = 0; k < (int)modePos.size(); k++) {
            if (!lacks(secs[k])) continue;
            int d = modePos[k] - pos;
            int dist = d >= 0 ? d : -d + 1; // a mode after the word wins a tie
            if (dist <= reach && dist < bestDist) { best = k; bestDist = dist; }
        }
        return best;
    };

    int globalSpeed = -1;
    bool totalGiven = false;
    std::vector<std::string> notes;

    // 2. durations and speeds
    for (int i = 0; i < n; i++) {
        if (toks[i].num) {
            if (i > 0 && toks[i - 1].w == "seed") continue;
            std::string unit = i + 1 < n && !toks[i + 1].num ? toks[i + 1].w : "";
            if (unit == "x") {
                int sp = speedFromMultiplier(toks[i].v);
                int k = nearest(i, 3, [](Section const& x) { return x.speed < 0; });
                if (k >= 0) secs[k].speed = sp; else globalSpeed = sp;
            } else if (int mult = unitSeconds(unit)) {
                float d = (float)(toks[i].v * mult);
                int k = nearest(i, 6, [](Section const& x) { return x.seconds < 0; });
                if (k >= 0) secs[k].seconds = d;
                else { s.seconds = std::clamp(d, 5.f, 600.f); totalGiven = true; }
            }
        } else {
            int sp = speedFromWord(toks[i].w);
            if (sp < 0) continue;
            // "double" / "triple" next to "speed" or a mode; "normal" is also a difficulty word
            int k = nearest(i, 3, [](Section const& x) { return x.speed < 0; });
            if (k >= 0) secs[k].speed = sp; else globalSpeed = sp;
        }
    }
    (void)totalGiven;

    // 3. fill in what the prompt did not say
    int d = s.difficulty;
    int defaultSpeed = globalSpeed >= 0 ? globalSpeed : (d <= 4 ? 1 : d <= 7 ? 2 : 3);
    if (secs.empty()) {
        plan.sections = presetSections(s);
        if (globalSpeed >= 0) for (auto& sec : plan.sections) sec.speed = globalSpeed;
    } else {
        float specified = 0;
        int unset = 0;
        for (auto const& sec : secs) { if (sec.seconds >= 0) specified += sec.seconds; else unset++; }
        float each = 6;
        if (unset > 0 && s.seconds > specified) each = std::max(5.f, (s.seconds - specified) / unset);
        for (auto& sec : secs) {
            if (sec.seconds < 0) sec.seconds = each;
            if (sec.speed < 0) sec.speed = defaultSpeed;
        }
        plan.sections = secs;
    }

    // 4. colours
    plan.theme = presetTheme(s.style);
    std::vector<Color> picked;
    std::vector<std::string> colorNames;
    for (auto const& tok : toks) {
        for (auto const& nc : COLORS) {
            if (tok.w == nc.name) { picked.push_back(nc.c); colorNames.push_back(nc.name); }
        }
    }
    if (!picked.empty()) {
        plan.theme.bg = scaled(picked[0], 0.55f);
        plan.theme.ground = scaled(picked[0], 0.8f);
        plan.theme.block = luminance(picked[0]) > 170 ? Color{30, 30, 60} : Color{255, 255, 255};
        if (picked.size() > 1) plan.theme.hazard = picked[1];
        else plan.theme.hazard = luminance(picked[0]) > 90 ? Color{255, 255, 255} : Color{255, 80, 80};
    }

    // 5. say what was understood
    static const char* SPEEDS[] = {"0.5x", "1x", "2x", "3x", "4x"};
    std::string text = fmt_plan_header(s);
    for (size_t i = 0; i < plan.sections.size(); i++) {
        auto const& sec = plan.sections[i];
        char buf[80];
        std::snprintf(buf, sizeof buf, "%s%ds %s at %s", i ? " > " : "", (int)std::lround(sec.seconds),
            modeName(sec.mode), SPEEDS[std::clamp(sec.speed, 0, 4)]);
        text += buf;
        if (text.size() > 600) { text += " ..."; break; }
    }
    if (!colorNames.empty()) {
        text += ". Colours: ";
        for (size_t i = 0; i < colorNames.size(); i++) text += (i ? ", " : "") + colorNames[i];
    }
    plan.summary = text;
    return plan;
}

Result generate(Request const& req) {
    Gen g;
    g.rng.seed(req.seed);
    g.theme = req.theme;

    int x = (int)std::ceil(req.startBlock);
    if (x <= 0) x = 8; // a little runway at the very start of a level

    if (req.applyTheme) {
        float tx = x - 1.5f;
        g.colorTrigger(tx, 12, CH_BG, req.theme.bg, 0.3f);
        g.colorTrigger(tx, 13, CH_GROUND, req.theme.ground, 0.3f);
        g.colorTrigger(tx, 14, CH_GROUND2, req.theme.ground, 0.3f);
        g.colorTrigger(tx, 15, CH_BLOCK, req.theme.block, 0);
        g.colorTrigger(tx, 16, CH_HAZARD, req.theme.hazard, 0);
    }

    bool first = true;
    Mode prev = Mode::Cube;
    int prevSpeed = 1;
    int index = 0;
    for (Section sec : req.sections) {
        sec.speed = std::clamp(sec.speed, 0, 4);
        sec.difficulty = std::clamp(sec.difficulty, 1, 10);
        sec.seconds = std::clamp(sec.seconds, 2.f, 120.f);

        x += g.transition(x, first, prev, prevSpeed, sec);
        int n = std::max(30, (int)std::lround(sec.seconds * SPEED_BPS[sec.speed]));

        if (req.applyTheme && index > 0) {
            Color base = req.theme.cycles ? rotated(req.theme.bg, index) : req.theme.bg;
            Color bg = index % 2 ? scaled(base, 0.6f) : base;
            g.colorTrigger((float)x, 12, CH_BG, bg, 0.5f);
        }
        if (req.applyTheme && req.theme.pulses) {
            for (int px = x + 4; px < x + n; px += 8)
                g.pulseTrigger((float)px, 13, CH_BG, scaled(req.theme.hazard, 0.5f));
        }
        g.section(x, n, sec);
        x += n;

        first = false;
        prev = sec.mode;
        prevSpeed = sec.speed;
        index++;
    }

    sanitize(g.out);

    Result res;
    res.endBlock = (float)x + 6;
    res.objs = std::move(g.out);
    return res;
}

} // namespace lb
