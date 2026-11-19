// coop-tutimages (Jep's 2026-10-27 playtest: P2, joining during the B1 opening scene, sees no tutorial pictures): the 2D
// pictures of a floor's opening scene, on the copy that shows the other's scene (CoopWorldsSceneView).
//
// A scene's pictures (B1's lesson board: e29_1.img to e29_11.img of gedit/s99/chara/e29img.pak and e29img2.pak, one page
// at a time) are event sprites its script loads into texture blocks 45-49 (_LOAD_SPRITE_TEXTURE, _LOAD_TEXTURE) from
// packs it read (_LOAD_CHR_FILE) and then places (_SP_*, _DRAW_SPRITE). Only the copy playing the scene runs that script.
// So that copy says which pack entries each block was made of (ScenePicFiles, as they change and twice a second), and
// each tick which sprites it draws (ScenePicSprites); the other loads
// the same entries from its own game data into the same blocks and draws the same sprites. No pixels cross the link.

#include "coop/game_ns.hpp"
#include "coop/coop_worlds.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "collision.hpp"
#include "dataread.hpp"
#include "data/data_read.hpp"
#include "edit.hpp"
#include "editground.hpp"
#include "editloop3.hpp" // (after the headers it leans on, as port/src/editloop3.cpp has them)
#include "mdt.hpp"
#include "mglib.hpp"
#include "npcharacter.hpp" // coop-sceneactors
#include "rect.hpp"
#include "snd.hpp"
#include "spritetable.hpp"
#include "texture.hpp"

#include "coop/coop.hpp"
#include "coop/coop_camera.hpp" // coop-latefall: ArrivalChrFile
#include "coop/coop_core.hpp"   // coop-sceneactors: kModelQuads
#include "coop/netplay_socket.hpp"

namespace dc::game {
ED_SPRITE    *EdEventSpriteAt(int index);   // ps2/src/editloop3.cpp (coop-tutimages)
CSpriteTable *EdEventSpriteTable(int back); // ps2/src/editloop3.cpp (coop-tutimages)
} // namespace dc::game

namespace {

constexpr std::uint32_t kScenePicFiles = 5;   // worlds message kinds (coop_worlds.cpp's Kind)
constexpr std::uint32_t kScenePicSprites = 6;
constexpr std::uint32_t kScenePacks = 7; // coop-b1lesson
constexpr int           kFirstBlock = 45; // the event sprites' texture blocks
constexpr int           kBlocks = 5;
constexpr int           kEntries = 4; // pack entries a block is made of, at most (B1's: one a page)
constexpr int           kSprites = 16;

#pragma pack(push, 1)
struct ScenePicBlock {
    char file[40];                // the pack the entries came from (a game data path), "" for a block not loaded
    char entry[kEntries][16];     // the pack's entries, "" past the last
};

struct ScenePicFiles {
    std::uint32_t magic;
    std::uint32_t player;
    std::uint32_t kind; // kScenePicFiles
    std::uint32_t seq;
    std::int32_t  map; // the dungeon and floor (0 up) whose scene it is, as SceneView's
    std::int32_t  floor;
    ScenePicBlock blocks[kBlocks];
};

struct ScenePicSprite {
    char          texture[16]; // its texture's name, in block
    std::uint8_t  block;
    std::uint8_t  flags; // 1 rotated, 2 bilinear
    std::int16_t  x, y, width, height; // on screen
    std::int16_t  u, v, u_width, v_height; // of its texture
    std::uint8_t  red, green, blue, alpha;
    std::int16_t  pivot_x, pivot_y;
    float         angle;
};

struct ScenePicSprites {
    std::uint32_t  magic;
    std::uint32_t  player;
    std::uint32_t  kind; // kScenePicSprites
    std::uint32_t  seq;  // the SceneView sent with it
    std::uint32_t  count;
    ScenePicSprite sprites[kSprites];
};

// coop-b1lesson: every pack the sender's scene read so far (_LOAD_CHR_FILE), oldest first: the models its actors were built
// from (the elder) live in the dungeon's read buffer, which the state set leaves out.
constexpr int kPacks = 12;

struct ScenePacks {
    std::uint32_t magic;
    std::uint32_t player;
    std::uint32_t kind; // kScenePacks
    std::uint32_t seq;
    std::int32_t  map;
    std::int32_t  floor;
    std::uint32_t count;
    char          path[kPacks][40];
};
#pragma pack(pop)

static_assert(sizeof(ScenePacks) == kCoopWorldsScenePacksBytes, "coop_worlds.hpp names this message's size");
static_assert(sizeof(ScenePacks) != sizeof(ScenePicFiles) && sizeof(ScenePacks) != sizeof(ScenePicSprites), "worlds messages are told apart by their size");

// (The link's datagrams are read into a buffer of sizeof(Chunk) + 64 bytes: netplay.cpp.)
static_assert(sizeof(ScenePicFiles) <= 1088 && sizeof(ScenePicSprites) <= 1088, "a worlds message fits one datagram");
static_assert(sizeof(ScenePicFiles) != sizeof(ScenePicSprites), "worlds messages are told apart by their size");

// The sending copy: the packs its scene read (by _LOAD_CHR_FILE's index) and what each block was made of.
std::string   g_chr_path[16];
ScenePicBlock g_made[kBlocks];
unsigned      g_made_version = 0; // counts changes to g_made
unsigned      g_sent_version = 0;
unsigned      g_sent_count = 0;

// coop-b1lesson: the sending copy's packs of the scene under way (CoopWorldsSceneChrFile), and the count it said last.
std::vector<std::string> g_scene_packs;
unsigned                 g_packs_version = 0, g_packs_sent_version = 0;

// coop-b1lesson: the joining copy: the packs the other's scene read, as it last said, and this copy's own reads of them
// (its game data), with every place a model could start in them by its header's print (CoopWorldsSceneModel).
std::vector<std::string>                      g_heard_packs;
std::vector<std::vector<u_long128>>           g_pack_data;
std::vector<std::string>                      g_pack_read;
std::unordered_map<std::uint32_t, unsigned *> g_pack_models;
int                                           g_pack_finds = 0;

// The showing copy: what the other said, when, and what this copy loaded into each block.
struct Heard {
    bool                                  files_set = false;
    ScenePicFiles                         files{};
    bool                                  sprites_set = false;
    ScenePicSprites                       sprites{};
    std::chrono::steady_clock::time_point sprites_at{};
} g_heard;

struct Loaded {
    bool                                on = false;
    ScenePicBlock                       made{};
    std::vector<std::vector<u_long128>> data; // the pack and its entries' copies: the block's textures point into these
} g_loaded[kBlocks];

bool NoPictures() {
    static const char *const mutate = TestEnv("DC_WORLDS_MUTATE");

    return mutate != nullptr && std::strcmp(mutate, "scene_no_pictures") == 0;
}

void Copy(char *to, std::size_t size, const char *from) {
    std::snprintf(to, size, "%s", from != nullptr ? from : "");
}

bool SameBlock(const ScenePicBlock &a, const ScenePicBlock &b) {
    return std::memcmp(&a, &b, sizeof a) == 0;
}

// This copy's sprites as the game's EdEventSpriteDraw draws them: the event sprites by block, then the sprite table.
unsigned GatherSprites(ScenePicSprites *out) {
    unsigned n = 0;

    for (int block = kFirstBlock; block < kFirstBlock + kBlocks; block++) {
        for (int i = 0; i < 16 && n < kSprites; i++) {
            const ED_SPRITE *sprite = EdEventSpriteAt(i);

            if (sprite == nullptr || sprite->enabled == 0 || sprite->layer == 0 || sprite->texture.block != block) {
                continue;
            }
            ScenePicSprite &s = out->sprites[n++];

            Copy(s.texture, sizeof s.texture, sprite->texture.name);
            s.block = (std::uint8_t) block;
            s.flags = (sprite->rotated != 0 ? 1 : 0) | (sprite->bilinear != 0 ? 2 : 0);
            s.x = (std::int16_t) sprite->x;
            s.y = (std::int16_t) sprite->y;
            s.width = sprite->width < 0 ? sprite->source_width : sprite->width;
            s.height = sprite->height < 0 ? sprite->source_height : sprite->height;
            s.u = sprite->source_x;
            s.v = sprite->source_y;
            s.u_width = sprite->source_width;
            s.v_height = sprite->source_height;
            s.red = (std::uint8_t) sprite->red;
            s.green = (std::uint8_t) sprite->green;
            s.blue = (std::uint8_t) sprite->blue;
            s.alpha = (std::uint8_t) sprite->alpha;
            s.pivot_x = sprite->rotation_x;
            s.pivot_y = sprite->rotation_y;
            s.angle = sprite->rotation;
        }
    }

    // (The table holds each texture's TEX0 only: its name is the event block's texture with that TEX0.)
    CSpriteTable *table = EdEventSpriteTable(0);

    for (int layer = table->list_count - 1; layer >= 0 && table->pool != nullptr; layer--) {
        for (SPRITE_TABLE *node = table->heads[layer]; node != nullptr && node != table->tails[layer] && n < kSprites; node = node->next) {
            const CTexture *texture = nullptr;

            for (int t = 0; t < kTextureCount; t++) {
                const CTexture &candidate = TexManager.textures[t];

                if (candidate.block >= kFirstBlock && candidate.block < kFirstBlock + kBlocks && candidate.tex0 == node->tex0) {
                    texture = &candidate;
                    break;
                }
            }
            if (texture == nullptr) {
                continue;
            }
            ScenePicSprite &s = out->sprites[n++];

            Copy(s.texture, sizeof s.texture, texture->name);
            s.block = (std::uint8_t) texture->block;
            s.flags = 0;
            s.x = node->x;
            s.y = node->y;
            s.width = node->width;
            s.height = node->height;
            s.u = node->u;
            s.v = node->v;
            s.u_width = node->u_width;
            s.v_height = node->v_height;
            s.red = (std::uint8_t) node->red;
            s.green = (std::uint8_t) node->green;
            s.blue = (std::uint8_t) node->blue;
            s.alpha = (std::uint8_t) node->alpha;
            s.pivot_x = s.pivot_y = 0;
            s.angle = 0.0f;
        }
    }
    return n;
}

void Unload(int b, bool apart) {
    Loaded &loaded = g_loaded[b];

    if (!loaded.on) {
        return;
    }
    // (Merged, the state set brought the other's texture blocks: this copy's are gone already, so only its buffers go.)
    if (apart) {
        TexManager.DeleteTextureBlock(kFirstBlock + b);
    }
    loaded.on = false;
    loaded.made = ScenePicBlock{};
    loaded.data.clear();
    std::fprintf(stderr, "worlds: scene pictures: block %d unloaded%s\n", kFirstBlock + b, apart ? "" : " (merged: its buffers only)");
    std::fflush(stderr);
}

// Block b made of what the other's was made of, from this copy's own game data, as _LOAD_SPRITE_TEXTURE makes it.
void Load(int b, const ScenePicBlock &made) {
    Unload(b, true);

    const std::uintmax_t size = dc::data::DataFileSize(made.file);

    if (size == 0 || size > (64u << 20)) {
        std::fprintf(stderr, "worlds: scene pictures: no %s in this copy's game data\n", made.file);
        std::fflush(stderr);
        g_loaded[b].made = made; // (not asked again)
        return;
    }

    Loaded &loaded = g_loaded[b];

    loaded.data.emplace_back((std::size_t) (size / sizeof(u_long128)) + 4096 / sizeof(u_long128) + 1);

    char path[sizeof made.file];
    int  read = 0;

    Copy(path, sizeof path, made.file);
    if (LoadFile2(path, loaded.data[0].data(), &read, 0) == 0) {
        loaded.data.clear();
        loaded.made = made;
        return;
    }

    LOADTEXTURE_INFO2 table[kEntries + 1] = {};
    int               n = 0;

    for (int e = 0; e < kEntries && made.entry[e][0] != 0; e++) {
        char name[sizeof made.entry[e]];
        int  entry_size = 0;

        Copy(name, sizeof name, made.entry[e]);

        u_int *file = GetPackFile((u_int *) loaded.data[0].data(), name, &entry_size);

        if (file == nullptr || entry_size <= 0) {
            continue;
        }
        loaded.data.emplace_back((std::size_t) entry_size / sizeof(u_long128) + 1);
        std::memcpy(loaded.data.back().data(), file, (std::size_t) entry_size);
        table[n].name = (char *) loaded.data.back().data();
        table[n].block_no = kFirstBlock + b;
        table[n].mipmap = false;
        n++;
    }
    table[n].name = nullptr;
    TexManager.DeleteTextureBlock(kFirstBlock + b);
    TexManager.CleanUpBuffer();
    TexManager.LoadTextureBlockEX(kFirstBlock + b, table);
    loaded.on = true;
    loaded.made = made;
    std::fprintf(stderr, "worlds: scene pictures: block %d loaded from %s (%d of its entries, first %s)\n", kFirstBlock + b, made.file, n,
                 made.entry[0]);
    std::fflush(stderr);
}

bool SpritesFresh() {
    return g_heard.sprites_set && std::chrono::steady_clock::now() - g_heard.sprites_at <= std::chrono::milliseconds(500);
}

} // namespace

// coop-sceneactors (at the end of this file)
static void SendActors(std::uint32_t magic, int player, std::uint32_t seq, int map, int floor);
static bool ReceiveActors(const void *data, int size, int other);
static void ActorsTick(bool apart, int map, int floor);

void CoopWorldsSceneChrFile(int index, const char *path) {
    if (index >= 0 && index < 16) {
        g_chr_path[index] = path != nullptr ? path : "";
    }
    coop::ArrivalChrFile(index, path); // coop-latefall: a scene's own pack, or the stairwell's (game state)
    // coop-b1lesson: the scene's packs so far, once each.
    if (path != nullptr && path[0] != 0 && (int) g_scene_packs.size() < kPacks &&
        std::find(g_scene_packs.begin(), g_scene_packs.end(), std::string(path)) == g_scene_packs.end()) {
        g_scene_packs.emplace_back(path);
        g_packs_version++;
    }
}

namespace dc::game {
std::uint32_t Draw3DModelPrint(const unsigned int *data); // port/src/visualvu1.cpp (coop-joinatla)
} // namespace dc::game

namespace {

// A model's print, as the port's mesh records name their model (port/src/visualvu1.cpp's MdtPrint: coop-joinatla, the whole
// model, so two models with one header are never taken for each other).
std::uint32_t HeaderPrint(const unsigned *data) {
    return Draw3DModelPrint(data);
}

// Each pack the other's scene read, from this copy's own game data, once; every 16-byte place in it that could start a
// model (its size fits what is left of the pack) indexed by its header's print.
void ReadScenePacks() {
    for (const std::string &file : g_heard_packs) {
        if (std::find(g_pack_read.begin(), g_pack_read.end(), file) != g_pack_read.end()) {
            continue;
        }
        g_pack_read.push_back(file);

        const std::uintmax_t size = dc::data::DataFileSize(file);

        std::fprintf(stderr, "worlds: scene join: reading %s (%llu bytes)\n", file.c_str(), (unsigned long long) size);
        std::fflush(stderr);
        if (size == 0 || size > (32u << 20)) {
            std::fprintf(stderr, "worlds: scene join: no %s in this copy's game data\n", file.c_str());
            continue;
        }
        // (LoadFile2 reads whole sectors: room past the file's end, as Load leaves.)
        g_pack_data.emplace_back((std::size_t) (size / sizeof(u_long128)) + 4096 / sizeof(u_long128) + 1);

        char path[64];
        int  read = 0;

        Copy(path, sizeof path, file.c_str());
        if (LoadFile2(path, g_pack_data.back().data(), &read, 0) == 0 || read <= 0) {
            g_pack_data.pop_back();
            continue;
        }

        unsigned *const   words = (unsigned *) g_pack_data.back().data();
        const std::size_t count = (std::size_t) read / 4;
        int               places = 0;

        for (std::size_t at = 0; at + sizeof(MDT_HEADER) / 4 <= count; at += 4) {
            const MDT_HEADER *header = (const MDT_HEADER *) (words + at);

            if (header->size < sizeof(MDT_HEADER) || header->size / 4 > count - at) {
                continue;
            }
            g_pack_models.emplace(HeaderPrint(words + at), words + at);
            places++;
        }
        std::fprintf(stderr, "worlds: scene join: %s read from this copy's game data (%d bytes, %d places a model could start)\n", file.c_str(),
                     read, places);
    }
    std::fflush(stderr);
}

} // namespace

void CoopWorldsScenePacksTick(bool scene, bool joining) {
    // The sending copy: a scene's packs are its own (none between scenes).
    if (!scene && !g_scene_packs.empty()) {
        g_scene_packs.clear();
        g_packs_version++;
    }
    // The joining copy (held for the other's scene it joins, then in it): the other's packs read before its state comes,
    // so the first frame after it finds the models (a mesh record built empty is not built again); dropped once the scene
    // is over. DC_WORLDS_MUTATE=lesson_no_models (twin): none read, as before (the elder not drawn).
    static const char *const mutate = TestEnv("DC_WORLDS_MUTATE");

    if (joining && !(mutate != nullptr && std::strstr(mutate, "lesson_no_models") != nullptr)) {
        ReadScenePacks();
    } else if (!joining && !scene && !g_pack_data.empty()) {
        std::fprintf(stderr, "worlds: scene join: the scene's packs dropped (%zu read, %d models found in them)\n", g_pack_data.size(), g_pack_finds);
        std::fflush(stderr);
        g_pack_data.clear();
        g_pack_read.clear();
        g_pack_models.clear();
        g_heard_packs.clear();
        g_pack_finds = 0;
    }
}

unsigned int *CoopWorldsSceneModel(std::uint32_t print) {
    const auto it = g_pack_models.find(print);

    if (it == g_pack_models.end() || HeaderPrint(it->second) != print) {
        return nullptr;
    }
    if (g_pack_finds++ < 12) {
        std::fprintf(stderr, "worlds: scene join: model %08x found in the scene's packs (this copy's own read)\n", (unsigned) print);
        std::fflush(stderr);
    }
    return it->second;
}

void CoopWorldsSceneTexture(int block, int file, const char *entry, int order) {
    if (block < kFirstBlock || block >= kFirstBlock + kBlocks || order < 0 || order >= kEntries) {
        return;
    }
    ScenePicBlock &made = g_made[block - kFirstBlock];

    if (order == 0) {
        made = ScenePicBlock{};
        Copy(made.file, sizeof made.file, file >= 0 && file < 16 ? g_chr_path[file].c_str() : "");
    }
    Copy(made.entry[order], sizeof made.entry[order], entry);
    g_made_version++;
}

void CoopWorldsScenePicturesSend(std::uint32_t magic, int player, std::uint32_t seq, int map, int floor) {
    // The blocks' makings at once when they change (B1's board loads each page's picture as it turns to it: the other
    // draws a page only once it has it), and twice a second besides, so a lost one comes again.
    if (g_made_version != g_sent_version || seq % 30 == 1) {
        g_sent_version = g_made_version;
        ScenePicFiles files{magic, (std::uint32_t) player, kScenePicFiles, seq, map, floor};

        for (int b = 0; b < kBlocks; b++) {
            files.blocks[b] = g_made[b];
        }
        NetSocketSend(&files, sizeof files);
    }

    // coop-b1lesson: the scene's packs, as they change and once a second.
    if (g_packs_version != g_packs_sent_version || seq % 60 == 2) {
        g_packs_sent_version = g_packs_version;
        ScenePacks packs{magic, (std::uint32_t) player, kScenePacks, seq, map, floor, (std::uint32_t) g_scene_packs.size()};

        for (std::size_t i = 0; i < g_scene_packs.size(); i++) {
            Copy(packs.path[i], sizeof packs.path[i], g_scene_packs[i].c_str());
        }
        NetSocketSend(&packs, sizeof packs);
    }

    ScenePicSprites sprites{magic, (std::uint32_t) player, kScenePicSprites, seq, 0};
    const unsigned  count = GatherSprites(&sprites);

    sprites.count = count;
    NetSocketSend(&sprites, sizeof sprites);
    // (Tests: what this copy drew of each 30th view it sent, as the other's draw lines say what it drew.)
    if ((count != g_sent_count || seq % 30 == 0) && CoopDebug()) {
        std::fprintf(stderr, "worlds: scene pictures sent %u: %u sprites%s%s\n", seq, count, count != 0 ? ", first " : "",
                     count != 0 ? sprites.sprites[0].texture : "");
    }
    g_sent_count = count;
    SendActors(magic, player, seq, map, floor); // coop-sceneactors
}

unsigned CoopWorldsScenePicturesSent() {
    return g_sent_count;
}

bool CoopWorldsScenePicturesReceive(const void *data, int size, int other) {
    if (ReceiveActors(data, size, other)) { // coop-sceneactors
        return true;
    }
    if (size == (int) sizeof(ScenePacks)) {
        ScenePacks packs;

        std::memcpy(&packs, data, sizeof packs);
        if (packs.kind != kScenePacks) {
            return false;
        }
        if (packs.player == (std::uint32_t) other && packs.count <= (std::uint32_t) kPacks) {
            g_heard_packs.clear();
            for (std::uint32_t i = 0; i < packs.count; i++) {
                char path[sizeof packs.path[i] + 1];

                Copy(path, sizeof path, packs.path[i]);
                g_heard_packs.emplace_back(path);
            }
        }
        return true;
    }
    if (size == (int) sizeof(ScenePicFiles)) {
        ScenePicFiles files;

        std::memcpy(&files, data, sizeof files);
        if (files.kind != kScenePicFiles) {
            return false; // (another worlds message of this size)
        }
        if (files.player == (std::uint32_t) other) {
            g_heard.files_set = true;
            g_heard.files = files;
        }
        return true;
    }
    if (size == (int) sizeof(ScenePicSprites)) {
        ScenePicSprites sprites;

        std::memcpy(&sprites, data, sizeof sprites);
        if (sprites.kind != kScenePicSprites) {
            return false;
        }
        if (sprites.player == (std::uint32_t) other && sprites.count <= (std::uint32_t) kSprites) {
            g_heard.sprites_set = true;
            g_heard.sprites = sprites;
            g_heard.sprites_at = std::chrono::steady_clock::now();
        }
        return true;
    }
    return false;
}

void CoopWorldsScenePicturesTick(bool apart, int map, int floor) {
    const bool showing = apart && CoopWorldsSceneView(nullptr, nullptr, nullptr) && !NoPictures();

    for (int b = 0; b < kBlocks; b++) {
        const bool wanted = showing && g_heard.files_set && g_heard.files.map == map && g_heard.files.floor == floor &&
                            g_heard.files.blocks[b].file[0] != 0;

        if (!wanted) {
            Unload(b, apart);
        } else if (!SameBlock(g_loaded[b].made, g_heard.files.blocks[b])) {
            Load(b, g_heard.files.blocks[b]);
        }
    }
    if (!showing) {
        g_heard.files_set = false;
        g_heard.sprites_set = false;
    }
    ActorsTick(apart, map, floor); // coop-sceneactors
}

void CoopWorldsSceneSpritesDraw() {
    if (!SpritesFresh() || !CoopWorldsSceneView(nullptr, nullptr, nullptr) || NoPictures()) {
        return;
    }

    const ScenePicSprites &heard = g_heard.sprites;
    int                    reloaded = -1;
    unsigned               drawn = 0;

    for (unsigned i = 0; i < heard.count; i++) {
        const ScenePicSprite &s = heard.sprites[i];
        const int             b = s.block - kFirstBlock;

        if (b < 0 || b >= kBlocks || !g_loaded[b].on) {
            continue;
        }

        char name[sizeof s.texture + 1];

        Copy(name, sizeof name, s.texture);

        CTexture *texture = TexManager.GetTexture(name, s.block);

        if (texture == nullptr) {
            continue;
        }
        if (reloaded != s.block) {
            TexManager.ReloadTexture(GetVif1Packet(), s.block);
            reloaded = s.block;
        }

        const CRect_i_ screen(s.x, s.y, s.width, s.height);
        const CRect_i_ texel(s.u, s.v, s.u_width, s.v_height);

        setbilinear((s.flags & 2) != 0 ? 1 : 0);
        if ((s.flags & 1) == 0) {
            set2DSprite(GetVif1Packet(), texture, screen, texel, s.red, s.green, s.blue, s.alpha);
        } else {
            set2DSpriteRot(GetVif1Packet(), texture, screen, texel, s.pivot_x, s.pivot_y, s.angle, s.red, s.green, s.blue, s.alpha);
        }
        drawn++;
    }
    setbilinear(0);

    // (Tests: what this copy drew of each 30th view the other sent, as its sent lines say.)
    static std::uint32_t logged = 0;

    if (CoopDebug() && heard.seq % 30 == 0 && heard.seq != logged) {
        logged = heard.seq;
        std::fprintf(stderr, "worlds: scene pictures drawn %u: %u of %u sprites\n", heard.seq, drawn, heard.count);
    }
}

// coop-sceneactors (rc3: "her scene view draws fewer meshes than his screen"): the other's scene's own actors on the copy that
// shows it. Measured on the B1 lesson after a resync (the cave entry): his frame drew 119 meshes, hers of the same view 67-75;
// the difference was the scene's event NPC (slot 1, the lesson's elder, e29c03c.cfg of gedit/s99/chara/e29.chr: 28 meshes and its
// shadow) and the second (away2.cfg). The scene's script loads them (_LOAD_CHARA) and moves them, and only the copy playing the
// scene runs it: the showing copy has its camera (CoopWorldsSceneView), its player (ScenePoseTick) and its pictures (above), and
// had none of its actors. Now the playing copy says, with each view, which pack entry each drawn NPC was made from and where it
// stands, how it turns and where its motion is; the showing copy makes the same model from its own game data, into an actor of
// its own (never the game's NPC slots, which its own held script uses), poses it so, and draws it and its shadow in the view.
// Its textures go in the town bodies' blocks of players 4 to 6 (no floor uses them; one only while it holds nothing), and go
// when the view ends, only while the block still holds what this made. DC_WORLDS_MUTATE=scene_no_actors (twin): none drawn.
namespace {

constexpr std::uint32_t kSceneActors = 8; // (worlds message kind)
constexpr int           kActors = 6;      // the event NPC slots (EdEventInfo.npc_count in a dungeon)
constexpr int           kActorBlocks[] = {134, 135, 136, 137, 138, 139}; // (TownBodyBlock's for players 4 to 6: town only)

#pragma pack(push, 1)
struct SceneActor {
    std::uint8_t  on;   // 1 drawn, 2 its shadow drawn too
    std::int8_t   pack; // the pack it was made from: its place in the scene's packs (ScenePacks), -1 none
    std::uint8_t  pad[2];
    char          cfg[24]; // the pack's entry (_LOAD_CHARA's name)
    float         pos[3], rot[3], scale[3];
    float         ambient[4]; // ambient_offset (its fade in [3])
    std::int32_t  motion_no, motion_flags;
    float         motion_speed, time, blend, blend_step;
    std::uint32_t frame, next_frame;
    std::int32_t  asked, playing, blending;
};

struct SceneActors {
    std::uint32_t magic;
    std::uint32_t player;
    std::uint32_t kind; // kSceneActors
    std::uint32_t seq;
    std::int32_t  map;
    std::int32_t  floor;
    SceneActor    actors[kActors];
};
#pragma pack(pop)

static_assert(sizeof(SceneActors) == kCoopWorldsSceneActorsBytes, "coop_worlds.hpp names this message's size");
static_assert(sizeof(SceneActors) != sizeof(ScenePacks) && sizeof(SceneActors) != sizeof(ScenePicFiles) &&
                  sizeof(SceneActors) != sizeof(ScenePicSprites),
              "worlds messages are told apart by their size");
static_assert(sizeof(SceneActors) <= 1088, "a worlds message fits one datagram");

// The playing copy: what each NPC slot was made from (the pack's path and the entry), as _LOAD_CHARA loaded it.
std::string g_npc_pack[kActors];
std::string g_npc_cfg[kActors];

// The showing copy: the other's last, when, and its own actors.
struct HeardActors {
    bool                                  set = false;
    SceneActors                           msg{};
    std::chrono::steady_clock::time_point at{};
} g_heard_actors;

struct Actor {
    CNPCharacter                       *body = nullptr;
    CTexAnimeData                      *tex_anime = nullptr;
    u_char                             *model = nullptr;
    CDataAlloc2<1>                      arena;
    std::vector<std::vector<u_long128>> pack; // the pack, read from this copy's game data
    std::string                         from; // "<pack>|<cfg>" it was made from ("" none)
    int                                 block = -1;
    std::vector<std::string>            textures; // the names it put in its block
    bool                                drawn = false;
} g_actors[kActors];

bool NoActors() {
    static const char *const mutate = TestEnv("DC_WORLDS_MUTATE");

    return mutate != nullptr && std::strstr(mutate, "scene_no_actors") != nullptr;
}

bool ActorsFresh() {
    return g_heard_actors.set && std::chrono::steady_clock::now() - g_heard_actors.at <= std::chrono::milliseconds(500);
}

int BlockTextures(int block, std::vector<std::string> *names) {
    int n = 0;

    for (const CTexture &texture : TexManager.textures) {
        if (texture.name[0] != 0 && texture.block == block) {
            n++;
            if (names != nullptr) {
                names->emplace_back(texture.name);
            }
        }
    }
    return n;
}

void ActorUnload(int i, bool apart) {
    Actor &a = g_actors[i];

    if (a.from.empty()) {
        return;
    }
    // (Its block emptied only while it holds what this made: merged, the state set brought the other's table.)
    std::vector<std::string> now;

    if (a.block >= 0) {
        BlockTextures(a.block, &now);
    }
    const bool mine = apart && !now.empty() && std::all_of(now.begin(), now.end(), [&](const std::string &name) {
        return std::find(a.textures.begin(), a.textures.end(), name) != a.textures.end();
    });

    if (mine) {
        TexManager.DeleteTextureBlock(a.block);
        TexManager.CleanUpTextureList();
    }
    std::fprintf(stderr, "worlds: scene actors: slot %d (%s) put away%s\n", i, a.from.c_str(),
                 mine || a.block < 0 ? "" : " (its block not its own now: its buffers only)");
    std::fflush(stderr);
    a.from.clear();
    a.pack.clear();
    a.textures.clear();
    a.block = -1;
    a.drawn = false;
}

// Slot i made as the other's was, from this copy's own read of the same pack, as _LOAD_CHARA makes it.
void ActorLoad(int i, const std::string &pack, const char *cfg) {
    Actor &a = g_actors[i];

    ActorUnload(i, true);
    a.from = pack + "|" + cfg; // (not asked again if it cannot be made)

    int block = -1;

    for (int b : kActorBlocks) {
        bool taken = BlockTextures(b, nullptr) != 0;

        for (const Actor &other : g_actors) {
            taken = taken || other.block == b;
        }
        if (!taken) {
            block = b;
            break;
        }
    }
    const std::uintmax_t size = dc::data::DataFileSize(pack);

    if (block < 0 || size == 0 || size > (32u << 20)) {
        std::fprintf(stderr, "worlds: scene actors: slot %d (%s) not made: %s\n", i, a.from.c_str(), block < 0 ? "no free block" : "no such pack here");
        std::fflush(stderr);
        return;
    }
    if (a.body == nullptr) {
        a.body = new CNPCharacter();
        a.tex_anime = new CTexAnimeData[64];
        a.model = new (std::align_val_t{64}) u_char[static_cast<std::size_t>(coop::kModelQuads) * 16];
    }
    a.pack.emplace_back((std::size_t) (size / sizeof(u_long128)) + 4096 / sizeof(u_long128) + 1);

    char path[64];
    int  read = 0;

    Copy(path, sizeof path, pack.c_str());
    if (LoadFile2(path, a.pack[0].data(), &read, 0) == 0 || read <= 0) {
        a.pack.clear();
        return;
    }
    TexManager.DeleteTextureBlock(block);
    TexManager.CleanUpBuffer();
    TexManager.CleanUpTextureList();
    a.arena.base = a.model;
    a.arena.buffer = a.model;
    a.arena.used = 0;
    a.arena.limit = coop::kModelQuads;

    CNPCharacter *body = a.body;
    char          name[sizeof(SceneActor::cfg) + 1];

    Copy(name, sizeof name, cfg);
    body->Initialize();
    for (int t = 0; t < 64; t++) {
        a.tex_anime[t].Initialize();
    }
    body->InitializeTexAnime(a.tex_anime, 64);
    body->LoadPackData2((unsigned int *) a.pack[0].data(), name, &a.arena, block, &a.arena, 0);
    if (body->frame == nullptr) {
        std::fprintf(stderr, "worlds: scene actors: slot %d (%s) not made: no model\n", i, a.from.c_str());
        std::fflush(stderr);
        TexManager.DeleteTextureBlock(block);
        TexManager.CleanUpTextureList();
        a.pack.clear();
        return;
    }
    // (as _LOAD_CHARA sets an event NPC up; no motion camera: its motions never move this copy's camera)
    body->texture_block = block;
    body->initialized = 1;
    body->draw_enabled = 1;
    body->near_camera = 1;
    body->ambient_offset[3] = 128.0f;

    CFrameAttr attr;

    attr.clip_enable = false;
    attr.fog_enable = true;
    body->frame->SetAttr(attr, 1, 4);
    body->FootSoundEnable(0);
    body->EventEnable(0);
    a.block = block;
    BlockTextures(block, &a.textures);
    std::fprintf(stderr, "worlds: scene actors: slot %d made from %s in block %d (%zu textures)\n", i, a.from.c_str(), block, a.textures.size());
    std::fflush(stderr);
}

// Posed as the other's stands this tick: place, turn, size, fade and motion clock, then one step of its motion as the game's.
void ActorPose(Actor &a, const SceneActor &s) {
    CNPCharacter *body = a.body;

    body->SetPosition(s.pos[0], s.pos[1], s.pos[2]);
    body->SetRotation(s.rot[0], s.rot[1], s.rot[2]);
    body->SetScale(s.scale[0], s.scale[1], s.scale[2]);
    for (int k = 0; k < 4; k++) {
        body->ambient_offset[k] = s.ambient[k];
    }

    MOTION_STATE &state = body->motion_type.state;

    body->motion_no = s.motion_no;
    body->motion_flags = s.motion_flags;
    body->motion_speed = s.motion_speed;
    state.time = s.time;
    state.blend = s.blend;
    state.blend_step = s.blend_step;
    state.frame = s.frame;
    state.next_frame = s.next_frame;
    state.motion_no = s.asked;
    state.playing_no = s.playing;
    state.blending = s.blending;
    body->CCharacter::Step(); // (the motion's pose; not CNPCharacter's, which would move its fade and sequences)
    body->ShadowStep();
    body->ClothStep(0);
}

} // namespace

void CoopWorldsSceneNpcLoaded(int index, int file, const char *cfg) {
    if (index < 0 || index >= kActors) {
        return;
    }
    g_npc_pack[index] = file >= 0 && file < 16 ? g_chr_path[file] : "";
    g_npc_cfg[index] = cfg != nullptr ? cfg : "";
}

// The playing copy: each drawn NPC of its scene, with the view (CoopWorldsScenePicturesSend).
static void SendActors(std::uint32_t magic, int player, std::uint32_t seq, int map, int floor) {
    SceneActors msg{magic, (std::uint32_t) player, kSceneActors, seq, map, floor};
    unsigned    sent = 0;

    for (int i = 0; i < kActors; i++) {
        SceneActor &s = msg.actors[i];

        s.pack = -1;
        if (EdEventInfo.npcs == nullptr || i >= EdEventInfo.npc_count || EdEventInfo.npc_draw[i] == 0 || g_npc_cfg[i].empty()) {
            continue;
        }
        CNPCharacter &npc = EdEventInfo.npcs[i];
        const auto    at = std::find(g_scene_packs.begin(), g_scene_packs.end(), g_npc_pack[i]);

        if (npc.frame == nullptr || npc.initialized == 0 || at == g_scene_packs.end()) {
            continue;
        }
        s.on = (std::uint8_t) (1 | (EdEventInfo.npc_shadow_draw[i] != 0 ? 2 : 0));
        s.pack = (std::int8_t) (at - g_scene_packs.begin());
        Copy(s.cfg, sizeof s.cfg, g_npc_cfg[i].c_str());

        float v[4];

        npc.GetPosition(v);
        std::memcpy(s.pos, v, sizeof s.pos);
        npc.frame->GetRotation(v);
        std::memcpy(s.rot, v, sizeof s.rot);
        std::memcpy(s.scale, npc.frame->scale, sizeof s.scale);
        std::memcpy(s.ambient, npc.ambient_offset, sizeof s.ambient);

        const MOTION_STATE &state = npc.motion_type.state;

        s.motion_no = npc.motion_no;
        s.motion_flags = npc.motion_flags;
        s.motion_speed = npc.motion_speed;
        s.time = state.time;
        s.blend = state.blend;
        s.blend_step = state.blend_step;
        s.frame = state.frame;
        s.next_frame = state.next_frame;
        s.asked = state.motion_no;
        s.playing = state.playing_no;
        s.blending = state.blending;
        sent++;
    }
    NetSocketSend(&msg, sizeof msg);
    // (Tests: the actors of each 5th view sent, as the showing copy's drawn lines say what it drew of them.)
    if (CoopDebug() && seq % 5 == 0) {
        std::fprintf(stderr, "worlds: scene actors sent %u: %u\n", seq, sent);
    }
}

static bool ReceiveActors(const void *data, int size, int other) {
    if (size != (int) sizeof(SceneActors)) {
        return false;
    }
    SceneActors msg;

    std::memcpy(&msg, data, sizeof msg);
    if (msg.kind != kSceneActors) {
        return false;
    }
    if (msg.player == (std::uint32_t) other) {
        g_heard_actors.set = true;
        g_heard_actors.msg = msg;
        g_heard_actors.at = std::chrono::steady_clock::now();
    }
    return true;
}

// The showing copy, each tick: its actors made, posed or put away as the other's are.
static void ActorsTick(bool apart, int map, int floor) {
    const bool showing = apart && CoopWorldsSceneView(nullptr, nullptr, nullptr) && !NoActors() && ActorsFresh() &&
                         g_heard_actors.msg.map == map && g_heard_actors.msg.floor == floor;

    for (int i = 0; i < kActors; i++) {
        const SceneActor &s = g_heard_actors.msg.actors[i];
        const bool        wanted = showing && (s.on & 1) != 0 && s.pack >= 0 && s.pack < (int) g_heard_packs.size();
        Actor            &a = g_actors[i];

        a.drawn = false;
        if (!wanted) {
            ActorUnload(i, apart);
            continue;
        }
        char cfg[sizeof s.cfg + 1];

        Copy(cfg, sizeof cfg, s.cfg);
        if (a.from != g_heard_packs[s.pack] + "|" + cfg) {
            ActorLoad(i, g_heard_packs[s.pack], cfg);
        }
        if (a.block >= 0 && a.body != nullptr && a.body->frame != nullptr) {
            ActorPose(a, s);
            a.drawn = true;
        }
    }
}

// In the view's draw (CoopDraw): each actor's shadow, then its body, as the game draws an event NPC.
void CoopWorldsSceneActorsDraw() {
    if (!CoopWorldsSceneView(nullptr, nullptr, nullptr)) {
        return;
    }
    unsigned drawn = 0;

    for (int i = 0; i < kActors; i++) {
        Actor &a = g_actors[i];

        if (!a.drawn || NoActors()) {
            continue;
        }
        if ((g_heard_actors.msg.actors[i].on & 2) != 0) {
            float light[4][4];
            float colour[4][4];
            float saved[4][4];

            MGGetPLight(light, colour);
            sceVu0CopyMatrix(saved, light);
            light[0][0] *= 0.2f;
            light[2][0] *= 0.2f;
            MGSetPLight(light, colour);
            TexManager.ReloadTexture(GetVif1Packet(), 15);
            MGBeginDrawShadow(*(sceGsTex0 *) &TexManager.GetTexture((char *) "shadow_buf", -1)->tex0);
            a.body->DrawShadow();
            MGEndDrawShadow(0x40);
            sceVu0CopyMatrix(light, saved);
            MGSetPLight(light, colour);
        }
        TexManager.ReloadTexture(GetVif1Packet(), a.block);
        a.body->TextureAnime(a.block);
        a.body->Draw();
        drawn++;
    }
    // (Tests: what this copy drew of each 5th view, as the other's sent lines say; 0 under the twin.)
    static std::uint32_t logged = 0;
    const std::uint32_t  seq = g_heard_actors.msg.seq;

    if (CoopDebug() && seq % 5 == 0 && seq != logged) {
        logged = seq;
        std::fprintf(stderr, "worlds: scene actors drawn %u: %u\n", seq, drawn);
    }
}
