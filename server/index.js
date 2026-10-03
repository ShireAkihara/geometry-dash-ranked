// GD Ranked server: matchmaking, MMR (Elo), leaderboard, win/loss arbitration,
// and AI performance feedback from Claude. The Anthropic key lives ONLY here.
import express from "express";
import fs from "fs";
import crypto from "crypto";

const PORT = process.env.PORT || 3000;
const POOL = (process.env.LEVEL_POOL || "128").split(",").map(Number); // replace with real level IDs
const MODEL = process.env.CLAUDE_MODEL || "claude-sonnet-5-5";
const DB_FILE = "./db.json";

const app = express();
app.use(express.json());

let db = fs.existsSync(DB_FILE) ? JSON.parse(fs.readFileSync(DB_FILE, "utf8")) : { players: {} };
const save = () => fs.writeFileSync(DB_FILE, JSON.stringify(db));

const queue = new Map();   // accountID -> { username, mmr, since }
const assign = new Map();  // accountID -> matchId
const matches = new Map(); // matchId -> match

const tierOf = (m) => m >= 1800 ? "Master" : m >= 1500 ? "Diamond" : m >= 1300 ? "Platinum"
  : m >= 1100 ? "Gold" : m >= 900 ? "Silver" : "Bronze";

function player(id, username) {
  if (!db.players[id]) db.players[id] = { name: username || `Player ${id}`, mmr: 1000, wins: 0, losses: 0 };
  if (username) db.players[id].name = username;
  return db.players[id];
}

app.post("/queue/join", (req, res) => {
  const { accountID, username } = req.body;
  if (!accountID) return res.status(400).json({ error: "accountID required" });
  const p = player(accountID, username);
  assign.delete(accountID);
  queue.set(accountID, { username: p.name, mmr: p.mmr, since: Date.now() });
  res.json({ ok: true });
});

app.post("/queue/leave", (req, res) => {
  queue.delete(req.body.accountID);
  res.json({ ok: true });
});

app.get("/queue/status", (req, res) => {
  const id = Number(req.query.accountID);
  const mid = assign.get(id);
  const m = mid && matches.get(mid);
  if (!m) return res.json({ matched: false });
  const other = m.players.find((p) => p !== id);
  res.json({ matched: true, matchId: m.id, levelID: m.levelID, opponent: db.players[other].name });
});

function makeMatch(a, b) {
  const m = {
    id: crypto.randomUUID(), players: [a, b], reports: {}, final: false, results: {},
    levelID: POOL[Math.floor(Math.random() * POOL.length)], createdAt: Date.now(),
  };
  matches.set(m.id, m);
  assign.set(a, m.id); assign.set(b, m.id);
  queue.delete(a); queue.delete(b);
}

setInterval(() => {
  const now = Date.now();
  const list = [...queue.entries()].sort((x, y) => x[1].since - y[1].since);
  const used = new Set();
  for (const [ida, a] of list) {
    if (used.has(ida)) continue;
    for (const [idb, b] of list) {
      if (ida === idb || used.has(idb)) continue;
      const waited = (now - Math.min(a.since, b.since)) / 1000;
      if (Math.abs(a.mmr - b.mmr) <= 100 + waited * 10) {
        makeMatch(ida, idb); used.add(ida); used.add(idb); break;
      }
    }
  }
}, 1000);

const clamp = (n) => Math.max(0, Math.min(100, Number(n) || 0));

app.post("/match/report", (req, res) => {
  const { matchId, accountID, outcome, percent, timeMs, deaths } = req.body;
  const m = matches.get(matchId);
  if (!m || !m.players.includes(accountID)) return res.status(404).json({ error: "no such match" });
  if (m.final || m.reports[accountID]) return res.json({ ok: true });

  // basic sanity check: impossible completion times are rejected as cheating
  let o = outcome;
  if (o === "completed" && Number(timeMs) < 2000) o = "invalid";

  m.reports[accountID] = { outcome: o, percent: clamp(percent), timeMs, deaths, at: Date.now() };
  m.firstReportAt ??= Date.now();

  if (o === "completed") finalize(m, accountID);          // first to finish wins
  else if (Object.keys(m.reports).length === 2) resolveByPercent(m);
  res.json({ ok: true });
});

function resolveByPercent(m) {
  const [a, b] = m.players;
  const pa = m.reports[a]?.outcome === "invalid" ? -1 : (m.reports[a]?.percent ?? -1);
  const pb = m.reports[b]?.outcome === "invalid" ? -1 : (m.reports[b]?.percent ?? -1);
  finalize(m, pa === pb ? null : pa > pb ? a : b);
}

setInterval(() => { // timeout: opponent never reported
  const now = Date.now();
  for (const m of matches.values()) {
    if (!m.final && m.firstReportAt && now - m.firstReportAt > 60000) resolveByPercent(m);
    if (now - m.createdAt > 30 * 60000) { matches.delete(m.id); m.players.forEach((p) => assign.delete(p)); }
  }
}, 5000);

function finalize(m, winner) {
  if (m.finalizing) return;
  m.finalizing = true;
  const [a, b] = m.players;
  const pa = player(a), pb = player(b);
  const ea = 1 / (1 + 10 ** ((pb.mmr - pa.mmr) / 400));
  const sa = winner === null ? 0.5 : winner === a ? 1 : 0;
  const K = 32;
  const da = Math.round(K * (sa - ea));
  const db_ = -da;
  pa.mmr += da; pb.mmr += db_;
  if (winner === a) { pa.wins++; pb.losses++; } else if (winner === b) { pb.wins++; pa.losses++; }
  save();
  m.results[a] = { result: winner === null ? "draw" : winner === a ? "win" : "loss", mmrDelta: da, newMMR: pa.mmr };
  m.results[b] = { result: winner === null ? "draw" : winner === b ? "win" : "loss", mmrDelta: db_, newMMR: pb.mmr };

  Promise.all(m.players.map(async (id) => {
    m.results[id].aiFeedback = await aiFeedback(m, id).catch(() => "");
  })).finally(() => { m.final = true; m.players.forEach((p) => assign.delete(p)); });
}

async function aiFeedback(m, id) {
  if (!process.env.ANTHROPIC_API_KEY) return "";
  const other = m.players.find((p) => p !== id);
  const prompt =
    `You are a Geometry Dash ranked coach. Give 2-3 short sentences in Indonesian rating this player's performance and one tip.\n` +
    JSON.stringify({ result: m.results[id].result, you: m.reports[id] ?? "no report", opponent: m.reports[other] ?? "no report" });
  const r = await fetch("https://api.anthropic.com/v1/messages", {
    method: "POST",
    headers: {
      "content-type": "application/json",
      "x-api-key": process.env.ANTHROPIC_API_KEY,
      "anthropic-version": "2023-06-01",
    },
    body: JSON.stringify({ model: MODEL, max_tokens: 300, messages: [{ role: "user", content: prompt }] }),
  });
  const j = await r.json();
  return j.content?.[0]?.text ?? "";
}

app.get("/match/result", (req, res) => {
  const m = matches.get(req.query.matchId);
  const id = Number(req.query.accountID);
  if (!m || !m.players.includes(id) || !m.final) return res.json({ final: false });
  res.json({ final: true, ...m.results[id] });
});

app.get("/profile", (req, res) => {
  const p = player(Number(req.query.accountID), req.query.username);
  res.json({ ...p, tier: tierOf(p.mmr) });
});

app.get("/leaderboard", (_req, res) => {
  const rows = Object.values(db.players).sort((x, y) => y.mmr - x.mmr).slice(0, 50)
    .map((p) => ({ name: p.name, mmr: p.mmr, tier: tierOf(p.mmr) }));
  res.json(rows);
});

app.listen(PORT, () => console.log(`GD Ranked server on :${PORT}`));
