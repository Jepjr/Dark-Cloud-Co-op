<div align="center">

# Features

**Everything the co-op adds to Dark Cloud, one section each, and how to use it.**

[Back to the README](README.md)

</div>

Each section says which release it came in. The clips are both players' screens side by side: player 1 on the left, player 2 on the right.

<details>
<summary><b>All features</b></summary>

**Playing together**
- [Drop in, drop out](#drop-in-drop-out)
- [Play apart](#play-apart)
- [Shared story and world](#shared-story-and-world)
- [Profiles](#profiles)
- [Connecting](#connecting)
- [Local play](#local-play)
- [Quitting](#quitting)
- [Out of sync](#out-of-sync)

**Items and characters**
- [Send items](#send-items)
- [Shared key items](#shared-key-items)
- [Shared storage](#shared-storage)
- [Bag upgrades](#bag-upgrades)
- [Trade characters](#trade-characters)
- [Character-limited floors](#character-limited-floors) (new in v0.1.1)

**Dungeons**
- [Fighting together](#fighting-together)
- [Leaving a floor](#leaving-a-floor)
- [Stairs to floor select](#stairs-to-floor-select)
- [Chests and Atla](#chests-and-atla)
- [Partner notices](#partner-notices) (new in v0.1.1)
- [Story scenes that need everyone](#story-scenes-that-need-everyone) (new in v0.1.1)
- [Pausing](#pausing)

**Town**
- [Fishing together](#fishing-together) (new in v0.1.1)

**On your screen**
- [Player colours](#player-colours)
- [Your partner on the map](#your-partner-on-the-map)
- [Area title cards](#area-title-cards)

</details>

---

## Playing together

### Drop in, drop out

*Since v0.1.0*

Player 2 can join or leave at any time. The host doesn't stop: joining while the host is in a menu, a cutscene, fishing or a fight just brings you in next to what's going on.

- **Joining a game that's already running:** the lobby shows up on the joiner's screen only. The host keeps playing while you pick your profile and character.
- **If you drop and reconnect,** you get your profile and character back automatically, no lobby.
- **If the host drops,** they need to host again.

<div align="right"><a href="#features">back to top</a></div>

### Play apart

*Since v0.1.0*

Both players are their own person. You can be anywhere: one in town and one in the dungeon, on different floors, in different houses, or building Georama separately. One player's activity never holds up the other unless it really has to.

| Shared between players | Each player's own |
|---|---|
| Story progress | Where you are and where you're going |
| The state of the world | Your bag and inventory |
| Weapons and the characters' stats | Your menus |
| Key items | Your camera and character |

When you're apart, what you change in the world (a chest opened, a house built) is there for the other player when they get to it.

### Shared story and world

*Since v0.1.0*

There's one story and one world, and both players see the same thing in it.

- **Story moments wait for player 1.** Player 2 can't set off a story scene alone. If player 2 is already there when one starts, they watch it too.
- **Cutscenes stay on your own screen.** When the other player opens a chest or watches a scene, you see them do it in the world. Your camera, your text and your fades are never taken over.

### Profiles

*Since v0.1.0*

Each player has a **profile**: a name your own bag (your items and gilda) is saved under. Profiles are saved with the host's save slot, so your bag lives in that host's game.

- **In the lobby,** each player picks a profile (or makes a new one) and a character, and locks them in before the game loads. Your usual profile is already selected for you.
- **A new profile starts with an empty bag.** See [Bag upgrades](#bag-upgrades) for what carries over.

### Connecting

*Since v0.1.0*

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

*Since v0.1.0*

**Local** on the MULTIPLAYER screen gives you an early split-screen mode, and a multi-screen mode (one window per player) for dual monitors or a TV. Split-screen is still very much in development.

### Quitting

*Since v0.1.0*

If the host quits to the title, everyone goes back to the title. If anyone else quits, only they leave, and the host plays on.

### Out of sync

*Since v0.1.0*

Everyone runs the same game in lockstep, so both screens show the same world. If the two copies ever disagree, the zone reloads for both players. Nothing is lost, but in a dungeon you restart the floor.

<div align="right"><a href="#features">back to top</a></div>

## Items and characters

### Send items

*Since v0.1.0*

<img width="1280" alt="Send items" src="https://github.com/user-attachments/assets/2398b616-7508-482b-8644-0676221f5d82" />

Open your bag, go to the **Items** page, pick an item and press <kbd>Select</kbd>. It goes straight into the other player's bag. You can send while you're apart, too.

### Shared key items

*Since v0.1.0*

Key items live on a key items tab both players share, so neither of you is ever missing a key.

### Shared storage

*Since v0.1.0*

Both players use the same storage, and both can have it open at the same time.

### Bag upgrades

*Since v0.1.0*

A bag upgrade counts for every profile in the save, including profiles made later.

### Trade characters

*Since v0.1.0*

<img width="1280" alt="Trade characters" src="https://github.com/user-attachments/assets/439a95bf-0cf8-4788-88e4-bbabc75de9e2" />

Switch to any character you've unlocked, from the menu or the character wheel. If the other player has the character you want, you can trade with them.

### Character-limited floors

*New in v0.1.1*

Some floors need a certain character. In co-op the floor's rule is met if **any** player is that character. If nobody is, player 1 is switched to it, as in single player.

- While you're on the floor, anyone can still switch or trade, as long as at least one player stays the floor's character. A switch that would leave nobody as that character is refused, and the menu greys it out.
- The player who is the floor's character can trade it to another player, who then becomes it.
- Everyone fights, whichever character they are.
- If the only player who is the floor's character leaves the floor, the lowest-numbered player still there is switched to it.

<div align="right"><a href="#features">back to top</a></div>

## Dungeons

### Fighting together

*Since v0.1.0*

<img width="1280" alt="Fight together" src="https://github.com/user-attachments/assets/97e8b66d-b229-4457-acc0-ef47d5e14dce" />

Both players fight on the same floor with their own character, weapon and lock-on. Monsters, drops and damage are the same on both screens. Each player's lock-on is drawn in their [colour](#player-colours) on both screens, so you can see who is on what.

### Leaving a floor

*Since v0.1.0*

When one player takes an exit, the other is asked if they want to come along. Say yes and you leave together. Say no and you stay; the door stays open for you to leave later on your own.

### Stairs to floor select

*Since v0.1.0*

Taking the stairs out of a dungeon brings you to the floor select screen instead of straight back to town. Back out of it to leave.

### Chests and Atla

*Since v0.1.0*

The opening cutscene plays only on the opener's screen. Everyone else sees them open it in the world and keeps playing. The opener can't be hurt while they're opening it.

### Partner notices

*New in v0.1.1*

When someone picks up something important, everyone else gets a short notice on their own screen saying what it was. It goes away by itself after the same time as an item drop's message.

- The player who picked it up sees the game's usual message, the same as single player.
- Notices are only for the important things: Atla finds, key items, story items and weapons. Ordinary drops show on the picker's screen only.

### Story scenes that need everyone

*New in v0.1.1*

<img width="1280" alt="Story scenes" src="https://github.com/user-attachments/assets/14f29866-75f4-41f0-a16e-ff00a75ad262" />

A few story scenes start when you use an item: the Odd Tone Flute in Wise Owl Forest and the music box key on the Shipwreck. Any player can use the item. Everyone else is asked if they want to come to the scene, and nothing happens until everyone says yes. Then it plays with everyone there.

- If anyone says no (or doesn't answer), the scene doesn't start and you keep the item.
- If you're apart, the others get a note that someone is waiting for them there.

Story floors work the same way: a story scene on a floor waits until player 1 is there, so a player alone can't run into it early.

### Pausing

*Since v0.1.0*

The world only pauses when everyone is paused. If you open a menu while your friend is still fighting, the game keeps going.

<div align="right"><a href="#features">back to top</a></div>

## Town

### Fishing together

*New in v0.1.1*

Both players can use the fishing sign and fish at the same time, each with their own fish, camera and menu. You see the other player fishing in the world, with their rod and line.

- **Fishing points and the fishing log are shared** by default: every catch counts for the party.
- To keep them per player instead, set `"coop_fishing_shared": false` under `"game"` in `save\config.json` (needs own bags). Switching either way keeps every point.
- Everyone fishes as Toan for now. Fishing as your own character is planned.

<div align="right"><a href="#features">back to top</a></div>

## On your screen

### Player colours

*Since v0.1.0*

Player 1 is pink and player 2 is teal, on both screens: map arrows, lock-on bars and markers.

### Your partner on the map

*Since v0.1.0*

The map shows where the other player is, so you can always find each other.

### Area title cards

*Since v0.1.0*

The area name card when you go somewhere new can be shown in full, shortened, or set up your own way. In `save\config.json`, under `"game"`:

| Setting | What it does |
|---|---|
| `"area_card": "retail"` | The game's own card (the default) |
| `"area_card": "short"` | A short card, about a tenth of a second |
| `"area_card": "custom"` | Your own timings: `area_card_start`, `area_card_fade_in`, `area_card_hold`, `area_card_fade_out` (in 60ths of a second), or `area_card_speed` to speed the whole card up |
| `"area_card_coop_only": false` | Use your setting in single player too (by default single player keeps the game's card) |

<div align="right"><a href="#features">back to top</a></div>
