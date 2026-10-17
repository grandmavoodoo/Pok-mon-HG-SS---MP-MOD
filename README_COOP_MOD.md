# HGSS Wireless Co-op Mod

Two players link over the DS's built-in **local wireless** (the same system the
Union Room and Pokémon Center Colosseum use) and then:

- **See each other in the overworld.** Your partner walks around as a real
  character on your screen, using their own boy/girl sprite, bike and surf
  sprites. **Their walking Pokémon** (the first Pokémon in their party) follows
  behind them.
- **See each other on the map.** Your partner shows up as a blinking icon on the
  Pokégear town map.
- **Interact.** Face your partner (or their Pokémon) and press **A** to open the
  co-op menu:
  - **Battle:** a real link battle. It runs on copies of your parties, so HP, PP
    and levels aren't changed afterwards, just like the Colosseum.
  - **Trade**
    - **Pokémon:** the game's own wireless trade screen (the Union Room one),
      including trade evolutions.
    - **Item:** pick an item from your Bag and how many (×1, ×5, ×10 or all).
    - **Money:** send $100, $1,000, $5,000, $10,000 or **Other amount**.
    - **Sell:** offer an item (and how many) or a Pokémon for a price you set.
      Your partner sees *"… offers … for $N. Will you buy?"*
    - **Buy:** offer $N for an item or a Pokémon. Your partner picks what to
      sell you, then you see exactly what it is and accept or decline.

  **Other amount** opens a number entry in the text box: ↑/↓ change the red
  digit, ←/→ pick a digit, A confirms, B goes back. Amounts you pay (gifts
  and offers) can't go over what's in your wallet. Prices you ask can be up to $999,999.

  Sold Pokémon are removed from your party. You can't sell your last Pokémon
  that can battle, or one holding Mail. A bought Pokémon goes to your party, or to the PC if
  the party is full. The buyer's money always leaves first, then the seller's
  goods, so nothing can be duplicated. If the link drops in the middle of a deal, the payment can be lost.
  - **Trainer Card:** view your partner's Trainer Card.

  Battles, trades and gifts pop up a *"… wants to … Accept?"* prompt on your
  partner's screen. Viewing a Trainer Card doesn't need permission.

Works in HeartGold and SoulSilver (US), including HG ↔ SS. Both players need
this version of the mod.

## Controls (in the overworld)

| Input | Action |
|---|---|
| Hold **L + R**, press **X** | Host a session (sound plays; wait for your partner) |
| Hold **L + R**, press **Y** | Join a session (connects to the first host it finds, gives up after 30 s) |
| Hold **L + R**, press **B** | Disconnect / cancel |
| Face your partner or their Pokémon, press **A** | Co-op menu |
| **B** while *"Waiting for …"* | Cancel your request |

Sounds: *PC log-in* = searching, *save jingle* = connected, *PC log-off* = disconnected,
*beep* = couldn't start (another wireless feature is already active).

## Pokéwalker

*Connect to Pokéwalker* (main menu) picks an IR port each time you open it:

1. **The IR in a Pokémon Game Card**, the same as the retail game. On a DSi
   or 3DS with TWiLight Menu++ (game on the SD card), put a HeartGold,
   SoulSilver, Black, White, Black 2 or White 2 card in Slot-1 **before**
   you start the game. nds-bootstrap only leaves Slot-1 powered on if an IR
   card is in it when the game starts.
2. **The 3DS's own IR port** (the black spot on the back), through the
   **RtcPwalker** cheat, when no Game Card IR answers. It still needs RTCom
   (TWL_FIRM patched with TWPatch), as before.
3. Neither: the game doesn't find a Pokéwalker, as in retail without IR.

You can leave RtcPwalker on: an IR card in Slot-1 still comes first. Two
things about the cheat itself:
- Without RTCom (a DSi, or a 3DS without the TWPatch firmware) the cheat
  freezes the game a few seconds after it boots, so turn it off there.
- Its 3DS IR service only starts about 17 seconds after the game boots. If you
  open *Connect to Pokéwalker* sooner, back out and open it again.

A DS or DS Lite running the game from a flashcard (DSpico, R4...) has no
IR: the console has no IR port and the flashcard is in Slot-1.

## What's in this folder

- `HeartGold_Coop.nds`, `SoulSilver_Coop.nds`: the patched games (US).
- `pokeheartgold/`: the pret HGSS decompilation, with the mod applied.
- `hgss_coop_mod.patch`: the whole mod as one patch (`git apply` it onto a fresh clone).

### Files the mod touches

| File | Change |
|---|---|
| `src/coop.c`, `include/coop.h`, `include/constants/coop.h` | **New.** Session control, wireless messages, partner character and Pokémon, requests, the `Coop` script command |
| `files/fielddata/script/scr_seq/scr_seq_0003.s` | Two new scripts: `std_coop_menu` (2072) and `std_coop_incoming` (2073) |
| `files/msgdata/msg/msg_0040.gmm` | Menu and prompt texts (rows 117–157) |
| `asm/macros/script.inc`, `src/data/fieldmap/script_cmd_table.h`, `include/scrcmd.h` | Script command 486 `Coop` (retail's unused `Dummy486` slot, so the table keeps its size) |
| `src/field_system.c` | Gets "may the player move" from `Coop_FieldControl`, which runs the co-op update every overworld frame |
| `src/battle/battle_setup.c`, `asm/overlay_12_battle_controller.s`, `asm/include/overlay_12_battle_controller.inc` | Wild and trainer battles ignore the co-op link (see *Normal battles while linked*) |
| `src/application/pokegear/map/*`, `pokegear_map_internal.h` | Partner icon on the Pokégear map |
| `include/unk_02034B0C.h`, `include/unk_02035900.h` | Declarations for two existing comm functions |
| `src/heap.c`, `src/error_handling.c`, `src/error_message_reset.c`, `include/error_message_reset.h` | Debug aid: the "communication error" screen shows where an assert or failed allocation came from |
| `src/pokewalker_ir.c`, `include/pokewalker_ir.h`, `asm/overlay_112.s`, `asm/include/overlay_112.inc`, `lib/include/nitro/card/backup.h` | **New.** Pokéwalker over a Game Card's IR or the 3DS IR port (see *Pokéwalker IR*) |
| `main.lsf` | Puts the co-op and Pokéwalker IR code and data in ITCM (see *nds-bootstrap* and *Cheats*) |
| `Makefile`, `config.mk`, `common.mk`, `tools/usrcheat_crc/` | Refuses to build a ROM where anything moved from its retail address, and gives the ROM the retail usrcheat.dat key (see *Cheats*) |

## How it works

- **Link:** hosting and joining call the same comm-manager entry points as the
  Pokémon Center Communication Club (`sub_02037D8C` host / `sub_02037DD4` join,
  `COMM_TYPE_SINGLE_BATTLE`, 2 players). The game's main loop keeps the link
  running even during menus and wild battles.
- **Wireless messages** (custom comm commands 22–25), registered the moment a
  session starts:
  - 22 **position:** map, x/z, facing, sprite state, map-grid cell, the lead
    Pokémon (species, form, gender, shiny, visible) and the last move (a
    counter plus the game's movement command: walk, run, bike, ledge jump...).
    Sent on change, plus a heartbeat every 30 frames.
  - 23 **request** / 24 **response:** battle, trade, gift, card, cancel;
    accept / decline / busy / no room / prompt shown / gift confirmed.
  - 25 **Trainer Card:** the 0x66C-byte card record, the same format the Union
    Room exchanges.
- **Partner character and Pokémon:** map objects `0xF2` and `0xF3`, created the
  same way as the walking Pokémon that follows you. Their script is the co-op
  menu, so talking to them uses the game's normal NPC-talk path. They only
  appear on the same map (or both outdoors) within 16 tiles.
- **Partner movement:** every move your partner starts is queued on your side
  and replayed with the same movement command, so their character walks, runs
  (with the running animation), bikes and jumps ledges at the real speed along
  the exact path, about 3 frames behind. If moves pile up (a slow moment on the
  link), the queue plays a step faster until it has caught up. More than 8
  tiles or 8 moves behind, the character is moved straight to the latest spot
  without being re-created. Re-creating it reloads its graphics and made the
  game hitch. Their Pokémon retraces the tiles the character leaves, jumps the
  same ledges, and keeps the character's current pace. It walks fast when they
  run, like your own follower.
- **Menus and prompts** are ordinary field scripts. They call the `Coop` script
  command, which does the networking. If your partner is in a menu or battle when
  a request arrives, they get 3 seconds to become free, otherwise you're told
  they're busy.
- **Gifts can't be duplicated:** the receiver accepts, the sender removes the
  item or money from their own game, and only then does the receiver's game add it.
  HMs can't be sent, and Key Items and Mail aren't offered.
- **Choosing the item** uses the Bag screen from the party menu's *Give*
  option, so anything a Pokémon can't hold (HMs, Apricorn Balls) gets
  *"… can't be held"* and you can pick something else.
- **Battles and trades:** after the partner accepts, both DSes meet at a sync
  point and start the link battle (`CallTask_CoopLinkBattle`) or the wireless
  trade (`CallTask_WirelessTrade`) together. Both use their own wireless
  messages, so the mod re-registers its messages right after, and neither side
  sends co-op messages again until both pass a second sync point.
- **Normal battles while linked:** the link stays up during wild and trainer
  battles, which never happens in the retail game. Two places in the battle
  engine read the link's player ID whenever wireless is on: `BattleSetup_New`
  and the "command finished" reply (`ov12_0226430C`). For the player who joined
  (ID 1) that pointed the battle's message queue at the wrong row, which tripped
  a `GF_ASSERT` and the "communication error" screen. Both now use ID 0 unless
  it's a real link battle.
- **Dropped link:** the Colosseum's "strict mode" (fatal error screen + forced
  reset) is kept off in the overworld, so if your partner walks out of range or
  turns their DS off, you're just disconnected.
- **Pokéwalker IR:** the Pokéwalker app (overlay 112) talks to the IR chip
  that HG/SS cards have in front of their save chip
  (`CARD_SpiWaitInit` / `CARD_SpiWaitReadRange` / `CARD_SpiWaitWriteRange`).
  Those calls, plus one in the app's exit, now go to `src/pokewalker_ir.c`
  (in ITCM; one-for-one call swaps, so overlay 112 keeps its retail size):
  - When the app starts it runs the retail IR chip test (SPI command 08h
    answers AAh, the same check overlay 1 and the app itself make). If a chip
    answers, the retail code runs unchanged.
  - Otherwise it asks the RtcPwalker cheat's ARM7 service (RTCom) to open the
    3DS IR port, using the cheat's own protocol: a request block at
    0x027FF400, data at 0x027FF410, an IPC sync interrupt, and flags = 0xF0
    when the ARM7 is done. If nothing answers within 1 s, it goes back to the
    retail code. The app keeps running while it waits.
  - The cheat's ARM9 half only installs itself while the retail call is at
    0x021E5B9C, so it no longer applies, and this file does its job. That half
    took over whether or not a Game Card IR was there, and waited forever for
    the ARM7 service.
- **nds-bootstrap (TWiLight Menu++):** nds-bootstrap has a built-in
  anti-piracy fix for HeartGold/SoulSilver (`IPKE-FFFF.ips` /
  `IPGE-FFFF.ips`). `FFFF` means it applies to *every* ROM with that game code,
  modded or not, and it patches fixed addresses. It turns the
  `bl DC_FlushRange` in `FS_StartOverlay` (0x020D8C1C) into a hook that
  disables the anti-piracy checks in overlays 123 and 1, then calls
  `DC_FlushRange` at 0x020D2894. So the mod keeps everything in the ARM9
  binary up to and including the NitroSDK at its retail size and addresses.
  - All co-op code and data live in ITCM (see *Cheats* below).
  - Hooks inside retail functions only swap which function a call goes to
    (e.g. `FieldSystem_Control` calls `Coop_FieldControl` in place of
    `FieldSystem_IsPlayerMovementAllowed`), so those functions keep their size.

  Earlier builds had grown the code before the SDK by 0x2B8 bytes. That made
  nds-bootstrap's patch overwrite an instruction in `FS_LoadOverlayInfo` and
  jump into the middle of `OS_ReadMessage`, a crash. The build now checks both
  addresses and fails if they move.
- **Cheats (usrcheat.dat):** TWiLight Menu++, Pico Launcher and other
  R4-style loaders look up a game's cheats in `usrcheat.dat` by its game code
  plus a CRC32 of the first 512 bytes of the ROM header. The retail US keys
  are `IPKE-4DFFBF91` (HG) and `IPGE-2D5118CA` (SS). Any modded ROM has a
  different header, so no cheats show up for it. Two things make the retail
  cheats work on this mod:
  - **Same key as retail:** the build sets the header's reserved word at
    0x16C so that the CRC32 comes out to the retail key
    (`tools/usrcheat_crc/usrcheat_crc.py`). Loaders don't use that word, and
    the DS header checksum (0x15E) doesn't cover it. Your usrcheat.dat needs
    no changes.
  - **Same memory layout as retail:** Action Replay codes use fixed RAM
    addresses. For example, about 2,300 of the HeartGold codes start from the
    save pointer at 0x0211186C, and battle and field codes patch overlay code.
    So the mod adds nothing to the game's RAM:
    - Co-op code, tables and variables all sit in ITCM.
    - `Coop` reuses the retail game's unused script command 486
      (`Dummy486`), so the command table doesn't grow.
    - The battle-engine hook is a same-size call swap.

    Everything the game has at a fixed address stays where retail has it:
    static data, every overlay, and where the heaps start. The one exception
    is the Pokégear overlay, which grows for the partner icon. No cheat
    patches Pokégear code. The time, weather and Voltorb Flip cheats that
    share its addresses check for their own overlay's code first, so they do
    nothing while the Pokégear is open, the same as in retail. The build
    checks the layout against every retail-named symbol
    (`tools/usrcheat_crc/check_layout.py`) and refuses to produce a ROM if
    anything moved.

## Building

Prebuilt: **`HeartGold_Coop.nds`** and **`SoulSilver_Coop.nds`** in this folder.

To rebuild after changing the code (this is the setup that works on this PC):

1. **Proprietary tools** are already installed in `pokeheartgold/tools/mwccarm`
   and `pokeheartgold/tools/bin`, with the linker templates copied into place.
2. **MSYS2 (`C:\msys64`)** has the needed packages installed in the UCRT64
   environment: gcc, libpng, pugixml, pkgconf, python and
   arm-none-eabi-binutils. `tools/nitroarc` is the one helper tool that needs
   Unix APIs, so it was built separately with the MSYS `gcc`.
3. **No spaces in the path.** The Makefile breaks on the space in
   `pokemon mod`, so the project is built from a virtual drive. In PowerShell or cmd:
   ```
   subst K: "C:\Users\tonyb\Desktop\pokemon mod\pokeheartgold"
   ```
   (`subst` mappings go away when you reboot. Run it again first.)
4. **Build** from the MSYS2 UCRT64 shell (`C:\msys64\ucrt64.exe`):
   ```
   cd /k
   make heartgold  COMPARE=0 WINPATH=cygpath
   make soulsilver COMPARE=0 WINPATH=cygpath
   ```
   - `COMPARE=0` turns off the checksum check against the retail ROM. The mod
     changes the code, so it can't match.
   - `WINPATH=cygpath` fixes Windows paths in the compiler's dependency files.
   - Outputs: `build/heartgold.us/pokeheartgold.us.nds` and
     `build/soulsilver.us/pokesoulsilver.us.nds`.

The clone's `core.autocrlf` is set to `false`. Git for Windows had converted
some data files (e.g. `tools/gen_fx_consts/fx_const.csv`) to Windows line
endings, which broke a build tool.

## Testing

**melonDS, two instances on one PC:** open the modded ROM, then use
*System → Multiplayer → Launch new instance* and load the ROM there too
(HG in one, SS in the other is fine). Local wireless between instances is
emulated. Two melonDS windows started separately (e.g. by double-clicking two
ROMs) can't see each other; the second one has to come from that menu. It has
its own key setup under *Config → Input and hotkeys*. If they won't find each
other, set up melonDS with BIOS and firmware dumps from your own DS.
1. Load a save in each instance where you're standing outside next to each other.
2. Instance 1: hold L+R, press X. Instance 2: hold L+R, press Y.
3. Walk around and check that each window shows the other player, with their
   lead Pokémon following them (if walking Pokémon are showing in that spot).
4. Open the Pokégear map and check for the blinking partner icon.
5. Face your partner and press A. Try each option:
   - **Battle** and **Trade → Pokémon:** your partner accepts; both games go
     into the battle/trade screen and come back to the overworld afterwards.
   - **Trade → Item / Money:** check that it leaves your Bag/wallet and shows
     up in your partner's.
   - **Trainer Card:** your partner's card should open.
6. After a battle or trade, walk around again and check you still see each other.

**Real hardware:** put the `.nds` on a flashcard in each DS. The DSes need to be
within normal local-wireless range.

**nds-bootstrap / TWiLight Menu++ (DSi, 3DS):** runs these ROMs like the
retail game (see *nds-bootstrap* under *How it works*). Keep the ROMs'
game code (IPKE/IPGE): nds-bootstrap's HeartGold/SoulSilver fixes are keyed
on it. Don't put your own AP patch for these ROMs in
`_nds/nds-bootstrap/apFix/`, since that replaces the built-in one. The save
is the normal 512 KB HGSS save, the same raw `.sav` format melonDS uses.

**Cheats:** the mod shows the same cheat list as retail HeartGold/SoulSilver
(US), from any `usrcheat.dat` that has them. Both `usrcheat.dat` (RtcPwalker)
and `usrcheat1.dat` (DeadSkullzJr's database) in this folder work as they are.
- **TWiLight Menu++:** the file must be named `usrcheat.dat`, in
  `sd:/_nds/TWiLightMenu/extras/`. The cheats are in the game's per-game
  settings, the same as for any retail game.
- **Pico Launcher (DSpico):** `/_pico/usrcheat.dat`. Highlight the game and
  press Y.
- Each loader reads one file named `usrcheat.dat`, so pick one per SD card.
  To use the big database, rename `usrcheat1.dat` to `usrcheat.dat`. It has
  every retail HG/SS cheat except RtcPwalker.
- For these loaders the mod counts as the retail game, so a cheat you switch
  on is on for the retail ROM too (Pico Launcher saves the on/off state in
  usrcheat.dat).
- Cheats run on your own DS only. Your partner sees their effects the same
  way they see anything else you do (where you walk, what's in your party).
- **RtcPwalker:** only its ARM7 half (the 3DS IR service) still installs on
  the mod; the mod has its own ARM9 half (see *Pokéwalker*). With it on, an IR
  Game Card in Slot-1 still works.

**Pokéwalker with a Game Card:** test it on a DSi or 3DS with TWiLight
Menu++. Put the IR card (HG/SS/BW/B2W2) in Slot-1, then start the mod from
the SD card and choose *Connect to Pokéwalker*. Try it with RtcPwalker off
and on: the card should work both ways. Then take the card out, restart with
RtcPwalker on, and check that the 3DS IR port is used.

## Known limitations / not yet verified

The ROMs build cleanly, but the co-op features haven't been tested in-game
yet. Expect to debug a few things on the first test, most likely these:

- **Other link features:** disconnect (L+R+B) before using the Union Room,
  Colosseum, Pal Pad, Pokéwalker, GTS or Wi-Fi. The mod refuses to connect if
  something else is already using the wireless, and disconnects when you enter
  the Union Room, Colosseum or Battle Tower. But starting one of those while
  linked isn't handled and may hang.
- **2 players only.** That's the limit of the battle-type comm session. More
  players would need a Union Room-style session.
- **Surfing:** your partner shows the surfing pose but no Pokémon under them.
- **Movement:** moves the game doesn't make with a plain movement command
  (warp tiles, some scripted moves) aren't replayed. The partner walks straight
  there instead. Ledge jumps play the ledge sound on your side too. When your
  partner turns around, their Pokémon may briefly bump into them. Getting on or
  off the bike re-creates the character, so it skips ahead to where your
  partner is.
- **Both players need this version.** The position message grew by 2 bytes, so
  older builds can't link with this one.
- **Partner's Pokémon on your screen** uses the normal follower sprite but not
  the real follower's extra behaviour (mood, emotes, reacting when you talk to
  it). Talking to it opens the co-op menu.
- **Both players must answer.** If your partner is in a menu, battle or cutscene
  for more than 3 seconds when you send a request, you'll get *"… is busy right
  now."* Try again when they're free.
- **Trainer Card view** shows your partner's card as the Union Room does.
- **The Pokégear icon** is placed when the Pokégear opens and doesn't move while it's open.
- **Saving while linked** stores the partner character in your save. The mod
  removes it the next time you load without a link.
- **Battle rules:** there's no level cap or item clause. It's a plain single battle
  with each player's real levels (`maxLevel = 0`).
