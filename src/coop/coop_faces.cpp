#include "coop/coop_faces.hpp"
#include "coop/game_ns.hpp"
#include "coop/coop.hpp"
#include "coop/coop_actor.hpp"
#include "coop/coop_core.hpp"
#include "coop/coop_town.hpp"
#include "coop/netplay.hpp"

#include <array>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>

#include "character.hpp"
#include "data/data_read.hpp"
#include "dataread.hpp"
#include "ebattle.hpp"
#include "platform/config.hpp"
#include "render/texture_registry.hpp"
#include "texture.hpp"
#include "tim2.hpp"
#include "textureanime.hpp"

// coop-faces (see coop_faces.hpp). Written by Jep.

namespace coop {

namespace {

constexpr int kMembers = 5;   // Toan, Xiao, Goro, Ruby, Ungaga (Osmond's head is a helmet: no face sheet on the disc)
constexpr int kToanLooks = 9; // the fishing model's (c01d_turi.cfg) groups 0-8

// A held look made of strips of the member's own sheet (sheet x, y, w, h onto the face at x, y, as TEX_ANIME_DATA), for a member whose
// sheet has no look of its own close to Toan's: Goro's (only a blink and a talk on the disc).
struct Strip {
    int group;
    s16 sx, sy, w, h, dx, dy;
};
// Goro (c06p: eyes 128 wide x 32 onto the face at y 12, mouths at y 48): 20 surprised (his talk's widest mouth, ぱく4, held), 21
// straining (his blink's shut eyes, 目ぱち2, held), 22 a little surprised (his talk's first mouth, ぱく1, held). Each with the other band's
// default strip first.
const Strip kGoroLooks[] = {
    {20, 128, 0, 128, 32, 0, 12}, {20, 0, 128, 128, 32, 0, 48}, //
    {21, 0, 0, 128, 32, 0, 48},   {21, 128, 64, 128, 32, 0, 12}, //
    {22, 128, 0, 128, 32, 0, 12}, {22, 0, 32, 128, 32, 0, 48},
};

// Where each member's face frames are on the disc (the plan's survey, debug\notes\facial-plan.md): the face picture its play body
// paints on the head (plate), the sheet of eyes and mouths copied over it, the pack and configuration that hold both, and the
// configuration's blink group. The plates of the scenes' models are the play bodies' own pictures (Xiao, Goro, Ungaga byte for byte;
// Ruby close), so only the sheet is loaded: into the body's own block, beside its plate.
struct MemberFace {
    const char *plate;
    const char *sheet;
    const char *pack;
    const char *cfg;
    int         blink;
    int         stamp; // the group of the default eyes and mouth, copied once as a look changes
    // A fisher's looks from another sheet and configuration (Toan's: the fishing model's, drawn on scene e305's sheet), at groups
    // look_base + n. Null: the looks are in the face's own configuration.
    const char *look_sheet;
    const char *look_pack;     // the pack holding look_sheet
    const char *look_cfg_pack; // the pack holding look_cfg
    const char *look_cfg;
    int         look_base;
    // Toan's fishing look (T0-T8, the fishing model's groups) -> this member's group (-1: the blink). Jep's picks (11-18: the defaults
    // of the picture sheet, debug\notes\faces-looks.md).
    std::array<int, kToanLooks> looks;
    const Strip                *strips;
    int                         strip_count;
};
const MemberFace kFaces[kMembers] = {
    // Toan: his dungeon pack's own sheet (c01d11, in c01d01_dun.img beside the plate) and its blink group, which retail leaves off.
    // The fishing model (c01d_turi) carries the same plate and sheet, pixel for pixel, and turns no group on: it blinks with these
    // too. Its fishing looks (c01d_turi.cfg's groups) copy from a c01d01an the fishing pack lacks: scene e305's (gedit/s11), whose
    // 13 strips match them by place and name; retail never shows them (debug\notes\faces-looks.md).
    {"c01d01", "c01d11", "dun/mainchara/c01d.chr", "base.cfg", 1, 10, "c01d01an", "gedit/s11/chara/e305c01d.chr", "chara/c01d_turi.chr", "c01d_turi.cfg", 10,
     {-1, -1, 12, 13, 14, 15, 16, 17, 18}, nullptr, 0},
    // Xiao (e04c04b: blink 1, talk 2, laugh 3, eyes squeezed 4)
    {"c04b01", "c04b01an", "gedit/e01/chara/e04c04b.chr", "e04c04b.cfg", 1, 0, nullptr, nullptr, nullptr, nullptr, 0, {-1, -1, 3, 3, 4, 4, -1, 3, 3}, nullptr, 0},
    // Goro (c06p: blink 1, talk 2; his looks made of those strips, kGoroLooks)
    {"c06a01", "c06a01an", "gedit/s01/chara/c06p.chr", "info.cfg", 1, 0, nullptr, nullptr, nullptr, nullptr, 0, {-1, -1, 22, 20, 21, 21, -1, 20, 20}, kGoroLooks,
     static_cast<int>(std::size(kGoroLooks))},
    // Ruby (e228c05a: blink 1, right look 2, sulky 3, smirk 4, troubled 5, left look 6, looking up 7, smirk 8, talk 9, wink 10, twist 11)
    {"c05a01", "c05a01an", "gedit/e03/chara/e228c05a.chr", "info.cfg", 1, 0, nullptr, nullptr, nullptr, nullptr, 0, {-1, -1, 2, 7, 3, 3, 3, 7, 7}, nullptr, 0},
    // Ungaga (e323_1c10a: blink 1, eyes shut 2, closing 3, opening 4, weak 5-7, talk 8, sulky 9, mouth open 10, puzzled 11, talk 12)
    {"c10a01", "c10a01an", "gedit/e04/chara/e323_1c10a.chr", "e323_1c10a.cfg", 1, 0, nullptr, nullptr, nullptr, nullptr, 0, {-1, -1, 11, 10, 2, 2, 9, 10, 10},
     nullptr, 0},
};

// A member's sheet (its IMG archive, copied out of the pack and kept: its pixels stay where the texture table points) and the
// configuration text; the same for its fishing looks' own sheet and configuration, when it has them apart.
struct Sheet {
    u_char *img = nullptr;
    int     img_size = 0;
};
struct MemberCache {
    Sheet       sheet, look_sheet;
    Sheet       scene_plate; // coop-faces2: the scene pack's own face picture (the sheet's table)
    std::string cfg, look_cfg;
    bool        ok = false;
};
MemberCache g_cache[kMembers];
bool        g_prepared = false;

constexpr int kRecords = 192; // Ruby's e228 configuration is the longest (about 80 of the sheet's strips); Toan's two about 30
constexpr int kKits = 12;     // six players' bodies, stand-ins, player 1's own

struct Kit {
    CCharacter   *body = nullptr;
    int           block = -1;
    int           member = -1; // -1: no member's plate in the block (or built nothing: none tried again till the block changes)
    bool          looks = false; // built with the fishing looks' own sheet (a fisher's: Toan's)
    bool          scene_face = false; // coop-faces2: the scene's own face laid over the play face (SceneFaceFor)
    int           plate_slot = -1, sheet_slot = -1, look_slot = -1;
    u_long        plate_tex0 = 0, sheet_tex0 = 0, look_tex0 = 0;
    int           slots_seen = -1; // the table's slots in use when it found nothing (tried again when that changes)
    CTextureAnime anime;
    CTexAnimeData records[kRecords];
    int           count = 0;
    int           look = -1, toan_look = 0, state = -1;
    long long     draws = 0;
    unsigned long used = 0;
};
Kit          *g_kits = nullptr;
unsigned long g_use = 0;

bool Test() {
    static const bool test = TestEnvOn("DC_COOP_TEST_FACES");
    return test;
}

int SlotsInUse() {
    int n = 0;

    for (const CTexture &texture : TexManager.textures) {
        n += texture.name[0] != 0 ? 1 : 0;
    }
    return n;
}

// The texture named name in block (the kit's own lookup), or anywhere, the newest first (retail's config loader: the twin).
int FindSlot(const char *name, int block) {
    return TexManager.SearchTextureName(const_cast<char *>(name), Mutated("faces_anyblock") ? -1 : block);
}

int OtherPlates(const char *name, int block, int *highest = nullptr) {
    int n = 0;

    for (int i = 0; i < kTextureCount; i++) {
        const CTexture &texture = TexManager.textures[i];

        if (texture.name[0] != 0 && texture.block != block && std::strcmp(texture.name, name) == 0) {
            n++;
            if (highest != nullptr) {
                *highest = i;
            }
        }
    }
    return n;
}

bool StillThere(const Kit &kit) {
    const auto same = [&](int slot, const char *name, u_long tex0) {
        if (slot < 0 || slot >= kTextureCount) {
            return false;
        }
        const CTexture &texture = TexManager.textures[slot];

        return std::strcmp(texture.name, name) == 0 && texture.tex0 == tex0 && (texture.block == kit.block || Mutated("faces_anyblock"));
    };
    const MemberFace &face = kFaces[kit.member];

    return same(kit.plate_slot, face.plate, kit.plate_tex0) && same(kit.sheet_slot, face.sheet, kit.sheet_tex0) &&
           (!kit.looks || same(kit.look_slot, face.look_sheet, kit.look_tex0));
}

// One record of the sheet onto the plate (the configuration's coordinates: EnterTexAnime turns them as retail's loader does).
void Enter(Kit &kit, const CTexture *sheet, const CTexture *plate, int group, int sx, int sy, int w, int h, int dx, int dy, int duration, int &n) {
    CTexAnimeData record;

    record.group = static_cast<s16>(group);
    record.kind = 0;
    record.source_x = static_cast<s16>(sx);
    record.source_y = static_cast<s16>(sy);
    record.source_width = static_cast<s16>(w);
    record.source_height = static_cast<s16>(h);
    record.dest_x = static_cast<s16>(dx);
    record.dest_y = static_cast<s16>(dy);
    record.duration = static_cast<s16>(duration);
    record.first_texture.Copy(const_cast<CTexture *>(sheet));
    record.second_texture.Copy(const_cast<CTexture *>(plate));
    n += kit.anime.EnterTexAnime(&record);
}

// The configuration's records of sheet_name onto the plate, at group + base, each with the two pictures found in the body's own
// block (the plan's trap: CTextureAnime::LoadCFGFile finds them by name anywhere, the newest first, so two bodies of one member share one's).
int ReadRecords(Kit &kit, const char *sheet_name, const char *plate_name, const CTexture *sheet, const CTexture *plate, const std::string &cfg, int base) {
    int    group = -1, n = 0;
    size_t at = 0;

    while (at < cfg.size()) {
        size_t      end = cfg.find('\n', at);
        std::string line = cfg.substr(at, end == std::string::npos ? std::string::npos : end - at);
        int         g = 0, on = 0;
        char        from[32] = {}, to[32] = {};
        int         sx = 0, sy = 0, w = 0, h = 0, dx = 0, dy = 0, frames = 0, hold = 0;

        at = end == std::string::npos ? cfg.size() : end + 1;
        if (std::sscanf(line.c_str(), " TEX_ANIME_DATA \"%31[^\"]\" , %d , %d , %d , %d , \"%31[^\"]\" , %d , %d , %d , %d", from, &sx, &sy, &w, &h, to, &dx, &dy,
                        &frames, &hold) == 10) {
            if (group >= 0 && std::strcmp(from, sheet_name) == 0 && std::strcmp(to, plate_name) == 0) {
                Enter(kit, sheet, plate, group + base, sx, sy, w, h, dx, dy, hold != 0 ? -1 : frames, n);
            }
        } else if (std::strncmp(line.c_str() + std::strspn(line.c_str(), " \t"), "TEX_ANIME_END", 13) == 0) {
            group = -1;
        } else if (std::sscanf(line.c_str(), " TEX_ANIME %d , %d", &g, &on) == 2) {
            group = g >= 0 && g + base < CTextureAnime::kGroupCount ? g : -1;
        }
    }
    return n;
}

// sheet's IMG archive into block unless the block has a picture of that name; false when the table has no room for it.
bool SheetIn(const char *name, const Sheet &sheet, int block, int in_use, const char *whose, int member) {
    if (TexManager.SearchTextureName(const_cast<char *>(name), block) >= 0 || sheet.img == nullptr) {
        return true;
    }
    // (a full table stops the game: texture.cpp TextureSlotTaken; the town's own loads keep 24 slots, coop_town's kTownBodySlotsKept)
    if (PortTextureSlotsLimit() - in_use <= 24) {
        std::fprintf(stderr, "coop-faces: p%d %s (member %d) keeps a still face: the texture table is too full for %s (%d of %d slots)\n", NetplayLocalPlayer(), whose,
                     member, name, in_use, PortTextureSlotsLimit());
        return false;
    }
    TexManager.EnterIMGFile(sheet.img, block, 0, 1);
    return true;
}

// coop-faces2 (Jep's 11-18 look at the faces clip: marks on Ruby's face): a sheet whose colour table is not its face's. A data set
// without baked texture documents keeps the game's pictures as palette indices (render/texture_registry.cpp, the legacy path), and a
// strip's copy moves indices (CTextureAnime::TexAnime -> PortMoveImage -> CopyTexture), drawn through the face's own table. Retail
// only ever copies a sheet onto the face of its own pack (one table). Ruby's sheet is a story scene's (e228c05a) and her play face is
// her dungeon pack's: their tables differ (747 of 1024 bytes), so her copied eyes came out dark with green and white dots. Here the
// scene's own face picture (the same pack as the sheet, the same table: the face that scene shows) is laid over the play face, its
// indices and its table, by retail's full-picture copy (TexAnime's for a strip the size of its picture), so the sheet's strips show
// their own colours; outside the eye and mouth bands the scene's face is the play face's within a little shading (Ruby: mean 1.5 of
// 255 a channel). It is laid again whenever the face's table is not the scene's (the body's block built again, a texture rebuild).
// Said at the kit's build: the error the raw copy would show through the face's table (mean, 99th percentile). The scene's face is a
// private renderer texture, outside the texture table and the state set. DC_COOP_MUTATE=faces_no_translate (twin): copied raw.
struct SceneFace {
    unsigned                  key = 0, palette = 0;
    unsigned                  width = 0, height = 0;
    std::array<uint32_t, 256> table{};
};
SceneFace g_scene_face[kMembers];

float ColourGap(uint32_t a, uint32_t b) {
    float most = 0.0f;

    for (int c = 0; c < 4; c++) {
        most = std::max(most, std::fabs(static_cast<float>((a >> (8 * c)) & 0xFF) - static_cast<float>((b >> (8 * c)) & 0xFF)));
    }
    return most;
}

// The named picture of an IMG archive decoded as the texture manager's raw path decodes it (indices and its own table).
bool DecodePicture(const u_char *buffer, const char *name, dc::render::PortDecodedTexture &out) {
    if (buffer == nullptr) {
        return false;
    }
    const bool  swizzled = std::memcmp(buffer, "IM2", 3) == 0;
    const auto *header = reinterpret_cast<const IMG_head *>(buffer);
    const auto *entries = reinterpret_cast<const IMG_entry *>(header + 1);

    for (u_int i = 0; i < header->pictures; i++) {
        if (std::strcmp(entries[i].name, name) != 0) {
            continue;
        }
        const auto   *head = reinterpret_cast<const TM2_head *>(buffer + entries[i].offset);
        const auto   *picture = reinterpret_cast<const TM2_picture *>(reinterpret_cast<const u_char *>(head) + 16);
        const u_char *image = reinterpret_cast<const u_char *>(picture) + picture->header_size;
        const u_char *levels[1] = {image};

        if (head->image_type != TIM2_IDTEX8) {
            return false;
        }
        return dc::render::PortDecodeTexture(1, head->image_width, head->image_height, levels, 1, image + picture->image_size, picture->clut_colors, swizzled,
                                             out);
    }
    return false;
}

// The scene's face over the play face plate (both index pictures): its indices, then its table, as TexAnime copies a whole picture.
bool LayFace(const SceneFace &scene, const CTexture *plate) {
    const sceGsTex0 &to = *reinterpret_cast<const sceGsTex0 *>(&plate->tex0);

    return dc::render::PortMoveImage(scene.key, SCE_GS_PSMT8, 0, 0, static_cast<int>(scene.width), static_cast<int>(scene.height), to.TBP0, to.PSM, 0, 0) &&
           dc::render::PortMoveImage(scene.palette, SCE_GS_PSMCT32, 0, 0, 16, 16, to.CBP, to.CPSM, 0, 0);
}

// At the kit's build (members whose sheet is a scene's): the scene's face laid over the play face when their tables differ (above).
void SceneFaceFor(Kit &kit, const MemberCache &cache, const MemberFace &face, const CTexture *plate, const char *whose) {
    std::array<uint32_t, 256>      play{};
    const sceGsTex0               &plate_tex0 = *reinterpret_cast<const sceGsTex0 *>(&plate->tex0);
    dc::render::PortDecodedTexture sheet;

    kit.scene_face = false;
    // (a colour picture, the baked path: the copy carries the sheet's own colours)
    if (!dc::render::PortPaletteEntries(plate_tex0.CBP, play.data()) || !DecodePicture(cache.sheet.img, face.sheet, sheet) || sheet.levels.empty()) {
        return;
    }
    if (sheet.palette == play) {
        return; // (one table: Xiao's, Goro's and Ungaga's scene sheets)
    }
    // (the error the raw copy shows through the face's table, over the sheet's pixels)
    std::vector<float> raw;
    double             sum = 0.0;

    for (uint8_t index : sheet.levels[0]) {
        raw.push_back(ColourGap(sheet.palette[index], play[index]));
        sum += raw.back();
    }
    std::sort(raw.begin(), raw.end());
    const bool  on = !Mutated("faces_no_translate");
    SceneFace  &scene = g_scene_face[kit.member];

    if (on && scene.key == 0) {
        dc::render::PortDecodedTexture decoded;

        if (DecodePicture(cache.scene_plate.img, face.plate, decoded) && decoded.palette == sheet.palette) {
            scene.width = decoded.width;
            scene.height = decoded.height;
            scene.table = decoded.palette;
            scene.key = dc::render::PortCreateTexture(decoded, dc::render::PortTextureOwner::Other, &scene.palette);
        }
    }
    kit.scene_face = on && scene.key != 0 && LayFace(scene, plate);
    std::array<uint32_t, 256> now{};
    const bool                same = dc::render::PortPaletteEntries(plate_tex0.CBP, now.data()) && now == sheet.palette;

    std::fprintf(stderr, "coop-faces: p%d %s (member %d): its sheet %s has another colour table than its face: shown %s; copied raw: error mean %.1f (99%% %.0f); "
                         "the face's table the sheet's now %d\n",
                 NetplayLocalPlayer(), whose, kit.member, face.sheet, kit.scene_face ? "on the scene's own face" : "raw", raw.empty() ? 0.0 : sum / raw.size(),
                 raw.empty() ? 0.0f : raw[raw.size() * 99 / 100], same ? 1 : 0);
}

// Each draw of a kit that lays the scene's face: laid again when the face's table is not the scene's (its picture read again).
void SceneFaceKept(const Kit &kit) {
    std::array<uint32_t, 256> now{};
    const CTexture           &plate = TexManager.textures[kit.plate_slot];
    const SceneFace          &scene = g_scene_face[kit.member];

    if (kit.scene_face && dc::render::PortPaletteEntries(reinterpret_cast<const sceGsTex0 *>(&plate.tex0)->CBP, now.data()) && now != scene.table) {
        LayFace(scene, &plate);
    }
}

// The body's face kit, built in its block: which member's plate the block holds, the member's sheet beside it (loaded there when
// the block has none; a fisher's looks' own sheet too), the records, the blink on.
void Build(Kit &kit, bool fisher, const char *whose) {
    const int in_use = SlotsInUse();

    kit.member = -1;
    kit.count = 0;
    kit.look = -1;
    kit.looks = false;
    for (int m = 0; m < kMembers && kit.member < 0; m++) {
        if (TexManager.SearchTextureName(const_cast<char *>(kFaces[m].plate), kit.block) >= 0) {
            kit.member = m;
        }
    }
    if (kit.member < 0 || !g_cache[kit.member].ok) {
        kit.member = -1;
        kit.slots_seen = in_use;
        return;
    }
    const MemberFace  &face = kFaces[kit.member];
    const MemberCache &cache = g_cache[kit.member];
    const bool         looks = fisher && face.look_sheet != nullptr && cache.look_sheet.img != nullptr;

    if (!SheetIn(face.sheet, cache.sheet, kit.block, in_use, whose, kit.member) ||
        (looks && !SheetIn(face.look_sheet, cache.look_sheet, kit.block, SlotsInUse(), whose, kit.member))) {
        kit.member = -1;
        kit.slots_seen = SlotsInUse();
        return;
    }
    kit.plate_slot = FindSlot(face.plate, kit.block);
    kit.sheet_slot = FindSlot(face.sheet, kit.block);
    kit.look_slot = looks ? FindSlot(face.look_sheet, kit.block) : -1;
    if (kit.plate_slot < 0 || kit.sheet_slot < 0 || (looks && kit.look_slot < 0)) {
        kit.member = -1;
        kit.slots_seen = SlotsInUse();
        return;
    }
    const CTexture *plate = &TexManager.textures[kit.plate_slot];
    const CTexture *sheet = &TexManager.textures[kit.sheet_slot];

    kit.plate_tex0 = plate->tex0;
    kit.sheet_tex0 = sheet->tex0;
    kit.anime.Initialize(kit.records, kRecords);
    for (CTexAnimeData &record : kit.records) {
        record.Initialize();
    }
    kit.count = ReadRecords(kit, face.sheet, face.plate, sheet, plate, cache.cfg, 0);
    if (looks) {
        const CTexture *look = &TexManager.textures[kit.look_slot];

        kit.look_tex0 = look->tex0;
        kit.looks = true;
        kit.count += ReadRecords(kit, face.look_sheet, face.plate, look, plate, cache.look_cfg, face.look_base);
    }
    // (a held look of strips: every strip plays on into the next, the last holds)
    for (int i = 0; i < face.strip_count; i++) {
        const Strip &s = face.strips[i];
        const bool   last = i + 1 == face.strip_count || face.strips[i + 1].group != s.group;

        Enter(kit, sheet, plate, s.group, s.sx, s.sy, s.w, s.h, s.dx, s.dy, last ? -1 : 0, kit.count);
    }
    if (kit.member != 0) { // coop-faces2 (Toan's sheet is his own pack's: one table with his face)
        SceneFaceFor(kit, cache, face, plate, whose);
    }
    kit.anime.DisableAll();
    kit.anime.Enable(face.blink);
    kit.look = face.blink;
    std::fprintf(stderr, "coop-faces: p%d %s (member %d) face kit built in block %d: %d records%s, plate slot %d (block %d), sheet slot %d (block %d), %d same-named "
                         "plates elsewhere, blink group %d\n",
                 NetplayLocalPlayer(), whose, kit.member, kit.block, kit.count, kit.looks ? " (with the fishing looks' sheet)" : "", kit.plate_slot,
                 TexManager.textures[kit.plate_slot].block, kit.sheet_slot, TexManager.textures[kit.sheet_slot].block, OtherPlates(face.plate, kit.block), face.blink);
}

// The group's records once, in order (the default eyes and mouth), so a look that covers only the eyes or the mouth does not keep
// the last look's other half.
void Stamp(const Kit &kit, int group) {
    int n = 0;

    for (const CTexAnimeData *record = kit.anime.first[group]; record != nullptr && n < kRecords; record = record->next, n++) {
        dc::render::PortMoveImage(*reinterpret_cast<const sceGsTex0 *>(&record->first_texture.tex0), record->source_x, record->source_y, record->source_width,
                                  record->source_height, *reinterpret_cast<const sceGsTex0 *>(&record->second_texture.tex0), record->dest_x, record->dest_y);
    }
}

int RecordAt(const Kit &kit, int group) {
    int n = 0;

    for (const CTexAnimeData *record = kit.anime.first[group]; record != nullptr && n < kRecords; record = record->next, n++) {
        if (record == kit.anime.current[group]) {
            return n;
        }
    }
    return -1;
}

int RecordsIn(const Kit &kit, int group) {
    int n = 0;

    for (const CTexAnimeData *record = kit.anime.first[group]; record != nullptr && n < kRecords; record = record->next) {
        n++;
    }
    return n;
}

// The player (0 up) whose fishing model body is, -1 for none: the fishing model is the body the fishing loads its pack into
// (_LOAD_MAIN_CHARA c01d_turi: the game's player's own, or that player's town body in an own event), a picture of that name in its block.
int FisherOf(const CCharacter *body, int block) {
    if (TexManager.SearchTextureName(const_cast<char *>("c01d_turi"), block) < 0) {
        return -1;
    }
    for (int k = 0; k < kMaxPlayers; k++) {
        if (CoopTownPlayerBody(k, nullptr, nullptr) == body && CoopFishingStateOf(k) >= 0) {
            return k;
        }
    }
    return -1;
}

// DC_COOP_TEST_FACES_FISH=<state>:<draws>[,...] (tests): from a fisher's first fishing draw, their fishing state read as each state
// in turn for that many draws (a bite needs a fish by the float, which no test can call up), then their own again.
int TestState(int real, long long draws) {
    static const char *list = TestEnv("DC_COOP_TEST_FACES_FISH");
    long long          from = 0;

    for (const char *at = list; at != nullptr && *at != 0;) {
        int state = 0, n = 0;

        if (std::sscanf(at, "%d:%d", &state, &n) != 2) {
            break;
        }
        if (draws < from + n) {
            return state;
        }
        from += n;
        at = std::strchr(at, ',');
        at = at != nullptr ? at + 1 : nullptr;
    }
    return real;
}

} // namespace

bool FacesOn() {
    // (DC_COOP_MUTATE=faces_always: the twin of the off test, this copy's game.coop_faces not read)
    return (dc::platform::ConfigGet().coop_faces || Mutated("faces_always")) && !Mutated("faces_off");
}

int FacesFishLook(int state, long long draws) {
    switch (state) {
    case ED_FISHING_BITE_CHECK: // a fish at the bait (the game's own "now!" help)
    case ED_FISHING_HOOKED:
        return 3; // surprised
    case ED_FISHING_BATTLE:
        return (draws / 12) % 2 == 0 ? 4 : 5; // gritted teeth, the two in turn (a fifth of a second each)
    case ED_FISHING_LANDED:
    case ED_FISHING_RESULT:
        return 7; // surprised 2
    case ED_FISHING_SINK:
        return 2; // side look (the bait lost)
    default:
        return 0; // standing, casting, waiting, reeling in: the blink
    }
}

int FacesLookFor(int member, int toan_look) {
    if (member < 0 || member >= kMembers) {
        return -1;
    }
    const int look = toan_look >= 0 && toan_look < kToanLooks ? kFaces[member].looks[toan_look] : -1;

    return look >= 0 ? look : kFaces[member].blink;
}

void FacesRebuildForTest() {
    for (int i = 0; g_kits != nullptr && i < kKits; i++) {
        g_kits[i].member = -1;
        g_kits[i].slots_seen = -1;
    }
}

void FacesPrepare() {
    if (g_prepared || !FacesOn()) {
        return;
    }
    g_prepared = true;

    constexpr int kPackBytes = 4 * 1024 * 1024; // (the biggest of the packs is under 2 MB)
    static u_int *pack = reinterpret_cast<u_int *>(new (std::align_val_t{64}) u_char[kPackBytes]);

    const auto load = [&](const char *path) {
        int size = 0;

        if (LoadFile(const_cast<char *>(path), pack, &size) == 0 || size <= 0 || size > kPackBytes) {
            std::fprintf(stderr, "coop-faces: cannot load %s\n", path);
            return false;
        }
        return true;
    };
    const auto cfg_of = [&](const char *name, std::string &into) {
        int   size = 0;
        char *cfg = reinterpret_cast<char *>(GetPackFile(pack, const_cast<char *>(name), &size));

        if (cfg != nullptr && size > 0) {
            into.assign(cfg, static_cast<size_t>(size));
        }
        return !into.empty();
    };
    // (copied as chararead's IMG command copies a pack's picture: its disc identity with it, so the texture loads from it)
    const auto sheet_of = [&](const char *name, Sheet &into) {
        const std::string img_name = std::string(name) + ".img";
        int               size = 0;
        u_int            *img = GetPackFile(pack, const_cast<char *>(img_name.c_str()), &size);

        if (img == nullptr || size <= 0) {
            return false;
        }
        dc::data::DataReadSource source;
        const bool               has_source = dc::data::DataReadSourceRange(img, static_cast<std::size_t>(size), source);

        into.img = new (std::align_val_t{64}) u_char[static_cast<size_t>(size) + 64];
        into.img_size = size;
        std::memcpy(into.img, img, static_cast<size_t>(size));
        if (has_source) {
            dc::data::DataReadRegisterCopiedSource(into.img, static_cast<std::size_t>(size), into.img, source);
        }
        return true;
    };
    for (int m = 0; m < kMembers; m++) {
        const MemberFace &face = kFaces[m];
        MemberCache      &cache = g_cache[m];

        // (Toan's blink sheet is in his body's own pictures)
        if (!load(face.pack) || !cfg_of(face.cfg, cache.cfg) || (m != 0 && !sheet_of(face.sheet, cache.sheet))) {
            std::fprintf(stderr, "coop-faces: %s gave no face (%s, %s): member %d keeps a still face\n", face.pack, face.cfg, face.sheet, m);
            continue;
        }
        cache.ok = true;
        if (m != 0) {
            sheet_of(face.plate, cache.scene_plate); // coop-faces2 (laid only when its table is needed)
        }
        if (face.look_sheet != nullptr && !(load(face.look_pack) && sheet_of(face.look_sheet, cache.look_sheet) && load(face.look_cfg_pack) &&
                                            cfg_of(face.look_cfg, cache.look_cfg))) {
            std::fprintf(stderr, "coop-faces: member %d's fishing looks did not load (%s, %s): blink only\n", m, face.look_pack, face.look_cfg);
            cache.look_sheet = Sheet{};
        }
    }
    std::fprintf(stderr, "coop-faces: p%d face sheets ready: Toan %d (fishing looks %d), Xiao %d, Goro %d, Ruby %d, Ungaga %d\n", NetplayLocalPlayer(),
                 g_cache[0].ok ? 1 : 0, g_cache[0].look_sheet.img != nullptr ? 1 : 0, g_cache[1].ok ? 1 : 0, g_cache[2].ok ? 1 : 0, g_cache[3].ok ? 1 : 0,
                 g_cache[4].ok ? 1 : 0);
}

void FacesDraw(CCharacter *body, int block, int fisher, const char *whose) {
    if (body == nullptr || !FacesOn()) {
        return;
    }
    FacesPrepare();
    // The body's own face animation first: a group of its own on (Toan's town model's blink, a scene's or a talk's look on a body)
    // plays as retail plays it, and its kit waits.
    unsigned own = 0;

    for (int g = 0; g < CTextureAnime::kGroupCount; g++) {
        own |= body->tex_anime.enabled[g] != 0 ? 1u << g : 0u;
    }
    if (own != 0) {
        if (static long long said = 0; Test() && ++said % 30 == 1) {
            std::fprintf(stderr, "coop-faces: test: p%d %s keeps its own face animation (groups %#x) at link tick %lld\n", NetplayLocalPlayer(), whose, own,
                         (long long) NetplayLinkTick());
        }
        return;
    }
    if (g_kits == nullptr) {
        g_kits = new Kit[kKits];
    }
    if (fisher < 0) {
        fisher = FisherOf(body, block);
    }
    // (DC_COOP_MUTATE=faces_fish_off: the fishing state not read, the twin of the looks' tests)
    const int real = fisher >= 0 && !Mutated("faces_fish_off") ? CoopFishingStateOf(fisher) : -1;

    // The body's kit (by body and block), else the least lately used one.
    Kit *kit = nullptr;

    for (int i = 0; i < kKits && kit == nullptr; i++) {
        if (g_kits[i].body == body && g_kits[i].block == block) {
            kit = &g_kits[i];
        }
    }
    if (kit == nullptr) {
        kit = &g_kits[0];
        for (int i = 1; i < kKits; i++) {
            if (g_kits[i].used < kit->used) {
                kit = &g_kits[i];
            }
        }
        kit->body = body;
        kit->block = block;
        kit->member = -1;
        kit->slots_seen = -1;
    }
    kit->used = ++g_use;

    // (first draw, or its block was built again: a new floor, a member swap, a state set's texture rebuild, a fishing model loaded)
    const bool wants_looks = real >= 0 && kit->member >= 0 && kFaces[kit->member].look_sheet != nullptr && !kit->looks;

    if (kit->member >= 0 ? !StillThere(*kit) || wants_looks : kit->slots_seen != SlotsInUse()) {
        Build(*kit, real >= 0, whose);
        kit->draws = 0;
    }
    if (kit->member < 0) {
        return;
    }

    // A fisher's look, from their fishing state, as this member's.
    const MemberFace &face = kFaces[kit->member];
    const int         state = real >= 0 ? TestState(real, kit->draws) : -1;
    const int         toan_look = state >= 0 ? FacesFishLook(state, kit->draws) : 0;
    const int         look = FacesLookFor(kit->member, toan_look);

    kit->toan_look = toan_look;
    kit->state = state;
    if (look != kit->look && kit->anime.first[look] != nullptr) {
        if (Test()) {
            std::fprintf(stderr, "coop-faces: test: p%d %s member %d shows look %d (was %d) for Toan's fishing look %d (fishing state %d) at link tick %lld\n",
                         NetplayLocalPlayer(), whose, kit->member, look, kit->look, toan_look, state, (long long) NetplayLinkTick());
        }
        kit->anime.DisableAll();
        Stamp(*kit, kit->looks || face.stamp == 0 ? face.stamp : 0);
        kit->anime.Enable(look);
        kit->look = look;
    }
    SceneFaceKept(*kit); // coop-faces2
    kit->anime.TexAnime(block);
    kit->draws++;

    if (Test() && kit->draws % 30 == 1) {
        int       other_slot = -1;
        const int others = OtherPlates(face.plate, block, &other_slot);

        std::fprintf(stderr, "coop-faces: test: p%d %s member %d block %d: group %d record %d of %d, sheet slot %d block %d, plate slot %d block %d, same-named plates "
                             "elsewhere %d (highest slot %d), look %d from Toan's %d (fishing state %d, looks' sheet slot %d block %d), draw %lld at link tick %lld\n",
                     NetplayLocalPlayer(), whose, kit->member, block, kit->look, RecordAt(*kit, kit->look), RecordsIn(*kit, kit->look), kit->sheet_slot,
                     TexManager.textures[kit->sheet_slot].block, kit->plate_slot, TexManager.textures[kit->plate_slot].block, others, other_slot, kit->look,
                     toan_look, state, kit->look_slot, kit->looks ? TexManager.textures[kit->look_slot].block : -1, kit->draws, (long long) NetplayLinkTick());
    }
}

} // namespace coop
