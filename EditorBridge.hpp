#pragma once

#include <Geode/Geode.hpp>

#include "AIClient.hpp"
#include "LevelBuilder.hpp"

// Everything that touches the open editor.
namespace bridge {

struct Stats {
    int added = 0;
    int removed = 0;
    int changed = 0;
};

int objectCount(EditorUI* ui);

// x (in blocks) just past the right-most object, 0 for an empty level.
float levelEndBlock(EditorUI* ui);

// Text description of the level for the AI. Lists at most `maxObjects`
// objects; `listed` and `total` report how many made it in.
std::string describeLevel(EditorUI* ui, int maxObjects, int& listed, int& total);

void clearLevel(EditorUI* ui);

// Adds objects as one undoable step. Returns how many were created.
int addObjects(EditorUI* ui, std::vector<lb::Obj> const& objs);

// Runs a full AI plan: clear, edits, generated sections, hand-placed objects.
Stats applyPlan(EditorUI* ui, ai::Plan const& plan, uint32_t seed);

} // namespace bridge
