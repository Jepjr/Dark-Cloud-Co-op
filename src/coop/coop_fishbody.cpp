// coop-xiaofish (Jep 11-16): each player fishes as their own character. See coop_fishbody.hpp.
#include "coop/coop_fishbody.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include <libvu0.h>

#include "character.hpp"
#include "cloth.hpp"
#include "dataread.hpp"
#include "frame.hpp"
#include "framevu1.hpp"
#include "gameutil.hpp"

#include "coop/coop.hpp"
#include "coop/coop_actor.hpp" // kMaxPlayers
#include "coop/coop_core.hpp"

using namespace coop;

namespace dc::game {
extern const std::array<const char *, 6> charaNameTbl; // btitem.cpp: each member's .chr pack
} // namespace dc::game

namespace coop::fishbody {
namespace {

// A role is one bone of the body as both models have it. bone: the frame's name in that model. tip and (x, y, z): for an arm or leg
// bone, the joint it points at and the direction it points along in the shared reference pose (arms level, legs down), so models that
// rest differently (Toan and Xiao arms out, Goro, Ruby and Ungaga arms down) share one reference. (0, 0, 0): the rest pose as it is.
// The model's space: y up, its face +z, its right -x. debug\tools\anim\fishbody.py reads these tables from this file.
struct Role {
    const char *role;
    const char *bone;
    const char *tip;
    int         x, y, z;
};

// (the tables' rows, by number)
enum : int {
    kCentre = 0,
    kLeftArm = 7,
    kLeftFore = 8,
    kLeftHand = 9,
    kRightArm = 11,
    kRightFore = 12,
    kRightHand = 13,
    kRightThigh = 14,
    kRightShin = 15,
    kRightFoot = 16,
    kRightToe = 17,
    kLeftThigh = 18,
    kLeftShin = 19,
    kLeftFoot = 20,
    kLeftToe = 21,
    kRoles = 22
};

// Toan's fishing model, chara/c01d_turi.chr (his dungeon body's skeleton with the rod, float and hook beside it)
// fishbody-map toan
const Role kToan[kRoles] = {
    {"centre", "ctr", "", 0, 0, 0},
    {"root", "c01d_1_2_1_1", "", 0, 0, 0},
    {"hips", "jnt1_1", "", 0, 0, 0},
    {"spine", "jnt2_1", "", 0, 0, 0},
    {"neck", "jnt11_1", "", 0, 0, 0},
    {"head", "jnt12_1", "", 0, 0, 0},
    {"lcollar", "jnt8_1", "", 0, 0, 0},
    {"larm", "jnt9_1", "jnt9_2", 1, 0, 0},
    {"lfore", "jnt9_2", "eff9", 1, 0, 0},
    {"lhand", "jnt10_1", "eff10", 1, 0, 0},
    {"rcollar", "jnt16_1", "", 0, 0, 0},
    {"rarm", "jnt17_1", "jnt17_2", -1, 0, 0},
    {"rfore", "jnt17_2", "eff17", -1, 0, 0},
    {"rhand", "jnt18_1", "eff18", -1, 0, 0},
    {"rthigh", "jnt22_1", "jnt22_2", 0, -1, 0},
    {"rshin", "jnt22_2", "eff22", 0, -1, 0},
    {"rfoot", "jnt23_1", "", 0, 0, 0},
    {"rtoe", "jnt24_1", "", 0, 0, 0},
    {"lthigh", "jnt5_1", "jnt5_2", 0, -1, 0},
    {"lshin", "jnt5_2", "eff5", 0, -1, 0},
    {"lfoot", "jnt6_1", "", 0, 0, 0},
    {"ltoe", "jnt7_1", "", 0, 0, 0},
};
// fishbody-map end

// Xiao, dun/mainchara/c04b.chr. Her right hand holds the rod at its item point, in her palm (her weapon point is her left hand's).
// fishbody-grip xiao item
// fishbody-map xiao
const Role kXiao[kRoles] = {
    {"centre", "c_ctr", "", 0, 0, 0},
    {"root", "g_ctr", "", 0, 0, 0},
    {"hips", "jnt1_1", "", 0, 0, 0},
    {"spine", "jnt2_1", "", 0, 0, 0},
    {"neck", "jnt11_1", "", 0, 0, 0},
    {"head", "jnt12_1", "", 0, 0, 0},
    {"lcollar", "jnt6_1", "", 0, 0, 0},
    {"larm", "jnt10_1", "jnt10_2", 1, 0, 0},
    {"lfore", "jnt10_2", "eff10", 1, 0, 0},
    {"lhand", "jnt9_1", "eff9", 1, 0, 0},
    {"rcollar", "jnt19_1", "", 0, 0, 0},
    {"rarm", "jnt20_1", "jnt20_2", -1, 0, 0},
    {"rfore", "jnt20_2", "eff20", -1, 0, 0},
    {"rhand", "jnt21_1", "eff21", -1, 0, 0},
    {"rthigh", "jnt25_1", "jnt25_2", 0, -1, 0},
    {"rshin", "jnt25_2", "eff25", 0, -1, 0},
    {"rfoot", "jnt26_1", "", 0, 0, 0},
    {"rtoe", "jnt27_1", "", 0, 0, 0},
    {"lthigh", "jnt3_1", "jnt3_2", 0, -1, 0},
    {"lshin", "jnt3_2", "eff3", 0, -1, 0},
    {"lfoot", "jnt4_1", "", 0, 0, 0},
    {"ltoe", "jnt5_1", "", 0, 0, 0},
};
// fishbody-map end

// Goro, dun/mainchara/c06b.chr. He rests arms down, his hammer in his left hand (its weapon point); his right hand's item point
// is in its palm. One spine bone less than Toan (hips and spine are his two), his legs hang from his centre.
// fishbody-grip goro item
// fishbody-map goro
const Role kGoro[kRoles] = {
    {"centre", "null69", "", 0, 0, 0},
    {"root", "null71_1", "", 0, 0, 0},
    {"hips", "jnt3_1", "", 0, 0, 0},
    {"spine", "jnt4_1", "", 0, 0, 0},
    {"neck", "jnt5_1", "", 0, 0, 0},
    {"head", "jnt6_1", "", 0, 0, 0},
    {"lcollar", "jnt8_1", "", 0, 0, 0},
    {"larm", "jnt9_1", "jnt9_2", 1, 0, 0},
    {"lfore", "jnt9_2", "eff9", 1, 0, 0},
    {"lhand", "jnt10_1", "eff10", 1, 0, 0},
    {"rcollar", "jnt17_1", "", 0, 0, 0},
    {"rarm", "jnt18_1", "jnt18_2", -1, 0, 0},
    {"rfore", "jnt18_2", "eff18", -1, 0, 0},
    {"rhand", "jnt19_1", "eff19", -1, 0, 0},
    {"rthigh", "jnt22_1", "jnt22_2", 0, -1, 0},
    {"rshin", "jnt22_2", "eff22", 0, -1, 0},
    {"rfoot", "jnt28_1", "", 0, 0, 0},
    {"rtoe", "jnt31_1", "", 0, 0, 0},
    {"lthigh", "jnt11_1", "jnt11_2", 0, -1, 0},
    {"lshin", "jnt11_2", "eff11", 0, -1, 0},
    {"lfoot", "jnt24_1", "", 0, 0, 0},
    {"ltoe", "jnt33_1", "", 0, 0, 0},
};
// fishbody-map end

// Ruby, dun/mainchara/c05a.chr. Her bones are named, not numbered, and her hands have no end joint: a hand's direction is its
// forearm's ("-<frame>": from that frame to this one). Her right hand has no holding point (her weapon point hangs on her
// forearm, at the wrist): her left hand's item point, mirrored ("~item"). Her centre is her hips.
// fishbody-grip ruby ~item
// fishbody-map ruby
const Role kRuby[kRoles] = {
    {"centre", "hip", "", 0, 0, 0},
    {"root", "ruby_3_2_4", "", 0, 0, 0},
    {"hips", "hip", "", 0, 0, 0},
    {"spine", "spine", "", 0, 0, 0},
    {"neck", "neck", "", 0, 0, 0},
    {"head", "face", "", 0, 0, 0},
    {"lcollar", "l_sdr", "", 0, 0, 0},
    {"larm", "l_arm", "l_farm", 1, 0, 0},
    {"lfore", "l_farm", "l_hand", 1, 0, 0},
    {"lhand", "l_hand", "-l_farm", 1, 0, 0},
    {"rcollar", "r_sdr", "", 0, 0, 0},
    {"rarm", "r_arm", "r_farm", -1, 0, 0},
    {"rfore", "r_farm", "r_hand", -1, 0, 0},
    {"rhand", "r_hand", "-r_farm", -1, 0, 0},
    {"rthigh", "r_upleg", "r_leg", 0, -1, 0},
    {"rshin", "r_leg", "r_foot", 0, -1, 0},
    {"rfoot", "r_foot", "", 0, 0, 0},
    {"rtoe", "r_toe", "", 0, 0, 0},
    {"lthigh", "l_upleg", "l_leg", 0, -1, 0},
    {"lshin", "l_leg", "l_foot", 0, -1, 0},
    {"lfoot", "l_foot", "", 0, 0, 0},
    {"ltoe", "l_toe", "", 0, 0, 0},
};
// fishbody-map end

// Ungaga, dun/mainchara/c10b.chr. His staff's weapon point is in his right palm.
// fishbody-grip ungaga weapon
// fishbody-map ungaga
const Role kUngaga[kRoles] = {
    {"centre", "null84", "", 0, 0, 0},
    {"root", "c10a_2", "", 0, 0, 0},
    {"hips", "jnt1_1", "", 0, 0, 0},
    {"spine", "jnt27_1", "", 0, 0, 0},
    {"neck", "jnt3_1", "", 0, 0, 0},
    {"head", "jnt4_1", "", 0, 0, 0},
    {"lcollar", "jnt16_1", "", 0, 0, 0},
    {"larm", "jnt32_1", "jnt32_2", 1, 0, 0},
    {"lfore", "jnt32_2", "eff32", 1, 0, 0},
    {"lhand", "jnt29_1", "eff29", 1, 0, 0},
    {"rcollar", "jnt38_1", "", 0, 0, 0},
    {"rarm", "jnt39_1", "jnt39_2", -1, 0, 0},
    {"rfore", "jnt39_2", "eff39", -1, 0, 0},
    {"rhand", "jnt40_1", "eff40", -1, 0, 0},
    {"rthigh", "jnt22_1", "jnt22_2", 0, -1, 0},
    {"rshin", "jnt22_2", "eff22", 0, -1, 0},
    {"rfoot", "jnt24_1", "", 0, 0, 0},
    {"rtoe", "jnt26_1", "", 0, 0, 0},
    {"lthigh", "jnt12_1", "jnt12_2", 0, -1, 0},
    {"lshin", "jnt12_2", "eff12", 0, -1, 0},
    {"lfoot", "jnt13_1", "", 0, 0, 0},
    {"ltoe", "jnt14_1", "", 0, 0, 0},
};
// fishbody-map end

// Osmond, dun/mainchara/c18a.chr. His gun's weapon point is in his right palm. His head is one bone (neck2); the neck role is
// the chain's start under it (chn3, where neck2 sits). His feet rest below his root: he hovers.
// fishbody-grip osmond weapon
// fishbody-hover osmond
// fishbody-map osmond
const Role kOsmond[kRoles] = {
    {"centre", "null11", "", 0, 0, 0},
    {"root", "null13", "", 0, 0, 0},
    {"hips", "hip", "", 0, 0, 0},
    {"spine", "spine", "", 0, 0, 0},
    {"neck", "chn3", "", 0, 0, 0},
    {"head", "neck2", "", 0, 0, 0},
    {"lcollar", "l_srd", "", 0, 0, 0},
    {"larm", "l_arm", "l_farm", 1, 0, 0},
    {"lfore", "l_farm", "eff5", 1, 0, 0},
    {"lhand", "l_hand", "eff6", 1, 0, 0},
    {"rcollar", "r_srd", "", 0, 0, 0},
    {"rarm", "r_arm", "r_farm", -1, 0, 0},
    {"rfore", "r_farm", "eff16", -1, 0, 0},
    {"rhand", "r_hand", "eff17", -1, 0, 0},
    {"rthigh", "r_upleg", "r_leg", 0, -1, 0},
    {"rshin", "r_leg", "eff21", 0, -1, 0},
    {"rfoot", "r_foot", "", 0, 0, 0},
    {"rtoe", "r_toe", "", 0, 0, 0},
    {"lthigh", "l_upleg", "l_leg", 0, -1, 0},
    {"lshin", "l_leg", "eff8", 0, -1, 0},
    {"lfoot", "l_foot", "", 0, 0, 0},
    {"ltoe", "l_toe", "", 0, 0, 0},
};
// fishbody-map end

const Role *MapOf(int member) {
    static const Role *const maps[6] = {nullptr, kXiao, kGoro, kRuby, kUngaga, kOsmond};

    return member >= 0 && member < 6 ? maps[member] : nullptr;
}

// The members who hover (as the fishbody-hover lines above): no foot planted, the centre as high as their own idle holds it.
bool HoversOf(int member) {
    return member == 5; // (Osmond)
}

// The frame in the member's right hand that holds the rod (as the fishbody-grip lines above): "~<frame>" is the left hand's
// frame mirrored onto the right hand.
const char *GripOf(int member) {
    static const char *const grips[6] = {nullptr, "item", "item", "~item", "weapon", "weapon"};

    return member >= 0 && member < 6 ? grips[member] : nullptr;
}

// ---- small sums: rotations as m[row][column] (column c the bone's axis c), no trigonometry anywhere (every copy must get the
// same bits: the rod's step goes into the fishing line's simulation)
struct M3 {
    float m[3][3];
};
struct V3 {
    float v[3];
};

M3 Mul(const M3 &a, const M3 &b) {
    M3 out;

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            out.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
        }
    }
    return out;
}

M3 Transpose(const M3 &a) {
    M3 out;

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            out.m[i][j] = a.m[j][i];
        }
    }
    return out;
}

// a with its axes' lengths taken out: a pure turn (a frame's matrix may carry a scale, as Ungaga's forearms' ends do).
M3 Ortho(const M3 &a) {
    M3 out = a;

    for (int c = 0; c < 3; c++) {
        const float n = std::sqrt(a.m[0][c] * a.m[0][c] + a.m[1][c] * a.m[1][c] + a.m[2][c] * a.m[2][c]);

        for (int r = 0; n > 0.0f && r < 3; r++) {
            out.m[r][c] = a.m[r][c] / n;
        }
    }
    return out;
}

// The inverse (a frame's matrix may carry a scale: its transpose is not its inverse).
M3 Inverse(const M3 &a) {
    const float(&m)[3][3] = a.m;
    const float det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                      m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    const float k = det != 0.0f ? 1.0f / det : 0.0f;

    return {{{(m[1][1] * m[2][2] - m[1][2] * m[2][1]) * k, (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * k, (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * k},
             {(m[1][2] * m[2][0] - m[1][0] * m[2][2]) * k, (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * k, (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * k},
             {(m[1][0] * m[2][1] - m[1][1] * m[2][0]) * k, (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * k, (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * k}}};
}

V3 Apply(const M3 &a, const V3 &v) {
    return {{a.m[0][0] * v.v[0] + a.m[0][1] * v.v[1] + a.m[0][2] * v.v[2], a.m[1][0] * v.v[0] + a.m[1][1] * v.v[1] + a.m[1][2] * v.v[2],
             a.m[2][0] * v.v[0] + a.m[2][1] * v.v[1] + a.m[2][2] * v.v[2]}};
}

V3 Add(const V3 &a, const V3 &b) {
    return {{a.v[0] + b.v[0], a.v[1] + b.v[1], a.v[2] + b.v[2]}};
}

V3 Sub(const V3 &a, const V3 &b) {
    return {{a.v[0] - b.v[0], a.v[1] - b.v[1], a.v[2] - b.v[2]}};
}

V3 Scale(const V3 &a, float k) {
    return {{a.v[0] * k, a.v[1] * k, a.v[2] * k}};
}

float Dot(const V3 &a, const V3 &b) {
    return a.v[0] * b.v[0] + a.v[1] * b.v[1] + a.v[2] * b.v[2];
}

float Length(const V3 &a) {
    return std::sqrt(Dot(a, a));
}

V3 Unit(const V3 &a) {
    const float n = Length(a);

    return n > 0.0f ? Scale(a, 1.0f / n) : a;
}

// The shortest turn taking direction a onto direction b (a quaternion from the cross and dot, made a matrix).
M3 Between(const V3 &from, const V3 &to) {
    const V3    a = Unit(from), b = Unit(to);
    float       x = a.v[1] * b.v[2] - a.v[2] * b.v[1], y = a.v[2] * b.v[0] - a.v[0] * b.v[2], z = a.v[0] * b.v[1] - a.v[1] * b.v[0];
    float       w = 1.0f + Dot(a, b);
    const float n = std::sqrt(x * x + y * y + z * z + w * w);

    if (n > 0.0f) {
        x /= n, y /= n, z /= n, w /= n;
    } else {
        w = 1.0f;
    }
    return {{{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
             {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
             {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)}}};
}

// The angle between two directions, in degrees (the numbers' only: never in the pose).
float Angle(const V3 &a, const V3 &b) {
    return std::acos(std::clamp(Dot(Unit(a), Unit(b)), -1.0f, 1.0f)) * 57.29578f;
}

float OffLine(const V3 &point, const V3 &origin, const V3 &axis) {
    const V3    v = Sub(point, origin);
    const float along = Dot(v, axis);

    return std::sqrt(std::max(0.0f, Dot(v, v) - along * along));
}

// ---- a model as its pack has it: the frames' names, parents and rest places
struct Bone {
    char name[32];
    int  parent;
    bool mesh;
    M3   rot; // rest, local to the parent
    V3   pos;
};

struct Rig {
    bool              read = false; // tried
    bool              ok = false;
    std::vector<Bone> bones;
    std::vector<M3>   rest_rot; // rest, in the model's space
    std::vector<V3>   rest_pos;
    int               of[kRoles] = {};
    M3                ref[kRoles] = {};
    float             leg = 1.0f, arm = 1.0f;
    int               grip = -1, rod = -1, shown[3] = {-1, -1, -1}; // Toan's fishing model: the hand's weapon point, the rod, the three props
    int               tip = -1;                                     // and the rod's tip
    M3                rod_in_grip = {};                             // the rod's turn in the weapon point's frame (it is rigid there)
    float             hold_along = 0.0f;                            // from the rod's origin up its line to where Toan's palm holds it
    V3                tip_pos = {};                                 // the tip in the rod's own frame
    V3                hold_off = {};                                // a member's model: the rod's holding point in its right hand's frame
    bool              hovers = false;                               // and whether it hovers (no foot planted, its centre lifted)
    float             lift = 0.0f;                                  // (how high: its own idle's centre over its rest)
};

int Find(const Rig &rig, const char *name) {
    for (size_t i = 0; i < rig.bones.size(); i++) {
        if (std::strcmp(rig.bones[i].name, name) == 0) {
            return (int) i;
        }
    }
    return -1;
}

int LittleInt(const u_char *at) {
    int value;

    std::memcpy(&value, at, 4);
    return value;
}

// A pack's member by name: each has an 80-byte header (its name, the header's size, the member's size, the step to the next).
const u_char *Member(const u_char *pack, int size, const char *name, int *member_size) {
    for (int at = 0; at + 76 <= size;) {
        const char *here = reinterpret_cast<const char *>(pack + at);
        const int   head = LittleInt(pack + at + 64), bytes = LittleInt(pack + at + 68), step = LittleInt(pack + at + 72);

        if (here[0] == 0) {
            break;
        }
        if (name == nullptr ? (std::strlen(here) > 4 && std::strcmp(here + std::strlen(here) - 4, ".cfg") == 0) : std::strcmp(here, name) == 0) {
            if (head < 0 || bytes < 0 || at + head + bytes > size) {
                return nullptr;
            }
            *member_size = bytes;
            return pack + at + head;
        }
        if (step < 76) {
            break;
        }
        at += step;
    }
    return nullptr;
}

// The file a configuration names for a command (its MODEL "name" line, not SHADOW_MODEL's; its MOTION line).
bool CfgName(const u_char *cfg, int size, const char *command, char *out, size_t out_size) {
    const char  *text = reinterpret_cast<const char *>(cfg);
    const size_t n = std::strlen(command);

    for (int at = 0; at < size;) {
        int end = at;

        while (end < size && text[end] != '\n') {
            end++;
        }
        if (end - at > (int) n + 1 && std::strncmp(text + at, command, n) == 0 && (text[at + n] == ' ' || text[at + n] == '\t')) {
            const char *open = static_cast<const char *>(std::memchr(text + at, '"', end - at));
            const char *close = open != nullptr ? static_cast<const char *>(std::memchr(open + 1, '"', text + end - open - 1)) : nullptr;

            if (close != nullptr && (size_t) (close - open - 1) < out_size) {
                std::memcpy(out, open + 1, close - open - 1);
                out[close - open - 1] = 0;
                return true;
            }
        }
        at = end + 1;
    }
    return false;
}

// The first frame of a configuration's first motion (its first "KEY start, end, speed" line), or -1.
int FirstMotionStart(const u_char *cfg, int size) {
    const char *text = reinterpret_cast<const char *>(cfg);

    for (int at = 0; at < size;) {
        int end = at;

        while (end < size && text[end] != '\n') {
            end++;
        }
        while (at < end && (text[at] == ' ' || text[at] == '\t')) {
            at++;
        }
        if (end - at > 4 && std::strncmp(text + at, "KEY", 3) == 0 && (text[at + 3] == ' ' || text[at + 3] == '\t')) {
            int start = 0, digits = 0;

            for (at += 3; at < end && (text[at] == ' ' || text[at] == '\t'); at++) {
            }
            for (; at < end && text[at] >= '0' && text[at] <= '9'; at++, digits++) {
                start = start * 10 + (text[at] - '0');
            }
            if (digits > 0) {
                return start;
            }
        }
        at = end + 1;
    }
    return -1;
}

// A frame's place keyed by a motion file at a motion frame (its translation channel's keys, blended between the two either side
// as the game does), or false. The file: channels of a 32-byte header (frame, target, kind, a spare word, key count, more
// follow) and 32-byte keys (the motion frame at 0, four floats at 16); kind 2 is a translation.
bool KeyedPlace(const u_char *mot, int size, int frame, int at_frame, V3 &out) {
    for (int at = 0; at + 32 <= size;) {
        const int channel = LittleInt(mot + at), kind = LittleInt(mot + at + 8), count = LittleInt(mot + at + 16), more = LittleInt(mot + at + 20);
        const int keys = at + 32;

        if (count < 0 || keys + count * 32 > size) {
            return false;
        }
        if (channel == frame && kind == 2 && count > 0) {
            int before = 0;

            for (int k = 0; k < count && LittleInt(mot + keys + k * 32) <= at_frame; k++) {
                before = k;
            }
            float a[3], b[3];
            const int t0 = LittleInt(mot + keys + before * 32);

            std::memcpy(a, mot + keys + before * 32 + 16, sizeof a);
            std::memcpy(b, a, sizeof b);
            float t = 0.0f;

            if (before + 1 < count && t0 < at_frame) {
                const int t1 = LittleInt(mot + keys + (before + 1) * 32);

                std::memcpy(b, mot + keys + (before + 1) * 32 + 16, sizeof b);
                t = t1 > t0 ? (float) (at_frame - t0) / (float) (t1 - t0) : 0.0f;
            }
            out = {{a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t}};
            return true;
        }
        at = keys + count * 32;
        if (more == 0) {
            break;
        }
    }
    return false;
}

// Where a two-bone leg reaches for a point want away (most: its full length): as far, until the last kSoft of its length,
// which it eases into (t / (1 + t): no step in the knee's bend as the leg straightens, where a straight reach turns the knee
// fastest; Ruby's and Ungaga's knees snapped there). Never its full length. Sums and a division: every copy gets the same bits.
constexpr float kSoft = 0.025f;

float SoftReach(float want, float most) {
    const float start = most * (1.0f - kSoft), soft = most * kSoft - 0.01f;

    if (want <= start) {
        return want;
    }
    const float t = (want - start) / soft;

    return start + soft * t / (1.0f + t);
}

// The pack read through the game's own loader (so stripped data comes back as the disc's), into a buffer of this unit's own.
// Where a role's bone ends at rest: its tip frame; for a tip "-<frame>" (a bone with no end joint, as Ruby's hands), as far
// past the joint as that frame is before it (the bone points along its parent's line).
bool RestEnd(const Rig &rig, const Role *map, int role, V3 &end) {
    const char *tip = map[role].tip;
    const int   at = Find(rig, tip[0] == '-' ? tip + 1 : tip);

    if (at < 0) {
        return false;
    }
    const V3 joint = rig.rest_pos[rig.of[role]];

    end = tip[0] == '-' ? Add(joint, Sub(joint, rig.rest_pos[at])) : rig.rest_pos[at];
    return true;
}

// The rod's holding point in the right hand's own frame: the grip frame's rest place from the wrist; "~<frame>": the left hand's
// frame mirrored onto the right hand (Ruby: her right hand has none).
bool HoldOffset(const Rig &rig, const char *grip, V3 &out) {
    const int at = grip != nullptr ? Find(rig, grip[0] == '~' ? grip + 1 : grip) : -1;

    if (at < 0) {
        return false;
    }
    const int rhand = rig.of[kRightHand];
    V3        v = Sub(rig.rest_pos[at], rig.rest_pos[grip[0] == '~' ? rig.of[kLeftHand] : rhand]);

    if (grip[0] == '~') {
        v.v[0] = -v.v[0];
    }
    out = Apply(Inverse(rig.rest_rot[rhand]), v);
    return true;
}

bool ReadRig(Rig &rig, const char *path, const Role *map, bool hovers) {
    static u_char *buffer = nullptr; // (kept for the run, as the bodies' pack buffers are)

    rig.read = true;
    if (buffer == nullptr) {
        buffer = new (std::align_val_t{64}) u_char[kPackBytes];
    }
    int size = 0;

    if (LoadFile(const_cast<char *>(path), buffer, &size) == 0 || size <= 0 || size > kPackBytes) {
        std::fprintf(stderr, "coop-fishbody: cannot read %s\n", path);
        return false;
    }
    int           cfg_size = 0, mds_size = 0;
    char          model[64] = "";
    const u_char *cfg = Member(buffer, size, nullptr, &cfg_size);
    const u_char *mds = cfg != nullptr && CfgName(cfg, cfg_size, "MODEL", model, sizeof model) ? Member(buffer, size, model, &mds_size) : nullptr;

    if (mds == nullptr || mds_size < 16 || std::memcmp(mds, "MDS", 3) != 0) {
        std::fprintf(stderr, "coop-fishbody: %s: no model (%s)\n", path, model);
        return false;
    }
    const int count = LittleInt(mds + 8), first = LittleInt(mds + 12);

    if (count <= 0 || count > 512 || first < 16 || first + count * 112 > mds_size) {
        std::fprintf(stderr, "coop-fishbody: %s: %d frames do not fit its model\n", path, count);
        return false;
    }
    rig.bones.resize(count);
    rig.rest_rot.resize(count);
    rig.rest_pos.resize(count);
    for (int i = 0; i < count; i++) {
        const u_char *at = mds + first + i * 112;
        Bone         &bone = rig.bones[i];
        float         m[16];

        std::memcpy(bone.name, at + 8, 32);
        bone.name[31] = 0;
        bone.mesh = LittleInt(at + 40) != 0;
        bone.parent = LittleInt(at + 44);
        std::memcpy(m, at + 48, sizeof m);
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) {
                bone.rot.m[r][c] = m[4 * c + r];
            }
            bone.pos.v[r] = m[12 + r];
        }
        if (bone.parent >= i) {
            std::fprintf(stderr, "coop-fishbody: %s: frame %d's parent %d comes after it\n", path, i, bone.parent);
            return false;
        }
        if (bone.parent < 0) {
            rig.rest_rot[i] = bone.rot;
            rig.rest_pos[i] = bone.pos;
        } else {
            rig.rest_rot[i] = Mul(rig.rest_rot[bone.parent], bone.rot);
            rig.rest_pos[i] = Add(rig.rest_pos[bone.parent], Apply(rig.rest_rot[bone.parent], bone.pos));
        }
    }
    for (int role = 0; role < kRoles; role++) {
        const int i = Find(rig, map[role].bone);

        if (i < 0) {
            std::fprintf(stderr, "coop-fishbody: %s has no frame %s (%s)\n", path, map[role].bone, map[role].role);
            return false;
        }
        rig.of[role] = i;
        rig.ref[role] = Ortho(rig.rest_rot[i]); // (a pure turn: a frame above may carry a scale)
        if (map[role].x != 0 || map[role].y != 0 || map[role].z != 0) {
            V3 end;

            if (!RestEnd(rig, map, role, end)) {
                std::fprintf(stderr, "coop-fishbody: %s has no frame %s (%s's tip)\n", path, map[role].tip, map[role].role);
                return false;
            }
            const V3 canon = {{(float) map[role].x, (float) map[role].y, (float) map[role].z}};

            rig.ref[role] = Mul(Between(Sub(end, rig.rest_pos[i]), canon), rig.ref[role]);
        }
    }
    // A member who hovers (Osmond): his centre as high over its rest as his own idle holds it (motion 0's first frame).
    if (hovers) {
        char          motion[64] = "";
        int           mot_size = 0;
        const u_char *mot = CfgName(cfg, cfg_size, "MOTION", motion, sizeof motion) ? Member(buffer, size, motion, &mot_size) : nullptr;
        const int     start = FirstMotionStart(cfg, cfg_size);
        V3            keyed;

        if (mot == nullptr || start < 0 || !KeyedPlace(mot, mot_size, rig.of[kCentre], start, keyed)) {
            std::fprintf(stderr, "coop-fishbody: %s: no idle centre to hover at (motion %s, first key %d)\n", path, motion, start);
            return false;
        }
        rig.hovers = true;
        rig.lift = keyed.v[1] - rig.bones[rig.of[kCentre]].pos.v[1];
    }
    rig.leg = rig.rest_pos[rig.of[kRightThigh]].v[1] - rig.rest_pos[rig.of[kRightFoot]].v[1];
    rig.arm = Length(Sub(rig.rest_pos[rig.of[kRightFore]], rig.rest_pos[rig.of[kRightArm]])) +
              Length(Sub(rig.rest_pos[rig.of[kRightHand]], rig.rest_pos[rig.of[kRightFore]]));
    rig.ok = rig.leg > 0.0f && rig.arm > 0.0f;
    return rig.ok;
}

Rig g_toan;    // Toan's fishing model
Rig g_rigs[6]; // each member's own model

const Rig *Source() {
    if (!g_toan.read) {
        if (ReadRig(g_toan, "chara/c01d_turi.chr", kToan, false)) {
            g_toan.grip = Find(g_toan, "weapon");
            g_toan.rod = Find(g_toan, "turizao");
            g_toan.shown[0] = g_toan.rod;
            g_toan.shown[1] = Find(g_toan, "maru_uki");
            g_toan.shown[2] = Find(g_toan, "turibari");
            g_toan.tip = Find(g_toan, "sao");
            g_toan.ok = g_toan.grip >= 0 && g_toan.rod >= 0 && g_toan.shown[1] >= 0 && g_toan.shown[2] >= 0 && g_toan.tip >= 0;
            if (g_toan.ok) {
                // (at rest the rod lies on the hand's weapon point, its line through the palm: halfway from the wrist to the hand's end)
                const int end = Find(g_toan, kToan[kRightHand].tip);
                const V3  axis = Apply(g_toan.rest_rot[g_toan.rod], {{0.0f, 1.0f, 0.0f}});
                const V3  palm = Scale(Add(g_toan.rest_pos[g_toan.of[kRightHand]], g_toan.rest_pos[end]), 0.5f);

                g_toan.rod_in_grip = Mul(Transpose(g_toan.rest_rot[g_toan.grip]), g_toan.rest_rot[g_toan.rod]);
                g_toan.hold_along = Dot(Sub(palm, g_toan.rest_pos[g_toan.grip]), axis);
                g_toan.tip_pos = g_toan.bones[g_toan.tip].pos;
            }
        }
        std::fprintf(stderr, "coop-fishbody: Toan's fishing model read: %s, %d frames, leg %.2f, arm %.2f\n", g_toan.ok ? "ok" : "NOT as the map expects",
                     (int) g_toan.bones.size(), g_toan.leg, g_toan.arm);
    }
    return g_toan.ok ? &g_toan : nullptr;
}

const Rig *Target(int member) {
    const Role *map = member >= 0 && member < 6 ? MapOf(member) : nullptr;

    if (map == nullptr) {
        return nullptr;
    }
    Rig &rig = g_rigs[member];

    if (!rig.read) {
        if (ReadRig(rig, charaNameTbl[member], map, HoversOf(member))) {
            rig.ok = HoldOffset(rig, GripOf(member), rig.hold_off);
        }
        std::fprintf(stderr, "coop-fishbody: member %d's model %s read: %s, %d frames, leg %.2f, arm %.2f, hold (%.3f %.3f %.3f), lift %.2f\n", member,
                     charaNameTbl[member], rig.ok ? "ok" : "NOT as the map expects", (int) rig.bones.size(), rig.leg, rig.arm, rig.hold_off.v[0],
                     rig.hold_off.v[1], rig.hold_off.v[2], rig.lift);
    }
    return rig.ok ? &rig : nullptr;
}

// A model's frames as the game holds them: one array, in the model's order (MotionProc indexes it so). Null unless the model's
// frames carry the rig's names.
CFrameVu1 *Frames(CCharacter *chara, const Rig &rig) {
    if (chara == nullptr || chara->frame == nullptr) {
        return nullptr;
    }
    CFrameVu1 *frames = static_cast<CFrameVu1 *>(chara->frame);

    // (the first, a role bone and the last: a model of another make fails one of them)
    for (int i : {0, rig.of[kRightHand], (int) rig.bones.size() - 1}) {
        if (std::strncmp(frames[i].name, rig.bones[i].name, 31) != 0) {
            return nullptr;
        }
    }
    return frames;
}

// A frame's place in the world as (rotation with its axes' lengths taken out, position).
void World(CFrame &frame, M3 &rot, V3 &pos) {
    sceVu0FMATRIX m;

    frame.GetLWMatrix(m);
    for (int c = 0; c < 3; c++) {
        const float n = std::sqrt(m[c][0] * m[c][0] + m[c][1] * m[c][1] + m[c][2] * m[c][2]);

        for (int r = 0; r < 3; r++) {
            rot.m[r][c] = n > 0.0f ? m[c][r] / n : (r == c ? 1.0f : 0.0f);
        }
    }
    pos = {{m[3][0], m[3][1], m[3][2]}};
}

// The pose of a member's last Solve: every frame of its model, local to its parent, and in the model's space.
struct Solved {
    bool            on = false;
    std::vector<M3> rot, world_rot;
    std::vector<V3> pos, world_pos;
};
Solved g_solved[6];

} // namespace

bool Mapped(int member) {
    if (Mutated("fishbody_toan") || member < 0 || member >= 6 || MapOf(member) == nullptr) {
        return false;
    }
    return Source() != nullptr && Target(member) != nullptr;
}

const char *RightWrist(int member) {
    const Role *map = member >= 0 && member < 6 ? MapOf(member) : nullptr;

    return map != nullptr ? map[kRightHand].bone : nullptr;
}

const char *Grip(int member) {
    return member >= 0 && member < 6 ? GripOf(member) : nullptr;
}

float HoldAlong() {
    const Rig *toan = Source();

    return toan != nullptr ? toan->hold_along : 0.0f;
}

bool HoldInHand(int member, float *out) {
    const Rig *rig = Mapped(member) ? Target(member) : nullptr;

    if (rig == nullptr) {
        return false;
    }
    std::memcpy(out, rig->hold_off.v, sizeof rig->hold_off.v);
    return true;
}

namespace {
void WalkParts(CFrame *frame, std::string &names, int &count) {
    // (CFrameVu1::DrawVu1's own rules: draw bit 1 off with 2 on skips the frame and all below it; a mesh draws while any bit is
    // on; bit 2 skips the children; a child with bit 4 is skipped by its parent)
    const short on = frame->attr.draw_on;

    if (!(on & 1) && (on & 2)) {
        return;
    }
    if (static_cast<CFrameVu1 *>(frame)->visual != nullptr && on != 0) {
        names += names.empty() ? "" : " ";
        names += frame->name;
        count++;
    }
    if (!(on & 2)) {
        for (CFrame *child = frame->child; child != nullptr; child = child->brother) {
            if (!(child->attr.draw_on & 4)) {
                WalkParts(child, names, count);
            }
        }
    }
}
} // namespace

int DrawnParts(CCharacter *chara, const char **names) {
    static std::string list;
    int                count = 0;

    list.clear();
    if (chara != nullptr && chara->frame != nullptr) {
        WalkParts(chara->frame, list, count);
        // (CCharacter::Draw draws every cloth it wears after its frames, whatever their frames' bits)
        for (int i = 0; chara->cloth != nullptr && i < CHARA_CLOTH_MAX; i++) {
            if (chara->cloth[i] != nullptr) {
                list += list.empty() ? "" : " ";
                list += "cloth:";
                list += chara->cloth[i]->frame != nullptr ? chara->cloth[i]->frame->name : "?";
                count++;
            }
        }
    }
    *names = list.c_str();
    return count;
}

bool DrawnBox(CCharacter *fisher, int member, float *low, float *high) {
    const Rig *toan = Source();
    CFrameVu1 *frames = toan != nullptr ? Frames(fisher, *toan) : nullptr;

    if (frames == nullptr) {
        return false;
    }
    const bool own = Mapped(member) && Solve(fisher, member, nullptr, nullptr);
    M3         root_rot;
    V3         root_pos;

    World(frames[0], root_rot, root_pos);
    for (int i = 0; i < 3; i++) {
        low[i] = 1.0e9f;
        high[i] = -1.0e9f;
    }
    for (int role = 0; role < kRoles; role++) {
        M3 w;
        V3 p;

        if (own) {
            p = Add(root_pos, Apply(root_rot, g_solved[member].world_pos[Target(member)->of[role]]));
        } else {
            World(frames[toan->of[role]], w, p);
        }
        for (int i = 0; i < 3; i++) {
            low[i] = std::min(low[i], p.v[i]);
            high[i] = std::max(high[i], p.v[i]);
        }
    }
    return true;
}

bool HoldsFishingModel(CCharacter *fisher) {
    const Rig *toan = Source();

    return toan != nullptr && Frames(fisher, *toan) != nullptr;
}

bool Solve(CCharacter *fisher, int member, Rod *rod_out, Numbers *numbers) {
    if (!Mapped(member)) {
        return false;
    }
    const Rig &s = *Source();
    const Rig &d = *Target(member);
    CFrameVu1 *frames = Frames(fisher, s);

    if (frames == nullptr) {
        return false;
    }
    // Toan's role bones in the model's space (against the model's first frame, which stands where the fisher does)
    M3 root_rot, rot[kRoles];
    V3 root_pos, pos[kRoles];

    World(frames[0], root_rot, root_pos);
    const M3 into = Transpose(root_rot);

    for (int role = 0; role < kRoles; role++) {
        M3 w;
        V3 p;

        World(frames[s.of[role]], w, p);
        rot[role] = Mul(into, w);
        pos[role] = Apply(into, Sub(p, root_pos));
    }
    M3 grip_rot;
    V3 grip_pos;

    World(frames[s.grip], grip_rot, grip_pos);
    grip_rot = Mul(into, grip_rot);
    grip_pos = Apply(into, Sub(grip_pos, root_pos));
    // Toan's centre in its parent's space, against where it rests: the member's moves as far, by its leg's length
    const float leg_scale = d.leg / s.leg, arm_scale = d.arm / s.arm;
    V3          moved;
    {
        const int parent = s.bones[s.of[kCentre]].parent;
        M3        w;
        V3        p;

        World(frames[parent], w, p);
        w = Mul(into, w);
        p = Apply(into, Sub(p, root_pos));
        moved = Scale(Sub(Apply(Transpose(w), Sub(pos[kCentre], p)), s.bones[s.of[kCentre]].pos), leg_scale);
        moved.v[1] += d.lift; // (a member who hovers: as high as their own idle holds them)
    }
    Solved   &out = g_solved[member];
    const int count = (int) d.bones.size();

    out.rot.resize(count);
    out.pos.resize(count);
    out.world_rot.resize(count);
    out.world_pos.resize(count);
    int role_of[512];

    std::fill(role_of, role_of + count, -1);
    for (int role = 1; role < kRoles; role++) {
        role_of[d.of[role]] = role;
    }
    // Each leg's thigh and shin, once its foot is placed (below): their turns in the model's space.
    M3   held[kRoles];
    bool is_held[kRoles] = {};

    for (int i = 0; i < count; i++) {
        const Bone &bone = d.bones[i];
        M3          r = bone.rot;
        V3          p = bone.pos;
        const bool  top = bone.parent < 0;

        if (i == d.of[kCentre]) {
            p = Add(p, moved);
        }
        if (const int role = role_of[i]; role >= 0 && !top) {
            // how far Toan's bone has turned from its reference, taken from this model's reference
            M3 want = Mul(Mul(rot[role], Transpose(s.ref[role])), d.ref[role]);

            if ((role == kRightThigh || role == kLeftThigh) && !d.hovers && !Mutated("fishbody_feet_free")) {
                // The leg bent so its ankle stands where Toan's does: its rest place plus Toan's ankle's move from rest, by the leg's
                // length. A planted foot of Toan's then stays planted whatever the hips do (the plain turns let it drift 0.63 in the
                // hook-set's lunge: debug\tools\anim\check.py). The knee keeps the side it bends to.
                // DC_COOP_MUTATE=fishbody_feet_free (twin): the plain turns.
                const int shin = role + 1, foot = role + 2;
                const V3  hip = Add(out.world_pos[bone.parent], Apply(out.world_rot[bone.parent], p));
                const M3  shin_want = Mul(Mul(rot[shin], Transpose(s.ref[shin])), d.ref[shin]);
                const V3  upper = d.bones[d.of[shin]].pos;
                const V3  lower = Apply(Inverse(d.rest_rot[d.of[shin]]), Sub(d.rest_pos[d.of[foot]], d.rest_pos[d.of[shin]]));
                const V3  knee0 = Add(hip, Apply(want, upper));
                const V3  ankle0 = Add(knee0, Apply(shin_want, lower));
                const V3  ankle = Add(d.rest_pos[d.of[foot]], Scale(Sub(pos[foot], s.rest_pos[s.of[foot]]), leg_scale));
                const float l1 = Length(upper), l2 = Length(lower);
                const V3    to = Sub(ankle, hip);
                const float reach = SoftReach(std::max(Length(to), std::fabs(l1 - l2) + 0.01f), l1 + l2);
                const V3    along = Unit(to);
                const float x = (l1 * l1 - l2 * l2 + reach * reach) / (2.0f * reach);
                const float y = std::sqrt(std::max(0.0f, l1 * l1 - x * x));
                const V3    off = Sub(knee0, hip);
                const V3    bend = Sub(off, Scale(along, Dot(off, along)));

                if (Length(bend) >= 1.0e-4f) {
                    const V3 knee = Add(hip, Add(Scale(along, x), Scale(Unit(bend), y)));
                    const M3 first = Between(off, Sub(knee, hip));
                    const M3 second = Between(Apply(first, Sub(ankle0, knee0)), Sub(Add(hip, Scale(along, reach)), knee));

                    held[role] = Mul(first, want);
                    held[shin] = Mul(second, Mul(first, shin_want));
                    is_held[role] = is_held[shin] = true;
                }
            }
            if (is_held[role]) {
                want = held[role];
            }
            r = Mul(Inverse(out.world_rot[bone.parent]), want); // (a frame above may carry a scale: its inverse, not its transpose)
        }
        out.rot[i] = r;
        out.pos[i] = p;
        out.world_rot[i] = top ? r : Mul(out.world_rot[bone.parent], r);
        out.world_pos[i] = top ? p : Add(out.world_pos[bone.parent], Apply(out.world_rot[bone.parent], p));
    }
    out.on = true;
    // The rod hangs on the drawn body's right hand (Jep 11-16: "the fishing pole isn't in Xiao's hand and it doesn't move in
    // relation to her hand"; the first cut carried it beside Toan's hidden hand, moved by the wrists' difference): its line runs
    // through the hand's holding point and it turns with the hand, rigid in it as Toan's is in his weapon point; its origin as far
    // down its line from the hold as from Toan's palm.
    const V3 hand = out.world_pos[d.of[kRightHand]];
    const M3 turn = Mul(grip_rot, s.rod_in_grip);
    const V3 axis = Apply(turn, {{0.0f, 1.0f, 0.0f}});
    const V3 hold = Add(hand, Apply(out.world_rot[d.of[kRightHand]], d.hold_off));
    const V3 root = Sub(hold, Scale(axis, s.hold_along));
    const V3 tip = Add(root, Apply(turn, s.tip_pos));

    if (rod_out != nullptr) {
        Rod     &rod = *rod_out;
        M3       w;
        V3       keyed;
        const M3 turn_world = Mul(root_rot, turn);

        World(frames[s.tip], w, keyed); // (Toan's rod's tip as the motion keys it)
        std::memcpy(rod.root, Add(root_pos, Apply(root_rot, root)).v, sizeof rod.root);
        std::memcpy(rod.turn, turn_world.m, sizeof rod.turn);
        std::memcpy(rod.tip, Add(root_pos, Apply(root_rot, tip)).v, sizeof rod.tip);
        std::memcpy(rod.hold, Add(root_pos, Apply(root_rot, hold)).v, sizeof rod.hold);
        if (Mutated("fishbody_rod_toan")) {
            std::memcpy(rod.tip, keyed.v, sizeof rod.tip);
        }
        for (int i = 0; i < 3; i++) {
            rod.tip_step[i] = rod.tip[i] - keyed.v[i];
        }
    }

    if (numbers != nullptr) {
        Numbers &n = *numbers;
        auto     at = [&](int role) { return out.world_pos[d.of[role]]; };
        // (the palms: halfway from the wrist to the hand's end; a hand with no end joint (Ruby's) points along its forearm)
        const int   s_tip = Find(s, kToan[kRightHand].tip);
        const char *d_name = MapOf(member)[kRightHand].tip;
        const int   d_tip = Find(d, d_name[0] == '-' ? d_name + 1 : d_name);
        const V3    d_end = d_name[0] == '-' ? Add(hand, Sub(hand, out.world_pos[d_tip])) : out.world_pos[d_tip];
        M3          w;
        V3          s_end;

        World(frames[s_tip], w, s_end);
        s_end = Apply(into, Sub(s_end, root_pos));
        std::memcpy(n.hand, hand.v, sizeof n.hand);
        std::memcpy(n.left, at(kLeftHand).v, sizeof n.left);
        std::memcpy(n.hold, hold.v, sizeof n.hold);
        std::memcpy(n.tip, tip.v, sizeof n.tip);
        std::memcpy(n.hand_world, Add(root_pos, Apply(root_rot, hand)).v, sizeof n.hand_world);
        n.palm_toan = OffLine(Scale(Add(pos[kRightHand], s_end), 0.5f), grip_pos, axis);
        n.palm = OffLine(Scale(Add(hand, d_end), 0.5f), root, axis);
        n.left_rod_toan = OffLine(pos[kLeftHand], grip_pos, axis);
        n.left_rod = OffLine(at(kLeftHand), root, axis);
        n.floor = 1.0e9f;
        for (int side = 0; side < 2; side++) {
            const int foot = side == 0 ? kRightFoot : kLeftFoot, toe = side == 0 ? kRightToe : kLeftToe;
            const V3  his = Sub(pos[foot], s.rest_pos[s.of[foot]]), theirs = Sub(at(foot), d.rest_pos[d.of[foot]]);

            n.ankle_rise[side] = theirs.v[1] - his.v[1] * leg_scale;
            n.ankle_slide[side] = std::hypot(theirs.v[0] - his.v[0] * leg_scale, theirs.v[2] - his.v[2] * leg_scale);
            n.floor = std::min({n.floor, theirs.v[1], at(toe).v[1] - d.rest_pos[d.of[toe]].v[1]});
        }
        const int joints[4][3] = {{kRightArm, kRightFore, kRightHand}, {kLeftArm, kLeftFore, kLeftHand}, {kRightThigh, kRightShin, kRightFoot}, {kLeftThigh, kLeftShin, kLeftFoot}};

        for (int j = 0; j < 4; j++) {
            n.bend_toan[j] = Angle(Sub(pos[joints[j][1]], pos[joints[j][0]]), Sub(pos[joints[j][2]], pos[joints[j][1]]));
            n.bend[j] = Angle(Sub(at(joints[j][1]), at(joints[j][0])), Sub(at(joints[j][2]), at(joints[j][1])));
        }
        n.arm_scale = arm_scale;
        n.leg_scale = leg_scale;
    }
    return true;
}

namespace {
// A frame's own transform, kept while a pose stands in for it.
struct Kept {
    CFrame       *frame;
    sceVu0FMATRIX local;
    sceVu0FVECTOR scale, position, rotation;
    int           no_rotation, rot_type, srt;
};
struct KeptBody {
    CCharacter       *body = nullptr;
    std::vector<Kept> frames;
};
KeptBody g_kept[kMaxPlayers];

void Keep(KeptBody &kept, CFrame *frame) {
    Kept k;

    k.frame = frame;
    sceVu0CopyMatrix(k.local, frame->local);
    sceVu0CopyVector(k.scale, frame->scale);
    sceVu0CopyVector(k.position, frame->position);
    sceVu0CopyVector(k.rotation, frame->rotation);
    k.no_rotation = frame->no_rotation;
    k.rot_type = frame->rot_type;
    k.srt = frame->srt;
    kept.frames.push_back(k);
}

// The frame's local transform is m and nothing else (no scale, rotation or position parts on top).
void Put(CFrame *frame, sceVu0FMATRIX m) {
    frame->srt = 0;
    frame->SetTransMatrix(m);
}
} // namespace

bool Pose(CCharacter *body, int member) {
    const Rig *rig = member >= 0 && member < 6 ? Target(member) : nullptr;
    Solved    &pose = g_solved[member >= 0 && member < 6 ? member : 0];
    CFrameVu1 *frames = rig != nullptr ? Frames(body, *rig) : nullptr;

    if (frames == nullptr || !pose.on || member < 0 || member >= 6) {
        return false;
    }
    KeptBody *kept = nullptr;

    for (KeptBody &k : g_kept) {
        if (k.body == body) {
            return true; // (posed already: PoseEnd first)
        }
        kept = kept == nullptr && k.body == nullptr ? &k : kept;
    }
    if (kept == nullptr) {
        return false;
    }
    kept->body = body;
    kept->frames.clear();
    const int count = (int) rig->bones.size();

    // The first frame stands where the character does (its own place and turn, on top of its local transform): its local
    // transform its rest, as the solve has it. coop-fishing3: Ungaga's own motions key a small turn on it, which turned his
    // whole drawn body off the pose (his drawn wrist 0.036 off, the rod's hold 0.084 off his drawn hand); the others' are not keyed.
    // DC_COOP_MUTATE=fishbody_root_motion (twin): the first frame left as the body's motion has it.
    if (!Mutated("fishbody_root_motion")) {
        Keep(*kept, &frames[0]);
        for (int c = 0; c < 3; c++) {
            for (int r = 0; r < 3; r++) {
                frames[0].local[c][r] = pose.rot[0].m[r][c];
            }
            frames[0].local[3][c] = pose.pos[0].v[c];
        }
        frames[0].world_valid = false;
    }
    // Every frame below the first: the solved place, a frame with no role its rest.
    for (int i = 1; i < count; i++) {
        if (rig->bones[i].parent < 0) {
            continue;
        }
        sceVu0FMATRIX m;

        Keep(*kept, &frames[i]);

        sceVu0UnitMatrix(m);
        for (int c = 0; c < 3; c++) {
            for (int r = 0; r < 3; r++) {
                m[c][r] = pose.rot[i].m[r][c];
            }
            m[3][c] = pose.pos[i].v[c];
        }
        Put(&frames[i], m);
    }
    // The skin is bent in the motion's step from the frames (MotionProc2, the frame table's pass): again, from the posed frames.
    if (body->motion_no < 0) {
        body->SetMotion(0, 0);
    }
    int             index = -1;
    tagMOTION_TYPE *motion = body->GetMotionParam(body->motion_no, &index, nullptr, nullptr, nullptr);

    if (motion != nullptr) {
        for (Mot_List *list = motion->proc_list2; list != nullptr;) {
            list = MotionProc2(body->frame, motion, motion->frame_info, list);
        }
    }
    // Its shadow's own skeleton, by name: the role bones' turns and the centre's place (the rest of it stays as its motion left it).
    if (body->shadow_frame != nullptr) {
        const Role *map = MapOf(member);

        for (int role = 0; role < kRoles; role++) {
            CFrame *frame = body->shadow_frame->SearchFrame(const_cast<char *>(map[role].bone));
            const int i = rig->of[role];

            if (frame == nullptr || frame == body->shadow_frame) {
                continue;
            }
            sceVu0FMATRIX m;

            Keep(*kept, frame);
            sceVu0UnitMatrix(m);
            for (int c = 0; c < 3; c++) {
                for (int r = 0; r < 3; r++) {
                    m[c][r] = pose.rot[i].m[r][c];
                }
                m[3][c] = pose.pos[i].v[c];
            }
            Put(frame, m);
        }
        if (tagMOTION_TYPE *shadow = body->shadow_motion[0]; shadow != nullptr) {
            for (Mot_List *list = shadow->proc_list2; list != nullptr;) {
                list = MotionProc2(body->shadow_frame, shadow, shadow->frame_info, list);
            }
        }
    }
    return true;
}

void PoseEnd(CCharacter *body) {
    for (KeptBody &kept : g_kept) {
        if (kept.body != body) {
            continue;
        }
        for (Kept &k : kept.frames) {
            sceVu0CopyMatrix(k.frame->local, k.local);
            sceVu0CopyVector(k.frame->scale, k.scale);
            sceVu0CopyVector(k.frame->position, k.position);
            sceVu0CopyVector(k.frame->rotation, k.rotation);
            k.frame->no_rotation = k.no_rotation;
            k.frame->rot_type = k.rot_type;
            k.frame->srt = k.srt;
            k.frame->world_valid = false;
        }
        kept.body = nullptr;
        kept.frames.clear();
    }
}

namespace {
// What PropsOnlyBegin changed, to put back: per fisher (six at most at once).
struct Hidden {
    CCharacter       *fisher = nullptr;
    short             draw_on[512];
    short             shadow_draw_on = 0;
    CCloth           *cloth[CHARA_CLOTH_MAX] = {};
    std::vector<Kept> frames; // the rod's, the float's and the hook's own transforms
};
Hidden g_hidden[kMaxPlayers];

Hidden *HiddenFor(CCharacter *fisher, bool make) {
    for (Hidden &h : g_hidden) {
        if (h.fisher == fisher) {
            return &h;
        }
    }
    for (Hidden &h : g_hidden) {
        if (make && h.fisher == nullptr) {
            return &h;
        }
    }
    return nullptr;
}

// a * b, [column][row] as the frames keep them.
void Mul4(sceVu0FMATRIX out, const sceVu0FMATRIX a, const sceVu0FMATRIX b) {
    sceVu0FMATRIX m;

    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            m[c][r] = a[0][r] * b[c][0] + a[1][r] * b[c][1] + a[2][r] * b[c][2] + a[3][r] * b[c][3];
        }
    }
    std::memcpy(out, m, sizeof m);
}

// frame stands at world (a world matrix) for the draw: its local transform against its parent's place, its own kept.
void PlaceInWorld(std::vector<Kept> &kept, CFrame *frame, sceVu0FMATRIX world) {
    sceVu0FMATRIX parent, inverse, local;

    if (frame->parent == nullptr) {
        return;
    }
    frame->parent->GetLWMatrix(parent);
    sceVu0InversMatrix(inverse, parent);
    Mul4(local, inverse, world);
    KeptBody one;

    Keep(one, frame);
    kept.push_back(one.frames[0]);
    Put(frame, local);
}
} // namespace

void PropsOnlyBegin(CCharacter *fisher, const Rod &rod) {
    const Rig *toan = Source();
    CFrameVu1 *frames = toan != nullptr ? Frames(fisher, *toan) : nullptr;
    Hidden    *h = frames != nullptr ? HiddenFor(fisher, true) : nullptr;

    if (h == nullptr || h->fisher == fisher) {
        return; // (not the fishing model, or begun already)
    }
    const int count = (int) toan->bones.size();

    h->fisher = fisher;
    h->frames.clear();
    for (int i = 0; i < count; i++) {
        h->draw_on[i] = frames[i].attr.draw_on;
        // (a prop and what hangs under it stay; every other mesh of the model is Toan's body)
        bool prop = false;

        for (int up = i; up >= 0; up = toan->bones[up].parent) {
            prop = prop || up == toan->shown[0] || up == toan->shown[1] || up == toan->shown[2];
        }
        if (toan->bones[i].mesh && !prop) {
            frames[i].attr.draw_on = 0;
        }
    }
    // His cloth (Jep 11-16: "Toan's shawl is also showing over Xiao's model"): the turban and the poncho are cloth, which
    // CCharacter::Draw draws after the frames whatever the frames' draw bits say; out of the character for the draw.
    for (int i = 0; i < CHARA_CLOTH_MAX; i++) {
        h->cloth[i] = fisher->cloth != nullptr ? fisher->cloth[i] : nullptr;
        if (fisher->cloth != nullptr && !Mutated("fishbody_shawl_left")) {
            fisher->cloth[i] = nullptr;
        }
    }
    if (fisher->shadow_frame != nullptr) {
        h->shadow_draw_on = fisher->shadow_frame->attr.draw_on;
        fisher->shadow_frame->attr.draw_on = 2; // (the whole shadow model left out)
    }
    if (Mutated("fishbody_rod_toan")) {
        return; // (the twin: the rod where Toan's hidden hand holds it)
    }
    // The rod in the member's hand; the float and the hook as far from where the motion keys them as the rod's tip is.
    sceVu0FMATRIX world;

    sceVu0UnitMatrix(world);
    for (int c = 0; c < 3; c++) {
        for (int r = 0; r < 3; r++) {
            world[c][r] = rod.turn[r][c];
        }
        world[3][c] = rod.root[c];
    }
    PlaceInWorld(h->frames, &frames[toan->shown[0]], world);
    for (int prop = 1; prop < 3; prop++) {
        frames[toan->shown[prop]].GetLWMatrix(world);
        for (int c = 0; c < 3; c++) {
            world[3][c] += rod.tip_step[c];
        }
        PlaceInWorld(h->frames, &frames[toan->shown[prop]], world);
    }
}

void PropsOnlyEnd(CCharacter *fisher) {
    Hidden *h = HiddenFor(fisher, false);

    if (h == nullptr) {
        return;
    }
    h->fisher = nullptr;
    const Rig *toan = Source();
    CFrameVu1 *frames = toan != nullptr ? Frames(fisher, *toan) : nullptr;

    if (frames == nullptr) {
        h->frames.clear();
        return; // (another model loaded into the body meanwhile: nothing of the old one to put back)
    }
    const int count = (int) toan->bones.size();

    for (int i = 0; i < count; i++) {
        frames[i].attr.draw_on = h->draw_on[i];
    }
    for (int i = 0; fisher->cloth != nullptr && i < CHARA_CLOTH_MAX; i++) {
        fisher->cloth[i] = h->cloth[i];
    }
    if (fisher->shadow_frame != nullptr) {
        fisher->shadow_frame->attr.draw_on = h->shadow_draw_on;
    }
    for (Kept &k : h->frames) {
        sceVu0CopyMatrix(k.frame->local, k.local);
        sceVu0CopyVector(k.frame->scale, k.scale);
        sceVu0CopyVector(k.frame->position, k.position);
        sceVu0CopyVector(k.frame->rotation, k.rotation);
        k.frame->no_rotation = k.no_rotation;
        k.frame->rot_type = k.rot_type;
        k.frame->srt = k.srt;
        k.frame->world_valid = false;
    }
    h->frames.clear();
    // Every frame's world matrix made again from where the model stands, so nothing of the draw is left in the frames.
    for (int i = 0; i < count; i++) {
        sceVu0FMATRIX m;

        frames[i].GetLWMatrix(m);
    }
}

} // namespace coop::fishbody
