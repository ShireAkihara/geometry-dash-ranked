#pragma once
#include <Geode/Geode.hpp>
using namespace geode::prelude;

// Full replacement UI for the main menu.
class RankedHub : public CCLayer {
public:
    static RankedHub* create(MenuLayer* owner);

protected:
    bool init(MenuLayer* owner);
    void refreshProfile();
    void toggleBoard();
    void onFind();
    void pollQueue(float dt);
    void setStatus(std::string const& s);
    void openLevel(int id);

    MenuLayer* m_owner = nullptr;
    CCLabelBMFont* m_profile = nullptr;
    CCLabelBMFont* m_status = nullptr;
    ButtonSprite* m_findSpr = nullptr;
    CCNode* m_board = nullptr;
    bool m_searching = false;
};
