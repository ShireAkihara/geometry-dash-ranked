# GD Ranked (Geode mod)

1v1 ranked mode for Geometry Dash 2.2081 (Geode 5.10.1), Windows + Android.
Replaces the main menu with its own hub, finds matches, tracks MMR/tier, shows a leaderboard,
and detects win/loss automatically. A server-side Claude call writes a short performance review after each match.

## Build
Push to GitHub: the `Build Mod` workflow builds Windows, Android32 and Android64 and uploads a combined `.geode`.

## Server
```
cd server && npm install
ANTHROPIC_API_KEY=sk-ant-... LEVEL_POOL=<id1,id2,...> npm start
```
Then set "Ranked server URL" in the mod settings. Use HTTPS in production.
Never put the Anthropic key inside the mod.

## Known gaps
- Player identity is the raw account ID (spoofable). Add Argon verification before going public.
- Globed is only detected for now (`GlobedBridge.hpp`); ranking/arbitration is on this server.
- After a match is found, the level opens via search; the player taps Play.
