#!/usr/bin/env python3
"""
gen_bots.py — generate the persistent CoD4-MP "fake Xbox Live population" database.

Phase 1 of the bot-ecosystem: a SQLite DB of ~1000 bots, each with a name, real
rank/XP/prestige, a BW-native skill (1-7) + playstyle, a favorite gametype, and an
"online window" (the hours of day they tend to be playing). Downstream:
  - a roster selector picks who's "online now" (regulars + randoms) at match start,
  - the game spawns those exact identities (name + skill + playstyle),
  - XP sync writes match results back, and an offline cron advances the population.

Usage:
  python3 gen_bots.py                 # create ~1000 bots at the default DB path
  python3 gen_bots.py --count 1000 --db ~/.local/share/cod4_mp/bots.db --seed 1
  python3 gen_bots.py --reset         # wipe + regenerate

Default DB: $XDG_DATA_HOME/cod4_mp/bots.db (or ~/.local/share/cod4_mp/bots.db).
"""
import argparse, os, random, sqlite3, time

# ---- name generation: believable gamertag-style handles ---------------------
PREFIX = ["", "", "", "x", "xX", "Pro", "Dark", "Silent", "Mad", "Iron", "Toxic", "Lone",
          "Big", "Lil", "Sgt", "Cpt", "Mr", "The", "Dr", "Sir", "OG", "Real", "Its", "Just",
          "Sneaky", "Crazy", "Epic", "Savage", "Frost", "Night", "Shadow", "Red", "Blue", "Ghost"]
CORE   = ["Wolf", "Sniper", "Reaper", "Hunter", "Ninja", "Demon", "Tiger", "Viper", "Hawk",
          "Phantom", "Raptor", "Cobra", "Falcon", "Bear", "Shark", "Bullet", "Blade", "Killer",
          "Gamer", "Soldier", "Marine", "Ranger", "Sniper", "Trigger", "Recon", "Maverick",
          "Striker", "Goose", "Ace", "Bandit", "Outlaw", "Rogue", "Specter", "Havoc", "Wraith",
          "Crusher", "Slayer", "Predator", "Warden", "Goblin", "Saint", "Titan", "Comet", "Storm"]
SUFFIX = ["", "", "", "Xx", "_", "TTV", "YT", "HD", "MW", "x", "z", "360", "07", "187", "420",
          "69", "99", "23", "2007", "v2", "Pro", "Gaming", "Live", "USA", "UK", "Sr", "Jr"]
FIRST  = ["Jake", "Tyler", "Cody", "Brandon", "Austin", "Dylan", "Hunter", "Mason", "Logan",
          "Ethan", "Connor", "Blake", "Chris", "Mike", "Josh", "Nick", "Ryan", "Sean", "Devin",
          "Kyle", "Trevor", "Marcus", "Andre", "Diego", "Ivan", "Yuki", "Liam", "Noah", "Cole"]

def gen_name(rng):
    style = rng.random()
    if style < 0.55:                 # gamertag combo
        n = rng.choice(PREFIX) + rng.choice(CORE) + rng.choice(SUFFIX)
        if rng.random() < 0.30 and not n[-1].isdigit():
            n += str(rng.randint(1, 9999))
    elif style < 0.80:               # firstname + number
        n = rng.choice(FIRST) + (str(rng.randint(1, 9999)) if rng.random() < 0.8 else "")
    else:                            # first + core
        n = rng.choice(FIRST) + rng.choice(CORE)
    return n[:31] or "Player"        # client name cap

# ---- progression model ------------------------------------------------------
MAX_RANK = 54                        # 0-based (Lv55 = Commander, the cap)
MAX_XP   = 65000                     # ~RANKXP at Lv55 (matches observed 65540)
def xp_for_rank(r):
    # smooth increasing curve to ~MAX_XP at MAX_RANK (early ranks cheap, later steep)
    return int(MAX_XP * (r / MAX_RANK) ** 1.7)

PLAYSTYLES = ["rusher", "camper", "objective", "sniper", "tactical", "runner"]
GAMETYPES  = ["war", "dm", "dom", "sab", "sd", "koth"]

SCHEMA = """
CREATE TABLE IF NOT EXISTS bots (
  id            INTEGER PRIMARY KEY,
  name          TEXT NOT NULL UNIQUE,
  rank          INTEGER NOT NULL,   -- 0..54 (0-based; menu displays +1)
  xp            INTEGER NOT NULL,   -- RANKXP
  prestige      INTEGER NOT NULL,   -- 0..10
  skill         INTEGER NOT NULL,   -- Bot Warfare difficulty 1..7
  playstyle     TEXT    NOT NULL,   -- rusher|camper|objective|sniper|tactical|runner
  fav_gametype  TEXT    NOT NULL,   -- war|dm|dom|sab|sd|koth (mostly plays this)
  online_start  INTEGER NOT NULL,   -- hour 0..23 they come online
  online_end    INTEGER NOT NULL,   -- hour 0..23 they go offline (may wrap past midnight)
  is_regular    INTEGER NOT NULL,   -- 1 = recurring "regular", 0 = casual
  kills         INTEGER NOT NULL,
  deaths        INTEGER NOT NULL,
  wins          INTEGER NOT NULL,
  losses        INTEGER NOT NULL,
  last_active   INTEGER NOT NULL    -- unix ts (updated by play / cron)
);
CREATE INDEX IF NOT EXISTS idx_online   ON bots(online_start, online_end);
CREATE INDEX IF NOT EXISTS idx_regular  ON bots(is_regular);
CREATE INDEX IF NOT EXISTS idx_fav      ON bots(fav_gametype);
"""

def default_db():
    root = os.environ.get("XDG_DATA_HOME") or os.path.join(os.path.expanduser("~"), ".local", "share")
    return os.path.join(root, "cod4_mp", "bots.db")

def weighted_rank(rng):
    # most of the population is low-mid rank; a tail of veterans + prestige
    r = rng.random()
    if r < 0.55:   return rng.randint(0, 24)     # Lv1-25 casual majority
    if r < 0.85:   return rng.randint(25, 44)    # Lv26-45 regulars
    return rng.randint(45, MAX_RANK)             # Lv46-55 veterans

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--db", default=default_db())
    ap.add_argument("--count", type=int, default=1000)
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("--reset", action="store_true")
    a = ap.parse_args()
    rng = random.Random(a.seed)
    os.makedirs(os.path.dirname(a.db), exist_ok=True)
    con = sqlite3.connect(a.db)
    if a.reset:
        con.execute("DROP TABLE IF EXISTS bots")
    con.executescript(SCHEMA)

    existing = {row[0] for row in con.execute("SELECT name FROM bots")}
    now = int(time.time())
    added = 0
    attempts = 0
    while added < a.count and attempts < a.count * 50:
        attempts += 1
        name = gen_name(rng)
        if name in existing:
            continue
        existing.add(name)
        rank = weighted_rank(rng)
        prestige = 0
        if rng.random() < 0.18:                  # ~18% have prestiged
            prestige = min(10, int(rng.expovariate(1.2)) + 1)
        skill = max(1, min(7, int(rng.gauss(4.2, 1.4))))   # bell around 4, BW 1-7
        playstyle = rng.choice(PLAYSTYLES)
        fav = rng.choice(GAMETYPES)
        # online window: a start hour + a 3-8h session, allowed to wrap past midnight
        start = rng.randint(0, 23)
        end = (start + rng.randint(3, 8)) % 24
        is_regular = 1 if rng.random() < 0.18 else 0       # ~18% regulars
        # K/D/W-L shaped by skill (higher skill -> better ratios) + volume by rank
        games = rank * rng.randint(8, 18) + rng.randint(0, 50)
        kdr = 0.5 + skill * 0.18 + rng.gauss(0, 0.15)
        deaths = max(1, int(games * rng.uniform(6, 12)))
        kills = max(0, int(deaths * max(0.2, kdr)))
        wins = int(games * rng.uniform(0.40, 0.60))
        losses = max(0, games - wins)
        xp = xp_for_rank(rank) + rng.randint(0, max(1, xp_for_rank(min(MAX_RANK, rank + 1)) - xp_for_rank(rank)))
        last = now - rng.randint(0, 14 * 86400)            # active within ~2 weeks
        con.execute(
            "INSERT INTO bots(name,rank,xp,prestige,skill,playstyle,fav_gametype,"
            "online_start,online_end,is_regular,kills,deaths,wins,losses,last_active) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            (name, rank, xp, prestige, skill, playstyle, fav, start, end, is_regular,
             kills, deaths, wins, losses, last))
        added += 1
    con.commit()
    total = con.execute("SELECT COUNT(*) FROM bots").fetchone()[0]
    regs  = con.execute("SELECT COUNT(*) FROM bots WHERE is_regular=1").fetchone()[0]
    print(f"bots.db: {a.db}")
    print(f"added {added} (total {total}; regulars {regs})")
    print("rank histogram (Lv brackets):")
    for lo, hi in ((0,9),(10,19),(20,29),(30,39),(40,49),(50,54)):
        c = con.execute("SELECT COUNT(*) FROM bots WHERE rank BETWEEN ? AND ?", (lo,hi)).fetchone()[0]
        print(f"  Lv{lo+1:>2}-{hi+1:<2}: {'#'*(c//8)} {c}")
    print("sample:")
    for row in con.execute("SELECT name,rank,prestige,skill,playstyle,fav_gametype,online_start,online_end,is_regular "
                           "FROM bots ORDER BY RANDOM() LIMIT 8"):
        nm,rk,pr,sk,ps,fav,os_,oe,reg = row
        tag = "REG" if reg else "   "
        print(f"  {tag} {nm:<22} Lv{rk+1:<2} P{pr} skill{sk} {ps:<10} {fav:<5} online {os_:02d}:00-{oe:02d}:00")
    con.close()

if __name__ == "__main__":
    main()
