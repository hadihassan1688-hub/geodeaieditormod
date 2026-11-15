#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>

#include "AIPopup.hpp"

using namespace geode::prelude;

// Adds the "AI" button next to undo / redo / delete in the level editor.
class $modify(AIEditorUI, EditorUI) {
    struct Fields {
        CCMenuItemSpriteExtra* aiButton = nullptr;
    };

    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;
        AIPopup::resetConversation();

        auto label = CCLabelBMFont::create("AI", "bigFont.fnt");
        auto sprite = CircleButtonSprite::create(label, CircleBaseColor::Pink, CircleBaseSize::Small);
        sprite->setScale(0.85f);
        auto button = CCMenuItemSpriteExtra::create(sprite, this, menu_selector(AIEditorUI::onAIButton));
        button->setID("ai-button"_spr);

        // Sit one step to the right of the trash button, in the same menu.
        auto anchor = m_trashBtn ? m_trashBtn : m_redoBtn;
        if (anchor && anchor->getParent()) {
            auto parent = anchor->getParent();
            float step = 40.f;
            if (m_trashBtn && m_redoBtn) {
                step = std::abs(m_trashBtn->getPositionX() - m_redoBtn->getPositionX());
            }
            button->setPosition(anchor->getPosition() + CCPoint{step, 0});
            parent->addChild(button);
            parent->updateLayout();
        } else {
            auto menu = CCMenu::create();
            menu->setPosition({0, 0});
            auto win = CCDirector::get()->getWinSize();
            button->setPosition({150.f, win.height - 22.f});
            menu->addChild(button);
            this->addChild(menu, 10);
        }
        m_fields->aiButton = button;
        return true;
    }

    void onAIButton(CCObject*) {
        if (auto popup = AIPopup::create(this)) popup->show();
    }

    // The editor hides its buttons while playtesting; follow along.
    void showUI(bool show) {
        EditorUI::showUI(show);
        if (auto button = m_fields->aiButton) button->setVisible(show);
    }
};
