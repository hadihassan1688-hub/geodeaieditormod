#pragma once
// Pure C++ level generation. No Geode / cocos includes here on purpose, so this
// file can be compiled and tested on its own (see tests/gen_test.cpp).

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace lb {

constexpr float BLOCK = 30.f; // editor units per grid block

struct Color {
    int r = 255, g = 255, b = 255;
};

// One level object. Positions are in editor units (30 units = 1 block).
struct Obj {
    int id = 1;
    float x = 0, y = 0;
    float rot = 0;
    float scale = 1;
    bool flipX = false, flipY = false;
    int color = 0; // main colour channel, 0 = leave default
    // Any further raw "key,value" properties of the GD object string.
    std::vector<std::pair<int, std::string>> extra;
};

// Turns objects into the editor's object string ("1,ID,2,X,3,Y,...;").
std::string serialize(std::vector<Obj> const& objs);

enum class Mode { Cube, Ship, Ball, Ufo, Wave, Robot, Spider, Swing };

struct Section {
    Mode mode = Mode::Cube;
    int speed = 1;        // 0 = 0.5x, 1 = 1x, 2 = 2x, 3 = 3x, 4 = 4x
    int difficulty = 5;   // 1 (free) .. 10 (extreme)
    float seconds = 8;    // how long the section lasts at its speed
};

struct Theme {
    Color bg{40, 125, 255};
    Color ground{0, 102, 255};
    Color block{255, 255, 255};
    Color hazard{255, 255, 255};
    bool pulses = false; // background pulse triggers through the level
    bool saws = false;   // extra sawblades in open areas
    bool cycles = false; // background hue changes from section to section
};

enum class Style { Classic, HellDemon, NeonMix, WaveRush, SkyFlight, Toxic, Space, Sunset, Ice, Candy };
constexpr int STYLE_COUNT = 10;
char const* styleName(Style s);

struct Settings {
    Style style = Style::Classic;
    int difficulty = 5;     // 1..10
    float seconds = 45;     // total length
    uint32_t seed = 1;
};

struct Request {
    std::vector<Section> sections;
    Theme theme;
    bool applyTheme = true;
    uint32_t seed = 1;
    float startBlock = 0; // x (in blocks) where the first section begins
};

struct Result {
    std::vector<Obj> objs;
    float endBlock = 0; // x (in blocks) where the generated part ends
};

// Blocks per second for a speed index.
float blocksPerSecond(int speed);

Theme presetTheme(Style style);
std::vector<Section> presetSections(Settings const& s);

// Reads plain words out of a prompt ("long hell wave level, extreme, fast")
// and adjusts the settings. Returns a short description of what it understood.
std::string applyPromptKeywords(std::string const& prompt, Settings& s);

// Everything the offline generator understood from a prompt.
struct PromptPlan {
    Settings settings;
    Theme theme;
    std::vector<Section> sections;
    std::string summary; // human readable, shown to the player
};

// Reads a prompt such as "10s fast wave then 8 second ship, red, hard, seed 7".
// Understands: game modes in the order written, a duration and speed per mode,
// a total length, difficulty words, style words, colours and a seed. Anything
// it does not name falls back to `base` and the style presets.
PromptPlan planFromPrompt(std::string const& prompt, Settings base);

Result generate(Request const& req);

// Helpers shared with the AI plan parser.
bool parseMode(std::string const& name, Mode& out);
bool parseHexColor(std::string const& hex, Color& out);
char const* modeName(Mode m);

} // namespace lb
