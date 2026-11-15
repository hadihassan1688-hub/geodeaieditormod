#include "EditorBridge.hpp"

#include <algorithm>
#include <map>

using namespace geode::prelude;

namespace bridge {

namespace {

using Props = std::vector<std::pair<int, std::string>>;

std::vector<Ref<GameObject>> allObjects(EditorUI* ui) {
    std::vector<Ref<GameObject>> out;
    auto arr = ui->m_editorLayer->m_objects;
    if (!arr) return out;
    out.reserve(arr->count());
    for (auto obj : CCArrayExt<GameObject*>(arr)) {
        if (obj) out.emplace_back(obj);
    }
    return out;
}

Props saveProps(EditorUI* ui, GameObject* obj) {
    Props out;
    std::string save = obj->getSaveString(ui->m_editorLayer);
    auto parts = string::split(save, ",");
    for (size_t i = 0; i + 1 < parts.size(); i += 2) {
        auto key = numFromString<int>(parts[i]);
        if (key) out.emplace_back(key.unwrap(), parts[i + 1]);
    }
    return out;
}

std::string joinProps(Props const& props) {
    std::string s;
    for (auto const& [k, v] : props) {
        if (!s.empty()) s += ",";
        s += fmt::format("{},{}", k, v);
    }
    return s + ";";
}

void setProp(Props& props, int key, std::string value) {
    for (auto& [k, v] : props) {
        if (k == key) { v = std::move(value); return; }
    }
    props.emplace_back(key, std::move(value));
}

float propFloat(Props const& props, int key, float fallback) {
    for (auto const& [k, v] : props) {
        if (k == key) return numFromString<float>(v).unwrapOr(fallback);
    }
    return fallback;
}

// The editor draws objects shifted from their saved position. Measure the
// shift on a real object instead of assuming a constant.
CCPoint saveOffset(EditorUI* ui, std::vector<Ref<GameObject>> const& objs) {
    if (objs.empty()) return {0, 0};
    GameObject* obj = objs.front();
    auto props = saveProps(ui, obj);
    return {
        obj->getPositionX() - propFloat(props, 2, obj->getPositionX()),
        obj->getPositionY() - propFloat(props, 3, obj->getPositionY()),
    };
}

int pasteString(EditorUI* ui, std::string const& str) {
    if (str.empty()) return 0;
    auto created = ui->pasteObjects(str, false, false);
    int count = created ? created->count() : 0;
    ui->deselectAll();
    return count;
}

} // namespace

int objectCount(EditorUI* ui) {
    auto arr = ui->m_editorLayer->m_objects;
    return arr ? arr->count() : 0;
}

float levelEndBlock(EditorUI* ui) {
    auto objs = allObjects(ui);
    if (objs.empty()) return 0;
    auto offset = saveOffset(ui, objs);
    float maxX = 0;
    for (auto const& obj : objs) maxX = std::max(maxX, obj->getPositionX() - offset.x);
    return std::ceil(maxX / lb::BLOCK) + 1;
}

std::string describeLevel(EditorUI* ui, int maxObjects, int& listed, int& total) {
    auto objs = allObjects(ui);
    total = (int)objs.size();
    listed = 0;
    if (objs.empty()) return "The level is empty.";

    auto offset = saveOffset(ui, objs);
    std::sort(objs.begin(), objs.end(), [](auto const& a, auto const& b) {
        return a->getPositionX() < b->getPositionX();
    });
    float endBlock = (objs.back()->getPositionX() - offset.x) / lb::BLOCK;

    std::string text = fmt::format(
        "{} objects, from x=0 to x={:.0f} blocks.\n", total, endBlock
    );

    std::map<int, int> perId;
    for (auto const& obj : objs) perId[obj->m_objectID]++;
    text += "Object ids and how many of each: ";
    for (auto const& [id, n] : perId) text += fmt::format("{}x{} ", id, n);
    text += "\n";

    listed = std::min(total, std::max(0, maxObjects));
    if (listed > 0) {
        text += "Objects as id,x,y in blocks, sorted by x:\n";
        for (int i = 0; i < listed; i++) {
            GameObject* obj = objs[i];
            text += fmt::format("{},{:.1f},{:.1f};", obj->m_objectID,
                (obj->getPositionX() - offset.x) / lb::BLOCK,
                (obj->getPositionY() - offset.y) / lb::BLOCK);
        }
        text += "\n";
    }
    if (listed < total) {
        text += fmt::format(
            "Only the first {} of {} objects are listed above; the rest of the level is "
            "not shown to you. Say so if the request depends on objects you cannot see.\n",
            listed, total
        );
    }
    return text;
}

void clearLevel(EditorUI* ui) {
    ui->deselectAll();
    for (auto const& obj : allObjects(ui)) ui->m_editorLayer->removeObject(obj, true);
    ui->updateButtons();
}

int addObjects(EditorUI* ui, std::vector<lb::Obj> const& objs) {
    return pasteString(ui, lb::serialize(objs));
}

Stats applyPlan(EditorUI* ui, ai::Plan const& plan, uint32_t seed) {
    Stats stats;
    auto layer = ui->m_editorLayer;

    if (plan.clearLevel) {
        stats.removed += objectCount(ui);
        clearLevel(ui);
    }

    if (!plan.edits.empty()) {
        ui->deselectAll();
        auto objs = allObjects(ui);
        auto offset = saveOffset(ui, objs);
        std::string rebuilt;
        for (auto const& edit : plan.edits) {
            for (auto& obj : objs) {
                if (!obj) continue; // already handled by an earlier edit
                float bx = (obj->getPositionX() - offset.x) / lb::BLOCK;
                float by = (obj->getPositionY() - offset.y) / lb::BLOCK;
                if (bx < edit.xMin || bx > edit.xMax || by < edit.yMin || by > edit.yMax) continue;
                if (edit.objectId != 0 && obj->m_objectID != edit.objectId) continue;

                if (edit.op == ai::Edit::Op::Delete) {
                    stats.removed++;
                } else {
                    // Rewrite the object: change its saved properties and paste it back.
                    auto props = saveProps(ui, obj);
                    if (edit.op == ai::Edit::Op::Move) {
                        setProp(props, 2, fmt::format("{:.2f}", propFloat(props, 2, 0) + edit.dx * lb::BLOCK));
                        setProp(props, 3, fmt::format("{:.2f}", propFloat(props, 3, 0) + edit.dy * lb::BLOCK));
                    } else {
                        for (auto const& [k, v] : edit.props) setProp(props, k, v);
                    }
                    rebuilt += joinProps(props);
                    stats.changed++;
                }
                layer->removeObject(obj, false);
                obj = nullptr;
            }
        }
        pasteString(ui, rebuilt);
    }

    if (!plan.sections.empty()) {
        lb::Request req;
        req.sections = plan.sections;
        req.seed = seed;
        req.applyTheme = plan.hasTheme;
        if (plan.hasTheme) req.theme = plan.theme;
        req.startBlock = plan.sectionsStart >= 0 ? plan.sectionsStart : levelEndBlock(ui);
        stats.added += addObjects(ui, lb::generate(req).objs);
    } else if (plan.hasTheme) {
        // colours only: an empty generation still emits the colour triggers
        lb::Request req;
        req.theme = plan.theme;
        req.startBlock = 1.5f;
        stats.added += addObjects(ui, lb::generate(req).objs);
    }

    if (!plan.objects.empty()) stats.added += addObjects(ui, plan.objects);

    ui->updateButtons();
    return stats;
}

} // namespace bridge
