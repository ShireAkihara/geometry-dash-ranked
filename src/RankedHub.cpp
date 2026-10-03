#include "RankedHub.hpp"
#include "RankedClient.hpp"
#include "GlobedBridge.hpp"
#include <Geode/ui/GeodeUI.hpp>

RankedHub* RankedHub::create(MenuLayer* owner) {
    auto r = new RankedHub();
    if (r->init(owner)) { r->autorelease(); return r; }
    delete r;
    return nullptr;
}

static CCMenuItemSpriteExtra* makeBtn(CCMenu* menu, ButtonSprite* spr, std::function<void()> fn) {
    auto item = CCMenuItemExt::createSpriteExtra(spr, [fn](CCObject*) { fn(); });
    menu->addChild(item);
    return item;
}

bool RankedHub::init(MenuLayer* owner) {
    if (!CCLayer::init()) return false;
    m_owner = owner;
    auto win = CCDirector::get()->getWinSize();

    auto bg = CCLayerGradient::create({12, 12, 40, 255}, {70, 20, 100, 255});
    this->addChild(bg, -2);

    auto title = CCLabelBMFont::create("GD RANKED", "goldFont.fnt");
    title->setScale(1.3f);
    title->setPosition({win.width / 2, win.height - 40});
    this->addChild(title);

    m_profile = CCLabelBMFont::create("Loading profile...", "bigFont.fnt");
    m_profile->setScale(0.5f);
    m_profile->setPosition({win.width / 2, win.height - 78});
    this->addChild(m_profile);

    m_status = CCLabelBMFont::create(gdr::globedLoaded() ? "Globed: detected" : "Globed: not installed", "chatFont.fnt");
    m_status->setPosition({win.width / 2, win.height - 100});
    this->addChild(m_status);

    // main column
    auto main = CCMenu::create();
    main->setPosition({win.width / 2, win.height / 2 - 15});
    main->setContentSize({260, 110});
    main->setLayout(ColumnLayout::create()->setGap(8.f)->setAxisReverse(true)->setAutoScale(false));
    m_findSpr = ButtonSprite::create("Find Match");
    makeBtn(main, m_findSpr, [this] { onFind(); });
    makeBtn(main, ButtonSprite::create("Leaderboard"), [this] { toggleBoard(); });
    main->updateLayout();
    this->addChild(main);

    // bottom row: vanilla features reachable from our own UI
    auto bottom = CCMenu::create();
    bottom->setPosition({win.width / 2, 28});
    bottom->setContentSize({win.width - 40, 40});
    bottom->setLayout(RowLayout::create()->setGap(8.f)->setAutoScale(false));
    auto small = [](char const* t) { return ButtonSprite::create(t, "bigFont.fnt", "GJ_button_04.png", 0.7f); };
    makeBtn(bottom, small("Garage"),  [this] { m_owner->onGarage(nullptr); });
    makeBtn(bottom, small("Options"), [this] { m_owner->onOptions(nullptr); });
    makeBtn(bottom, small("Mods"),    [] { openModsList(); });
    makeBtn(bottom, small("Exit"),    [this] { m_owner->onQuit(nullptr); });
    bottom->updateLayout();
    this->addChild(bottom);

    m_board = CCNode::create();
    m_board->setVisible(false);
    this->addChild(m_board, 10);

    refreshProfile();
    return true;
}

void RankedHub::setStatus(std::string const& s) { m_status->setString(s.c_str()); }

void RankedHub::refreshProfile() {
    RankedClient::get().profile([self = WeakRef(this)](Profile p) {
        if (auto h = self.lock()) {
            h->m_profile->setString(fmt::format("{} | MMR {} | {}W {}L", p.tier, p.mmr, p.wins, p.losses).c_str());
        }
    });
}

void RankedHub::toggleBoard() {
    if (m_board->isVisible()) { m_board->setVisible(false); return; }
    m_board->setVisible(true);
    m_board->removeAllChildren();
    auto win = CCDirector::get()->getWinSize();
    auto panel = CCLayerColor::create({0, 0, 0, 220}, win.width - 80, win.height - 120);
    panel->setPosition({40, 60});
    m_board->addChild(panel);
    auto loading = CCLabelBMFont::create("Loading...", "bigFont.fnt");
    loading->setScale(0.5f);
    loading->setPosition({win.width / 2, win.height / 2});
    m_board->addChild(loading);

    RankedClient::get().leaderboard([self = WeakRef(this)](std::vector<BoardRow> rows) {
        auto h = self.lock();
        if (!h || !h->m_board->isVisible()) return;
        h->m_board->removeAllChildren();
        auto win = CCDirector::get()->getWinSize();
        auto panel = CCLayerColor::create({0, 0, 0, 220}, win.width - 80, win.height - 120);
        panel->setPosition({40, 60});
        h->m_board->addChild(panel);
        if (rows.empty()) {
            auto l = CCLabelBMFont::create("No data (server offline?)", "bigFont.fnt");
            l->setScale(0.5f);
            l->setPosition({win.width / 2, win.height / 2});
            h->m_board->addChild(l);
            return;
        }
        float y = win.height - 80;
        for (size_t i = 0; i < rows.size() && i < 10; i++) {
            auto& r = rows[i];
            auto l = CCLabelBMFont::create(fmt::format("#{}  {}  -  {} ({})", r.rank, r.name, r.mmr, r.tier).c_str(), "bigFont.fnt");
            l->setScale(0.4f);
            l->setAnchorPoint({0, 0.5f});
            l->setPosition({60, y});
            h->m_board->addChild(l);
            y -= 18;
        }
    });
}

void RankedHub::onFind() {
    auto& c = RankedClient::get();
    if (m_searching) {
        m_searching = false;
        this->unschedule(schedule_selector(RankedHub::pollQueue));
        c.leaveQueue();
        m_findSpr->setString("Find Match");
        setStatus("Search cancelled");
        return;
    }
    setStatus("Joining queue...");
    c.joinQueue([self = WeakRef(this)](bool ok) {
        auto h = self.lock();
        if (!h) return;
        if (!ok) { h->setStatus("Server unreachable"); return; }
        h->m_searching = true;
        h->m_findSpr->setString("Cancel");
        h->setStatus("Searching for opponent...");
        h->schedule(schedule_selector(RankedHub::pollQueue), 2.f);
    });
}

void RankedHub::pollQueue(float) {
    RankedClient::get().queueStatus([self = WeakRef(this)](QueueStatus s) {
        auto h = self.lock();
        if (!h || !h->m_searching || !s.matched) return;
        h->m_searching = false;
        h->unschedule(schedule_selector(RankedHub::pollQueue));
        h->m_findSpr->setString("Find Match");
        auto& c = RankedClient::get();
        c.activeMatchId = s.matchId;
        c.activeLevelID = s.levelID;
        c.opponentName = s.opponent;
        h->setStatus("Match found vs " + s.opponent);
        h->openLevel(s.levelID);
    });
}

void RankedHub::openLevel(int id) {
    auto search = GJSearchObject::create(SearchType::Search, std::to_string(id));
    auto scene = LevelBrowserLayer::scene(search);
    CCDirector::get()->pushScene(CCTransitionFade::create(0.5f, scene));
}
