#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/MDTextArea.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/web.hpp>

#include "AIClient.hpp"
#include "LevelBuilder.hpp"

class AIPopup : public geode::Popup {
public:
    static AIPopup* create(EditorUI* ui);

    // Forget the AI conversation (called when an editor session starts).
    static void resetConversation();

protected:
    bool init(EditorUI* ui);

    void onStyle(cocos2d::CCObject*);
    void onDifficulty(cocos2d::CCObject*);
    void onLength(cocos2d::CCObject*);
    void onGenerate(cocos2d::CCObject*);
    void onAskAI(cocos2d::CCObject*);
    void onSettings(cocos2d::CCObject*);
    void onReroll(cocos2d::CCObject*);
    void onQuickAsk(cocos2d::CCObject*);
    void onHistory(cocos2d::CCObject*);
    void tick(float dt);

    void refreshLabels();
    void setStatus(std::string const& text);
    void setBusy(bool busy);
    void generateOffline(bool allowClear = true);
    void rememberPrompt(std::string const& prompt);
    void applyPlan(ai::Plan const& plan);
    // Runs `action` right away, or after a confirmation when it would wipe a level.
    void confirmClear(bool willClear, geode::Function<void()> action);

    EditorUI* m_ui = nullptr;
    geode::TextInput* m_prompt = nullptr;
    geode::MDTextArea* m_status = nullptr;
    ButtonSprite* m_styleSpr = nullptr;
    ButtonSprite* m_difficultySpr = nullptr;
    ButtonSprite* m_lengthSpr = nullptr;
    CCMenuItemToggler* m_replaceToggle = nullptr;
    CCMenuItemSpriteExtra* m_generateBtn = nullptr;
    CCMenuItemSpriteExtra* m_askBtn = nullptr;
    cocos2d::CCNode* m_spinner = nullptr;
    cocos2d::CCLabelBMFont* m_timer = nullptr;
    float m_elapsed = 0;

    lb::Settings m_settings;
    int m_lengthIndex = 2;
    int m_historyIndex = -1;
    bool m_canReroll = false; // the last thing done was an offline Generate that can be undone
    std::string m_pendingPrompt;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_request;
};
