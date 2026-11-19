<div align="center">

# Features

**Everything the co-op adds to Dark Cloud, one section each, and how to use it.**

[Back to the README](README.md)

</div>

The clips are both players' screens side by side: player 1 on the left, player 2 on the right.

<details>
<summary><b>All features</b></summary>

**Playing together**
- [Drop in, drop out](#drop-in-drop-out)
- [Play apart](#play-apart)
- [Shared story and world](#shared-story-and-world)
- [Profiles](#profiles)
- [Connecting](#connecting)
- [Local play](#local-play)

**Items and characters**
- [Send items](#send-items)
- [Shared key items](#shared-key-items)
- [Shared storage](#shared-storage)
- [Bag upgrades](#bag-upgrades)
- [Trade characters](#trade-characters)
- [Character-limited floors](#character-limited-floors)

**Dungeons**
- [Fighting together](#fighting-together)
- [Leaving a floor](#leaving-a-floor)
- [Arriving on a floor together](#arriving-on-a-floor-together)
- [Between floors](#between-floors)
- [Back floors](#back-floors)
- [Story scenes that need everyone](#story-scenes-that-need-everyone)
- [Pausing](#pausing)

**Town**
- [Fishing together](#fishing-together)
- [Time of day](#time-of-day)

**On your screen**
- [Player colours](#player-colours)
- [Faces that move](#faces-that-move)
- [Your partner on the map](#your-partner-on-the-map)
- [Where is everyone](#where-is-everyone)
- [Area title cards](#area-title-cards)

**Settings**
- [Settings](#settings)

</details>

---

## Playing together

### Drop in, drop out

Player 2 can join or leave at any time. The host doesn't stop or wait for you, whatever they're doing: a menu, a cutscene, fishing, a fight or a dungeon floor.

- **Joining loads you in like loading a save:** you start in the town of the world's current map, at the spot a save load puts you, and go find the host from there.
- **Coming back to the same floor:** if you dropped out on a dungeon floor and the host is still on that floor when you reconnect, you go back next to them. Anywhere else, you load into town.

- **Joining a game that's already running:** the lobby shows up on the joiner's screen only. The host keeps playing while you pick your profile and character.
- **If you drop and reconnect,** you get your profile and character back automatically, no lobby.
- **If the host drops,** they need to host again.

<div align="right"><a href="#features">back to top</a></div>

### Play apart

Both players are their own person. You can be anywhere: one in town and one in the dungeon, on different floors, in different houses, or building Georama separately. One player's activity never holds up the other unless it really has to.

| Shared between players | Each player's own |
|---|---|
| Story progress | Where you are and where you're going |
| The state of the world | Your bag and inventory |
| Weapons and the characters' stats | Your menus |
| Key items | Your camera and character |

When you're apart, what you change in the world (a chest opened, a house built) is there for the other player when they get to it.

While anyone builds Georama, the townspeople keep walking around, and you see them from build mode too. Put a part down where one is standing and only that one moves, at once, to a free spot somewhere else in town (the same spot on every player's screen).

### Shared story and world

There's one story and one world, and both players see the same thing in it.

- **Story moments wait for player 1.** Player 2 can't set off a story scene alone. If player 2 is already there when one starts, they watch it too, with the scene's own camera and text, including its name screen (only the player typing can change it).
- **Story doors ask first.** Opening the door to a house whose story scene is still to come asks everyone else first, even someone standing right beside you. You see who's coming, and you can start without the others or cancel (see [Story scenes that need everyone](#story-scenes-that-need-everyone)).
- **Cutscenes stay on your own screen.** When the other player opens a chest or watches a scene of their own, you see them do it in the world. Your camera, your text and your fades are never taken over.

### Profiles

Each player has a **profile**: a name your own bag (your items and gilda) is saved under. Profiles are saved with the host's save slot, so your bag lives in that host's game.

- **In the lobby,** each player picks a profile (or makes a new one) and a character, and locks them in before the game loads. Your usual profile is already selected for you.
- **Your profile name is how the others see you:** questions, waiting boxes and messages name players by profile, and when another player comes into or leaves your area you see their character's face and their name ("Jep has entered the area"). You aren't told about your own moves.
- **A new profile starts with an empty bag.** See [Bag upgrades](#bag-upgrades) for what carries over.
- **A save holds up to six players' bags.** The first time you save over a save from v0.1.1 or earlier, its old co-op files are kept beside it in the save's folder (`coop-p2.dat.v0.1.1-backup` and the like): rename one back to restore it.

### Connecting

On the title screen, START and CONTINUE are single player. **MULTIPLAYER** opens **Host**, **Join** and **Local**.

| Connect by | Good for |
|---|---|
| **IP address** | Same home network, or over the internet with [Tailscale](https://tailscale.com) |
| **NAT code** | Over the internet. The joining player enters the host's code. If the host's network has strict NAT settings, both players need to enter each other's code. |
| **Steam** | Joining through Steam. Needs a one-time setup: run `Set up Steam.bat` in the mod's folder. It asks before each step. |

- The lobby hides your internet address until you press <kbd>R1</kbd>, and remembers your choice. The NAT code is always shown, since that's what you send your friend.
- Both players need the same version of the mod. The game checks, and won't connect otherwise.
- When you join, you play by the host's game settings for that session. Your own `config.json` isn't changed.

### Local play

**Local** on the MULTIPLAYER screen gives you an early split-screen mode, and a multi-screen mode (one window per player) for dual monitors or a TV. Split-screen is still very much in development.

<div align="right"><a href="#features">back to top</a></div>

## Items and characters

### Send items

<img width="1280" alt="Send items" src="https://github.com/user-attachments/assets/2398b616-7508-482b-8644-0676221f5d82" />

Open your bag, go to the **Items** page, pick an item and press <kbd>Select</kbd>. It goes straight into the other player's bag. You can send while you're apart, too.

### Shared key items

Key items live on a key items tab both players share, so neither of you is ever missing a key. The Fishing Rod lives there too: once the story gives it, every player can use any town's fishing sign, alone or together.

### Shared storage

Both players use the same storage, and both can have it open at the same time.

### Bag upgrades

A bag upgrade counts for every profile in the save, including profiles made later.

### Trade characters

<img width="1280" alt="Trade characters" src="https://github.com/user-attachments/assets/439a95bf-0cf8-4788-88e4-bbabc75de9e2" />

Switch to any character you've unlocked, from the menu or the character ring (D-pad Down in a dungeon and in town, <kbd>Select</kbd> too in a dungeon; in town <kbd>Select</kbd> is the Georama's). If the other player has the character you want, you can trade with them. D-pad Up opens your element ring the same way. Each ring is your own: the game goes on for the others while it's open.

In town too: the Triangle menu's Allies page changes your character there the same way as in a dungeon, and asks for a trade when the other player has it. Toan still does the town's talking, shopping and Georama.

You can trade from anywhere, even when you're in different areas: the other player is asked on their own screen, the same way as when you're together, and nothing changes until they say yes. While you wait for their answer you stay where you are, and Circle cancels. The host can keep trades to players in the same area with `coop_swap_same_zone`.

A trade lasts through new floors and trips to town, and in town each player looks like the character they play. Toan himself shows while player 1 talks to someone, in scenes and in Georama.

If someone is mid-attack, the question still shows at once; their swing plays out and the answer counts once it's over.

Each player uses their own character's skills: whoever is Xiao jumps her gaps (once Xiao has joined the story), and anyone else gets the game's own "can't" message, without stopping the others. The one who jumps sees the game's own jump cutscene on their screen; everyone else watches Xiao make the real jump in the world.

### Character-limited floors

Some floors need a certain character. In co-op the floor's rule is met if **any** player is that character. If nobody is, player 1 is switched to it, as in single player.

- While you're on the floor, anyone can still switch or trade, as long as at least one player stays the floor's character. A switch that would leave nobody as that character is refused, and the menu greys it out.
- The player who is the floor's character can trade it to another player, who then becomes it.
- Everyone fights, whichever character they are.
- If the only player who is the floor's character leaves the floor, the lowest-numbered player still there is switched to it.
- If the floor's character is played by someone somewhere else, the player taking the stairs waits (Circle cancels) and that player is asked: go to the floor, swap characters, or cancel.
- A story scene that needs Toan (like the duel in the Divine Beast Cave) is played by whoever is Toan, with their controller. If nobody is, player 1 becomes Toan for the scene. Afterwards everyone is their own character again.

<div align="right"><a href="#features">back to top</a></div>

## Dungeons

### Fighting together

<img width="1280" alt="Fight together" src="https://github.com/user-attachments/assets/97e8b66d-b229-4457-acc0-ef47d5e14dce" />

Both players fight on the same floor with their own character, weapon and lock-on. Monsters, drops and damage are the same on both screens. Each player's lock-on is drawn in their [colour](#player-colours) on both screens, so you can see who is on what.

Every player attacks as player 1 does, whichever character they are: press again during a swing to go on with the combo (Toan's five hits, Ungaga's three), and hold the button to charge and let go into the character's charge attack (Toan's leaping strike, Goro's smash). The rest of the buttons work for every player as for player 1 too: the element ring, the three quick-use slots and Dran's Feather.

You see the other player, the monsters they are fighting and their damage numbers as far as the floor itself is drawn on your screen, and they go out of view with the walls around them. A monster only the other player is near sounds from where it is, so you don't hear their fights from across the floor.

### Reviving a player

A player who runs out of life lies down while the others play on. Walk up to them and a red "!" shows over them on your screen; press <kbd>Cross</kbd> and the game asks "Use a Revive Powder?" (with a chest or door nearer you than them, <kbd>Cross</kbd> opens that instead). Yes uses one from your own bag and they get up with half their life. If you have none, the game tells you. You can also use a Revive Powder on them from your items as before. Lying down, you can't lock on and monsters leave you alone; <kbd>Triangle</kbd> opens your menu with only **Move**, so with nobody to revive you, you can leave the dungeon the game's way (an Escape Powder, or half your gilda) and get up in town while the others play on. If everyone is down, it's game over.

### Leaving a floor

When one player takes an exit, the other is asked if they want to come along. Say yes and you leave together. Say no and you stay; the door stays open for you to leave later on your own.

Opening a door with a key never stops the other player: the door opens for everyone while they keep playing.

### Arriving on a floor together

Go down to a new floor together and the entrance plays like one scene: the characters come in one after another, 0.4 seconds apart, always in the same order (Toan, Xiao, Goro, Ruby, Ungaga, Osmond), each from their own spot: out of the same doorway, down the same stairs. The first one in that order plays the game's own entrance. On Moon Sea each character beams down onto their own spot around the landing pad, in their own beam of light.

Players who reach a floor while someone is still coming in through its entrance play their own entrance as they arrive, and everyone sees everyone come in. Nobody waits for anyone. Arrive after that and you appear where the entrance ends.

### Between floors

Leaving a dungeon floor brings you to the screen between floors, where you can go back to town or carry on to the next floor.

Everyone gets their own between-floors screen, even when you leave together, and chooses on their own. Pick the same floor and you meet there. Pick different floors, or go back to town, and you each go your own way. The same goes when a resync brings everyone back to the dungeon's entrance.

### Back floors

Some floors have a hidden back floor behind a door that needs an item. Using the item opens the door for every player, for good on that floor.

Going through asks only the player at the door; nobody else stops. They go in on their own, and everyone else is asked if they want to come along, like at an exit. Say no and you keep playing where you are; you can go through the door yourself later. The front and back of a floor are separate places: you only meet up with players on the same side.

### Story scenes that need everyone

<img width="1280" alt="Story scenes" src="https://github.com/user-attachments/assets/14f29866-75f4-41f0-a16e-ff00a75ad262" />

Some story scenes start when you open a door (a house whose scene is still to come) or use an item (the Odd Tone Flute in Wise Owl Forest, the music box key on the Shipwreck). Any player can start one. Everyone else gets a question on their own screen: "Player 1 wants to start a story scene. Come along?"

While they answer, you see who's coming: a row for each player with their face and whether they've said yes, said no, or not answered yet. It updates as answers come in.

- When everyone says yes, the scene starts by itself, with everyone there.
- **START NOW** starts it with the players who said yes. Anyone else is left where they are and keeps playing. At a door they stay outside. On a floor they keep playing the same floor on their own while the scene plays, and you meet up again once it's over. A story that needs Toan can't start without player 1, so if player 1 is left out, he waits where he stands while it plays.
- **CANCEL** (or Circle) stops it: nothing starts, nobody moves, and you keep the item.
- If you're apart, the others get a note that someone is waiting for them there.

Story floors work the same way: a story scene on a floor waits until player 1 is there, so a player alone can't run into it early.

### Pausing

The world only pauses when everyone is paused. If you open a menu while your friend is still fighting, the game keeps going.

<div align="right"><a href="#features">back to top</a></div>

## Town

### Fishing together

<img width="1280" alt="Fishing together" src="https://github.com/user-attachments/assets/ec1fb21c-326b-4d3a-a42e-395d1d7950e5" />

Both players can use the fishing sign and fish at the same time, each with their own rod, camera and menu, in one pond with the same fish for everyone. The fish swim in the pond all the time, whether anyone is fishing or not, and every player sees the same ones. Anyone can catch any fish: a fish goes for the nearest bait, and if two hooks are on the same bite, the first to press Cross gets it (player 1 when you press together). A caught fish is gone for everyone, and a new one swims in about ten seconds later, so the pond never runs dry. You see the other player fishing in the world, with their rod and line, and the fish they land held up in their hand. Each fisher gets their own spot by the water: the first keeps the usual one, the next stands beside them along the bank.

- **Fishing points are shared** by default: each player keeps their own points with their profile, and the fishing exchange shows and spends the total of everyone in the game. A catch goes to the catcher's own points; an exchange takes the price from everyone equally (when someone runs out, the others cover the rest). The fishing log shows the biggest catches of everyone who has played on the save, in the game now or not, each with the name of the player who caught it.
- To show each player only their own instead, change the [`coop_fishing_shared` setting](#settings). Switching either way moves no points.
- **Everyone fishes as their own character**: Xiao, Goro, Ruby, Ungaga and Osmond hold the rod in their right hand and cast as themselves (Osmond hovers, as always). Nobody carries their weapon while fishing.
- Every player can walk with the rod before casting.

<div align="right"><a href="#features">back to top</a></div>

### Time of day

When the town's time of day changes, it changes for everyone at once: the light, the villagers and where they are.

- **If you're free,** you see its scene as in single player, and time and the villagers stand still while it plays.
- **If you're busy** (talking, fishing, in a menu or a shop, reading a chest's message), you skip the scene and carry on; when you're done it's already the new time of day.

<div align="right"><a href="#features">back to top</a></div>

## On your screen

### Player colours

Player 1 is pink and player 2 is teal, on both screens: map arrows, lock-on bars and markers.

### Faces that move

Every player's character blinks, in town and on dungeon floors, with that character's own eyes from the game (in the original only Toan blinked, and only in town). While fishing, your face follows the catch: surprised at a bite, gritted teeth while you fight the fish, surprised again when you land it. Turn it off with `coop_faces` (below).

### Your partner on the map

The map shows where the other player is, so you can always find each other.

### Where is everyone

A short list at the right of your screen says where every other player is: which town, which house (by the name the Georama gives it, like "Macho's House"), which dungeon and floor, or "here" when you're together, and whether they're fishing, building Georama or watching a scene. Each name is in that player's colour. It hides whenever the game's own windows or a scene are on your screen, and in single player. This look is a placeholder for now. To turn it off, set `"coop_whereis": false` under `"game"` in `save\config.json`.

### Area title cards

The area name card when you go somewhere new can be shown in full, shortened, or set up your own way. See the [area card settings](#settings).

<div align="right"><a href="#features">back to top</a></div>

## Settings

Co-op settings live in `save\config.json` in the mod folder, under `"game"` unless a row says otherwise. Close the game before you edit the file. The game's own OPTION menu (screen, sound, controls) is Chronicle's: see its docs. Its **Co-op** page has the two settings you can change while you play: **Where Is Everyone** (`coop_whereis`, each player's own) and **Trade Only Together** (`coop_swap_same_zone`, player 1's: dimmed on the other players' page). The rest below are only in the file.

When you join someone, you play by the **host's** value for the settings marked *host's* for that session. Your own file isn't changed.

In the game's own Options page, each player keeps their own Clock, Dungeon Map, damage numbers, detail distance, Element Quick Select, Character Quick Select, Swap Keeps Lock-On, Always Win QTEs, Where Is Everyone and Message Speed (your own talks show text at your speed; scenes everyone watches use player 1's). Time Speed and Trade Only Together are player 1's for everyone, so they're dimmed on the other players' Options page.

In co-op every player always has their own item bag and gilda, their own camera and their own menus, a floor or room whose story is unseen is entered together, a floor's exit gate opens the floor menu and asks the others to come along, and while linked the keyboard plays your controller and the mouse is left out. Older files' `coop_own_bags`, `coop_independent_cameras`, `coop_menus`, `coop_menu_panel`, `netplay_keyboard`, `netplay_controller_only`, `netplay_delay`, `area_card_coop_only`, `coop_story_gather` and `coop_gate_picker` are no longer used: the game skips them and leaves them out the next time it saves.

### Playing together

| Setting | Default | What it does |
|---|---|---|
| `coop_swap_same_zone` *(host's; Options > Co-op > Trade Only Together)* | `false` | `true`: you can only trade characters while you're both in the same area; asking from another area is refused with a note on your screen. `false`: trade from anywhere. |
| `debug_mode` *(host's)* | `false` | `true`: turns on the game's debug features: its developer menu, the **debug start picker** (L3 and R3 together on player 1's pad: pick any town or dungeon floor to start in; saving stays off after a start from it) and the **Dran's Feather cheat** (hold L1 and R1, then press Select and Triangle: you get a Dran's Feather in quick slot 1). Meant for testing: leave it `false` for normal play. |

### Fishing

| Setting | Default | What it does |
|---|---|---|
| `coop_fishing_shared` *(host's)* | `true` | The fishing exchange and log show the total of everyone in the game (each player's points stay their own). `false`: each player sees only their own. Switching moves no points. |

### Screen

| Setting | Default | What it does |
|---|---|---|
| `area_card` | `"retail"` | The area name card in co-op (single player keeps the game's own): `"retail"` is the game's own, `"short"` about a tenth of a second, `"custom"` uses the timings below. |
| `area_card_start`, `area_card_fade_in`, `area_card_hold`, `area_card_fade_out` | `20`, `128`, `120`, `128` | Timings for `"custom"`, in 60ths of a second. |
| `area_card_speed` | `1.0` | Speeds the whole `"custom"` card up (2 is twice as fast). |
| `coop_whereis` *(Options > Co-op > Where Is Everyone)* | `true` | The [where is everyone](#where-is-everyone) list on your screen. `false`: hidden. |
| `coop_faces` | `true` | [Faces that move](#faces-that-move) on your screen. `false`: the original game's faces. Each player's own. |
| `window_title` | empty | The window's title. |
| `"video"` > `window_left`, `window_top` | `-1` | Where the window opens. `-1`: the default place. |

### Connecting

| Setting | Default | What it does |
|---|---|---|
| `netplay_port` | `47100` | The network port used to connect. Change it only if something else uses it. |
| `netplay_upnp` | `true` | Hosting by IP or NAT code asks your router to open the port. `false`: asks nobody. |
| `netplay_wan_lookup` | `true` | A host asks an internet service for their outside address, to show it. `false`: asks nobody. |
| `netplay_steam_rate` | `"auto"` | How fast a join's data goes over Steam. `"auto"` adjusts itself; a number is KB/s, or text such as `"2 MB/s"`. |
| `"input"` > `controller` | `0` | `0`: every controller works. `1` or `2`: only the first or second controller plays this copy (two copies on one PC). |

### Lobby

| Setting | Default | What it does |
|---|---|---|
| `lobby_local` | `"combined"` | Where players on the same PC join: `"combined"` (Start on a free controller in any lobby) or `"separate"` (MULTIPLAYER > Local has its own lobby). |
| `lobby_show_ip` | `false` | Show the host's outside address in the lobby. <kbd>R1</kbd> in the lobby changes it too. |

### Log

| Setting | Default | What it does |
|---|---|---|
| `write_log` | `true` | Keeps the game's output in `save\log.txt` (stays on your PC, never sent; the last three starts are kept). `false`: no log. |

<div align="right"><a href="#features">back to top</a></div>
