// The game data this mod plays (dropin, Jep 11-12): the disc release dcdata writes in languages.json and the SHA-256 of
// the models.json that Chronicle's dcdata at chronicle-pin.txt's commit writes for that disc. The launcher refuses other
// data with a plain message, and debug\build\make-release.ps1 copies both lines into the setup's release.txt, so the
// setup and the launcher check the same thing. Change it with a Chronicle re-pin whose dcdata writes another models.json.
// (launcher\ is not in the content version: changing this file never changes darkcloud.exe.)
#pragma once
#define DCO_DATA_RELEASE "NTSC 1.02"
#ifndef DCO_DATA_MODELS_SHA256   // (l-launcher-release builds it with its own stand-in's)
#define DCO_DATA_MODELS_SHA256 "B6EEDB274F09699142C73D97D4AE6F4267127B2AA8303622B18DBAA1CFB96D59"
#endif
