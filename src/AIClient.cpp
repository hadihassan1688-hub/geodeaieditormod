#include "AIClient.hpp"

#include <chrono>

using namespace geode::prelude;

namespace ai {

namespace {

char const* SYSTEM_PROMPT = R"PROMPT(You are the level-building assistant inside the Geometry Dash 2.2 level editor. The player types a request; you answer with one JSON plan that the mod applies to the open level. You can build whole levels, add parts, and change or delete what is already there.

# Coordinates
All positions you read and write are in blocks (1 block = 30 editor units). x grows to the right, the player starts near x=0. The top of the ground is y=0, so a block resting on the ground has its centre at y=0.5 and a block on top of it at y=1.5. Objects snap to cell centres (x.5, y.5) unless you want them off-grid.

# What the plan does, in this order
1. clear_level: true deletes every object first. Use it when the player asks for a new level from scratch and the level is not empty.
2. edits: change existing objects inside a rectangle (x_min..x_max, y_min..y_max, inclusive; object_id 0 matches every id).
   - "delete" removes them.
   - "move" shifts them by dx, dy blocks.
   - "set" overwrites raw properties given in props.
3. theme: colours for background, ground, blocks (colour channel 1) and hazards (colour channel 2) as "#rrggbb", or null to leave colours alone. pulses adds background pulses, saws adds sawblades in open areas.
4. sections: gameplay built by the mod's generator, placed one after another starting at sections_start_x (use -1 to continue after the last object in the level). The generator adds the portals between sections and builds layouts that are meant to be possible: spike jumps, stairs and orb gaps for cube and robot, block corridors for ship and swing, pillar gates for ufo, zig-zag corridors for wave, floor and ceiling spike runs for ball and spider. difficulty 1 is free, 5 is a hard level, 8 is demon, 10 is the tightest it can make. seconds is how long the section lasts.
5. objects: individual objects you place yourself, for anything the generator cannot do: decoration, text, triggers, custom structures, exact gameplay.

Prefer sections for long stretches of gameplay, because you can only write a few hundred objects per answer; use objects for the parts that need your own design. If a request needs more than about 300 hand-placed objects, do the first part and say in reply that the player can ask you to continue.

# Object reference
Solids: 1 block, 40 half slab (lower half of the cell).
Hazards: 8 spike, 39 small spike, 88 big sawblade, 89 medium sawblade, 98 small sawblade. Put a spike on a ceiling with flip_y true.
Pads: 35 yellow jump pad, 140 pink pad, 1332 red pad, 67 blue gravity pad.
Orbs: 36 yellow, 141 pink, 1333 red, 84 blue gravity, 1022 green, 1330 black, 1704 green dash, 1751 pink dash.
Mode portals: 12 cube, 13 ship, 47 ball, 111 ufo, 660 wave, 745 robot, 1331 spider, 1933 swing.
Other portals: 10 normal gravity, 11 flipped gravity, 45 mirror on, 46 mirror off, 99 normal size, 101 mini, 286 dual on, 287 dual off.
Speed portals: 200 half, 201 normal, 202 double, 203 triple, 1334 quadruple.
Other: 1329 user coin, 914 text, 31 start position.
Triggers: 899 colour, 901 move, 1006 pulse, 1007 alpha, 1049 toggle, 1346 rotate, 1268 spawn, 1520 shake.
You may use any other object id you are sure about.

props is a raw "key,value,key,value" string in the game's own object format, for properties that have no field of their own. Useful keys: 57 group ids separated by dots, 20 editor layer, 24 z layer, 25 z order, 22 second colour channel, 62 touch triggered, 87 multi trigger. Colour trigger: 7,8,9 red green blue, 10 duration, 23 target channel (1000 background, 1001 ground, 1004 object). Move trigger: 28 x offset and 29 y offset in editor units, 10 duration, 30 easing, 51 target group. Pulse trigger: 7,8,9 colour, 45 fade in, 46 hold, 47 fade out, 51 target id, 52 set to 1 when the target is a group. Alpha trigger: 10 duration, 35 opacity, 51 target group. Toggle trigger: 51 target group, 56 set to 1 to turn the group on. Leave props empty when you do not need it. Never put keys 1, 2 or 3 in props.

# Physics you can rely on
Speeds in blocks per second: half 8.4, normal 10.4, double 12.9, triple 15.6, quadruple 19.2. A cube jump lasts about 0.42 seconds and rises about 2 blocks, so at normal speed it clears 3 spikes in a row and at double speed 4. The cube dies when it hits a block from the side. The ship needs at least 3 blocks of height to be comfortable; the wave moves at 45 degrees and dies on any block; flying modes live between y=0 and y=10. Portals are about 3 blocks tall, so put them where the player has to pass through them. Leave reaction time: after any surprise give the player at least 0.4 seconds before the next input.

# Style
When the player names an existing level or creator as a reference, make an original level with that general mood, pacing and difficulty; do not try to reproduce its layout. "Hell" or "demon" style means dark red and black colours, fast speeds, tight wave and ship parts, sawblades and pulses.

# Common requests
- "Easier": use edits, not a rebuild. Delete about every second hazard in the hardest stretches (spikes, sawblades), keep every portal, trigger, orb and pad, and never delete solid blocks that form a corridor. Do not move portals.
- "Harder": add hazards with objects in open stretches, at least 2 blocks away from existing hazards and never inside blocks, and leave every existing path passable.
- "Decorate" or "make it prettier": add only triggers (colour 899, pulse 1006) placed above the playfield at y=12 to 16, spaced so the colours change with the music-like pacing of the level; leave gameplay objects alone.
- Recolour requests: use the theme field, which replaces the colour triggers at the start.
- If the level is empty and the player asks for changes, say so in reply instead of inventing objects.

# reply
A short, plain message to the player: what you did, or the answer to their question. If the request is only a question, leave every other part of the plan empty.)PROMPT";

matjson::Value numberField() { return matjson::makeObject({{"type", "number"}}); }
matjson::Value intField() { return matjson::makeObject({{"type", "integer"}}); }
matjson::Value boolField() { return matjson::makeObject({{"type", "boolean"}}); }
matjson::Value stringField() { return matjson::makeObject({{"type", "string"}}); }

matjson::Value enumField(std::initializer_list<char const*> values) {
    auto arr = matjson::Value::array();
    for (auto v : values) arr.push(v);
    return matjson::makeObject({{"type", "string"}, {"enum", arr}});
}

// A strict object schema: every property is required, nothing else allowed.
matjson::Value objectSchema(std::initializer_list<std::pair<std::string, matjson::Value>> props) {
    auto required = matjson::Value::array();
    auto properties = matjson::Value::object();
    for (auto const& [name, schema] : props) {
        required.push(name);
        properties.set(name, schema);
    }
    return matjson::makeObject({
        {"type", "object"},
        {"properties", properties},
        {"required", required},
        {"additionalProperties", false},
    });
}

matjson::Value arrayOf(matjson::Value items) {
    return matjson::makeObject({{"type", "array"}, {"items", std::move(items)}});
}

matjson::Value planSchema() {
    auto edit = objectSchema({
        {"op", enumField({"delete", "move", "set"})},
        {"x_min", numberField()}, {"x_max", numberField()},
        {"y_min", numberField()}, {"y_max", numberField()},
        {"object_id", intField()},
        {"dx", numberField()}, {"dy", numberField()},
        {"props", stringField()},
    });
    auto theme = objectSchema({
        {"background", stringField()}, {"ground", stringField()},
        {"blocks", stringField()}, {"hazards", stringField()},
        {"pulses", boolField()}, {"saws", boolField()},
    });
    auto nullableTheme = matjson::Value::array();
    nullableTheme.push(matjson::makeObject({{"type", "null"}}));
    nullableTheme.push(theme);
    auto section = objectSchema({
        {"mode", enumField({"cube", "ship", "ball", "ufo", "wave", "robot", "spider", "swing"})},
        {"speed", enumField({"0.5x", "1x", "2x", "3x", "4x"})},
        {"difficulty", intField()},
        {"seconds", numberField()},
    });
    auto object = objectSchema({
        {"id", intField()},
        {"x", numberField()}, {"y", numberField()},
        {"rotation", numberField()}, {"scale", numberField()},
        {"flip_x", boolField()}, {"flip_y", boolField()},
        {"color_channel", intField()},
        {"props", stringField()},
    });
    return objectSchema({
        {"reply", stringField()},
        {"clear_level", boolField()},
        {"edits", arrayOf(edit)},
        {"theme", matjson::makeObject({{"anyOf", nullableTheme}})},
        {"sections_start_x", numberField()},
        {"sections", arrayOf(section)},
        {"objects", arrayOf(object)},
    });
}

matjson::Value textMessage(char const* role, std::string const& text) {
    return matjson::makeObject({{"role", role}, {"content", text}});
}

// Server-side refusal fallbacks exist for these model families only.
bool supportsFallbacks(std::string const& model) {
    return model.starts_with("claude-opus-5") || model.starts_with("claude-fable-5") ||
        model.starts_with("claude-sonnet-5-5");
}

float num(matjson::Value const& v, float fallback = 0) {
    return (float)v.asDouble().unwrapOr(fallback);
}

} // namespace

std::vector<std::pair<int, std::string>> parseProps(std::string const& text) {
    std::vector<std::pair<int, std::string>> out;
    auto parts = string::split(text, ",");
    for (size_t i = 0; i + 1 < parts.size(); i += 2) {
        auto key = numFromString<int>(string::trim(parts[i]));
        if (!key || key.unwrap() <= 3) continue;
        auto value = string::replace(string::trim(parts[i + 1]), ";", "");
        out.emplace_back(key.unwrap(), value);
    }
    return out;
}

Result<Plan> parsePlan(std::string const& jsonText) {
    auto parsed = matjson::parse(jsonText);
    if (!parsed) return Err("The AI answer was not valid JSON.");
    auto const root = std::move(parsed).unwrap();
    if (!root.isObject()) return Err("The AI answer was not a plan.");

    Plan plan;
    plan.reply = root["reply"].asString().unwrapOr("");
    plan.clearLevel = root["clear_level"].asBool().unwrapOr(false);
    plan.sectionsStart = num(root["sections_start_x"], -1);

    for (auto const& e : root["edits"]) {
        Edit edit;
        auto op = e["op"].asString().unwrapOr("");
        if (op == "delete") edit.op = Edit::Op::Delete;
        else if (op == "move") edit.op = Edit::Op::Move;
        else if (op == "set") edit.op = Edit::Op::Set;
        else continue;
        edit.xMin = num(e["x_min"]); edit.xMax = num(e["x_max"]);
        edit.yMin = num(e["y_min"]); edit.yMax = num(e["y_max"]);
        if (edit.xMin > edit.xMax) std::swap(edit.xMin, edit.xMax);
        if (edit.yMin > edit.yMax) std::swap(edit.yMin, edit.yMax);
        edit.objectId = (int)e["object_id"].asInt().unwrapOr(0);
        edit.dx = num(e["dx"]); edit.dy = num(e["dy"]);
        edit.props = parseProps(e["props"].asString().unwrapOr(""));
        plan.edits.push_back(std::move(edit));
    }

    auto const& theme = root["theme"];
    if (theme.isObject()) {
        plan.hasTheme = true;
        lb::parseHexColor(theme["background"].asString().unwrapOr(""), plan.theme.bg);
        lb::parseHexColor(theme["ground"].asString().unwrapOr(""), plan.theme.ground);
        lb::parseHexColor(theme["blocks"].asString().unwrapOr(""), plan.theme.block);
        lb::parseHexColor(theme["hazards"].asString().unwrapOr(""), plan.theme.hazard);
        plan.theme.pulses = theme["pulses"].asBool().unwrapOr(false);
        plan.theme.saws = theme["saws"].asBool().unwrapOr(false);
    }

    for (auto const& s : root["sections"]) {
        lb::Section sec;
        if (!lb::parseMode(s["mode"].asString().unwrapOr(""), sec.mode)) continue;
        auto speed = s["speed"].asString().unwrapOr("1x");
        sec.speed = speed == "0.5x" ? 0 : speed == "2x" ? 2 : speed == "3x" ? 3 : speed == "4x" ? 4 : 1;
        sec.difficulty = (int)s["difficulty"].asInt().unwrapOr(5);
        sec.seconds = num(s["seconds"], 8);
        plan.sections.push_back(sec);
    }

    for (auto const& o : root["objects"]) {
        lb::Obj obj;
        obj.id = (int)o["id"].asInt().unwrapOr(0);
        if (obj.id <= 0) continue;
        obj.x = num(o["x"]) * lb::BLOCK;
        obj.y = num(o["y"]) * lb::BLOCK;
        obj.rot = num(o["rotation"]);
        obj.scale = num(o["scale"], 1);
        if (obj.scale <= 0) obj.scale = 1;
        obj.flipX = o["flip_x"].asBool().unwrapOr(false);
        obj.flipY = o["flip_y"].asBool().unwrapOr(false);
        obj.color = (int)o["color_channel"].asInt().unwrapOr(0);
        obj.extra = parseProps(o["props"].asString().unwrapOr(""));
        plan.objects.push_back(std::move(obj));
    }
    return Ok(std::move(plan));
}

web::WebRequest buildRequest(
    std::string const& prompt, std::string const& levelContext, std::vector<Turn> const& history
) {
    auto mod = Mod::get();
    auto model = mod->getSettingValue<std::string>("model");
    if (model.empty()) model = "claude-opus-5-5";

    auto messages = matjson::Value::array();
    for (auto const& turn : history) {
        messages.push(textMessage("user", turn.user));
        messages.push(textMessage("assistant", turn.assistant));
    }
    messages.push(textMessage("user", "The level right now:\n" + levelContext + "\n\nRequest: " + prompt));

    auto system = matjson::Value::array();
    system.push(matjson::makeObject({
        {"type", "text"},
        {"text", SYSTEM_PROMPT},
        {"cache_control", matjson::makeObject({{"type", "ephemeral"}})},
    }));

    auto body = matjson::makeObject({
        {"model", model},
        {"max_tokens", 16000},
        {"system", system},
        {"messages", messages},
        {"output_config", matjson::makeObject({
            {"effort", mod->getSettingValue<std::string>("effort")},
            {"format", matjson::makeObject({{"type", "json_schema"}, {"schema", planSchema()}})},
        })},
    });

    web::WebRequest req;
    req.header("x-api-key", mod->getSettingValue<std::string>("api-key"));
    req.header("anthropic-version", "2026-10-21");
    req.header("content-type", "application/json");
    if (supportsFallbacks(model)) {
        // If a safety classifier declines the request, let the API retry it on
        // the model Anthropic recommends instead of failing.
        body.set("fallbacks", "default");
        req.header("anthropic-beta", "server-side-fallback-2026-07-01");
    }
    req.bodyJSON(body);
    req.timeout(std::chrono::seconds(300));
    return req;
}

Result<Plan> readResponse(web::WebResponse const& res, std::string& rawJson) {
    auto json = res.json();
    if (!res.ok()) {
        std::string detail;
        if (json) detail = json.unwrap()["error"]["message"].asString().unwrapOr("");
        if (detail.empty()) detail = std::string(res.errorMessage());
        if (res.code() == 401) return Err("The API key was rejected. Check it in the mod settings.");
        if (res.code() == 429) return Err("Rate limited or out of credit: " + detail);
        if (res.code() <= 0) return Err("Could not reach the AI service: " + detail);
        return Err(fmt::format("AI request failed ({}): {}", res.code(), detail));
    }
    if (!json) return Err("The AI service sent an unreadable response.");
    auto const root = std::move(json).unwrap();

    auto stop = root["stop_reason"].asString().unwrapOr("");
    if (stop == "refusal") return Err("The AI declined this request. Try wording it differently.");
    if (stop == "max_tokens") return Err("The plan was too big for one answer, so nothing was changed. Ask for it in smaller steps.");

    for (auto const& block : root["content"]) {
        if (block["type"].asString().unwrapOr("") != "text") continue;
        rawJson = block["text"].asString().unwrapOr("");
        return parsePlan(rawJson);
    }
    return Err("The AI answer was empty.");
}

} // namespace ai
