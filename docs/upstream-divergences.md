# Where the mod deliberately does what upstream does not

Every place the co-op mod chooses to differ from Chronicle (and its Kazarov), on purpose: what upstream does, what we
do, why, and how to revisit it. Started at the re-pin to Chronicle 9418a550 (2026-11-10, Jep's rule: "put every
deliberate divergence from upstream in that file"). A re-pin reads this first; a code comment at each place points
here. Not listed: co-op's own additions where upstream has nothing to differ from (netplay, the lobby, the HUD), and
mechanical re-homes of our hooks into upstream's moved code (the development tree's debug\build\repin\review-*.md, not published).
Kept here at the mod's root (docs\) since the 9418a550 rebase: it is useful to anyone building on the mod.

## Game behaviour (Jep's decisions)

### The aerial dungeon map: dropped with upstream
- Upstream: deleted Chronicle's aerial map (the floor seen from above) at aa983e72 ("Revert Feat/texture overrides
  (#187)"); the dungeon map is retail's tiles only.
- We: the same (Jep, 2026-11-10, "1a"). It was our option (game.aerial_map, off by default since coop-minimapfill), so
  players see no change. Our hooks that lived in its file and drew on retail's map too stay, re-made on upstream's
  CDungeonMap::DrawMiniMap (game/dungeonmap.cpp): the partner's arrow in each player's fixed colour, the tests'
  DC_COOP_MAPARROWS / DC_COOP_MAPTILES reports. What existed only on the aerial map went with it (minimap2's symbols
  instead of bodies, the crystal's symbols, the item dots' re-placing, the map camera on player 2) and so did their rows.
  v-minimap-fill's twin is DC_COOP_MUTATE=mapfill now (the tiles see-through) instead of aerialmap.
- Revisit: only if upstream brings an overhead map back; this is then not a divergence at all.

### The title menu's MULTI-PLAYER row
- Upstream: the title menu drawn from the font (title::menu: START, CONTINUE, OPTION, EXIT; c6afc38d, e4369597,
  dbfeabd1).
- We: when co-op shows it (TitleMpShown), MULTIPLAYER is a fifth row between CONTINUE and OPTION (Jep, 2026-11-10,
  "2a"): five rows from y 274, 26 apart, so they stay above the trademark lines (405); rows and selects are told apart
  (title::menu RowOf / SelectAt), and coop's Host / Join submenus draw on the same rows in the menu's own letters
  (DrawRow). Without the row the layout is exactly upstream's (from 284, 28 apart). Coop's painted words
  (title_mpwords, title_letters.png) are gone with retail's painted rows. Rows: mp-title-row (+~pitch, the layout).
- Revisit: when upstream changes its row table, geometry or Draw: re-merge kRows, the top and pitch, and titleloop's
  row/select mapping (up/down, pointer, confirm).
- Needs a native check: the row's key title.menu.multiplayer in upstream's four language files, ours (2026-11-10):
  de_de MEHRSPIELER, es_es MULTIJUGADOR, fr_fr MULTIJOUEUR, it_it MULTIGIOCATORE.

### Quit from the in-game menu
- Upstream: e4369597 "Quit from Main Menu": Quit -> Yes returns this copy to the title at once (quit_to_title_pending;
  it exited the program before).
- We, single player: the same. In a netplay session (Jep's rule, 2026-11-10) the Yes is a lockstep command (kCmdQuit,
  coop_fullmenu.cpp FullMenuQuitSent; netplay.cpp ApplyQuit) that every copy applies on one tick: the host's (player
  1's) quit ends the session for every player, each copy at the title with its link closed and a line; anyone else's
  takes only that player to the title, a clean leave by the closed-game Leave path, and the host and the others play
  on. Per player, never "the other one". Rows: net-quit-host (+~alone), net-quit-p2 (+~all, ~untold).
- Why: upstream's local return left a linked copy at the title with its link up, which netplay never handled.
- Revisit: if upstream adds its own multiplayer quit; if the link grows past two copies (kLeaveEnded and the per-player
  apply already assume N).

### The floor builder's guards (dungeonmap.cpp, buildMapDat)
- Upstream: defines buildMapDat between scanRoomInfoWork and buildMapWork on purpose, so retail's reads and writes one
  row past either end of the grid land in the arrays retail had there (retail-faithful edge cells).
- We: 20-cell guards either side of buildMapDat in their own .data$ sections (g_build_map_before/after), cleared on both
  copies by a resync (PortBuildMapGuardsClear), PortBuildMapGuardsPlaced checks the layout. Upstream's neighbours hold
  leftovers of earlier builds, which differ between two copies with different histories, so two linked copies could
  build different floors from one seed (the co-op bug the guards fixed). Jep, 2026-11-10: keep the guards ("4a").
- Cost: an edge cell can differ from retail's build (the same on every copy).
- Revisit: upstream's layout with netplay's resync also clearing scanRoomInfoWork and buildMapWork on every copy, once
  something shows nothing else reads those two between builds; then re-run the floor rows (x-floor-*, x-dungeon-*).

## Upstream functions co-op replaces (the 31 former PORT_OVERRIDEs, and read_pad)

Upstream's definition is removed by a recipe hunk (a one-line comment names our file) and ours stands in. Each was
reviewed against upstream's changes since 6ec3b808 at the 9418a550 re-pin (the development tree's debug\build\repin\review-{A,B,C,I}.md:
kept ours / took upstream's / merged). Revisit each at every re-pin: diff upstream's function at the old and new pin
and carry any fix into ours.

| Function | Ours in | Why ours |
|---|---|---|
| SndSePlay | src\coop\coop_audio.cpp | on the partner's copy the player's fighting sounds are placed where he stands |
| SndSPSePlay | src\coop\coop_audio.cpp | coop-chestsound: the player's presentation jingles come from where he stands on the partner's copy |
| SndSetCamera (both) | src\coop\coop_audio.cpp | coop-soundpt: player 2's town sounds listen from her own camera |
| SetBattleStyle | src\coop\coop_audio.cpp | retail's, then this copy's own battle music (BattleMix) |
| BtBattleMusic_Init / _Stop | src\coop\coop_audio.cpp | this copy's own battle music goes with the game's |
| _BOM_SET, _SET_STATUS_ERR | src\coop\coop_combat.cpp | coop: combat: a trap the partner sets off goes off at, and ails, the partner |
| CMonstorUnit::CheckDmg | src\coop\coop_combat_damage.cpp | the partner's hits by the game's rules for her character, with her weapon, her shots falling off with distance from her |
| CRandomItem::Draw | src\coop\coop_interact.cpp | coop-dungeonp2: on the partner's copy her dropped items draw within reach of her, not the player (draw only) |
| CStealItem::Step / checkEvent | src\coop\coop_inventory.cpp | an item player 2 stole homes onto her and goes into her own bag |
| DrawtargetCursor | src\coop\coop_lockhud.cpp | on player 2's copy the lock corners are the other player's (dim, near her only) |
| _GET_DISTANCE, _GET_POSITION, _GUARD_SEARCH, _GET_CHR_ID, _GET_USER_STATUS | src\coop\coop_target.cpp | monster scripts read the player the monster is after (the partner's place, heading, member, status) |
| _CHK_USER_INNER_PRODUCT | src\coop\coop_target.cpp | the same, and it returns 1 (retail fell off its end; upstream now does too) |
| CMonstorUnit::CheckViewLevel / MoveCheck2 | src\coop\coop_target.cpp | monsters wake by the nearer of the players; MoveCheck2 keeps a monster's step clear of the partner as of the player |
| _OPEN_ESCAPE_WINDOW | src\coop\coop_useprompt.cpp | the gate's escape window is answered by both players' own prompts |
| getAtraToSaveData, CSaveData::SetGameFlag / SetMapFlag / VisitMap / SetGameIntFlag | src\coop\coop_worlds.cpp | the game's own, and a copy playing apart tells the other (one save for both) |
| CEditGround::StepWater | src\coop\coop_worlds.cpp | the water's random draws do not step the game's generator (each copy's camera sees different water) |
| StartMessageDraw | src\added\port\src\floor_title.cpp | the floor title card laid out as NTSC retail drew it on NTSC data, PAL's on PAL (DataNtscRelease) |
| motionDrive | src\added\port\src\dun\motiondrive.cpp | coop-p2absent: its regeneration counter is a global the link zeroes on every copy and checksums (retail's function static counted each copy's own ticks before a join); coop-chestworld; the player lying down |
| read_pad | (a block at the top of upstream's, game/gamepad.cpp) | while linked every port is ready and reads the pad all copies agreed on |

## Tell upstream

Bugs in Chronicle itself that we work around (each also under "Smaller ones" below); the coordinator passes them on.
- game/menu_misc.cpp InitMenuWeaponModelReference: retail's loop clears 12 entries of the 10-entry MenuWeaponModelInfo, and
  the two extra land on whatever the link puts after the table (not MenuCharaOldAmbient as the comment there expects). On
  9418a550's link that is MonstorScriptBuffer: opening the weapons menu sets the first monster script arena's base and
  buffer pointers to -1, in single player too.
- game/sysmes.cpp mes_buff: a heap buffer since 9418a550; SystemMessage (a global holding tex_buff, its pointer) is plain
  data, so anything that copies or restores globals wholesale (a save state, our state set) carries a dead heap address.
  Harmless in single player as it stands; worth knowing for any snapshot/rewind feature.

## Smaller ones

- DC_HEADLESS_PACED=1: upstream added the same switch as DC_HEADLESS_BOUNDED=1 (headless at the game's pace); ours is
  kept as an alias in port/src/main.cpp because the harness sets it in ~90 files. Revisit: rename in the harness, drop
  the alias.
- The system message's text buffer (game/sysmes.cpp mes_buff): upstream made retail's fixed 64 KB array a heap buffer
  (std::make_unique); we keep the fixed array (recipe 0061). SystemMessage.tex_buff points at it and SystemMessage is in
  the state set, so a join's or a fixture's set put the taker's heap address on every other copy, whose next system
  message wrote into its heap: exit crashes with heap corruption (0xC0000374) on the chest and Atla rows (repin phase 5,
  2026-11-10; found with snapshot_state.cpp's DC_SNAPSHOT_HEAPPTR trace). Revisit: if upstream re-points tex_buff itself
  or ClsMes stops holding the pointer; any new heap pointer inside a state-set global shows in that trace.
- The weapons menu's model table (game/menu_misc.cpp InitMenuWeaponModelReference): retail clears 12 entries of the
  10-entry MenuWeaponModelInfo; upstream keeps the loop and defines MenuCharaOldAmbient straight after the table for the two
  extra to land on. The link does not keep that order: on 9418a550's link MonstorScriptBuffer follows the table, and the
  weapons menu set the first monster script arena's pointers to -1 (t-triangle-menu-herweapons-guard). We clear the ten and
  write the two extra into MenuCharaOldAmbient by name (recipe 0046): retail's effect, without the overrun. Revisit: if
  upstream fixes the loop or the table's size. Worth telling upstream: it is their layout's bug in single player too.
- The autosave after a debug start: upstream's WriteAutosave (towns and, new, every dungeon floor) also refuses while
  DebugStartSavesOff (test runs never reach a real save).
