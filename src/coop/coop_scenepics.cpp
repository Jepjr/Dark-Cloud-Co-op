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
#include "rect.hpp"
#include "snd.hpp"
#include "spritetable.hpp"
#include "texture.hpp"

#include "coop/coop.hpp"
#include "coop/coop_camera.hpp" // coop-latefall: ArrivalChrFile
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

            for (int t = 0; t < 196; t++) {
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
}

unsigned CoopWorldsScenePicturesSent() {
    return g_sent_count;
}

bool CoopWorldsScenePicturesReceive(const void *data, int size, int other) {
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
