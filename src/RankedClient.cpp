#include "RankedClient.hpp"
#include <thread>
#include <chrono>

using JsonCb = std::function<void(matjson::Value const*)>;

static void run(EventListener<web::WebTask>& l, web::WebTask task, JsonCb on) {
    l.bind([on](web::WebTask::Event* e) {
        if (auto* res = e->getValue()) {
            if (res->ok()) {
                auto j = res->json();
                if (j) {
                    auto v = j.unwrap();
                    on(&v);
                    return;
                }
            }
            on(nullptr);
        } else if (e->isCancelled()) {
            on(nullptr);
        }
    });
    l.setFilter(std::move(task));
}

RankedClient& RankedClient::get() {
    static RankedClient inst;
    return inst;
}

std::string RankedClient::baseUrl() const {
    return Mod::get()->getSettingValue<std::string>("server-url");
}

web::WebRequest RankedClient::makeReq() const {
    web::WebRequest r;
    r.timeout(std::chrono::seconds(10));
    return r;
}

int RankedClient::accountID() const { return GJAccountManager::get()->m_accountID; }
std::string RankedClient::username() const { return GJAccountManager::get()->m_username; }

void RankedClient::joinQueue(std::function<void(bool)> cb) {
    matjson::Value b;
    b["accountID"] = accountID();
    b["username"] = username();
    auto r = makeReq();
    r.bodyJSON(b);
    run(m_join, r.post(baseUrl() + "/queue/join"), [cb](matjson::Value const* v) { cb(v != nullptr); });
}

void RankedClient::leaveQueue() {
    matjson::Value b;
    b["accountID"] = accountID();
    auto r = makeReq();
    r.bodyJSON(b);
    run(m_leave, r.post(baseUrl() + "/queue/leave"), [](matjson::Value const*) {});
}

void RankedClient::queueStatus(std::function<void(QueueStatus)> cb) {
    auto url = fmt::format("{}/queue/status?accountID={}", baseUrl(), accountID());
    run(m_status, makeReq().get(url), [cb](matjson::Value const* v) {
        QueueStatus s;
        if (v) {
            s.matched  = (*v)["matched"].asBool().unwrapOr(false);
            s.matchId  = (*v)["matchId"].asString().unwrapOr("");
            s.levelID  = (*v)["levelID"].asInt().unwrapOr(0);
            s.opponent = (*v)["opponent"].asString().unwrapOr("");
        }
        cb(s);
    });
}

void RankedClient::report(ReportData const& d, std::function<void(bool)> cb) {
    matjson::Value b;
    b["matchId"] = d.matchId;
    b["accountID"] = accountID();
    b["outcome"] = d.outcome;
    b["percent"] = d.percent;
    b["timeMs"] = d.timeMs;
    b["deaths"] = d.deaths;
    auto r = makeReq();
    r.bodyJSON(b);
    run(m_report, r.post(baseUrl() + "/match/report"), [cb](matjson::Value const* v) { cb(v != nullptr); });
}

static void showOutcome(MatchOutcome const& o) {
    std::string title = o.result == "win" ? "VICTORY" : (o.result == "draw" ? "DRAW" : "DEFEAT");
    std::string body = fmt::format("MMR {:+d} (now {})", o.mmrDelta, o.newMMR);
    if (!o.aiFeedback.empty()) body += "\n\n" + o.aiFeedback;
    FLAlertLayer::create(title.c_str(), body, "OK")->show();
}

void RankedClient::awaitResult(std::string matchId, int tries) {
    auto url = fmt::format("{}/match/result?matchId={}&accountID={}", baseUrl(), matchId, accountID());
    run(m_result, makeReq().get(url), [this, matchId, tries](matjson::Value const* v) {
        MatchOutcome o;
        if (v) {
            o.final      = (*v)["final"].asBool().unwrapOr(false);
            o.result     = (*v)["result"].asString().unwrapOr("");
            o.mmrDelta   = (*v)["mmrDelta"].asInt().unwrapOr(0);
            o.newMMR     = (*v)["newMMR"].asInt().unwrapOr(0);
            o.aiFeedback = (*v)["aiFeedback"].asString().unwrapOr("");
        }
        if (o.final) {
            clearMatch();
            showOutcome(o);
            return;
        }
        if (tries >= 40) { // ~80 s
            clearMatch();
            FLAlertLayer::create("GD Ranked", "Could not get the match result from the server.", "OK")->show();
            return;
        }
        std::thread([this, matchId, tries] {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            Loader::get()->queueInMainThread([this, matchId, tries] { awaitResult(matchId, tries + 1); });
        }).detach();
    });
}

void RankedClient::profile(std::function<void(Profile)> cb) {
    auto url = fmt::format("{}/profile?accountID={}&username={}", baseUrl(), accountID(), username());
    run(m_profile, makeReq().get(url), [cb](matjson::Value const* v) {
        Profile p;
        if (v) {
            p.mmr    = (*v)["mmr"].asInt().unwrapOr(1000);
            p.tier   = (*v)["tier"].asString().unwrapOr("Silver");
            p.wins   = (*v)["wins"].asInt().unwrapOr(0);
            p.losses = (*v)["losses"].asInt().unwrapOr(0);
        }
        cb(p);
    });
}

void RankedClient::leaderboard(std::function<void(std::vector<BoardRow>)> cb) {
    run(m_board, makeReq().get(baseUrl() + "/leaderboard"), [cb](matjson::Value const* v) {
        std::vector<BoardRow> rows;
        if (v) {
            if (auto arr = v->asArray()) {
                int i = 1;
                for (auto const& e : arr.unwrap()) {
                    BoardRow r;
                    r.rank = i++;
                    r.name = e["name"].asString().unwrapOr("?");
                    r.mmr  = e["mmr"].asInt().unwrapOr(0);
                    r.tier = e["tier"].asString().unwrapOr("");
                    rows.push_back(r);
                }
            }
        }
        cb(rows);
    });
}
