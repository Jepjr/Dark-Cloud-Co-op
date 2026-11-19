<div align="center">

# Dark Cloud Co-op

**Online co-op for Dark Cloud, built on [Chronicle](https://github.com/TheMoonPeople/Chronicle).**

[![Release](https://img.shields.io/github/v/release/Jepjr/Dark-Cloud-Co-op?style=flat-square)](https://github.com/Jepjr/Dark-Cloud-Co-op/releases)
[![Downloads](https://img.shields.io/github/downloads/Jepjr/Dark-Cloud-Co-op/total?style=flat-square)](https://github.com/Jepjr/Dark-Cloud-Co-op/releases)
[![Chronicle](https://img.shields.io/badge/Chronicle-9418a550-blue?style=flat-square)](https://github.com/TheMoonPeople/Chronicle/commit/9418a5506e2a08e7739aa68d70841df6bc516201)
[![Disc](https://img.shields.io/badge/disc-NTSC%20(US)%201.02-orange?style=flat-square)](#what-you-need)
[![Platform](https://img.shields.io/badge/platform-Windows%2010%20%7C%2011-lightgrey?style=flat-square)](#what-you-need)
[![Players](https://img.shields.io/badge/players-2-green?style=flat-square)](#known-issues)

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/G0E62AMQIX)

[How it works](#how-it-works) • [Features](#features) • [Where it's at](#where-its-at) • [What you need](#what-you-need) • [Installing](#installing) • [Playing together](#playing-together) • [Reporting bugs](#reporting-bugs) • [Building it yourself](#building-it-yourself) • [Credits](#credits)

</div>

---

Online co-op for Dark Cloud. It's a mod for [Chronicle](https://github.com/TheMoonPeople/Chronicle), the Dark Cloud decompilation. I had 3 goals with the project. The first was to keep the mod feeling as close as possible to the base game. The second was that players were autonomous. All players are fully independent of each other as far as moving around the world is concerned. Story events and the world state are shared, but inventory and traveling is separate. Third was that the game should feel as close as possible to being a native experience. We accomplish this by using lockstep syncing. Everything you see in the world is the same thing other players see.

> [!IMPORTANT]
> You need your own **NTSC (US) Dark Cloud disc, version 1.02**. Chronicle's own Windows instructions use the PAL disc, but this mod only supports the US disc for now.

## How it works

Player 2 can hop in and out at any time and plays as a separate person from player 1. Both players can be in town, go into the dungeon, work on georama, etc completely separate from each other, and one player's activity never holds up the other unless it truly has to.

| Shared between players | Each player's own |
|---|---|
| Story progress | Where you are and where you're going |
| The state of the world | Your bag and inventory |
| Weapons and the characters' stats | Your menus |
| Key items | Your camera and character |

If players fall out of sync, the zone reloads. You don't lose anything, but if you're in the dungeon you will restart the floor.

## Features

<table>
<tr>
<td width="50%" valign="top">
<a href="FEATURES.md#fighting-together"><img src="https://github.com/user-attachments/assets/97e8b66d-b229-4457-acc0-ef47d5e14dce" alt="Fight together" width="100%"></a>
<br><b>Fight together.</b> Same monsters on both screens, each with your own lock-on. <a href="FEATURES.md#fighting-together">More</a>
</td>
<td width="50%" valign="top">
<a href="FEATURES.md#send-items"><img src="https://github.com/user-attachments/assets/2398b616-7508-482b-8644-0676221f5d82" alt="Send items" width="100%"></a>
<br><b>Send items.</b> <kbd>Select</kbd> on your bag's Items page sends it to your friend. <a href="FEATURES.md#send-items">More</a>
</td>
</tr>
<tr>
<td width="50%" valign="top">
<a href="FEATURES.md#trade-characters"><img src="https://github.com/user-attachments/assets/439a95bf-0cf8-4788-88e4-bbabc75de9e2" alt="Trade characters" width="100%"></a>
<br><b>Trade characters.</b> Switch to any character you've unlocked, or trade with your friend. <a href="FEATURES.md#trade-characters">More</a>
</td>
<td width="50%" valign="top">
<a href="FEATURES.md#story-scenes-that-need-everyone"><img src="https://github.com/user-attachments/assets/14f29866-75f4-41f0-a16e-ff00a75ad262" alt="Story scenes" width="100%"></a>
<br><b>Story scenes.</b> Item-started story scenes wait until everyone says yes. New in v0.1.1. <a href="FEATURES.md#story-scenes-that-need-everyone">More</a>
</td>
</tr>
<tr>
<td width="50%" valign="top">
<a href="FEATURES.md#fishing-together"><img src="https://github.com/user-attachments/assets/a7749fb2-ef55-44ef-bdb3-6d8ff80436c7" alt="Fish together" width="100%"></a>
<br><b>Fish together.</b> Both at the fishing sign at once, each with your own fish, walking with your rod as your own character. <a href="FEATURES.md#fishing-together">More</a>
</td>
<td width="50%" valign="top">
<a href="FEATURES.md#faces-that-move"><img src="https://github.com/user-attachments/assets/d9e6aca3-4cf8-4275-8881-5d6c868ecb64" alt="Faces that move" width="100%"></a>
<br><b>Faces that move.</b> Every character blinks with their own eyes, and a fisher's face follows the catch. <a href="FEATURES.md#faces-that-move">More</a>
</td>
</tr>
</table>

- <b>Drop in, drop out.</b> Join or leave any time. The host never stops for you. <a href="FEATURES.md#drop-in-drop-out">More</a>
- <b>Play apart.</b> One in town, one in the dungeon. Same world, your own way. <a href="FEATURES.md#play-apart">More</a>
- <b>Leave together.</b> Take an exit and your friend is asked to come along. <a href="FEATURES.md#leaving-a-floor">More</a>

Also: [shared key items](FEATURES.md#shared-key-items), [shared storage](FEATURES.md#shared-storage), [profiles](FEATURES.md#profiles), [pausing only when everyone pauses](FEATURES.md#pausing) and more. **[All features](FEATURES.md)** • [Settings](FEATURES.md#settings)

## Where it's at

I tried to get this completely stable up until clearing Dran's Cave. A lot of the changes are systemic so shouldn't really have issues post Dran's cave but most of the testing has been on Norune and Dran's Cave.

### Known issues

- Only the NTSC (US) 1.02 disc works for now.
- Two players for now. The lobby shows 6, but the game only supports 2.
- Using an item through a story script while both players are together hasn't been tested.
- If the game falls out of sync, the zone reloads. In a dungeon that means reloading a new floor seed. Progress isn't lost though.

### Plans

I want to make this 6 player compatible. You'll notice the lobby already accepts 6 but the game won't. Some of the features were developed with being able to do 6 but for some it's still going to be a conversion.

The game balance is a bit skewed with every additional player. It's fine if you're exploring different floors but on the same floor it's a problem. I also want to make it possible to use the other characters at the beginning of the game, which would involve making weaker weapons.

<div align="right"><a href="#dark-cloud-co-op">back to top</a></div>

## What you need

| | |
|---|---|
| **PC** | Windows 10 or 11, 64-bit, with a graphics card that supports Vulkan (anything recent does) |
| **Disc** | Your own Dark Cloud disc image (.iso): **NTSC (US), version 1.02**. PAL is on the list. |
| **Chronicle** | Any version. The mod brings its own game and only uses Chronicle's folder and its game data. (The setup and building it yourself use commit [`9418a550`](https://github.com/TheMoonPeople/Chronicle/commit/9418a5506e2a08e7739aa68d70841df6bc516201); the setup grabs it if you have no Chronicle.) |
| **Disk space** | The mod is about 45 MB. If Chronicle hasn't extracted your disc yet, that's another 5.4 GB. |

## Installing

1. Grab **`Dark Cloud Co-op vX.Y.Z.zip`** from the [Releases](../../releases) page.
2. Extract it into your Chronicle folder, the one with `port` and `run.bat` in it. You should end up with `<Chronicle>\mods\Dark Cloud Co-op`.
3. Run **`Play Dark Cloud Co-op.exe`** in that folder.

The first time, it checks Chronicle's game data. If there's none yet, it asks for your disc image and extracts it (about 5 minutes). If the data is from a disc or a Chronicle version it can't play, it tells you what's wrong and leaves your data alone.

> [!WARNING]
> Nothing here is signed, so Windows may show "Windows protected your PC" the first time. Click **More info**, then **Run anyway**. Windows 11's Smart App Control blocks unsigned programs completely, so that has to be off.

> [!NOTE]
> **Playing with a friend:** they do the same with the same release, on their own Chronicle with their own disc. The game checks you're on the same version before you connect.

<details>
<summary><b>Or: the setup, which builds it on your PC</b></summary>

Use this if you'd rather build the game yourself, or have no Chronicle yet. It ends up with exactly the same game as the zip above (the same file, byte for byte), so you can play with people who used either.

1. Grab **`Dark Cloud Co-op vX.Y.Z setup.zip`** from the [Releases](../../releases) page.
2. Unzip it into your Chronicle folder's `mods` folder. (You can unzip it anywhere and point it at your Chronicle folder instead. If you don't have Chronicle, it'll set one up next to itself.)
3. If Chronicle hasn't extracted your disc yet, put your .iso next to the setup.
4. Run **`Set up Dark Cloud Co-op.bat`**.

It takes a few minutes and about 1 GB more for Chronicle's build tools. It needs your Chronicle at the commit above; if it isn't, or has data from a different disc, the setup stops and tells you what to do without changing anything. It says what it's going to download before it does, and checks every download. If something goes wrong partway through, run it again: it picks up where it left off (`setup-log.txt` has the details). Windows Defender may flag the freshly built game or hold one of the build's programs; the setup then names it: add your Chronicle folder's `mods` and `.kazarov` folders as exclusions in Windows Security and run it again.

</details>

<details>
<summary><b>A few more things worth knowing</b></summary>

- The mod sits in your Chronicle folder and uses Chronicle's game data. It doesn't make a second copy of the game, and never changes Chronicle's own files.
- `VERSION.txt` in the mod folder says which version you have. Two copies connect when their `content-version` lines match, however they were installed.
- Your Chronicle folder's `git status` will show `mods\` as new. Add `/mods/` to its `.git\info\exclude` if that bugs you.

</details>

<div align="right"><a href="#dark-cloud-co-op">back to top</a></div>

## Playing together

Both players need **the same version** of the mod. The game checks, and won't connect otherwise.

On the title screen, START and CONTINUE are single player. **MULTIPLAYER** opens the co-op options: **Host**, **Join** or **Local**.

### Profiles

Each player has a **profile**: a name your own bag (your items and gilda) is saved under. Profiles are saved with the host's save slot, so your bag lives in that host's game.

- **In the lobby,** each player picks a profile (or makes a new one) and a character, and locks them in before the game loads. Your usual profile is already selected for you.
- **Joining a game that's already running:** the lobby shows up on your screen only. The host keeps playing while you pick. You then load into town, as if you had loaded a save (back next to the host only if you dropped out on the floor they're still on).
- **If you drop and reconnect,** you get your profile and character back automatically, no lobby. If the **host** drops, they need to host again.
- **A new profile starts with an empty bag.** Bag upgrades count for every profile, including ones made later.

### Connecting

| Connect by | Good for |
|---|---|
| **IP address** | Same home network, or over the internet with [Tailscale](https://tailscale.com) |
| **NAT code** | Over the internet. The joining player enters the host's code. If the host's network has strict NAT settings, both players need to enter each other's code. |
| **Steam** | Joining through Steam. Needs a one-time setup (below). |

**Steam:** Steam joins need Valve's `steam_api64.dll`, which this mod doesn't include. Run **`Set up Steam.bat`** in `mods\Dark Cloud Co-op` if you want it: it asks before each step (No is the default), downloads the DLL, and opens Steam's install of Spacewar, the free app the mod connects through.

**Your IP address:** the lobby hides your internet address until you press <kbd>R1</kbd>, and remembers your choice. The NAT code is always shown, since that's what you send your friend.

**Local:** gives you the very much still in development splitscreen mode, and I left the version of multiscreen in that I used for testing since I thought people might want to use it on dual monitors or casting to a TV or something.

## Reporting bugs

Open an [issue](../../issues) and attach `save\log.txt` from **both** players if you can. The log stays on your PC and is never sent anywhere on its own.

> [!TIP]
> To turn the log off, see the [log setting](FEATURES.md#settings).

<div align="right"><a href="#dark-cloud-co-op">back to top</a></div>

## Building it yourself

<details>
<summary>The zip and the setup do all of this for you. Click here if you'd rather do it by hand.</summary>

You'll need Git, PowerShell 7, Python 3, CMake 3.28 or newer, 7-Zip, and a Chronicle clone with this repo at `mods\Dark Cloud Co-op`.

1. Add `/mods/` to `.git\info\exclude` in your Chronicle folder so its `git status` stays clean.
2. Get Chronicle's pinned toolchain with its own script. Note that this one also installs Rust, which Chronicle's disc extractor needs (the setup skips it by shipping that extractor prebuilt):
   ```
   pwsh -File tools\windows\acquire.ps1 -Root "mods\Dark Cloud Co-op\out"
   ```
3. Build:
   ```
   pwsh -File "mods\Dark Cloud Co-op\build\build.ps1"
   ```
   It builds Chronicle at the commit in `chronicle-pin.txt` (your checkout just needs that commit in its history), applies this mod's recipes, and writes `game\` and `Play Dark Cloud Co-op.exe`. It prints a `version: content ...` line: a build of a release's commit has that release's `content-version` (see its `VERSION.txt`) and the same `darkcloud.exe`, so it connects with the zip's and the setup's.
4. Extract your NTSC disc into Chronicle's data folder if you haven't already:
   ```
   game\dcdata.exe extract <your .iso> <Chronicle>\port\run\data
   ```

| Folder | What's in it |
|---|---|
| `src\` | The mod's own code |
| `recipes\` | Its edits to Chronicle's files, applied at build time. Chronicle's own files are never touched. |
| `build\` | The build scripts |
| `launcher\` | The launcher |
| `release\` | The setup |

</details>

## Credits

- [Chronicle](https://github.com/TheMoonPeople/Chronicle) by The Moon People: the decompilation and PC port this whole mod is built on.
- [Kazarov](https://github.com/TheMoonPeople/Kazarov), the engine layer Chronicle runs on.

If you'd like to support the project: [![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/G0E62AMQIX)

---

<sub>Dark Cloud is © Sony Interactive Entertainment and Level-5. This is a fan project, not affiliated with either. You need your own copy of the game; no game files are included here.</sub>
