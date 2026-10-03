#include <Geode/modify/MenuLayer.hpp>
#include "RankedHub.hpp"

// Replaces the vanilla main menu: every original node is hidden (hidden menus
// receive no touches) and our own full-screen hub is drawn on top.
class $modify(RankedMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        for (auto* child : CCArrayExt<CCNode*>(this->getChildren())) {
            child->setVisible(false);
        }
        this->addChild(RankedHub::create(this), 100);
        return true;
    }
};
