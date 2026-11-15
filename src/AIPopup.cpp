#include "AIPopup.hpp"

#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/LoadingSpinner.hpp>

#include <random>

#include "EditorBridge.hpp"

using namespace geode::prelude;

namespace {

constexpr float LENGTHS[] = {15, 25, 45, 75, 120};
constexpr char const* LENGTH_NAMES[] = {"Tiny", "Short", "Medium", "Long", "XL"};
constexpr char const* DIFFICULTY_NAMES[] = {
    "Auto", "Easy", "Normal", "Hard", "Harder", "Insane",
    "Easy Demon", "Hard Demon", "Insane Demon", "Extreme Demon",
};
constexpr size_t MAX_TURNS = 6;

// The conversation with the AI survives closing and reopening the popup.
std::vector<ai::Turn> g_history;

uint32_t randomSeed() {
    return std::random_device{}() % 1000000;
}

} // namespace

AIPopup* AIPopup::create(EditorUI* ui) {
    auto ret = new AIPopup();
    if (ret->init(ui)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

void AIPopup::resetConversation() {
    g_history.clear();
}

bool AIPopup::init(EditorUI* ui) {
    if (!Popup::init(440.f, 290.f)) return false;
    m_ui = ui;
    this->setTitle("AI Level Builder");

    m_prompt = TextInput::create(400.f, "Describe a level or a change...", "chatFont.fnt");
    m_prompt->setCommonFilter(CommonFilter::Any);
    m_prompt->setMaxCharCount(600);
    m_mainLayer->addChildAtPosition(m_prompt, Anchor::Top, {0, -58});

    // style / difficulty / length pickers: tap to cycle
    auto picker = [this](ButtonSprite*& spr, SEL_MenuHandler handler, float x) {
        spr = ButtonSprite::create("", 110, true, "bigFont.fnt", "GJ_button_04.png", 26.f, 0.5f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, handler);
        m_buttonMenu->addChildAtPosition(btn, Anchor::Top, {x, -98});
    };
    picker(m_styleSpr, menu_selector(AIPopup::onStyle), -138);
    picker(m_difficultySpr, menu_selector(AIPopup::onDifficulty), 0);
    picker(m_lengthSpr, menu_selector(AIPopup::onLength), 138);

    m_replaceToggle = CCMenuItemToggler::createWithStandardSprites(this, nullptr, 0.6f);
    m_buttonMenu->addChildAtPosition(m_replaceToggle, Anchor::Top, {-190, -134});
    auto replaceLabel = CCLabelBMFont::create("Replace the whole level", "bigFont.fnt");
    replaceLabel->setScale(0.35f);
    replaceLabel->setAnchorPoint({0, 0.5f});
    m_mainLayer->addChildAtPosition(replaceLabel, Anchor::Top, {-174, -134});

    auto generateSpr = ButtonSprite::create("Generate", "goldFont.fnt", "GJ_button_01.png", 0.8f);
    generateSpr->setScale(0.75f);
    m_generateBtn = CCMenuItemSpriteExtra::create(generateSpr, this, menu_selector(AIPopup::onGenerate));
    m_buttonMenu->addChildAtPosition(m_generateBtn, Anchor::Top, {60, -134});

    auto askSpr = ButtonSprite::create("Ask AI", "goldFont.fnt", "GJ_button_02.png", 0.8f);
    askSpr->setScale(0.75f);
    m_askBtn = CCMenuItemSpriteExtra::create(askSpr, this, menu_selector(AIPopup::onAskAI));
    m_buttonMenu->addChildAtPosition(m_askBtn, Anchor::Top, {160, -134});

    auto settingsSpr = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
    settingsSpr->setScale(0.5f);
    auto settingsBtn = CCMenuItemSpriteExtra::create(settingsSpr, this, menu_selector(AIPopup::onSettings));
    m_buttonMenu->addChildAtPosition(settingsBtn, Anchor::TopRight, {-22, -22});

    m_status = MDTextArea::create("", {400.f, 120.f});
    m_mainLayer->addChildAtPosition(m_status, Anchor::Bottom, {0, 72});

    m_spinner = LoadingSpinner::create(30.f);
    m_spinner->setVisible(false);
    m_mainLayer->addChildAtPosition(m_spinner, Anchor::Bottom, {0, 72});

    this->refreshLabels();
    this->setStatus(
        "**Generate** builds a level on this device from the pickers above. Words in the box "
        "count too, like `long hell wave level, extreme, seed 42`.\n\n"
        "**Ask AI** sends your request to Claude, which can build, add to, restyle or edit "
        "the level. It needs an API key: tap the gear."
    );
    return true;
}

void AIPopup::refreshLabels() {
    m_styleSpr->setString(lb::styleName(m_settings.style));
    m_difficultySpr->setString(DIFFICULTY_NAMES[std::clamp(m_settings.difficulty, 1, 10) - 1]);
    m_lengthSpr->setString(fmt::format("{} ({}s)", LENGTH_NAMES[m_lengthIndex], (int)LENGTHS[m_lengthIndex]).c_str());
}

void AIPopup::setStatus(std::string const& text) {
    m_status->setString(text.c_str());
}

void AIPopup::setBusy(bool busy) {
    m_spinner->setVisible(busy);
    m_status->setVisible(!busy);
    m_generateBtn->setEnabled(!busy);
    m_askBtn->setEnabled(!busy);
}

void AIPopup::onStyle(CCObject*) {
    m_settings.style = (lb::Style)(((int)m_settings.style + 1) % lb::STYLE_COUNT);
    this->refreshLabels();
}

void AIPopup::onDifficulty(CCObject*) {
    m_settings.difficulty = m_settings.difficulty % 10 + 1;
    this->refreshLabels();
}

void AIPopup::onLength(CCObject*) {
    m_lengthIndex = (m_lengthIndex + 1) % 5;
    this->refreshLabels();
}

void AIPopup::onSettings(CCObject*) {
    geode::openSettingsPopup(Mod::get());
}

void AIPopup::confirmClear(bool willClear, geode::Function<void()> action) {
    int count = bridge::objectCount(m_ui);
    if (!willClear || count == 0) {
        action();
        return;
    }
    geode::createQuickPopup(
        "Replace level",
        fmt::format("This deletes all <cr>{}</c> objects in the level and <cr>cannot be undone</c>.", count),
        "Cancel", "Replace",
        [action = std::move(action)](FLAlertLayer*, bool replace) mutable {
            if (replace) action();
        }
    );
}

void AIPopup::onGenerate(CCObject*) {
    this->confirmClear(m_replaceToggle->isToggled(), [this] { this->generateOffline(); });
}

void AIPopup::generateOffline() {
    lb::Settings s = m_settings;
    s.seconds = LENGTHS[m_lengthIndex];
    s.seed = randomSeed();
    std::string understood = lb::applyPromptKeywords(m_prompt->getString(), s);

    bool replace = m_replaceToggle->isToggled();
    if (replace) bridge::clearLevel(m_ui);

    lb::Request req;
    req.sections = lb::presetSections(s);
    req.theme = lb::presetTheme(s.style);
    req.seed = s.seed;
    req.startBlock = replace ? 0 : bridge::levelEndBlock(m_ui);
    auto result = lb::generate(req);
    int added = bridge::addObjects(m_ui, result.objs);

    std::string parts;
    for (auto const& sec : req.sections) {
        if (!parts.empty()) parts += ", ";
        parts += lb::modeName(sec.mode);
    }
    this->setStatus(fmt::format(
        "Built **{}**.\n\nAdded {} objects from x={:.0f} to x={:.0f} blocks: {}.\n\n"
        "Playtest it and fix anything that feels unfair. One undo removes it. "
        "Type `seed {}` to get this exact layout again.",
        understood, added, req.startBlock, result.endBlock, parts, s.seed
    ));
}

void AIPopup::onAskAI(CCObject*) {
    std::string prompt = m_prompt->getString();
    if (string::trim(prompt).empty()) {
        this->setStatus("Type what you want first, for example: `make a 40 second hell themed wave level` or `delete everything after x=200`.");
        return;
    }
    if (Mod::get()->getSettingValue<std::string>("api-key").empty()) {
        this->setStatus(
            "**Ask AI needs a Claude API key.**\n\nCreate one at console.anthropic.com, then tap "
            "the gear in the corner and paste it into *Claude API key*. Requests are billed to "
            "your Anthropic account.\n\nThe **Generate** button works without a key."
        );
        return;
    }

    int listed = 0, total = 0;
    int maxObjects = (int)Mod::get()->getSettingValue<int64_t>("context-objects");
    std::string context = bridge::describeLevel(m_ui, maxObjects, listed, total);

    m_pendingPrompt = prompt;
    this->setBusy(true);
    auto request = ai::buildRequest(prompt, context, g_history);
    m_request.spawn(request.post(ai::API_URL), [this, listed, total](web::WebResponse res) {
        this->setBusy(false);
        std::string raw;
        auto plan = ai::readResponse(res, raw);
        if (!plan) {
            this->setStatus("**Error:** " + plan.unwrapErr());
            return;
        }
        g_history.push_back({m_pendingPrompt, raw});
        if (g_history.size() > MAX_TURNS) g_history.erase(g_history.begin());

        ai::Plan p = std::move(plan).unwrap();
        if (listed < total) {
            p.reply += fmt::format(
                "\n\n*The AI only saw the first {} of {} objects. Raise \"Objects shown to the AI\" in the settings to show it more.*",
                listed, total
            );
        }
        if (m_replaceToggle->isToggled() && !p.sections.empty()) p.clearLevel = true;
        this->confirmClear(p.clearLevel, [this, p = std::move(p)] { this->applyPlan(p); });
    });
}

void AIPopup::applyPlan(ai::Plan const& plan) {
    auto stats = bridge::applyPlan(m_ui, plan, randomSeed());
    std::string summary = plan.reply.empty() ? "Done." : plan.reply;
    if (stats.added || stats.removed || stats.changed) {
        summary += fmt::format(
            "\n\nAdded {}, changed {}, removed {} objects.", stats.added, stats.changed, stats.removed
        );
    }
    this->setStatus(summary);
}
