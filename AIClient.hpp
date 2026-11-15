#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

#include "LevelBuilder.hpp"

namespace ai {

// One change to objects that already exist in the level.
struct Edit {
    enum class Op { Delete, Move, Set };
    Op op = Op::Delete;
    // region in blocks, inclusive
    float xMin = 0, xMax = 0, yMin = 0, yMax = 0;
    int objectId = 0; // 0 = any object
    float dx = 0, dy = 0; // Move, in blocks
    std::vector<std::pair<int, std::string>> props; // Set
};

// Everything the AI decided to do for one prompt.
struct Plan {
    std::string reply;
    bool clearLevel = false;
    std::vector<Edit> edits;
    bool hasTheme = false;
    lb::Theme theme;
    float sectionsStart = -1; // blocks; negative = after the last object
    std::vector<lb::Section> sections;
    std::vector<lb::Obj> objects;
};

struct Turn {
    std::string user;
    std::string assistant; // the raw JSON the model answered with
};

// Parses "key,value,key,value" object properties. Keys 1, 2 and 3 (id and
// position) are dropped because they are set through their own fields.
std::vector<std::pair<int, std::string>> parseProps(std::string const& text);

geode::Result<Plan> parsePlan(std::string const& jsonText);

// Builds the Messages API request. `levelContext` describes the level as it is now.
geode::utils::web::WebRequest buildRequest(
    std::string const& prompt, std::string const& levelContext, std::vector<Turn> const& history
);

// Turns the HTTP response into a plan. `rawJson` receives the model's text.
geode::Result<Plan> readResponse(geode::utils::web::WebResponse const& res, std::string& rawJson);

constexpr char const* API_URL = "https://api.anthropic.com/v1/messages";

} // namespace ai
