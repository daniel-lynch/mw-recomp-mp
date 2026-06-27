#!/usr/bin/env python3
"""
select_roster.py — pick the "online right now" lobby roster from the bot population.

Implements the play-window idea: only bots whose online window covers the current
hour are eligible, and selection is weighted toward REGULARS (so you keep seeing
familiar names) and toward bots who MAIN the chosen gametype, with casuals mixed in.
The result is written to roster.json; the launcher calls this once per queue, so the
same crew "stays in your lobby until you requeue" (re-run = new draw).

Usage:
  python3 select_roster.py --count 11 --gametype dom
  python3 select_roster.py --count 11 --gametype war --hour 21   # override the clock
  python3 select_roster.py ... --out ~/.local/share/cod4_mp/roster.json
"""
import argparse, json, os, random, sqlite3, time

def default_db():
    root = os.environ.get("XDG_DATA_HOME") or os.path.join(os.path.expanduser("~"), ".local", "share")
    return os.path.join(root, "cod4_mp", "bots.db")

def is_online(start, end, hour):
    if start == end:           # ~24h online
        return True
    if start < end:
        return start <= hour < end
    return hour >= start or hour < end   # window wraps past midnight

def weighted_sample(items, weights, k):
    """Efraimidis-Spirakis weighted sampling without replacement."""
    rng = random.random
    keyed = sorted(((rng() ** (1.0 / max(w, 1e-9)), it) for it, w in zip(items, weights)),
                   reverse=True)
    return [it for _, it in keyed[:k]]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--db", default=default_db())
    ap.add_argument("--out", default=os.path.join(os.path.dirname(default_db()), "roster.json"))
    ap.add_argument("--count", type=int, default=11)
    ap.add_argument("--gametype", default="war")
    ap.add_argument("--hour", type=int, default=None, help="override current hour 0-23 (default: now)")
    a = ap.parse_args()
    hour = a.hour if a.hour is not None else time.localtime().tm_hour

    con = sqlite3.connect(a.db)
    con.row_factory = sqlite3.Row
    rows = list(con.execute("SELECT * FROM bots"))
    online = [r for r in rows if is_online(r["online_start"], r["online_end"], hour)]
    pool = online if len(online) >= a.count else rows   # fall back to whole pop if too few on
    # weight: regular x3, mains-this-gametype x2 (stacks), everyone else baseline 1
    weights = [(3 if r["is_regular"] else 1) * (2 if r["fav_gametype"] == a.gametype else 1)
               for r in pool]
    picked = weighted_sample(pool, weights, min(a.count, len(pool)))

    roster = [{
        "id": r["id"], "name": r["name"], "rank": r["rank"], "xp": r["xp"],
        "prestige": r["prestige"], "skill": r["skill"], "playstyle": r["playstyle"],
        "fav_gametype": r["fav_gametype"], "is_regular": r["is_regular"],
    } for r in picked]
    payload = {"selected_at": int(time.time()), "hour": hour, "gametype": a.gametype,
               "online_pool": len(online), "roster": roster}
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    with open(a.out, "w") as f:
        json.dump(payload, f, indent=2)
    # also emit a dead-simple game-readable roster: one bot per line, pipe-delimited
    # "name|rank|prestige|skill|playstyle|fav_gametype" in spawn order (the game-side hook
    # parses this without a JSON lib). roster.json stays the rich source for the other tools.
    txt = os.path.splitext(a.out)[0] + ".txt"
    with open(txt, "w") as f:
        for r in roster:
            f.write(f"{r['name']}|{r['rank']}|{r['prestige']}|{r['skill']}|{r['playstyle']}|{r['fav_gametype']}\n")

    print(f"roster -> {a.out}")
    print(f"hour {hour:02d}:00  gametype {a.gametype}  online_pool {len(online)}/{len(rows)}  picked {len(roster)}")
    for r in roster:
        tag = "REG" if r["is_regular"] else "   "
        main = "*" if r["fav_gametype"] == a.gametype else " "
        print(f"  {tag}{main} {r['name']:<22} Lv{r['rank']+1:<2} P{r['prestige']} skill{r['skill']} {r['playstyle']}")
    con.close()

if __name__ == "__main__":
    main()
