# CoD4 external matchmaking server — design (2026-06-28)

Goal: evolve the current peer-gossip "broker" into a small **standalone matchmaking service** so we get
(1) **pre-Find-Match party invites** (invite a friend, form a party, queue together), (2) **anyone can host**
(not just the `COD4_MMHOST`-designated box), and (3) real **discovery/presence** without each client having to
know peer IPs. This is the natural promotion of what we already built.

---

## 1. Where we are (the primitive we're promoting)

`rexglue-sdk/src/kernel/xam/apps/xgi_app.cpp` already fakes the Xbox-Live session layer:
- `BrokerEntry` (XSESSION_INFO + host real IP:port-map + open/filled slots) and `ArbEntry` (arbitration
  registrants), kept in memory and **gossiped over UDP** (`COD4_MM_PEERS`/`COD4_MM_NETBROKER`, auto-learn) or
  via a shared folder (`COD4_MM_REGISTRY`).
- `XSessionCreate/Search/JoinRemote/ArbitrationRegister` are serviced from those records.
- One box is the host via `COD4_MMHOST`; discovery is point-to-point between known peers.

Limits that motivate a server: peers must pre-know each other; only the designated box hosts; there's no
presence/friends/invite layer; no NAT traversal beyond "both on the same VPN."

## 2. Architecture

```
   ┌──────────────┐         HTTPS/JSON (control) + UDP (rendezvous/relay)        ┌──────────────┐
   │  Client A     │  ───────────────────────────────────────────────────────►  │   mmserver    │
   │ (cod4_mp +    │  register-presence / register-host / search / create-party  │ (standalone   │
   │  rexglue SDK) │ ◄─────────────────────────────────────────────────────────  │  service)     │
   └──────────────┘         invite-push / party-state / host-list / peer-addr     └──────┬───────┘
          ▲   direct game UDP (host:59651) once rendezvous gives the real addr           │
          └──────────────────────────────────────────────────────────────────────────────┘
```

- **mmserver**: a small stateless-ish service (Go or Rust recommended — single static binary, easy to host on
  a VPS; C++ also fine to share structs). Holds: presence table, host/session registry, party table, invite
  queue, arbitration aggregation, and a UDP rendezvous/relay endpoint. In-memory + optional sqlite for
  persistence (botdb-style). No game logic — it's a registry + relay.
- **client**: the SDK's xam/xgi/xfriends/xinvite handlers become **clients of mmserver** instead of
  peer-gossipers. New env `COD4_MM_SERVER=host:port` (when set, takes precedence over `COD4_MM_PEERS`).

## 3. Server API (control plane — keep it boringly simple)

JSON over HTTPS (or a tiny binary UDP framing reusing the existing `BrokerEntry`/`ArbEntry` structs — those
already serialize cleanly; see the net broker). Endpoints:

| call | who | payload | returns |
|---|---|---|---|
| `POST /presence` | every client on signin + heartbeat (~5s) | xuid, gamertag, status{online,party,ingame}, session_token | ok |
| `GET /friends?xuid=` | client (friends list) | — | list of {xuid,gamertag,status} this player may invite/see |
| `POST /host` | whoever creates a hostable session | BrokerEntry (xnkid, real ip:port-map, open/filled, playlist/gametype, host xuid) | host_id |
| `DELETE /host/{id}` | host on teardown | — | ok (also TTL-reaped) |
| `GET /search?playlist=&gametype=` | joiner (XSessionSearch) | filters | list of BrokerEntry (open hosts) |
| `POST /party` | party leader | {leader_xuid, name} | party_id |
| `POST /party/{id}/invite` | leader | {target_xuid} | ok → pushes invite to target |
| `POST /party/{id}/join` | invitee on accept | {xuid} | party state (members + the leader's session info) |
| `GET /party/{id}` | members (poll/SSE) | — | members, leader, current session/host |
| `POST /arb` | each box at match start | ArbEntry (keyed on match-host xnkid) | full registrant list (aggregated) |
| `POST /rendezvous` | both peers | {my real ip:port, peer_xuid, match xnkid} | peer's real ip:port (NAT punch) |

Push (invite-received, party-state-changed): long-poll / SSE / a UDP notify back to the client's presence
address. The client surfaces it via `XNotify` (the in-game "invite received" toast).

## 4. Client integration — the SDK hooks to wire

These are the Xbox-360 APIs the title calls; the SDK already fakes some. Point each at mmserver:

- **Discovery / join** (already broker-backed): `XSessionSearch` → `GET /search`; `XSessionCreate` (host) →
  `POST /host`; `XSessionJoinRemote` uses the returned BrokerEntry (unchanged downstream — the connect path
  already works). Net: replace `BrokerReadHosts`/`BrokerPublishHost` transport with mmserver calls behind a
  `COD4_MM_SERVER` branch (the existing file/UDP paths stay as fallbacks).
- **Anyone-can-host:** drop the `COD4_MMHOST` *designation*. Let the title's own host-fallback decide who
  hosts (it already does — `sub_821AC9A0`); whoever ends up creating the arbitrated session calls `POST /host`.
  Remove the host-only gates that currently force one box (`BrokerReadHosts` returning empty for `COD4_MMHOST`
  becomes "don't return your *own* host_id", keyed on the server-issued id, not the env flag).
- **Presence / friends:** `XFriendsCreateEnumerator` / `XOnlineFriends` (rexglue `xam` friends APIs, currently
  stubbed) → `GET /friends`. `XUserGetSigninState`/signin → `POST /presence`. This gives the in-game Friends
  list real entries you can invite.
- **Invites (the headline):** hook `XInviteSend` (xam) → `POST /party/{id}/invite`; the invite-received
  notification → server push → `XNotify(XN_SYS_INVITE)` → the title shows the toast; accept →
  `XInviteGetAcceptedInfo` returns the party/session → `POST /party/{id}/join` → the title joins the party
  pre-Find-Match. The game already has the **"Invite Friends"** menu item (seen in the lobby) — it routes to
  `XInviteSend`, so this is mostly wiring an already-present UI.
- **Party sessions:** the title models a party as an XSession (the `0joinParty`/`0memberJoin` we trace). The
  party leader's session id is shared via `GET /party/{id}` so members converge on the same lobby, then Find
  Match carries the party (the normal flow), or the leader hosts and members join in-progress (needs Route A).

## 5. Transport / NAT

- **Control plane:** HTTPS to mmserver (works through any NAT — outbound only).
- **Game plane:** still direct peer-to-peer UDP on the Xbox ports (the host's real `ip:port` from the
  registry; the connect path is unchanged). `POST /rendezvous` does a **UDP hole-punch**: both peers send to
  the server, the server hands each the other's observed `ip:port`, both start sending → punches most NATs.
- **Relay fallback:** if hole-punch fails (symmetric NAT), mmserver can **relay** the game UDP (a TURN-ish
  path) — last resort, higher latency, but "anyone can play from anywhere" without VPN. Gate it
  (`COD4_MM_RELAY`) since it costs server bandwidth.

This removes the current VPN requirement: clients only need to reach mmserver; the server brokers the rest.

## 6. Migration path (incremental, each step shippable)

1. **Stand up mmserver** with `/host` + `/search` only; add a `COD4_MM_SERVER` branch in
   `BrokerPublishHost`/`BrokerReadHosts` (reuse the `BrokerEntry` wire format). Now any two clients find each
   other through one well-known server instead of pre-shared IPs. (Lowest risk; supersedes `COD4_MM_PEERS`.)
2. **Anyone-can-host:** remove the `COD4_MMHOST` designation gates; key "is this my own session" on the
   server `host_id`. Validate two arbitrary boxes, neither pre-designated, reach one match (needs **Route A**
   so the live host accepts the joiner — Route A is the prerequisite for clean any-host join-in-progress).
3. **Presence + friends:** `/presence` + `/friends` → real in-game Friends list.
4. **Invites:** `XInviteSend`/`XNotify`/`XInviteGetAcceptedInfo` ↔ `/party/invite` → pre-match party form-up.
5. **Arbitration via server** (`/arb`) — replaces the file/UDP arb aggregation; more robust than gossip.
6. **NAT hole-punch** (`/rendezvous`), then optional **relay** — drop the VPN requirement.

## 7. Open questions / risks

- **Auth/identity:** xuid is currently a fixed-per-profile value (`profiles.toml`); the server needs unique
  ids — issue a server-side id at first `/presence` (or require distinct `profiles.toml` xuids, which we
  already do). Add a lightweight `session_token` so a client can't impersonate another's xuid.
- **The party state machine is the hard guest-side part** — the title's lobby/party SM (the
  `0joinParty`/`0memberJoin`/`partystate` flow + the start-countdown readiness gate that prior work found is
  a *logical* gate, not network) must accept a server-brokered party. The server makes discovery/invite easy;
  it does **not** by itself fix the lobby-countdown / join-in-progress gates — **Route A** and the
  lobby-readiness handling are still required for the actual match to form. The server is the control plane;
  those guest hooks are the data plane.
- **Hosting the server:** a $5 VPS runs it; or one player runs it locally and others point at his IP (same as
  today's broker, but only the *server* needs a reachable address, not every peer).
- **Security:** it's an unauthenticated game-lobby relay for a 2007 game among friends — keep it minimal; rate-limit;
  don't store anything sensitive. Don't expose it publicly without basic abuse controls.

## 8. Relationship to current code

Reuse, don't rewrite: `BrokerEntry`/`ArbEntry` are already the right records; the net-broker thread
(`NetThreadMain`) already does UDP send/recv + an in-memory store — mmserver is "that store, centralized, with
presence/party/invite tables and an HTTP control plane." The `COD4_MM_SERVER` env selects it; `COD4_MM_PEERS`
(direct) and `COD4_MM_REGISTRY` (file) remain as serverless fallbacks for LAN/VPN testing.

**Build order recommendation:** do **Route A** first (it's the prerequisite for any-host join-in-progress and
is independent of the server), then mmserver step 1 (host/search), then invites (steps 3–4) which are the
feature you actually want.
