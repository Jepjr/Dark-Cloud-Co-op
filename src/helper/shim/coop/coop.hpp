#pragma once

// coop-steamhelper: the game's coop.hpp as the helper's build of netplay_steam_core.cpp needs it (dcsteam.cpp defines these).
const char *TestEnv(const char *name);
int         TestEnvInt(const char *name, int fallback);
bool        TestEnvOn(const char *name);
bool        TestEnvIs(const char *name, const char *value);
