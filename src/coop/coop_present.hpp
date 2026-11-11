#pragma once

#include "coop/game_ns.hpp"

#include "coop/coop_audio.hpp"
#include "coop/coop_message.hpp"

#include <cstdint>
#include <optional>

// coop-router (root fix 2, Jep 2026-10-30; notes: presentation-router-design.md): anything a player perceives that is not
// game state (sounds, messages, sprites, fades, camera cuts, rumble) is presentation, and two rules hold for all of it:
//   1. it goes to the right players: the copies (and in split screen the views) that should perceive it, no others;
//   2. it never touches lockstep state: it may read it, never write it (the random generator, globals the step reads).
// A Scope says who perceives what is presented while it lives, by player index (no "his" or "hers": N players). The
// older mechanisms are its forwards: For is SoundOwner + MessageBy, At is SoundFrom. Scopes nest; the innermost wins.
namespace coop::present {

struct For {
    int player; // 1..N: that player's alone
};

struct At {
    const float *pos; // the world's, placed: each copy hears it by distance from its own listener
};

struct Everyone {}; // every viewer, unplaced, said out loud (a story scene's music)

// The guard (rule 2). DC_COOP_PRESENT_GUARD=1 (the tests' switch; linked copies): leaving a scope other than Everyone
// compares the random generator and the lockstep checksum with what they were on entering it, and a change is logged
//   present: scope <where> changed the random generator (a -> b)
//   present: scope <where> changed the lockstep checksum (a -> b)
// naming the scope that wrote the step's state. MayStep() exempts a scope proven to run alike on every copy.
// DC_COOP_MUTATE=present_guard_off (its twin): no scope is checked.
class Scope {
public:
    Scope(For who, const char *where);
    Scope(At from, const char *where);
    Scope(Everyone, const char *where);
    ~Scope();
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

    Scope &MayStep();

private:
    void Enter();

    const char               *where_;
    bool                      guarded_ = false;
    unsigned int              rand_ = 0;
    std::uint32_t             checksum_ = 0;
    std::optional<SoundOwner> owner_;
    std::optional<SoundFrom>  from_;
    std::optional<MessageBy>  message_;
};

// How many scopes the guard checked and how many it caught, for the tests (logged every 600 checks).
long GuardChecked();
long GuardCaught();

} // namespace coop::present

#define COOP_PRESENT_STR2(x) #x
#define COOP_PRESENT_STR(x) COOP_PRESENT_STR2(x)
// The scope's name in the guard's line: its file and line.
#define COOP_PRESENT_HERE (__FILE__ ":" COOP_PRESENT_STR(__LINE__))
