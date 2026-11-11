#pragma once

// coop-steamhelper: the game's config as the helper's build of netplay_steam_core.cpp needs it: game.netplay_steam_rate, which
// the game passes on the helper's command line (--rate KB), dcsteam.cpp.
// As the game's own (Chronicle 9418a550), in namespace dc::platform.
namespace dc::platform {
struct HelperConfig {
    int netplay_steam_rate = 0;
};
const HelperConfig &ConfigGet();
} // namespace dc::platform
