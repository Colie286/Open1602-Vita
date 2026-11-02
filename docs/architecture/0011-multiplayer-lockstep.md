# 0011 – Multiplayer: lockstep through the host

Status: accepted · 2026-10-18

Builds on [ADR 0004](0004-deterministic-simulation.md): the simulation is deterministic and driven
by commands, so only the commands have to travel over the network. Implemented in `engine/net`; the
menu in `engine/app` (`multiplayer_menu.hpp`, `multiplayer.cpp`).

## Decision

1. **Star, not peer-to-peer.** Every computer is connected to the host only. Only the host needs a
   reachable port (UDP 16020); the others connect outwards, which works through home routers without
   any port forwarding (M-001). A host without a player of its own (a dedicated server) is possible
   the same way.
2. **Frames.** A frame is one call of `Simulation::tick()`, counted from the start of the session. The
   host collects the commands of all players, puts each into the next open frame and sends the frames
   **complete** to everyone ("everything up to frame N is here"). A frame without commands costs
   nothing but that bound. A computer runs complete frames only, so all of them run the same commands
   in the same frame. While the game is paused the frames go on and the game time stands still, so
   commands given during a pause (building) apply the same on every computer. The session drives the
   simulation in place of `Simulation::advance()`; the game speed sets how many frames run per second.
3. **Lead.** The host closes frames a few frames ahead of its own game: as many as a message to the
   slowest computer takes (from the measured round trip and the game speed, 2 to 50 frames). The lead
   grows at once and shrinks slowly. A computer with more frames in hand than the lead catches up.
4. **Slow computers.** The host runs at most one second of game time (plus the lead) ahead of the
   slowest computer; then it waits, and the game gets slower for everyone instead of unfair. If a
   computer stands completely still for 10 s (a debugger, a window being dragged), the game goes on
   without it (2.5 s for a computer that held everyone up before); it catches up, or gets the game anew once it is more than 30 s behind.
5. **Fingerprints.** Every 100 frames (5 s at normal speed; at higher speeds 100 times the speed
   rounded up to a power of two, so about every 5 s of real time) each computer sends the host its
   `Simulation::state_hash()`: XXH64 over the sections of an `.o1602` savegame, unpacked, without
   the original's file the game came from (it never changes) and without the file's header and
   CRC-32, and with the map's tiles as the XXH64 of each block of 16 × 16 tiles. The host compares
   it with its own; not every tick, because each fingerprint writes the game but for the tiles in
   memory, into a buffer kept from one fingerprint to the next. A block is hashed again only after
   one of its tiles changed, so the tiles, most of a large game, cost little after the first
   fingerprint (TB-26; before, on a real 12-player late game in a 2000 × 2000 world, 8 to 9 ms on a
   fast desktop, [performance.md](../performance.md)).
6. **One way for the start, joining, reconnecting and desyncs: the savegame.** The host sends a
   computer its game as an `.o1602` savegame ([ADR 0009](0009-own-save-format.md)) together
   with the speed, the pause and the frames already closed after it. That is how a computer comes into
   the game at the start, into a running game, back after a lost connection, and back into step after
   a desync (M-003). The host writes the game's sections on the game thread and packs them on a
   worker while the game goes on (TB-26); the computer gets the game as it was then, with the frames
   run since. The host is always right. A computer that diverges again right after loading
   (another version) is sent away after two tries. An epoch number per game sent keeps old reports of
   a computer from touching the new game.
7. **Senders.** Every command names its player; the host takes from a computer only commands for its
   own slot (review point 6), none from spectators, and only commands every computer can read back
   (`sim::command_size`; the host's own too). Only the host hands slots to the computer
   (`PlayerControl`); every player may pause, go on, set the game speed and rename himself, at most
   four times a second. With the setting „Speed: host only“ (`host_controls_speed`,
   MS-20) the host takes no pause, resume or speed orders from the others. A message that makes no
   sense, a message to the host larger than 1 MiB and a connection without `Hello` after 3 s
   disconnect the computer.
   **Admission by work.** A frame holds at most 1024 commands and 64 KiB of them, 64 places kept for
   the host's own (`PlayerControl`, names), which are never dropped. Each player has an equal share of
   the rest (at most 256) and of the frame's work, `kFrameWork` units of about one map tile looked at
   (`command_work`: a demolish rectangle its tiles on the island, a build order 16 per item, a soldier
   order 4 per unit, anything else 1). A command that costs more than a player has left still goes in,
   and that player's next commands wait until the following frames have paid it off: a rectangle over
   a whole island takes frames, it does not make one frame seconds long for everyone (MS-3). What does
   not fit waits for the next frames, in order, up to four frames' worth per player; beyond that the
   host drops it and tells the player (`Dropped`). A computer sends its player's commands paced the
   same way, so in practice only a modified one meets the limit. Soldier orders lose their
   `formation` on the host: the formations may differ between computers (data fixes) and the
   interface sends the places itself (DS-5).
   In the lobby a computer may change its slot four times a second, and the host sends the lobby at
   most every 100 ms.
8. **Slots.** A computer gets a free human slot of the world (or the one it asks for) and a token with
   which it gets the same slot back after a lost connection; the program keeps the token of the host
   it played at and sends it when it joins that host again (MS-4), also when the host lets nobody new
   into the running game. Tokens are random and belong to whoever
   holds the slot: a slot that changes hands (in the lobby, after a kick, to a newcomer) gets a new
   one, and the host's own slot takes none. A token never takes a slot from a connection that is
   still there: the newcomer is refused (`Taken`) and may come again once that connection has ended. In a game hosted while it runs (`--host`), the computer
   players' slots are open as well: the computer plays such a slot until someone takes it and again
   after he leaves. Without a free slot it watches. A slot that a person left stands empty for
   60 s (time to come back after a broken connection) and no newcomer gets it in that time, nor while
   a host migration may still bring its player back (MS-5, MS-14); then the computer plays it on under
   the player's name (`PlayerControl` from the host, announced in the chat), until the player or
   someone new takes it. The host chooses this on the create screen ("Computer takes over",
   on by default, shown in the lobby and passed to a successor host); without it, a slot that was
   left gives no commands until someone comes back. The player's name is set as a `PlayerRename`
   of that player.
   **Player IDs** (protocol version 24). The token lives only as long as the program; to know a
   person again after a restart, each installation draws a random 64-bit player ID once and keeps
   it in `settings.cfg` (`player_id`). `Hello` carries it, and the host passes a one-way verifier of
   it, the first 8 bytes of SHA-256 of "open1602 player" and the ID, into the game with the
   `PlayerControl` that gives a person a slot (and for every human slot when the game starts,
   the host's own included): `Player::owner`, saved with the game (`PSTA` version 4), the same on
   every computer and so also known to a successor host. A computer without a token gets the
   slot whose owner is its person, if nobody plays it, also when newcomers may not join the
   running game; a newcomer gets a slot of another person only when no other slot is free (and
   then owns it). A person kicked in the session has no such claim. When a savegame is hosted,
   the lobby knows each slot's owner (`LobbySeat::owner`, not sent): the host takes its own slot,
   and each person who comes in finds theirs; computer players a person played before are open
   for them. Loaded alone (no session), a game gives the person the human player they own, else
   the first, and the computer plays the other human players (`sim::take_single_player`). A
   savegame with several human players that knows none of them as this person's (saved before
   player IDs, on another computer, or after a new ID) asks first which one to play, a button per
   player with its name and colour (`Hud::ask_choice`); the game behind the question waits.
9. **Same data.** When a computer joins, the host checks the protocol version (from the first three
   bytes of `Hello`, which never change, so that any other version is told so) and a fingerprint of the
   game data the simulation depends on (`haeuser.cod`, `figuren.cod`), and the game's password if it
   has one. The names for new cities and ships (`Text.cod` of the installation that began the game)
   are part of the simulation's saved state, so they reach every computer with the game and
   installations in different languages can play together. `Hello` also names the content packages
   installed on the computer, each with version and hash; the host lets it in only with every
   package the game uses (`World::packages()`, before the start those given with
   `HostSession::set_packages`) and otherwise refuses it with `RefuseReason::Packages`, naming what
   is missing (protocol version 14), with their size when it would send them. A computer whose
   player agrees asks again with `Hello::fetch_packages`; the host sends the files (`PackageFiles`)
   before it takes the computer in, and the computer keeps a package only when its files have the
   game's hash (protocol version 15). Packages the computer has in addition are not loaded for the
   game ([packages.md](../modding/packages.md#packages-a-host-sends)).
10. **Transport.** Messages travel reliably and in order over ENet 1.3.18 (`third_party/enet`, MIT,
    shipped unchanged so that the build needs no network). The sessions only know the interface
    `net::Transport`; the tests have an in-memory network with latency, jitter and cuts, and a relay
    or Steam can be plugged in later. A packet is at most 1 MiB + 64 KiB both ways (ENet holds what
    it receives of a packet until it is whole, so a large limit let anyone make the host hold 32 MiB
    per connection, MS-7); larger messages, a game or many frames at once, travel as `Part`s of
    256 KiB, one after another, and are put together by the computer (up to 128 MiB, for worlds of
    4000 × 4000 tiles). The host encodes a game once for every computer that gets it in the same update.
    While the game comes in, the joining computer's lobby shows a progress bar
    (`ClientSession::join_stage()`: the host still writes it, its parts come in with the bytes so far,
    it is loaded); the whole message is read one update after its last part, so that a frame can
    show the loading first. The game reads and unpacks it on a worker thread
    (`ClientSession::load_in_background()`) and only starts it on the game thread: meanwhile the
    computer keeps hearing the host, so that a large world on a slow computer does not outlast the
    connection's timeout, and the messages that came meanwhile are read once the game is there. At most 4 connections
    come from one address, the host takes at most 8 spectators (`max_spectators`), and a computer it
    kicked does not come back in the same session unless it returns to a slot with its token (MS-12).
11. **Lobby.** A host may start without a game: it holds a `Lobby` with the title, a description of
    what is played and the slots of the game to come (open, a human, a computer player, closed).
    Computers come in, take open slots or watch, say they are ready and chat (at most 5 lines a
    second each). The host sets the kinds of the slots, removes players and starts when it wants;
    the game is made from the lobby and every computer gets it as it would get a running one
    (point 6). The lobby travels as a whole whenever it changes. A slot's index is the player of the
    world, and each slot has a banner (point 13); the slots of the free trader, the pirates and the
    natives are reserved and not shown. Every game has all its players: a slot nobody takes is played by
    the computer, and the host hands it to whoever comes into the running game (the command
    `PlayerControl`) and back to the computer when that player leaves.
    **All begin together** (protocol version 25): a game started from the lobby does not run on the
    host while the others still receive it. The host holds its first frame (`HostSession::starting()`,
    `Roster::starting`) until every player of the lobby has the game loaded; each computer tells the
    host how much of the game on its way it has (`LoadProgress`, a few times a second), and the roster
    carries it to everyone (`Member::loaded`). Meanwhile the lobby lists everyone in the game with a
    progress bar (scrolled when there are more than ten), and the others' games stand at frame 0, as
    there are no frames yet. A player whose game makes no progress for 60 s (`start_wait_ms`) is no
    longer waited for, and the host may stop waiting at any time (`start_now()`); such a player
    catches up like one who joins the running game. Spectators are not waited for.
12. **Players.** A new endless game has four to twelve players. Four play on the original's maps;
    more get a world of their own (ADR 0008), as much sea and land per player as the original's
    500 × 350 for four. Players five on sit behind the original's seven slots (slot 7 on) with
    banners of their own colours (`BannerTable::add_more_players`), named after them.
13. **Banners.** Each lobby slot carries a banner (`LobbySeat::banner`), and the lobby the banners on
    offer (`Lobby::banners`): the twelve player colours for an endless game and a scenario, none for a
    savegame or a running game, whose players keep theirs. A slot starts with the banner of its
    number (a scenario: its player's); the host makes them different when the lobby opens. A player
    asks for another banner of its own slot (`SetBanner`), the host sets its own and those of the
    slots no other person plays. A banner another person has is refused; one a computer's, an open
    or a closed slot has is swapped, so that a full lobby can still change colours. The banner
    stays with the slot when players come and go. At the start each player of the world gets its
    slot's banner, and the computers of an endless game the name of their colour; everything else
    follows from the banner as in a game of one player (flags, units, the coats of arms of
    `BannerArt`, whose pattern comes from the banner's id). Protocol version 6.
14. **Finding games.** A host answers searches on UDP port 16021 with what the menu lists (title, host,
    game, players and slots, started, password, joining, spectators, protocol version). A computer
    looking for games asks every network of the computer by broadcast, the computer itself and the
    hosts it joined before directly,
    every 3 s, and forgets a game that has not answered for 6 s. A host answers computers of the local
    network always, others (a forwarded port) twice a second each and 16 times a second in all: the
    answer is about 16 times the search, and a search's sender can be forged (MS-21). Nothing of this is needed to join:
    an address alone does it too, so games on the internet need no server in between.
15. **The menu is Open1602's own.** The original's network screens (DirectPlay sessions, its lobby)
    are not used. Open1602's menu has three screens: finding a game (the list fills by itself, an
    address can be typed, hosts joined before are remembered), creating one (a new endless game with
    its level and world number, a scenario or a savegame; title, password, shown on the local
    network, joining the running game, spectators, host without a player, port) and the lobby
    (slots, ready, chat, start; the host removes players and spectators). Its texts follow the
    language of the installation. Host names are looked up beside the game, so a slow name server
    does not stop the menu.
16. **Host migration.** When the host leaves a running game (it quits or its connection breaks),
    the game moves on to a successor instead of standing still:
    - *When.* A host that quits says so (`HostLeaving`, with the successor's address) before it
      disconnects; then everyone moves on at once. A connection that just breaks is not taken as the
      host leaving, since only this computer's own link may be down (MS-2): the computer first tries
      to get back into its slot at the host with its token, every half second for 10 s
      (`reconnect_ms`; the chat says so), and gets the host's game anew if it does. Only if the host
      cannot be reached does it take the host for gone and move on as below. A host that still holds
      the old connection refuses with `Taken`, which only means "again in a moment".
    - *Who.* The host names the successor in every `Roster`: of the computers that play a slot and
      run the game (not loading, not lagging), the one with the lowest slot, with its address as
      the host sees it (the address only to computers that play; spectators get it with
      `HostLeaving`). Only that computer gets `Standby` from the host: for each slot a verifier of
      its token, the first 8 bytes of SHA-256 of a salt the host drew and the token, and the slots
      the computer plays until someone comes, sent again whenever they change. Not the tokens
      themselves: the successor could take any slot with them while the host still runs. Slots the
      `Standby` names that the lobby does not have, or that are closed, are dropped.
      Everyone has the same roster, so everyone agrees on the successor.
    - *The successor* makes a `HostSession` from its ended `ClientSession` and listens on the port
      of the old host. The game goes on from the frame its own game is at; the frames the old host
      closed beyond it and that it already has stay closed. Password, joining and spectators stay
      as they were. The old host's slot goes to the computer (`PlayerControl` in the first new frame)
      and is open for whoever comes, like a slot the computer held from the start.
    - *The others* make a `ClientSession` from their ended one: the same name, slot and token,
      `Hello::returning` set. It connects to the successor's address (the address of the old host if
      the host saw the successor on its own computer), again every half second while the successor
      is not there yet, for 30 s; the successor may notice the host's leaving later than they do.
      The successor takes them back into their slots by their tokens, checked against the
      verifiers and only with `Hello::returning`; one without a slot (a spectator, or one whose
      slot was taken) watches instead of taking a free slot.
    - *Lockstep.* Every computer that comes back gets the successor's game as a savegame (point 6),
      with the frames closed after it. So all continue from one state, whatever frames each had
      received or run when the host left: frames of the old host beyond the successor's are dropped
      on every computer alike, a computer that was ahead goes back a few frames. Orders the old host
      had not put into a frame yet are lost, as orders in flight are when a connection breaks. The
      frame count goes on, so the fingerprints (point 5) keep their frames and are compared by the
      new host as before.
    - *Fallback.* If the game cannot move on (the host left before the game started, no successor,
      the successor cannot listen on the port, or it does not take a computer in within 30 s), that
      computer saves the game as `hostleft.o1602` as before: it loads with its own slot and the
      computer plays the other people's slots, and it can be hosted anew as a savegame.
    - *Protocol.* Version 3: `Hello::returning`, `Roster::successor` and `successor_address`, the
      message `Standby`.
17. **Chat.** A line (`Chat`, at most 200 bytes, 5 lines a second per computer) goes to everyone
    or privately to some players. It travels beside the frames: it never reaches the game, its
    savegames or its fingerprints. The host fills in who said it, so nobody speaks for another.
    - *To everyone:* the host sends the line to every computer, spectators too.
    - *Privately* (`everyone` false, the players in `to`): only players say something privately,
      and only to players a person plays. The host sends the line to the computers of those
      players and nowhere else, so the others never receive it, not even encrypted. It goes back
      to the computer that said it with `to` cut to the players it reached and the others in
      `missed` (a computer playing the slot, nobody, or a person who left). The host keeps nothing
      of it, so after a host migration the successor routes the same way.
    - *Spectators* read every line to everyone. Whether they may write, to everyone only, is the
      host's choice (`SessionConfig::spectators_chat`, `Lobby::spectators_chat`); the successor
      keeps it.
    - *Limits.* The host passes on every line and could read the private ones with a changed
      program, and ENet does not encrypt, so someone on the same network could read them too.
      Private means that no other player's computer gets the line, not that it is secret from
      the host.
    - *Protocol.* Version 13: `Chat::everyone`, `to` and `missed`, `Lobby::spectators_chat`.

## Rationale

- A star with a host keeps the order of the commands in one place: no agreement among all computers,
  no dependency on the order of the network, one place to check the senders. The delay is one round
  trip to the host, which does not matter for a building game.
- Taking the savegame for everything that means "a computer needs the game" saves three mechanisms
  of their own and is covered by the test "continuous against saving and loading".
- The network load does not depend on the game speed: one message per frame drawn and computer.
- A lobby before the game, rather than slots taken in a running one, lets the players choose their
  slots and the host set the computer players before anything happens; the same savegame path then
  starts the game for everyone.
- A search of its own on a separate port keeps the game port free of anything but sessions, and a
  host behind a router stays reachable by address.

## Consequences

- Videos, menus and windows do not pause the game in multiplayer (M-018); only pause commands do, for
  everyone. The automatic return to speed 1 at a fire or an attack works only on the host; the players change
  the speed by hand, unless only the host may.
- Loading savegames and starting new games is blocked during a session.
- The fingerprint costs writing the game but for the tiles in memory on every computer about every
  5 s of real time, and hashing the blocks of tiles that changed (point 5). The host writes games for joining computers a few at once and then at most one a second; it packs
  them on a worker.
- A host behind a router needs UDP port 16020 (or the one it chose) forwarded for players on the
  internet; the menu says so.
- The host tells every computer the address of the successor, and the successor verifiers of the
  slots' tokens, not the tokens: the successor is a player too, and the host still runs while it
  holds them. Changing the host costs one savegame sent to
  every computer, as at the start.

## Open

- The endless game's own world settings over the network.
- Host migration beyond the local network: the successor is reached at the address the host saw,
  on the old host's port, so on the internet only when that port is open at the successor too.
  Players who had left before the host and want to come back still look for the old host; the
  new host is not shown in the search of the local network (the others reach it by address). A
  relay for hosts behind strict NAT, IPv6 (ENet 1.3 knows IPv4 only), searches beyond the local
  network.
- Replays: the savegame plus the session's frames are already all one needs.
- End-to-end encryption of private chat lines (point 17), so that the host passes them on unread:
  a key pair per computer, its public key in the roster, each line sealed for its recipients.
