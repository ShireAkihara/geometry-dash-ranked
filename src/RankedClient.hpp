#pragma once
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <functional>
#include <string>
#include <vector>

using namespace geode::prelude;

struct Profile      { int mmr = 1000; std::string tier = "Silver"; int wins = 0; int losses = 0; };
struct QueueStatus  { bool matched = false; std::string matchId; int levelID = 0; std::string opponent; };
struct MatchOutcome { bool final = false; std::string result; int mmrDelta = 0; int newMMR = 0; std::string aiFeedback; };
struct BoardRow     { int rank = 0; std::string name; int mmr = 0; std::string tier; };
struct ReportData   { std::string matchId; std::string outcome; float percent = 0.f; int timeMs = 0; int deaths = 0; };

class RankedClient {
public:
    static RankedClient& get();

    void joinQueue(std::function<void(bool)> cb);
    void leaveQueue();
    void queueStatus(std::function<void(QueueStatus)> cb);
    void report(ReportData const& d, std::function<void(bool)> cb);
    void awaitResult(std::string matchId, int tries = 0);
    void profile(std::function<void(Profile)> cb);
    void leaderboard(std::function<void(std::vector<BoardRow>)> cb);

    // current match
    std::string activeMatchId;
    int activeLevelID = 0;
    std::string opponentName;
    bool inMatch() const { return !activeMatchId.empty(); }
    void clearMatch() { activeMatchId.clear(); activeLevelID = 0; opponentName.clear(); }

private:
    using Listener = EventListener<web::WebTask>;
    Listener m_join, m_leave, m_status, m_report, m_result, m_profile, m_board;

    std::string baseUrl() const;
    web::WebRequest makeReq() const;
    int accountID() const;
    std::string username() const;
};
