#pragma once

#include "coop/game_ns.hpp"
// coop-review1109, coop-guard: test instruments, off without DC_QA_WRITE_GUARD (coop_writeguard.cpp).
namespace coop {
constexpr int kGuardDraw = 0; // the outermost DrawRngEnter/Leave (the census only)
constexpr int kGuardMenu = 1; // a local menu's whole step, open or draw on its owner's copy (WriteGuardMenu below)
// site: what the log line names (a menu's site); caller: a draw's return address, named by its RVA when site is null.
// skip: a global the scope holds something else in (her parts menu: her own pad in GamePad's place), left out of its check.
void WriteGuardEnter(int kind, const char *site, void *caller = nullptr, const void *skip = nullptr);
void WriteGuardLeave(int kind);
void WriteGuardTick(long long tick, int player, bool lockstep); // start of NetplayTick

// coop-guard: a local menu's scope, outermost first (an inner one is the outer's): from before its sound owner, random numbers,
// pad, view and engine are swapped in to after they are all back, so what is left changed is what one copy's menu left in the
// shared world. on = false: no scope (a menu run on every copy, in the lockstep).
class WriteGuardMenu {
  public:
    explicit WriteGuardMenu(const char *site, bool on = true, const void *skip = nullptr) : on_(on) {
        if (on_) {
            WriteGuardEnter(kGuardMenu, site, nullptr, skip);
        }
    }
    ~WriteGuardMenu() {
        if (on_) {
            WriteGuardLeave(kGuardMenu);
        }
    }
    WriteGuardMenu(const WriteGuardMenu &) = delete;
    WriteGuardMenu &operator=(const WriteGuardMenu &) = delete;

  private:
    bool on_;
};
} // namespace coop
