#pragma once
#include <Geode/Geode.hpp>

// Globed is only used as the realtime/room layer. Ranking, MMR, leaderboard and
// win/loss arbitration live on the ranked server (server/index.js), because
// Globed does not store rankings. Wire Globed's mod API here once confirmed.
namespace gdr {
    inline bool globedLoaded() {
        return geode::Loader::get()->isModLoaded("dankmeme.globed2");
    }
}
