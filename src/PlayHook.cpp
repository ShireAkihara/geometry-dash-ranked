#include <Geode/modify/PlayLayer.hpp>
#include <chrono>
#include "RankedClient.hpp"

// Automatic win/loss detection: the mod only reports what happened
// (completed / quit + percent + deaths). The server decides win, loss or draw.
class $modify(RankedPlayLayer, PlayLayer) {
    struct Fields {
        bool ranked = false;
        bool reported = false;
        int deaths = 0;
        float best = 0.f;
        std::chrono::steady_clock::time_point t0;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        auto& c = RankedClient::get();
        m_fields->ranked = c.inMatch() && static_cast<int>(level->m_levelID) == c.activeLevelID;
        m_fields->t0 = std::chrono::steady_clock::now();
        return true;
    }

    void destroyPlayer(PlayerObject* p, GameObject* obj) {
        bool wasDead = m_player1->m_isDead;
        PlayLayer::destroyPlayer(p, obj);
        if (m_fields->ranked && !wasDead && m_player1->m_isDead) {
            m_fields->deaths++;
            m_fields->best = std::max(m_fields->best, static_cast<float>(this->getCurrentPercent()));
        }
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        if (!m_fields->ranked || m_fields->reported) return;
        if (m_isPracticeMode || m_isTestMode) return; // practice completions don't count
        sendReport("completed", 100.f);
    }

    void onQuit() {
        if (m_fields->ranked && !m_fields->reported) {
            sendReport("quit", std::max(m_fields->best, static_cast<float>(this->getCurrentPercent())));
        }
        PlayLayer::onQuit();
    }

    void sendReport(char const* outcome, float percent) {
        m_fields->reported = true;
        auto& c = RankedClient::get();
        ReportData d;
        d.matchId = c.activeMatchId;
        d.outcome = outcome;
        d.percent = percent;
        d.deaths = m_fields->deaths;
        d.timeMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - m_fields->t0).count());
        auto id = d.matchId;
        c.report(d, [id](bool ok) {
            if (ok) RankedClient::get().awaitResult(id);
            else FLAlertLayer::create("GD Ranked", "Could not send the result to the server.", "OK")->show();
        });
    }
};
