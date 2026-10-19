// BayviewForever: the one .asi that holds every feature (src/bayview-forever/main.cpp starts them).
//
// Each feature keeps its own source folder (src/covers/, src/h-shifter/, ...) and is compiled with
// -DBAYVIEW_FOREVER. Before including nfsu2.h it defines BF_TAG, the tag its lines get in BayviewForever.log:
//     #define BF_TAG "Covers"
//     #include "nfsu2.h"
// Its settings are sections of scripts\BayviewForever.ini named "<Feature>.<Section>" (e.g. [Covers.Earn]).

#pragma once

#include <windows.h>
#include <cstdarg>

// One version for the whole mod: bump it for every build that goes to the PC.
#define BF_VERSION "0.2.9"

namespace bf {

// In start order (alphabetical).
enum Feature {
    kCovers,
    kDragSteering,
    kEndlessCareer,
    kEventLogger,
    kGarageLogger,
    kHShifter,
    kParticleFix,
    kRaceAI,
    kRespawnAtGarage,
    kSurround,
    kUniqueParts,
    kVirtualGarage,
    kWheel,
    kNumFeatures
};

HMODULE Module();                       // BayviewForever.asi itself
bool FeatureOn(Feature f);              // [Features] switch (read once at startup)
void IniPath(char out[MAX_PATH]);       // full path of scripts\BayviewForever.ini (next to the .asi)
bool IniFound();                        // false = the file is missing: every feature uses its defaults
void LogV(const char* tag, const char* fmt, va_list args);  // one line in BayviewForever.log
bool ExeMd5Ok();                        // MD5 of speed2.exe = v1.2; computed once. Never call it from DllMain.

// Feature entry points, called from DllMain (each does what its own DllMain used to do).
void StartCovers();
void StartDragSteering();
void StartEndlessCareer();
void StartEventLogger();
void StartGarageLogger();
void StartHShifter();
void StartParticleFix();
void StartRaceAI();
void StartRespawnAtGarage();
void StartSurround();
void StartUniqueParts();
void StartVirtualGarage();
void StartWheel();

// Cross-feature calls (main thread).
// Endless Career calls these right after Career_RebuildActiveEvents (and its own races); each checks its own state.
void UniquePartsAfterRebuild();  // the unique part challenge for the car being driven
void CoversAfterRebuild();       // magazine challenges for the car being driven ([Covers.Earn] MagazineChallenge)

}  // namespace bf
