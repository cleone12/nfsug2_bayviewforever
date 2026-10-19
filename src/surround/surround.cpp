// Surround: turns the PC version's cut 5.1 / quad output back on, plus own-car sound placement and volume tweaks.
//
// The sound library still has full quad/5.1 output (DirectSound WAVE_FORMAT_EXTENSIBLE, panning around 4-5 speakers
// + LFE); only the game side asks for mono/stereo. The 3 CALLs to Snd_SetOutputMode (0x0046B2E5, 0x0046B2F6 at boot,
// 0x00486C6F on every world load) are redirected: stereo becomes the [Surround.Audio] Speakers layout (falls back to
// stereo if refused). Mono stays mono.
// The only CALL that sets a voice's per-channel volume (0x00724FAC -> Snd_SetVoiceChannelGain) is redirected for the
// speaker test, own-car placement and the [Balance] trims; the pan CALL (0x00725012) only feeds
// the direction statistics.
// Own car: the game places the player's sounds at the car's center, right behind the hood camera, so their direction
// jumps. The engine's voices (marked at its 3 CALLs to Snd_SetAzimuth) and, with AllOwnSounds, every mono voice
// pointing where the engine points (in-car views only) get a fixed front/rear blend instead. There is no separate
// exhaust sound: each engine is an accel + decel recording.
// Options menu: with 5.1 / quad running, the Audio Mode value reads "5.1 Surround" / "Quad" instead of "Stereo"
// ([Surround.Audio] MenuLabel; CALL 0x0049ABFC, the value text of FEAudioOptions_DrawAudioMode).
// Turbo: BlowOffVolume scales the blow-off volume (+0xE8) after SndBase_PlayOneShot (CALL 0x0047CB9D); SpoolVolume
// scales the spool volume (+0xE4) after each CarSfxTurbo_UpdateSpool (CALLs 0x0047CB2C, 0x0047CBAF). Both work in
// stereo too; the game caps every sound at full volume.
// Undo: [Features] Surround = 0.

#include <cmath>

#define BF_TAG "Surround"
#include "nfsu2.h"

using namespace nfsu2;

namespace {

const DWORD kPollInterval  = 50;     // ms between gear / sound state checks
const DWORD kReportEvery   = 20000;  // ms between direction statistics in the log

int g_mode = snd::kMode51;  // [Audio] Speakers: 6 -> 5.1, 4 -> quad, 2 -> stereo (off)
bool g_speakerTest = false;  // [Test] SpeakerTest
bool g_detailedLog = false;  // [Log] Detailed: sound dumps after mode changes + direction statistics every 20 s
bool g_ownAll = true;        // [OwnCar] AllOwnSounds: the engine's blend for every own-car sound (by direction)
int g_blowOffVolume = 100;   // [Sounds] BlowOffVolume: % of the game's turbo blow-off volume
int g_spoolVolume = 100;     // [Sounds] SpoolVolume: % of the game's turbo spool volume
bool g_ownEngine = true;     // [OwnCar] OwnEnginePlacement
int g_engineFront = 60;      // [OwnCar] EngineFront: % of the own engine sound from the front speakers
int g_engineCentre = 33;     // [OwnCar] EngineCentre: % of the engine's front share on the center speaker
float g_engineVolume = 1.0f; // [OwnCar] EngineVolume / 100
float g_trim[8] = {1, 1, 1, 1, 1, 1, 1, 1};  // [Balance] per 5.1 mixer channel (kCh51*), volume factor
float g_subSend = 0.0f;      // [Balance] SubwooferSend / 100: share of every sound also sent to the subwoofer
bool g_balanceOn = false;    // any trim != 100 or a subwoofer send
bool g_menuLabel = true;     // [Audio] MenuLabel: Options menu says "5.1 Surround" / "Quad" instead of "Stereo"
volatile LONG g_grantedMode = -1;  // last output mode the sound library accepted (-1 = none yet)
volatile LONG g_testChannel = -1;  // mixer channel that gets every sound, -1 = normal mix (set by the monitor thread)

const CodeCheck kCodeChecks[] = {
    {0x0046B2DD, {0x83, 0xE8, 0x00, 0x74, 0x12, 0x6A, 0x01, 0x48}, 8, "AudioSettings_SetAudioMode: mode != 0 -> push 1; dec eax"},
    // Turbo blow-off ([Sounds] BlowOffVolume)
    {0x0045EEA4, {0x8B, 0x86, 0xC0, 0x00, 0x00, 0x00, 0x85, 0xC0}, 8, "PlayOneShot: only if obj+0xC0 (sound) is 0"},
    {0x0045EF0B, {0x89, 0x86, 0xE8, 0x00, 0x00, 0x00}, 6, "PlayOneShot: volume -> obj+0xE8"},
    {0x0045EF73, {0x89, 0x86, 0xC0, 0x00, 0x00, 0x00}, 6, "PlayOneShot: sound -> obj+0xC0"},
    {0x0045EFDE, {0xC2, 0x14, 0x00}, 3, "PlayOneShot: RET 0x14"},
    {0x0045ED13, {0x8B, 0x86, 0xE8, 0x00, 0x00, 0x00, 0x0F, 0xAF}, 8, "turbo sound update: volume = obj+0xE8 x master"},
    {0x0047CB82, {0xC7, 0x86, 0x80, 0x01, 0x00, 0x00, 0x02}, 7, "turbo update: boost dropped -> state 2 (blow-off)"},
    // Turbo spool ([Sounds] SpoolVolume)
    {0x00472776, {0x8B, 0x86, 0xBC, 0x00, 0x00, 0x00, 0x85, 0xC0}, 8, "spool: no sound (+0xBC) -> start it, no volume"},
    {0x00472795, {0xC2, 0x04, 0x00}, 3, "spool: RET 4 (start path)"},
    {0x004729B4, {0x89, 0x86, 0xE4, 0x00, 0x00, 0x00}, 6, "spool: volume -> +0xE4"},
    {0x004729C0, {0xC2, 0x04, 0x00}, 3, "spool: RET 4"},
    {0x0045ED86, {0x8B, 0x8E, 0xE4, 0x00, 0x00, 0x00, 0x0F, 0xAF}, 8, "turbo sound update: spool volume = +0xE4 x master"},
    {0x0046B2E5, {0xE8, 0x97, 0x63, 0x2B, 0x00}, 5, "AudioSettings_SetAudioMode: CALL Snd_SetOutputMode (stereo)"},
    {0x0046B2F4, {0x6A, 0x00, 0xE8, 0x86, 0x63, 0x2B, 0x00, 0x83}, 8, "AudioSettings_SetAudioMode: push 0; CALL Snd_SetOutputMode (mono)"},
    {0x0046B2FC, {0xC4, 0x04}, 2, "AudioSettings_SetAudioMode: add esp,4 (caller pops the argument)"},
    {0x00486C4D, {0x8B, 0x0D, 0x50, 0xAA, 0x83, 0x00}, 6, "world load reads g_AudioMode 0x0083AA50"},
    {0x00486C60, {0x8B, 0x40, 0x14, 0x83, 0xE8, 0x00, 0x74, 0x05}, 8, "world load: mode != 0 ?"},
    {0x00486C68, {0x6A, 0x01, 0x48, 0xEB, 0x02, 0x6A, 0x00, 0xE8}, 8, "world load: push 1 / push 0; CALL"},
    {0x00486C70, {0x0D, 0xAA, 0x29, 0x00, 0x83, 0xC4, 0x04, 0xE8}, 8, "world load: CALL Snd_SetOutputMode; add esp,4; CALL"},
    {0x00486C78, {0xBE, 0xAB, 0x29, 0x00}, 4, "world load: CALL Snd_ApplyOutputMode"},
    {0x0047ABA4, {0xE8, 0x17, 0x07, 0xFF, 0xFF}, 5, "audio init: CALL AudioSettings_SetAudioMode(1)"},
    {0x00721681, {0x55, 0x8B, 0xEC, 0x51, 0x56, 0x8B, 0x75, 0x08}, 8, "start of Snd_SetOutputMode (cdecl mode)"},
    {0x00721689, {0x8D, 0x45, 0xFF, 0x50, 0x56, 0xE8, 0xAA, 0xFF}, 8, "Snd_SetOutputMode: CALL Snd_IsOutputModeSupported"},
    {0x007216A0, {0x80, 0x3D, 0x08, 0x46, 0x8B, 0x00, 0x00, 0x75}, 8, "Snd_SetOutputMode: running flag 0x008B4608"},
    {0x007216A8, {0x08, 0x89, 0x35, 0x84, 0x80, 0x81, 0x00}, 7, "Snd_SetOutputMode: stored mode 0x00818084"},
    {0x00724CD0, {0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x0C}, 6, "start of Snd_OutputModeToChannels"},
    // Direction statistics: Voice_UpdatePan pushes gains*, the u16 angle (voice+0x1C) and calls Snd_AngleToGains.
    {0x0072500C, {0x50, 0x0F, 0xB7, 0x41, 0x1C, 0x50, 0xE8, 0x61}, 8, "Voice_UpdatePan: push gains, angle; CALL Snd_AngleToGains"},
    {0x00725014, {0x08, 0x00, 0x00, 0x59, 0x59}, 5, "Voice_UpdatePan: caller pops 2 args"},
    {0x00725878, {0x55, 0x8B, 0xEC, 0x0F, 0xB6, 0x4D, 0x09, 0x56}, 8, "start of Snd_AngleToGains: reads high byte of the angle argument"},
    // Speaker test: Voice_UpdateGains: volume = (char)voice[0x5F] * voice[0x48] * (1/127), then per mixer channel
    // push volume * gain, channel, voice; CALL Snd_SetVoiceChannelGain.
    {0x00724F66, {0x8D, 0x04, 0xFF, 0xC1, 0xE0, 0x04, 0x03, 0x05}, 8, "Voice_UpdateGains: voice = [0x008B466C] + n*0x90 (1/2)"},
    {0x00724F6E, {0x6C, 0x46, 0x8B, 0x00}, 4, "Voice_UpdateGains: voice = [0x008B466C] + n*0x90 (2/2)"},
    {0x00724F74, {0x0F, 0xBE, 0x48, 0x5F}, 4, "Voice_UpdateGains: movsx volume byte voice+0x5F"},
    {0x00724F8F, {0xDB, 0x45, 0x08, 0xD8, 0x48, 0x48, 0xD8, 0x0D}, 8, "Voice_UpdateGains: * float voice+0x48 * [const]"},
    {0x00724F97, {0x4C, 0xA9, 0x78, 0x00}, 4, "Voice_UpdateGains: const 0x0078A94C (1/127)"},
    {0x0078A94C, {0x04, 0x02, 0x01, 0x3C}, 4, "float 1/127"},
    {0x00724FA3, {0x51, 0xD8, 0x0C, 0x9E, 0xD9, 0x1C, 0x24, 0x53}, 8, "Voice_UpdateGains: push volume*gain, channel"},
    {0x00724FAB, {0x57, 0xE8, 0xAC, 0x40, 0x00, 0x00, 0x0F, 0xB6}, 8, "Voice_UpdateGains: push voice; CALL Snd_SetVoiceChannelGain"},
    {0x00724FB8, {0x83, 0xC4, 0x0C}, 3, "Voice_UpdateGains: caller pops 3 args"},
    // Own engine placement: the player engine's direction calls (handle at +0xE0 / +0xFC, angle from vtable+0x3C).
    {0x00473F52, {0x8B, 0x8E, 0xE0, 0x00, 0x00, 0x00, 0x50, 0x51}, 8, "engine 1 layer: push angle, handle +0xE0"},
    {0x00473F5A, {0xE8, 0x34, 0x01, 0x2B, 0x00}, 5, "engine 1 layer: CALL Snd_SetAzimuth"},
    {0x00474127, {0x8B, 0x8E, 0xE0, 0x00, 0x00, 0x00, 0x50, 0x51}, 8, "engine 2 layers: push angle, handle +0xE0"},
    {0x0047412F, {0xE8, 0x5F, 0xFF, 0x2A, 0x00}, 5, "engine 2 layers: CALL Snd_SetAzimuth (accel layer)"},
    {0x0047427D, {0x8B, 0x8E, 0xFC, 0x00, 0x00, 0x00, 0x50, 0x51}, 8, "engine 2 layers: push angle, handle +0xFC"},
    {0x00474285, {0xE8, 0x09, 0xFE, 0x2A, 0x00}, 5, "engine 2 layers: CALL Snd_SetAzimuth (decel layer)"},
    {0x0047433F, {0xE8, 0x1C, 0xFB, 0xFF, 0xFF}, 5, "player engine update: CALL 1-layer update 0x00473E60"},
    {0x00474346, {0xE8, 0xE5, 0xFC, 0xFF, 0xFF}, 5, "player engine update: CALL 2-layer update 0x00474030"},
    {0x0078AF78, {0x10, 0x43, 0x47, 0x00}, 4, "player engine class (0x0078AF30) slot 18 = engine update 0x00474310"},
    {0x0078AF28, {0xF0, 0x10, 0x46, 0x00}, 4, "AI engine class (0x0078AEE0) slot 18 = its own update 0x004610F0"},
    {0x00724093, {0x55, 0x8B, 0xEC, 0x51, 0x80, 0x3D, 0x08, 0x46}, 8, "start of Snd_SetAzimuth"},
    {0x00725488, {0x0F, 0xBF, 0x0D, 0x0C, 0x46, 0x8B, 0x00}, 7, "handle lookup: voice count 0x008B460C"},
    {0x00725491, {0x25, 0xFF, 0x00, 0x00, 0x00}, 5, "handle lookup: voice index = handle & 0xFF"},
    {0x007254A6, {0x80, 0x79, 0x6D, 0x00, 0x74, 0x04, 0x39, 0x11}, 8, "handle lookup: voice+0x6D active, voice+0 == handle"},
    {0x00727FCD, {0x0F, 0xB6, 0x54, 0x11, 0x26}, 5, "voice group walk: group byte voice+0x26"},
    // Options menu label: FEAudioOptions_DrawAudioMode, value 1 -> FE text by hash "Stereo"
    {0x0049ABE1, {0xA1, 0x50, 0xAA, 0x83, 0x00}, 5, "Audio Mode value: MOV EAX,[g_AudioMode]"},
    {0x0049ABF2, {0xB9, 0xF8, 0x8B, 0xDA, 0x55, 0x51, 0x8B, 0x4E}, 8, "Audio Mode value: hash \"Stereo\"; PUSH; text object"},
    {0x0049ABFA, {0x30, 0x51, 0xE8, 0xFF, 0x1C, 0x07, 0x00, 0x83}, 8, "Audio Mode value: [ESI+0x30]; PUSH; CALL set text by hash"},
    {0x0049AC02, {0xC4, 0x08}, 2, "Audio Mode value: caller pops 2 args"},
    {0x0050C900, {0x56, 0x8B, 0x74, 0x24, 0x08, 0x85, 0xF6, 0x74}, 8, "prologue of FE text by hash (cdecl obj, hash)"},
    {0x00535FE0, {0x53, 0x56, 0x57, 0x8B, 0xF9, 0x8B, 0x47, 0x64}, 8, "prologue of FEString_SetText (obj+0x64 text)"},
    {0x00536046, {0xC2, 0x04, 0x00}, 3, "RET 4 of FEString_SetText"},
};

const char* ModeName(int mode) {
    switch (mode) {
        case snd::kModeMono: return "mono";
        case snd::kModeStereo: return "stereo";
        case snd::kModeQuad: return "quad";
        case snd::kMode51: return "5.1";
    }
    return "?";
}

// ---- Output mode --------------------------------------------------------------

typedef int(__cdecl* SetOutputModeFn)(int mode);
const SetOutputModeFn SetOutputMode = reinterpret_cast<SetOutputModeFn>(snd::kSetOutputMode);

void LogCaps() {
    if (!IsReadable(snd::kCaps, 4)) return;
    const unsigned char* c = reinterpret_cast<const unsigned char*>(snd::kCaps);
    Log("DirectSound accepted: mono %s, stereo %s, quad %s, 5.1 %s.",
        c[0] == 1 ? "yes" : "no", c[1] == 1 ? "yes" : "no", c[2] == 1 ? "yes" : "no", c[3] == 1 ? "yes" : "no");
}

// Replaces the game's 3 CALLs to Snd_SetOutputMode (cdecl: the caller pops the argument). Runs on the game's thread.
int __cdecl Hook_SetOutputMode(int mode) {
    static bool capsLogged = false;
    const uintptr_t site = reinterpret_cast<uintptr_t>(__builtin_return_address(0)) - 5;
    const int running = *reinterpret_cast<const unsigned char*>(snd::kRunning);

    int want = mode == snd::kModeStereo ? g_mode : mode;
    int result = SetOutputMode(want);
    if (!capsLogged) {  // the first call runs the DirectSound probe
        capsLogged = true;
        LogCaps();
    }
    if (result < 0 && want == snd::kMode51) {
        Log("  5.1 refused (%d), trying quad.", result);
        want = snd::kModeQuad;
        result = SetOutputMode(want);
    }
    if (result < 0 && want != mode) {
        Log("  %s refused (%d), falling back to the game's %s.", ModeName(want), result, ModeName(mode));
        want = mode;
        result = SetOutputMode(want);
    }
    Log("Output mode request from 0x%08X: game asked %s -> set %s, result %d (sound system %s).",
        static_cast<unsigned>(site), ModeName(mode), ModeName(want), result, running ? "running, applies on re-init" : "not running yet");
    if (result >= 0) InterlockedExchange(&g_grantedMode, want);
    return result;
}

// ---- Options menu label -----------------------------------------------------------

const uint32_t kTextStereo = 0x55DA8BF8;
const uintptr_t kSetTextByHash = 0x0050C900;      // cdecl(text object, text hash)
const uintptr_t kAudioModeTextCall = 0x0049ABFC;  // FEAudioOptions_DrawAudioMode's value text

// Replaces the CALL that writes the Audio Mode value text (cdecl, caller pops). Game's thread (menus).
void __cdecl Hook_AudioModeText(uintptr_t obj, uint32_t hash) {
    using Fn = void(__cdecl*)(uintptr_t, uint32_t);
    reinterpret_cast<Fn>(kSetTextByHash)(obj, hash);
    const LONG mode = g_grantedMode;
    if (hash != kTextStereo || (mode != snd::kMode51 && mode != snd::kModeQuad) || !obj || !IsReadable(obj, 0x70) ||
        ReadAt<uint32_t>(reinterpret_cast<const unsigned char*>(obj), feobj::kType) != 2)
        return;
    using SetFn = void(__fastcall*)(uintptr_t, void*, const char*);  // FEString_SetText: thiscall(obj, text), RET 4
    reinterpret_cast<SetFn>(kFEStringSetText)(obj, nullptr, mode == snd::kMode51 ? "5.1 Surround" : "Quad");
    *reinterpret_cast<uint32_t*>(obj + feobj::kFlags) |= 2;
}

// ---- Direction statistics -------------------------------------------------------

// Pan updates since the last report, by direction (45 degree sectors centered on: ahead, front right, right,
// rear right, behind, rear left, left, front left).
const int kNumSectors = 8;
const char* const kSectorNames[kNumSectors] = {"ahead", "front-right", "right", "rear-right",
                                               "behind", "rear-left", "left", "front-left"};
volatile LONG g_sectorCount[kNumSectors];
volatile LONG g_ownMarks;  // own engine voice marks since the last report (statistics)
volatile LONG g_ownCarMatches;  // gain updates of other own-car sounds placed by direction (statistics)

typedef void(__cdecl* AngleToGainsFn)(unsigned angle, float* gains);

// Replaces the only CALL to Snd_AngleToGains (cdecl, caller pops). Runs on the sound library's thread.
void __cdecl Hook_AngleToGains(unsigned angle, float* gains) {
    InterlockedIncrement(&g_sectorCount[((angle & 0xFFFF) + 0x1000) >> 13 & 7]);
    reinterpret_cast<AngleToGainsFn>(snd::kAngleToGains)(angle, gains);
}

void LogDirections() {
    LONG n[kNumSectors];
    LONG total = 0;
    for (int i = 0; i < kNumSectors; i++) {
        n[i] = InterlockedExchange(&g_sectorCount[i], 0);
        total += n[i];
    }
    const LONG ownMarks = InterlockedExchange(&g_ownMarks, 0);
    const LONG ownCar = InterlockedExchange(&g_ownCarMatches, 0);
    if (total == 0) return;
    char line[320];
    int len = snprintf(line, sizeof(line), "Sound directions since last report (%ld pan updates):", total);
    for (int i = 0; i < kNumSectors && len < static_cast<int>(sizeof(line)); i++)
        len += snprintf(line + len, sizeof(line) - len, " %s %ld%%", kSectorNames[i], n[i] * 100 / total);
    Log("%s; own engine voice marks: %ld; other own-car sound updates placed by direction: %ld", line, ownMarks,
        ownCar);
}

// ---- Speaker test -------------------------------------------------------------

typedef void(__cdecl* SetChannelGainFn)(int voice, int channel, float gain);

const int kMaxVoices = 256;

// ---- Own engine placement ---------------------------------------------------------
//
// EngineFront % of the engine's power over FL/C/FR ("engine bay"), the rest over RL/RR ("exhaust"). Engine voices are
// marked at the engine's 3 Snd_SetAzimuth CALLs (valid 0.5 s).

const DWORD kOwnMarkValidMs = 500;
volatile DWORD g_ownTick[kMaxVoices];  // GetTickCount() of the last mark, 0 = never
float g_ownGains[8];
float g_ownCarGains[8];                // the same blend without EngineVolume (other own-car sounds)
int g_ownGainsSlots = -1;              // output slot count g_ownGains was computed for

// The engine's latest directions (written on the game thread in Hook_OwnEngineAzimuth, read on the sound thread).
// Other own-car sounds (turbo, ...) get their direction from the same car position, so it matches the engine's.
const int kOwnAngleHistory = 4;
const DWORD kOwnAngleValidMs = 250;
const int kOwnAngleTolerance = 0x100;  // 1.4 deg
const int kAheadSector = 0x2000;       // engine direction within +-45 deg of ahead: chase view, no matching
volatile LONG g_ownAngle[kOwnAngleHistory];
volatile DWORD g_ownAngleTick[kOwnAngleHistory];
volatile LONG g_ownAngleNext;
bool g_ownCarNow[kMaxVoices];          // per voice, decided at its channel 0 (sound thread only)

typedef int(__cdecl* SetAzimuthFn)(int handle, int azimuth, int spread);

void MarkOwnVoice(int i, DWORD now) {
    g_ownTick[i] = now ? now : 1;
    InterlockedIncrement(&g_ownMarks);
}

// Marks the voices of one sound handle (the voice it names plus the voices sharing its group byte).
void MarkOwnVoices(int handle) {
    if (handle < 0) return;
    const int count = *reinterpret_cast<const unsigned short*>(snd::kVoiceCount);
    const unsigned char* voices = *reinterpret_cast<unsigned char* const*>(snd::kVoices);
    const int idx = handle & 0xFF;
    if (!voices || idx >= count || count > kMaxVoices) return;
    const unsigned char* v = voices + idx * snd::kVoiceSize;
    if (ReadAt<int>(v, snd::kVoiceHandle) != handle || !v[snd::kVoiceActive]) return;
    const DWORD now = GetTickCount();
    MarkOwnVoice(idx, now);
    const unsigned char group = v[snd::kVoiceGroup];
    if (!group) return;
    for (int i = 0; i < count; i++) {
        const unsigned char* w = voices + i * snd::kVoiceSize;
        if (i != idx && w[snd::kVoiceGroup] == group && w[snd::kVoiceActive] == 1 && ReadAt<int>(w, snd::kVoiceHandle) >= 0)
            MarkOwnVoice(i, now);
    }
}

// Replaces the player engine's 3 CALLs to Snd_SetAzimuth (cdecl, caller pops; the callers push 2 args, the
// function reads a 3rd, so all 3 stack slots are passed on unchanged). Marks first, so this update already uses
// the placement.
int __cdecl Hook_OwnEngineAzimuth(int handle, int azimuth, int spread) {
    if (g_ownEngine) MarkOwnVoices(handle);
    const int result = reinterpret_cast<SetAzimuthFn>(snd::kSetAzimuth)(handle, azimuth, spread);
    if (g_ownEngine && g_ownAll && handle >= 0) {
        const int count = *reinterpret_cast<const unsigned short*>(snd::kVoiceCount);
        const unsigned char* voices = *reinterpret_cast<unsigned char* const*>(snd::kVoices);
        const int idx = handle & 0xFF;
        if (voices && idx < count && count <= kMaxVoices) {
            const unsigned char* v = voices + idx * snd::kVoiceSize;
            if (ReadAt<int>(v, snd::kVoiceHandle) == handle) {
                const LONG slot = (InterlockedIncrement(&g_ownAngleNext) & 0x7FFFFFFF) % kOwnAngleHistory;
                g_ownAngle[slot] = ReadAt<unsigned short>(v, snd::kVoiceAngle);
                g_ownAngleTick[slot] = GetTickCount();
            }
        }
    }
    return result;
}

bool IsOwnEngineVoice(int voice);

// ---- Turbo blow-off volume ------------------------------------------------------------

typedef int(__thiscall* PlayOneShotFn)(void* obj, int a, int b, int c, int d, int id);
int g_blowOffLogged = 0;

typedef int(__thiscall* TurboSpoolFn)(void* obj, uint32_t dt);
bool g_spoolLogged = false;

// Replaces both CALLs to CarSfxTurbo_UpdateSpool (ECX = turbo sound object, one stack arg, RET 4). If the spool sound
// was playing, the call wrote a fresh volume to +0xE4: scale it.
int __fastcall Hook_TurboSpool(void* objPtr, void* /*edx*/, uint32_t dt) {
    unsigned char* obj = static_cast<unsigned char*>(objPtr);
    const bool playing = ReadAt<uintptr_t>(obj, snd::kTurboSpoolSound) != 0;
    const int result = reinterpret_cast<TurboSpoolFn>(snd::kTurboSpool)(objPtr, dt);
    if (!playing) return result;
    const int game = ReadAt<int>(obj, snd::kTurboSpoolVol);
    long long v = static_cast<long long>(game) * g_spoolVolume / 100;
    if (v < 0) v = 0;
    if (v > 0xFFFF) v = 0xFFFF;
    const int scaled = static_cast<int>(v);
    memcpy(obj + snd::kTurboSpoolVol, &scaled, sizeof(scaled));
    if (g_detailedLog && !g_spoolLogged && game > 8000) {
        g_spoolLogged = true;
        Log("Turbo spool: volume %d -> %d (%d%%; logged once, at the first strong boost).", game, scaled, g_spoolVolume);
    }
    return result;
}

// Replaces the turbo's only CALL to SndBase_PlayOneShot (ECX = turbo sound object, 5 stack args, RET 0x14). When it
// actually started a blow-off (no sound before, one after), scales the volume the game stored for it.
int __fastcall Hook_BlowOff(void* objPtr, void* /*edx*/, int a, int b, int c, int d, int id) {
    unsigned char* obj = static_cast<unsigned char*>(objPtr);
    const uintptr_t before = ReadAt<uintptr_t>(obj, snd::kTurboSound);
    const int result = reinterpret_cast<PlayOneShotFn>(snd::kPlayOneShot)(objPtr, a, b, c, d, id);
    if (before || !ReadAt<uintptr_t>(obj, snd::kTurboSound)) return result;
    const int game = ReadAt<int>(obj, snd::kTurboBlowOffVol);
    long long v = static_cast<long long>(game) * g_blowOffVolume / 100;
    if (v < 0) v = 0;
    if (v > 0xFFFF) v = 0xFFFF;  // the update multiplies it by the master volume (<= 0x7FFF) in 32 bits
    const int scaled = static_cast<int>(v);
    memcpy(obj + snd::kTurboBlowOffVol, &scaled, sizeof(scaled));
    if (g_detailedLog && g_blowOffLogged < 20) {
        g_blowOffLogged++;
        Log("Turbo blow-off (variant %d): volume %d -> %d (%d%%; the game caps the result at full volume, 32767 "
            "after its master volume).", ReadAt<int>(obj, snd::kTurboVariant), game, scaled, g_blowOffVolume);
    }
    return result;
}

// A mono voice that isn't the engine but points where the engine points (in an in-car view) is another sound of the
// own car.
bool IsOwnCarByAngle(int voice, const unsigned char* v) {
    if (!g_ownAll || v[snd::kVoiceChannels] != 1 || v[snd::kVoiceLfeOnly] || IsOwnEngineVoice(voice)) return false;
    const int angle = ReadAt<unsigned short>(v, snd::kVoiceAngle);
    const DWORD now = GetTickCount();
    for (int i = 0; i < kOwnAngleHistory; i++) {
        const DWORD t = g_ownAngleTick[i];
        if (!t || now - t > kOwnAngleValidMs) continue;
        const int e = static_cast<short>(static_cast<unsigned short>(g_ownAngle[i]));
        if (e > -kAheadSector && e < kAheadSector) continue;
        const int d = static_cast<short>(static_cast<unsigned short>(angle - e));
        if (d >= -kOwnAngleTolerance && d <= kOwnAngleTolerance) return true;
    }
    return false;
}

bool IsOwnEngineVoice(int voice) {
    const DWORD t = g_ownTick[voice];
    return t && GetTickCount() - t < kOwnMarkValidMs;
}

// The fixed front/rear blend for the current output layout, from the game's own pan (power-weighted average of
// the speaker directions, loudness matched to a sound straight ahead).
void ComputeOwnGains(int slots) {
    static const unsigned kFront[] = {0xE000, 0x0000, 0x2000};
    static const unsigned kRear[] = {0xA000, 0x6000};
    const AngleToGainsFn angleToGains = reinterpret_cast<AngleToGainsFn>(snd::kAngleToGains);
    float tmp[8], pf[8] = {}, pr[8] = {}, ref[8] = {};
    const float centre = g_engineCentre / 100.0f;
    const float weight[3] = {(1.0f - centre) / 2.0f, centre, (1.0f - centre) / 2.0f};  // front left, center, right
    for (int i = 0; i < 3; i++) {
        for (float& g : tmp) g = 0.0f;
        angleToGains(kFront[i], tmp);
        for (int c = 0; c < 8; c++) pf[c] += tmp[c] * tmp[c] * weight[i];
    }
    for (unsigned a : kRear) {
        for (float& g : tmp) g = 0.0f;
        angleToGains(a, tmp);
        for (int c = 0; c < 8; c++) pr[c] += tmp[c] * tmp[c] / 2.0f;
    }
    angleToGains(0, ref);
    const float f = g_engineFront / 100.0f;
    float sumP = 0.0f, sumRef = 0.0f, p[8];
    for (int c = 0; c < 8; c++) {
        p[c] = f * pf[c] + (1.0f - f) * pr[c];
        sumP += p[c];
        sumRef += ref[c] * ref[c];
    }
    const float norm = sumP > 0.0f ? sumRef / sumP : 0.0f;
    for (int c = 0; c < 8; c++) {
        g_ownCarGains[c] = c < slots ? sqrtf(p[c] * norm) : 0.0f;
        g_ownGains[c] = g_ownCarGains[c] * g_engineVolume;
    }
    g_ownGainsSlots = slots;
}

// Replaces the only CALL to Snd_SetVoiceChannelGain (cdecl, caller pops). Runs on the sound library's thread.
// Speaker test (in gear while driving): the voice's whole volume goes to one mixer channel, the others get 0.
void __cdecl Hook_SetChannelGain(int voice, int channel, float gain) {
    const LONG target = g_testChannel;
    const bool inRange = voice >= 0 && voice < kMaxVoices && channel >= 0 && channel < 8;
    const int slots = *reinterpret_cast<const unsigned char*>(snd::kOutputSlots);
    const bool own = inRange && g_ownEngine && slots >= 4 && IsOwnEngineVoice(voice);
    const unsigned char* v = inRange ? *reinterpret_cast<unsigned char* const*>(snd::kVoices) + voice * snd::kVoiceSize
                                     : nullptr;
    if (inRange && channel == 0) {
        g_ownCarNow[voice] = !own && g_ownEngine && slots >= 4 && IsOwnCarByAngle(voice, v);
        if (g_ownCarNow[voice]) InterlockedIncrement(&g_ownCarMatches);
    }
    const bool ownCar = inRange && !own && g_ownCarNow[voice];
    const bool balance = inRange && g_balanceOn && slots == 6 && target < 0;
    if (target >= 0 || own || ownCar || balance) {
        const float volume = static_cast<float>(static_cast<signed char>(v[snd::kVoiceVolByte])) *
                             ReadAt<float>(v, snd::kVoiceVolume) * (1.0f / 127.0f);
        if (target >= 0) {
            gain = channel == target ? volume : 0.0f;
        } else if (own || ownCar) {
            if (g_ownGainsSlots != slots) ComputeOwnGains(slots);
            gain = volume * (own ? g_ownGains[channel] : g_ownCarGains[channel]);
        }
        if (balance) {
            // [Balance]: speaker trims for every sound, plus a share of every normal sound on the subwoofer
            // (the game sends nothing there except its LFE-only sounds).
            if (channel == snd::kCh51Lfe && !v[snd::kVoiceLfeOnly] && g_subSend > 0.0f) gain += volume * g_subSend;
            gain *= g_trim[channel];
        }
    }
    reinterpret_cast<SetChannelGainFn>(snd::kSetChannelGain)(voice, channel, gain);
}

// Gear (car+0x434: 0 R, 1 N, 2 = 1st ...) -> 5.1 mixer channel, -1 = normal mix.
int GearToChannel(int gear) {
    switch (gear) {
        case 2: return snd::kCh51FrontLeft;
        case 3: return snd::kCh51Centre;
        case 4: return snd::kCh51FrontRight;
        case 5: return snd::kCh51RearRight;
        case 6: return snd::kCh51RearLeft;
        case 7: return snd::kCh51Lfe;
    }
    return -1;
}

const char* ChannelName(int ch) {
    switch (ch) {
        case snd::kCh51FrontLeft: return "front left";
        case snd::kCh51Centre: return "center";
        case snd::kCh51FrontRight: return "front right";
        case snd::kCh51RearRight: return "rear right";
        case snd::kCh51RearLeft: return "rear left";
        case snd::kCh51Lfe: return "subwoofer";
    }
    return "normal mix";
}

// Player 1's gear while driving (racing or drag), or -1 (menus, loading, pause, no player).
int ReadDrivingGear() {
    if (!IsReadable(player::kCount, 4)) return -1;
    const int count = *reinterpret_cast<const int*>(player::kCount);
    if (count <= 0 || count > 2 || !IsReadable(player::kList, 4)) return -1;
    const unsigned char* p1 = *reinterpret_cast<unsigned char* const*>(player::kList);
    if (!p1 || !IsReadable(reinterpret_cast<uintptr_t>(p1), player::kMode + 4)) return -1;
    const int mode = ReadAt<int>(p1, player::kMode);
    if (mode != 2 && mode != 3) return -1;  // CAR_RACING (also free roam) or DRAG_RACING
    const unsigned char* car = ReadAt<unsigned char*>(p1, player::kCar);
    if (!car || !IsReadable(reinterpret_cast<uintptr_t>(car), gearbox::car::kGear + 4)) return -1;
    return ReadAt<int>(car, gearbox::car::kGear);
}

// ---- Sound dump (diagnostic, read-only, plugin thread) ----------------------------

// Player 1's PLAYER_MODE (0 NONE, 1 PAUSEMENU, 2 CAR_RACING, 3 DRAG_RACING, 8 PRERACE, 9 FINISH_RACE), -1 = no player.
int ReadPlayerMode() {
    if (!IsReadable(player::kCount, 4)) return -1;
    const int count = *reinterpret_cast<const int*>(player::kCount);
    if (count <= 0 || count > 2 || !IsReadable(player::kList, 4)) return -1;
    const unsigned char* p1 = *reinterpret_cast<unsigned char* const*>(player::kList);
    if (!p1 || !IsReadable(reinterpret_cast<uintptr_t>(p1), player::kMode + 4)) return -1;
    return ReadAt<int>(p1, player::kMode);
}

const char* PlayerModeName(int mode) {
    switch (mode) {
        case -1: return "no player (menus/loading)";
        case 0: return "none";
        case 1: return "pause menu";
        case 2: return "racing/free roam";
        case 3: return "drag racing";
        case 8: return "pre-race";
        case 9: return "finish race";
    }
    return "other";
}

// Mixer channel names (mixer order, before the output remap), by output channel count incl. LFE.
const char* MixChannelName(int outChannels, int ch) {
    static const char* const k6[] = {"C", "FR", "RR", "RL", "FL", "LFE"};
    static const char* const k4[] = {"FR", "RR", "RL", "FL"};
    static const char* const k2[] = {"R", "L"};
    if (outChannels == 6 && ch < 6) return k6[ch];
    if (outChannels == 4 && ch < 4) return k4[ch];
    if (outChannels == 2 && ch < 2) return k2[ch];
    return "?";
}

void DumpSound(const char* why) {
    const int running = *reinterpret_cast<const unsigned char*>(snd::kRunning);
    const int outCh = *reinterpret_cast<const unsigned char*>(snd::kOutChannels);
    if (!running || outCh <= 0 || outCh > 8) {
        Log("Sound dump (%s): sound system not running.", why);
        return;
    }
    // Level per mixer channel: RMS of the current mix block, sampled 10 times over 0.5 s (peak and average).
    double sum[8] = {}, peak[8] = {};
    for (int n = 0; n < 10; n++) {
        for (int ch = 0; ch < outCh; ch++) {
            const float* buf = reinterpret_cast<float* const*>(snd::kChannelBuffers)[ch];
            if (!buf || !IsReadable(reinterpret_cast<uintptr_t>(buf), 256 * sizeof(float))) continue;
            double e = 0;
            for (int i = 0; i < 256; i++) {
                const double x = static_cast<double>(buf[i]) - snd::kMixBias;
                e += x * x;
            }
            const double rms = sqrt(e / 256) * 100.0 / 32768.0;  // % of 16-bit full scale
            sum[ch] += rms;
            if (rms > peak[ch]) peak[ch] = rms;
        }
        Sleep(50);
    }
    char line[400];
    int len = snprintf(line, sizeof(line), "Sound dump (%s): channel level %% avg/peak:", why);
    for (int ch = 0; ch < outCh && len < static_cast<int>(sizeof(line)); ch++)
        len += snprintf(line + len, sizeof(line) - len, " %s %.2f/%.2f", MixChannelName(outCh, ch), sum[ch] / 10, peak[ch]);
    Log("%s", line);

    // Playing voices (mixer state 2) with their gains per channel and their strongest FX send.
    const int count = *reinterpret_cast<const unsigned char*>(snd::kMixVoiceCount);
    const unsigned char* recs = *reinterpret_cast<unsigned char* const*>(snd::kMixVoices);
    const unsigned char* voices = *reinterpret_cast<unsigned char* const*>(snd::kVoices);
    int buses = *reinterpret_cast<const int*>(snd::kFxBusCount);
    if (buses < 0 || buses > 8) buses = 0;
    const int* busActive = *reinterpret_cast<int* const*>(snd::kFxBusActive);
    if (!recs || !voices || !IsReadable(reinterpret_cast<uintptr_t>(recs), count * snd::kMixVoiceSize) ||
        !IsReadable(reinterpret_cast<uintptr_t>(voices), count * snd::kVoiceSize)) {
        Log("  voice tables not readable.");
        return;
    }
    int playing = 0;
    for (int i = 0; i < count; i++) {
        const unsigned char* r = recs + i * snd::kMixVoiceSize;
        if (r[snd::kMixState] != 2) continue;
        playing++;
        if (playing > 40) continue;
        const unsigned char* v = voices + i * snd::kVoiceSize;
        len = snprintf(line, sizeof(line), "  voice %3d: %5u Hz, %d ch, angle %04X, vol %d*%.2f%s, gains",
                       i, ReadAt<unsigned short>(v, snd::kVoiceSampleRate), v[snd::kVoiceChannels],
                       ReadAt<unsigned short>(v, snd::kVoiceAngle), static_cast<signed char>(v[snd::kVoiceVolByte]),
                       ReadAt<float>(v, snd::kVoiceVolume), v[snd::kVoiceLfeOnly] ? ", LFE only" : "");
        for (int ch = 0; ch < outCh && len < static_cast<int>(sizeof(line)); ch++)
            len += snprintf(line + len, sizeof(line) - len, " %s %.3f", MixChannelName(outCh, ch),
                            ReadAt<float>(r, snd::kMixGains + ch * 4));
        const float* sends = ReadAt<float*>(r, snd::kMixSends);
        if (sends && buses > 0 && IsReadable(reinterpret_cast<uintptr_t>(sends), buses * 4) && len < static_cast<int>(sizeof(line))) {
            len += snprintf(line + len, sizeof(line) - len, ", sends");
            for (int b = 0; b < buses && len < static_cast<int>(sizeof(line)); b++) {
                const bool active = busActive && IsReadable(reinterpret_cast<uintptr_t>(busActive + b), 4) && busActive[b];
                len += snprintf(line + len, sizeof(line) - len, " %.3f%s", sends[b], active ? "" : "(off)");
            }
        }
        Log("%s", line);
    }
    Log("  %d of %d voices playing%s.", playing, count, playing > 40 ? " (first 40 listed)" : "");
}

// ---- Settings ---------------------------------------------------------------

void LoadSettings() {
    char path[MAX_PATH];
    bf::IniPath(path);
    const bool found = bf::IniFound();
    int speakers = static_cast<int>(GetPrivateProfileIntA("Surround.Audio", "Speakers", 6, path));
    if (speakers >= 6) g_mode = snd::kMode51;
    else if (speakers >= 4) g_mode = snd::kModeQuad;
    else g_mode = snd::kModeStereo;
    g_speakerTest = GetPrivateProfileIntA("Surround.Test", "SpeakerTest", 0, path) != 0;
    g_ownEngine = GetPrivateProfileIntA("Surround.OwnCar", "OwnEnginePlacement", 1, path) != 0;
    g_engineFront = static_cast<int>(GetPrivateProfileIntA("Surround.OwnCar", "EngineFront", 60, path));
    if (g_engineFront < 0) g_engineFront = 0;
    if (g_engineFront > 100) g_engineFront = 100;
    g_engineCentre = static_cast<int>(GetPrivateProfileIntA(  // the old "Center" spelling is still read
        "Surround.OwnCar", "EngineCenter", GetPrivateProfileIntA("Surround.OwnCar", "EngineCentre", 33, path), path));
    if (g_engineCentre < 0) g_engineCentre = 0;
    if (g_engineCentre > 100) g_engineCentre = 100;
    int engineVolume = static_cast<int>(GetPrivateProfileIntA("Surround.OwnCar", "EngineVolume", 100, path));
    g_engineVolume = (engineVolume < 0 ? 0 : (engineVolume > 200 ? 200 : engineVolume)) / 100.0f;
    g_ownAll = GetPrivateProfileIntA("Surround.OwnCar", "AllOwnSounds", 1, path) != 0;
    g_blowOffVolume = static_cast<int>(GetPrivateProfileIntA("Surround.Sounds", "BlowOffVolume", 100, path));
    if (g_blowOffVolume < 0) g_blowOffVolume = 0;
    if (g_blowOffVolume > 400) g_blowOffVolume = 400;
    g_spoolVolume = static_cast<int>(GetPrivateProfileIntA("Surround.Sounds", "SpoolVolume", 100, path));
    if (g_spoolVolume < 0) g_spoolVolume = 0;
    if (g_spoolVolume > 400) g_spoolVolume = 400;
    static const char* const kTrimKeys[6] = {"Center", "FrontRight", "RearRight", "RearLeft", "FrontLeft", "Subwoofer"};
    char trims[160] = "";
    int tlen = 0;
    for (int c = 0; c < 6; c++) {  // kCh51* order
        const int old = c == 0 ? static_cast<int>(GetPrivateProfileIntA("Surround.Balance", "Centre", 100, path)) : 100;
        int t = static_cast<int>(GetPrivateProfileIntA("Surround.Balance", kTrimKeys[c], old, path));
        t = t < 0 ? 0 : (t > 200 ? 200 : t);
        g_trim[c] = t / 100.0f;
        if (t != 100) g_balanceOn = true;
        tlen += snprintf(trims + tlen, sizeof(trims) - tlen, "%s%s %d%%", c ? ", " : "", kTrimKeys[c], t);
    }
    int sub = static_cast<int>(GetPrivateProfileIntA("Surround.Balance", "SubwooferSend", 0, path));
    g_subSend = (sub < 0 ? 0 : (sub > 100 ? 100 : sub)) / 100.0f;
    if (sub > 0) g_balanceOn = true;
    g_detailedLog = GetPrivateProfileIntA("Surround.Log", "Detailed", 0, path) != 0;
    g_menuLabel = GetPrivateProfileIntA("Surround.Audio", "MenuLabel", 1, path) != 0;
    Log("Settings (%s): Speakers=%d -> %s, OwnEnginePlacement=%d, EngineFront=%d%%, "
        "SpeakerTest=%d, detailed log %d.", found ? "BayviewForever.ini" : "file not found, defaults", speakers,
        ModeName(g_mode), g_ownEngine ? 1 : 0, g_engineFront, g_speakerTest ? 1 : 0, g_detailedLog ? 1 : 0);
    Log("Settings: EngineCenter=%d%%, EngineVolume=%.0f%%; [Balance] %s, SubwooferSend %.0f%% (%s, 5.1 only).",
        g_engineCentre, g_engineVolume * 100.0f, trims, g_subSend * 100.0f, g_balanceOn ? "on" : "all 100%: off");
}

// ---- Sound state log ----------------------------------------------------------

struct SoundState {
    int running, channels, lfe, outChannels, rate, audioMode, stored;
    bool operator!=(const SoundState& o) const {
        return running != o.running || channels != o.channels || lfe != o.lfe || outChannels != o.outChannels ||
               rate != o.rate || audioMode != o.audioMode || stored != o.stored;
    }
};

SoundState ReadState() {
    SoundState s;
    s.running = *reinterpret_cast<const unsigned char*>(snd::kRunning);
    s.channels = *reinterpret_cast<const unsigned char*>(snd::kChannels);
    s.lfe = *reinterpret_cast<const unsigned char*>(snd::kLfe);
    s.outChannels = *reinterpret_cast<const unsigned char*>(snd::kOutChannels);
    s.rate = *reinterpret_cast<const unsigned short*>(snd::kRate);
    s.audioMode = *reinterpret_cast<const int*>(snd::kAudioMode);
    s.stored = *reinterpret_cast<const int*>(snd::kModeStored);
    return s;
}

void MonitorLoop() {
    SoundState last = {-1, -1, -1, -1, -1, -1, -1};
    int lastGear = -2;
    int lastMode = -2;
    DWORD lastReport = GetTickCount();
    DWORD dumpAt = 0;  // tick of a pending sound dump, 0 = none
    char dumpWhy[64] = "";
    for (;;) {
        const SoundState s = ReadState();
        const int mode = ReadPlayerMode();
        if (mode != lastMode) {
            Log("Player mode %d (%s).", mode, PlayerModeName(mode));
            if (g_detailedLog) {
                snprintf(dumpWhy, sizeof(dumpWhy), "1.5 s after %s", PlayerModeName(mode));
                dumpAt = GetTickCount() + 1500;
                if (dumpAt == 0) dumpAt = 1;
            }
            lastMode = mode;
        }
        if (dumpAt && static_cast<LONG>(GetTickCount() - dumpAt) >= 0) {
            dumpAt = 0;
            DumpSound(dumpWhy);
            if (mode == 1) {  // still paused: dump again every 10 s
                snprintf(dumpWhy, sizeof(dumpWhy), "still in the pause menu");
                dumpAt = GetTickCount() + 10000;
                if (dumpAt == 0) dumpAt = 1;
            }
        }
        const int gear = ReadDrivingGear();
        if (gear != lastGear) {
            if (lastGear >= 0 && gear < 0) LogDirections();  // left driving: report what this drive sounded like
            const bool is51 = s.channels == 5 && s.lfe == 1;
            const LONG ch = g_speakerTest && is51 && gear >= 0 ? GearToChannel(gear) : -1;
            InterlockedExchange(&g_testChannel, ch);
            if (g_speakerTest && (gear >= 0 || lastGear >= 0))
                Log("Gear %d -> %s%s.", gear, ChannelName(ch), gear < 0 ? " (not driving)" : is51 ? "" : " (output is not 5.1)");
            lastGear = gear;
        }
        if (GetTickCount() - lastReport >= kReportEvery) {
            if (g_detailedLog) LogDirections();
            lastReport = GetTickCount();
        }
        if (s != last) {
            Log("Sound state: running %d, mixer speakers %d + LFE %d, output channels %d, %d Hz, menu Audio Mode %d (%s), "
                "next init mode %d (%s).",
                s.running, s.channels, s.lfe, s.outChannels, s.rate, s.audioMode, ModeName(s.audioMode), s.stored,
                ModeName(s.stored));
            last = s;
        }
        Sleep(kPollInterval);
    }
}

DWORD WINAPI WorkerThread(LPVOID) {
    if (!ExeMd5Ok()) {
        Log("Not changing anything.");
        return 0;
    }
    if (!CodeMatches(kCodeChecks, sizeof(kCodeChecks) / sizeof(kCodeChecks[0]))) {
        Log("Not changing anything.");
        return 0;
    }
    LoadSettings();
    if (g_blowOffVolume != 100) {
        if (RedirectCall(snd::kPlayOneShotCalls[0], snd::kPlayOneShot, reinterpret_cast<const void*>(&Hook_BlowOff)))
            Log("Turbo blow-off at %d%% of the game's volume (CALL 0x%08X).", g_blowOffVolume,
                static_cast<unsigned>(snd::kPlayOneShotCalls[0]));
        else
            Log("Could not hook the turbo blow-off CALL at 0x%08X: BlowOffVolume has no effect.",
                static_cast<unsigned>(snd::kPlayOneShotCalls[0]));
    }
    if (g_spoolVolume != 100) {
        int hooked = 0;
        for (uintptr_t site : snd::kTurboSpoolCalls)
            if (RedirectCall(site, snd::kTurboSpool, reinterpret_cast<const void*>(&Hook_TurboSpool))) hooked++;
        Log("Turbo spool at %d%% of the game's volume (%d of 2 CALLs hooked).", g_spoolVolume, hooked);
    }
    if (g_mode == snd::kModeStereo) {
        Log("Speakers=2: the game's own stereo. Not changing anything.");
        MonitorLoop();
        return 0;
    }
    if (*reinterpret_cast<const unsigned char*>(snd::kRunning))
        Log("Note: the sound system was already running when the plugin got ready; surround starts at the first world load.");
    for (uintptr_t site : snd::kSetOutputModeCallSites) {
        if (!RedirectCall(site, snd::kSetOutputMode, reinterpret_cast<const void*>(&Hook_SetOutputMode))) {
            Log("STOP: could not hook the output mode CALL at 0x%08X.", static_cast<unsigned>(site));
            return 0;  // CodeMatches checked all 3 sites first, so this only happens if memory protection fails
        }
    }
    Log("Hooked the 3 output mode CALLs: stereo becomes %s.", ModeName(g_mode));
    if (g_menuLabel) {
        if (RedirectCall(kAudioModeTextCall, kSetTextByHash, reinterpret_cast<const void*>(&Hook_AudioModeText)))
            Log("Options menu: Audio Mode shows the running layout (\"5.1 Surround\" / \"Quad\") instead of \"Stereo\".");
        else
            Log("Could not hook the Audio Mode text CALL at 0x%08X: the menu keeps saying \"Stereo\".",
                static_cast<unsigned>(kAudioModeTextCall));
    }
    if (!RedirectCall(snd::kAngleToGainsCall, snd::kAngleToGains, reinterpret_cast<const void*>(&Hook_AngleToGains)))
        Log("Could not hook the pan CALL at 0x%08X: no direction statistics.", static_cast<unsigned>(snd::kAngleToGainsCall));
    else
        Log("Hooked the pan CALL at 0x%08X: direction statistics after each drive%s.",
            static_cast<unsigned>(snd::kAngleToGainsCall), g_detailedLog ? " and every 20 s" : "");
    if (g_ownEngine) {
        int hooked = 0;
        for (uintptr_t site : snd::kOwnEngineAzimuthCalls)
            if (RedirectCall(site, snd::kSetAzimuth, reinterpret_cast<const void*>(&Hook_OwnEngineAzimuth))) hooked++;
        if (hooked == 3)
            Log("Own engine placement ON: %d%% front (FL C FR), %d%% rear (RL RR)%s.", g_engineFront, 100 - g_engineFront,
                g_ownAll ? "; other own-car sounds (turbo ...) get the same blend in in-car views" : "");
        else
            Log("Own engine placement: only %d of 3 CALLs hooked (memory protection?).", hooked);
    }
    if (g_speakerTest || g_ownEngine || g_balanceOn) {
        if (!RedirectCall(snd::kSetChannelGainCall, snd::kSetChannelGain, reinterpret_cast<const void*>(&Hook_SetChannelGain))) {
            Log("Could not hook the channel gain CALL at 0x%08X: no own-car placement / balance / speaker test.",
                static_cast<unsigned>(snd::kSetChannelGainCall));
        } else {
            if (g_speakerTest)
                Log("Speaker test ON: 1st front left, 2nd center, 3rd front right, 4th rear right, 5th rear left, "
                    "6th subwoofer; N/R normal mix.");
        }
    }
    MonitorLoop();
    return 0;
}

void Init() {
    Log("==== Surround started ====");
    // CryptoAPI must not run inside DllMain, so the MD5 check (and everything after it) runs on its own thread.
    HANDLE thread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
}

}  // namespace

void bf::StartSurround() { Init(); }
