// Shared addresses and helpers for NFSU2 plugins (speed2.exe v1.2 only).
// Every address here is documented, with how it was verified, in docs/addresses.md.
//
// BayviewForever (-DBAYVIEW_FOREVER): every feature .cpp includes this with its own BF_TAG, so all helpers below
// live in an unnamed namespace (each .cpp gets its own copy, and Log() writes that file's tag). Log() then goes to
// BayviewForever.log and ExeMd5Ok() uses the one shared MD5 check.

#pragma once

#include <windows.h>
#include <wincrypt.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#ifdef BAYVIEW_FOREVER
#include "bayview_forever.h"
#ifndef BF_TAG
#error "Define BF_TAG (the feature's tag in BayviewForever.log) before including nfsu2.h"
#endif
#endif

namespace nfsu2 {
#ifdef BAYVIEW_FOREVER
namespace {
#endif

// v1.2 exes with identical code and data (only the PE header differs): ours, and HOODLUM's No-CD.
const char* const kExpectedMd5[] = {"878678327c1aa744f2fe248cba081c86", "6445adbf4e8d64be51e96a69bba168a4"};

const uintptr_t kPlayer1Profile  = 0x0083A9E0;  // player 1's profile block; its car DB is at +0x3B0
const uintptr_t kCareerSlots     = 0x008406E4;  // g_CarDB_CareerSlots (g_CarDB + 0x5954)
const uintptr_t kCareerSlotCount = 0x00842EBC;  // g_CarDB_CareerSlotCount (normally 5)
const uintptr_t kCash            = 0x00861E74;  // g_Career_Cash
const uintptr_t kCurrentSlotHash = 0x00863480;  // g_Career_CurrentCarSlotHash
const size_t    kSlotSize        = 0x7F8;
const int       kMaxSlots        = 5;

// Offsets inside a career slot. See docs/structs.md.
namespace slot {
const size_t kVtable    = 0x000;  // code address; never copy or save
const size_t kHash      = 0x008;  // StringHash("CAREER_SLOT_n"); identifies the slot itself
const size_t kInUse     = 0x010;
const size_t kKind      = 0x014;  // 4 = career slot
const size_t kCarModel  = 0x020;  // StringHash of the car's CARS\ folder name
const size_t kSpent     = 0x7F0;  // cash spent on this car's parts
const size_t kLocked    = 0x7F4;
const size_t kOccupied  = 0x7F5;
}  // namespace slot

inline unsigned char* SlotPtr(int n) {
    return reinterpret_cast<unsigned char*>(kCareerSlots + n * kSlotSize);
}

template <typename T>
T ReadAt(const unsigned char* base, size_t off) {
    T v;
    memcpy(&v, base + off, sizeof(T));
    return v;
}

// Same hash the game uses (StringHash at 0x0043DB50).
inline uint32_t StringHash(const char* s) {
    uint32_t h = 0xFFFFFFFF;
    while (*s) h = h * 33 + static_cast<unsigned char>(*s++);
    return h;
}

// Folder names in CARS\ (the non-car folders like WHEELS and SPOILER are left out).
inline const char* CarName(uint32_t modelHash) {
    static const char* const kCarNames[] = {
        "240SX", "3000GT", "350Z", "4DR_SEDAN", "4DR_SEDAN02", "A3", "AMBULANCE", "BUS", "CELICA", "CIVIC",
        "COROLLA", "COUPE", "ECLIPSE", "ESCALADE", "FIRETRUCK", "FOCUS", "G35", "GOLF", "GTO", "HATCHBACK",
        "HATCHBACK02", "HUMMER", "IMPREZA", "IMPREZAWRX", "IS300", "LANCER", "LANCEREVO8", "MIATA", "MINIVAN",
        "MUSTANGGT", "NAVIGATOR", "NEON", "PANELVAN", "PARCELVAN", "PEUGOT", "PICKUP", "RSX", "RX7", "RX8",
        "S2000", "SENTRA", "SKYLINE", "SUPRA", "SUV", "TAXI", "TAXI02", "TIBURON", "TT",
    };
    if (modelHash == 0) return "-";
    for (const char* name : kCarNames)
        if (StringHash(name) == modelHash) return name;
    return "unknown";
}

// ---- Career events (Goal 2) ---------------------------------------------
// See docs/addresses.md and docs/structs.md, section "Career events".
namespace ev {
// Static tables loaded from GLOBALB by GCareer_LoadChunk (0x00512C00). Each is a pointer + count.
const uintptr_t kRaceTablePtr     = 0x008383E0;  // g_GRaces: GRace records (kRaceSize each)
const uintptr_t kRaceCount        = 0x008383E4;  // g_GRaceCount
const uintptr_t kShowcaseCount    = 0x00838408;  // g_GShowcaseCount
const uintptr_t kShowcaseTablePtr = 0x0083840C;  // g_GShowcases (kShowcaseSize each)
const uintptr_t kStageCount       = 0x00838418;  // stage data count (6)
const uintptr_t kStageTablePtr    = 0x0083841C;  // stage data (kStageDataSize each)
const uintptr_t kStringsPtr       = 0x00838428;  // g_GCareerStrings
const uintptr_t kStringsSize      = 0x0083842C;
const size_t    kRaceSize         = 0x88;
const size_t    kShowcaseSize     = 0x40;
const size_t    kStageDataSize    = 0x50;

// Per-profile career state in g_Career (0x0085AD40).
const uintptr_t kActiveEvents     = 0x00861F08;  // g_Career+0x71C8: GRace pointers of the events on the map
const uintptr_t kStageRecords     = 0x00862018;  // g_Career+0x72D8: stage records, kStageRecordSize each
const uintptr_t kCurrentStage     = 0x00862838;  // g_Career_CurrentStage (+0x7AF8); 6 after the career is beaten
const uintptr_t kStatusCount      = 0x0086283C;  // g_Career_EventStatusCount (+0x7AFC)
const uintptr_t kStatusList       = 0x00862840;  // event status entries (+0x7B00), kStatusEntrySize each
const uintptr_t kEnteredRace      = 0x0086345C;  // g_Career+0x871C: race of the marker the player drove into (or 0)
const size_t    kStageRecordSize  = 0x104;
const size_t    kStatusEntrySize  = 8;
const int       kMaxActive        = 64;
const int       kMaxStatus        = 384;  // 0x180, see maybe_Career_Reset
const int       kNumStageRecords  = 8;    // only 0-5 are real stages
const uintptr_t kCareer            = 0x0085AD40;  // g_Career
const size_t    kCareerEventList   = 0x7148;      // event-list sub-object (ECX for EventList_AddActive); its +0x80 = kActiveEvents
const size_t    kCareerSpecialMode = 0x0C08;      // non-zero -> the game shows no events at all (mode unknown)
const size_t    kCareerStage5Data  = 0x78EC;      // stage record 5's data pointer (read by Career_IsFinished)
const uintptr_t kCurrentCarBought  = 0x0086347D;  // g_Career+0x873D: 1 once the player owns a career car

// Functions (verified in Ghidra; see docs/addresses.md).
const uintptr_t kRebuildActiveEvents  = 0x00527D30;  // Career_RebuildActiveEvents: __fastcall(career), no stack args
const uintptr_t kRebuildCallSites[]   = {0x005291E4, 0x0053331B, 0x005417E0};  // all 3 CALLs, ECX = career
const uintptr_t kOnRaceWon            = 0x00541420;  // Career_OnRaceWon: __thiscall(career, raceKey), RET 4
const uintptr_t kOnRaceWonCallSites[] = {0x0054C9A9, 0x005416EA};  // end-of-race handler + "win a random race" (debug?)
const uintptr_t kOnRaceLost           = 0x00528500;  // Career_OnRaceLost: __thiscall(career, raceKey), RET 4
const uintptr_t kOnRaceLostCallSites[] = {0x0054C9BC};  // the only CALL, in Career_ApplyRaceResult (player did not win)
const uintptr_t kEventListAddActive   = 0x005007A0;  // EventList_AddActive: __thiscall(career+0x7148, race), RET 4
const uintptr_t kRaceSessionInit      = 0x00532EA0;  // RaceSession_Init: __thiscall(career+0xD24, race), RET 4
const uintptr_t kRaceSessionInitCallSites[] = {0x00541190, 0x00541101};  // Career_StartRace + prologue (DDAY) start
const size_t    kCareerRaceSession    = 0x0D24;      // race session object inside g_Career
namespace session {  // RaceSession byte flags (offsets from g_Career+0xD24)
const size_t kCounts = 1;  // +0xD25: result is applied at the finish
const size_t kPays   = 2;  // +0xD26: Career_OnRaceWon pays the payout (0 if the race was already won)
const size_t kReplay = 4;  // +0xD28: 1 = race already won -> Career_ApplyRaceResult ignores the result
}  // namespace session
const uintptr_t kCarTypesFindByName   = 0x00610130;  // CarTypes_FindByNameHash: __cdecl(modelHash) -> car type entry or 0
const uintptr_t kCarTypesTablePtr     = 0x008A1CCC;  // pointer to the car type table (kCarTypeSize each)
const size_t    kCarTypeSize          = 0x890;
const size_t    kCarTypeIndex         = 0x840;       // entry -> its index in the table
const size_t    kCarTypeIsSUV         = 0x88A;       // byte: SUV races allowed with this car
const uint32_t  kFinalRaceKey         = 0x04CCE3E1;  // S5_CIRCUIT_36 (Caleb)

namespace race {  // GRace record offsets
const size_t kName     = 0x00;  // u16 offset into g_GCareerStrings
const size_t kIntro    = 0x02;  // u16 string offset: movie before the race (0 = none)
const size_t kOutro    = 0x04;  // u16 string offset: movie after winning (read at 0x00513A71)
const size_t kTriggerName = 0x06;  // u16 string offset: EventTrigger name (its StringHash is kTrigger)
const size_t kKey      = 0x08;  // hash of the name; used by the status list
const size_t kIsSUV    = 0x0D;
const size_t kBehavior = 0x0F;  // 0 Circuit, 1 Sprint, 2 StreetX, 3 OpenWorld, 4 Drag, 5 Drift
const size_t kLegs     = 0x18;  // 4 x {u16 track, u8 reverse, u8 laps}
const size_t kTrigger  = 0x28;  // map trigger hash (where the marker is)
const size_t kForcedCar = 0x2C; // car the player must drive (0 = own car); only DDAY_EVENT_A uses it
const size_t kCash     = 0x30;
const size_t kIcon     = 0x34;  // 0 none, 1 sponsor, 2 URL, 3 regular
const size_t kDriveToGPS = 0x35;  // IsDriveToGPS
const size_t kStage    = 0x37;  // BelongsToStage
const size_t kOpponents   = 0x40;  // 5 opponents, kOpponentSize each (read by 0x005260B0, the race's AI setup)
const size_t kOpponentSize = 0x0C;
const size_t kNumOpp   = 0x7C;
const size_t kNumLegs  = 0x7E;
const size_t kHidden   = 0x7F;
}  // namespace race

namespace opp {  // offsets inside one opponent (race + kOpponents + n * kOpponentSize)
const size_t kName    = 0x0;  // u16 string offset
const size_t kMode    = 0x2;  // car level: 0 = fixed (kLevel), 3 = from the CURRENT stage's progress (stage 6 -> stage 0 -> level 0!)
const size_t kLevel   = 0x3;  // signed byte, used when kMode == 0; scale 0..10 (stage 5 runs 8..10)
const size_t kPreset  = 0x4;  // preset ride hash; StringHash("RANDOM") = random car of that level
const size_t kSkill   = 0x8;  // 3 bytes: Easy, Medium, Hard (index = difficulty setting)
const size_t kCatchUp = 0xB;
}  // namespace opp

namespace stagedata {  // GCareerStage offsets (static data)
const size_t kId        = 0x00;
const size_t kLastEvent = 0x28;  // key of the race that ends the stage
const size_t kMaxShown  = 0x30;  // 5 bytes: circuit, drag, streetX, drift, sprint
}  // namespace stagedata

namespace stagerec {  // per-profile stage record offsets
const size_t kComplete = 0xFC;
const size_t kData     = 0x100;  // pointer to the stage data
}  // namespace stagerec

namespace status {  // event status entry offsets
const size_t kKey        = 0;
const size_t kDiscovered = 4;
const size_t kState      = 6;  // 1 = won, 2 = race in progress, 4 = lost, 5 = world shop found
}  // namespace status

// Tracks and race markers (docs/addresses.md "Tracks, start lines and race markers").
const uintptr_t kTrackInfosPtr     = 0x00883DA0;  // g_TrackInfos: track info records (GLOBALB chunk 0x34201), kTrackInfoSize each
const uintptr_t kTrackInfoCount    = 0x00883DA4;  // g_TrackInfoCount (131 stock)
const size_t    kTrackInfoSize     = 0x128;
const uintptr_t kWorldTriggerFind  = 0x004FFC50;  // WorldTrigger_FindByHash: __cdecl(hash) -> world trigger record or 0
namespace trackinfo {  // track info record offsets
const size_t kReverseExists = 0x86;  // byte
const size_t kId            = 0x8A;  // u16 track ID
const size_t kLength        = 0x98;  // u32 meters per lap
}  // namespace trackinfo
namespace trigger {  // world trigger record offsets
const size_t kX    = 0x04;  // float, world meters
const size_t kY    = 0x08;
const size_t kHash = 0x30;
}  // namespace trigger

// Money pickups (docs/addresses.md "Money pickups").
const uintptr_t kMoneyPickupsPtr    = 0x00838430;  // g_MoneyPickups: records (GCareer sub-chunk 0x34A1E), kMoneyPickupSize each
const uintptr_t kMoneyPickupCount   = 0x00838434;  // g_MoneyPickupCount (59 stock)
const size_t    kMoneyPickupSize    = 12;
const uintptr_t kBuildLiveMoneyPickups = 0x005141B0;  // Career_BuildLiveMoneyPickups: __fastcall(g_Career), plain RET
const size_t    kCareerPickupFlags  = 0x8768;      // g_Career+0x8768 (0x008634A8): collected flag per pickup index (saved)
namespace pickup {  // money pickup record offsets
const size_t kCash      = 0x0;  // u16, paid by MoneyPickup_Collect
const size_t kCondition = 0x2;  // 0 always, 1 = only while current stage > kValue, 2 = only once race kValue is won
const size_t kIndex     = 0x3;  // collected flag index
const size_t kValue     = 0x4;  // i32 stage or race key
const size_t kTrigger   = 0x8;  // world trigger hash (type 19)
}  // namespace pickup
}  // namespace ev

// ---- Logging -------------------------------------------------------------

#ifdef BAYVIEW_FOREVER
// One line in BayviewForever.log, tagged with this feature's BF_TAG (main.cpp opens the file once per game run).
inline void Log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    bf::LogV(BF_TAG, fmt, args);
    va_end(args);
}
#else
inline FILE* g_log = nullptr;

inline void Log(const char* fmt, ...) {
    if (!g_log) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_log, "[%02d:%02d:%02d] ", t.wHour, t.wMinute, t.wSecond);
    va_list args;
    va_start(args, fmt);
    vfprintf(g_log, fmt, args);
    va_end(args);
    fputc('\n', g_log);
    fflush(g_log);  // so the log survives a crash
}
#endif

// Folder of speed2.exe, with a trailing backslash.
inline void GetGameDir(char out[MAX_PATH]) {
    GetModuleFileNameA(nullptr, out, MAX_PATH);
    char* slash = strrchr(out, '\\');
    if (slash) slash[1] = '\0';
}

#ifdef BAYVIEW_FOREVER
// <game folder>\BayviewForever\, with a trailing backslash (created if missing). Every file the mod writes goes
// here; only the .asi and .ini live in scripts\. main.cpp moves files older versions left in the game folder.
inline void GetDataDir(char out[MAX_PATH]) {
    GetGameDir(out);
    strncat(out, "BayviewForever\\", MAX_PATH - strlen(out) - 1);
    CreateDirectoryA(out, nullptr);
}
#endif

#ifndef BAYVIEW_FOREVER
// Opens <game folder>\<fileName>: appends by default, or starts a fresh file each game run.
inline void OpenLog(const char* fileName, bool append = true) {
    char dir[MAX_PATH], path[MAX_PATH];
    GetGameDir(dir);
    snprintf(path, sizeof(path), "%s%s", dir, fileName);
    g_log = fopen(path, append ? "a" : "w");
}
#endif

inline void LogSlot(int n, const unsigned char* s, const char* suffix = "") {
    uint32_t model = ReadAt<uint32_t>(s, slot::kCarModel);
    Log("  slot %d: car=%s (%08X) hash=%08X inuse=%u kind=%u locked=%u occupied=%u spent=%d%s",
        n, CarName(model), model,
        ReadAt<uint32_t>(s, slot::kHash),
        ReadAt<uint32_t>(s, slot::kInUse),
        ReadAt<uint32_t>(s, slot::kKind),
        s[slot::kLocked], s[slot::kOccupied],
        ReadAt<int32_t>(s, slot::kSpent),
        suffix);
}

// ---- Memory and version checks --------------------------------------------

// Checks every region the range touches. The exe's .data section is split into
// several regions at runtime (pages the game has written become PAGE_READWRITE,
// untouched ones stay PAGE_WRITECOPY), so one VirtualQuery is not enough.
inline bool IsReadable(uintptr_t addr, size_t len) {
    const uintptr_t end = addr + len;
    while (addr < end) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi))) return false;
        if (mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
        addr = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    }
    return true;
}

struct CodeCheck {
    uintptr_t     addr;
    unsigned char bytes[8];
    size_t        len;
    const char*   what;
};

// True if every check matches. Logs the first mismatch.
inline bool CodeMatches(const CodeCheck* checks, size_t count) {
    for (size_t i = 0; i < count; i++) {
        const CodeCheck& c = checks[i];
        if (!IsReadable(c.addr, c.len) || memcmp(reinterpret_cast<const void*>(c.addr), c.bytes, c.len) != 0) {
            Log("STOP: code at 0x%08X is not \"%s\".", static_cast<unsigned>(c.addr), c.what);
            return false;
        }
    }
    return true;
}

// MD5 of a file as 32 lowercase hex chars. Returns false on any error.
// Uses CryptoAPI, so never call it from DllMain.
inline bool Md5OfFile(const char* path, char out[33]) {
    bool ok = false;
    HCRYPTPROV prov = 0;
    HCRYPTHASH hash = 0;
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    if (CryptAcquireContextA(&prov, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT) &&
        CryptCreateHash(prov, CALG_MD5, 0, 0, &hash)) {
        static BYTE buf[64 * 1024];
        DWORD n = 0;
        ok = true;
        while (ReadFile(file, buf, sizeof(buf), &n, nullptr) && n > 0) {
            if (!CryptHashData(hash, buf, n, 0)) { ok = false; break; }
        }
        BYTE digest[16];
        DWORD digestLen = sizeof(digest);
        if (ok && CryptGetHashParam(hash, HP_HASHVAL, digest, &digestLen, 0) && digestLen == 16) {
            for (int i = 0; i < 16; i++) sprintf(out + i * 2, "%02x", digest[i]);
        } else {
            ok = false;
        }
    }
    if (hash) CryptDestroyHash(hash);
    if (prov) CryptReleaseContext(prov, 0);
    CloseHandle(file);
    return ok;
}

// Logs the exe path and MD5; true only for the v1.2 exe.
inline bool ExeMd5Check() {
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    Log("Game exe: %s", exePath);
    char md5[33] = {0};
    if (!Md5OfFile(exePath, md5)) {
        Log("STOP: could not compute the MD5 of the game exe.");
        return false;
    }
    bool known = false;
    for (const char* expected : kExpectedMd5) known = known || strcmp(md5, expected) == 0;
    if (!known) {
        Log("STOP: exe MD5 is %s, expected %s or %s (v1.2). Addresses would be wrong.", md5, kExpectedMd5[0],
            kExpectedMd5[1]);
        return false;
    }
    Log("Exe MD5 OK (%s).", md5);
    return true;
}

#ifdef BAYVIEW_FOREVER
// The MD5 is computed once for all features (bf::ExeMd5Ok logs it under [Main]).
inline bool ExeMd5Ok() {
    if (bf::ExeMd5Ok()) return true;
    Log("STOP: the game exe is not v1.2 (see the [Main] lines at the top).");
    return false;
}
#else
inline bool ExeMd5Ok() { return ExeMd5Check(); }
#endif

// ---- Hooking ---------------------------------------------------------------

// Redirects the CALL instruction at callSite (E8 rel32) from expectedTarget to newTarget.
// Refuses (returns false) unless the instruction is exactly CALL expectedTarget.
inline bool RedirectCall(uintptr_t callSite, uintptr_t expectedTarget, const void* newTarget) {
    if (!IsReadable(callSite, 5)) return false;
    unsigned char* p = reinterpret_cast<unsigned char*>(callSite);
    int32_t rel;
    memcpy(&rel, p + 1, 4);
    if (p[0] != 0xE8 || callSite + 5 + rel != expectedTarget) return false;

    int32_t newRel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(newTarget) - (callSite + 5));
    DWORD oldProtect;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
    memcpy(p + 1, &newRel, 4);
    VirtualProtect(p, 5, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    return true;
}

#ifdef BAYVIEW_FOREVER
// Like RedirectCall, but also accepts a CALL that another BayviewForever feature already redirected (its target lies
// inside BayviewForever.asi), so two features can hook one CALL site. *next = the target found there: the hook must
// call *next instead of the original (the original itself, or the other feature's hook, which calls on).
inline bool ChainCall(uintptr_t callSite, uintptr_t original, const void* newTarget, uintptr_t* next) {
    if (!IsReadable(callSite, 5)) return false;
    unsigned char* p = reinterpret_cast<unsigned char*>(callSite);
    int32_t rel;
    memcpy(&rel, p + 1, 4);
    const uintptr_t current = callSite + 5 + rel;
    const uintptr_t base = reinterpret_cast<uintptr_t>(bf::Module());
    const IMAGE_NT_HEADERS* nt =
        reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const bool ours = current >= base && current < base + nt->OptionalHeader.SizeOfImage;
    if (p[0] != 0xE8 || (current != original && !ours)) return false;

    int32_t newRel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(newTarget) - (callSite + 5));
    DWORD oldProtect;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
    *next = current;
    memcpy(p + 1, &newRel, 4);
    VirtualProtect(p, 5, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    return true;
}
#endif

// ---- Front end (menus), car lot and profile -------

const uintptr_t kProfile1Name            = 0x0083A9E0;  // g_Profile1_Name: 32-byte profile name (start of player 1's profile)
const size_t    kProfileNameSize         = 32;
const uintptr_t kFECarSessionBackup      = 0x00851148;  // g_FECarSession_ProfileBackup: profile copy made by maybe_FECarSession_Begin
                                                        // (garage menus AND customize shop); never restored by the game
const uintptr_t kFELaunchCar             = 0x005309E0;  // maybe_FE_LaunchCar(player, recordHash, 0), cdecl. NOT a redraw:
                                                        // it launches the current car into the world (dyno / test drive)
const uintptr_t kCarLotReplaceCar        = 0x00496050;  // CarLot_ReplaceCar: thiscall(carLotScreen, slotHash, newCarKey), RET 8
const uintptr_t kFEngProcessPads         = 0x0054D4B0;  // FEng_ProcessPadsForPackage: thiscall(FEng, package), RET 4
const uintptr_t kFECarDisplay            = 0x00839C40;  // g_FECarDisplay: +0 = pointer to the car record shown in the menus
const uintptr_t kRideInfoConstruct       = 0x0040C910;  // RideInfo constructor: thiscall(ride), plain RET; no destructor work needed
const uintptr_t kFEShowGarageCar         = 0x004D8EC0;  // cdecl(ride, mode, which): shows a RideInfo as the garage backdrop car
const uintptr_t kVtableCarRecord         = 0x0079B12C;  // vtable_CarRecord; [3] (+0xC) = CarRecord_vBuildRideInfo(player, ride, 0), RET 0xC

namespace fe {
// FEng object (ECX of FEng_ProcessPadsForPackage)
const size_t kPads       = 0x08;  // pointer to pad entries (kPadEntrySize each)
const size_t kPadCount   = 0xD4;  // number of pad entries
const size_t kPackages   = 0xE4;  // first open package
const size_t kPadEntrySize = 0x88;
const size_t kPadPrev    = 0x00;  // button bits last frame
const size_t kPadCur     = 0x04;  // button bits this frame
// FEPackage
const size_t kPkgNext    = 0x04;
const size_t kPkgName    = 0x0C;  // char*, e.g. "GarageMain.fng"
const size_t kPkgPadMask = 0x24;  // which pads this package listens to
// Pad bit numbers (bit n pressed -> g_FEPadPressMessages[n]); 0-3 are the directions.
const int kBitAccept = 4, kBitBack = 5, kBitStart = 6, kBitLTrigger = 7, kBitRTrigger = 8;
const int kBitButton0 = 9;  // BUTTON0..BUTTON9 = bits 9..18
const int kNumBits = 19;

inline const char* BitName(int bit) {
    static const char* const kNames[] = {"DIR0", "DIR1", "DIR2", "DIR3", "ACCEPT", "BACK", "START", "LTRIGGER",
                                         "RTRIGGER", "BUTTON0", "BUTTON1", "BUTTON2", "BUTTON3", "BUTTON4",
                                         "BUTTON5", "BUTTON6", "BUTTON7", "BUTTON8", "BUTTON9"};
    return (bit >= 0 && bit < kNumBits) ? kNames[bit] : "?";
}

inline const char* PackageName(uintptr_t pkg) {
    if (!pkg || !IsReadable(pkg + kPkgName, 4)) return "";
    const char* name = *reinterpret_cast<const char* const*>(pkg + kPkgName);
    return (name && IsReadable(reinterpret_cast<uintptr_t>(name), 1)) ? name : "";
}
}  // namespace fe

// ---- Career car select screen (UICareerCribCarSelect) ----

const uintptr_t kCareerIsFinished = 0x00513880;  // Career_IsFinished: fastcall(g_Career) -> AL; reads [ECX+0x78EC] (must be non-zero)
const uintptr_t kFEPrintf         = 0x00537B80;  // cdecl(packageName, objectHash, format, ...): printf into an FE text object

namespace carsel {
const uintptr_t kVtable     = 0x00795D00;  // vtable_UICareerCribCarSelect
const uintptr_t kDestructor = 0x004DFC30;  // vtable[0]: thiscall(screen, flags), RET 4
const uintptr_t kNotify     = 0x004DFC50;  // vtable[2]: thiscall(screen, msg, a, b, c), RET 0x10
const uintptr_t kScroll     = 0x004B04C0;  // UICareerCribCarSelect_Scroll: thiscall(screen, PAD_LEFT/PAD_RIGHT msg), RET 4
// screen
const size_t kPackageName = 0x04;   // char*
const size_t kPackage     = 0x18;   // FEPackage*
const size_t kFirst       = 0x4C;   // list sentinel: [0] first link, [1] last link (links point at entry+0x10)
const size_t kLast        = 0x50;
const size_t kHighlighted = 0x54;   // highlighted entry
const size_t kIndex       = 0x5C;   // its index
// list entry (0x940 bytes, built by FUN_004E0160 / FUN_00511DB0)
const size_t kLinkOffset = 0x10;    // entry+0x10 = {next link, prev link}
const size_t kNextLink   = 0x10;
const size_t kPrevLink   = 0x14;
const size_t kDirty      = 0x914;   // byte: rebuild the cached RideInfo (+0x20) from the record on next use
const size_t kCarType    = 0x918;   // car type table entry
const size_t kValue      = 0x91C;   // copy of slot +0x00C
const size_t kSlotHash   = 0x920;   // record hash (career slot)
const size_t kLockedFlag = 0x930;   // byte
const size_t kEmptyFlag  = 0x931;   // byte
}  // namespace carsel

const uint32_t kMsgPadAccept = 0x406415E3;  // StringHash("PAD_ACCEPT")
const uint32_t kMsgPadBack   = 0x911AB364;  // StringHash("PAD_BACK")
const uint32_t kMsgPadLeft   = 0x9120409E;  // StringHash("PAD_LEFT")
const uint32_t kMsgPadRight  = 0xB5971BF1;  // StringHash("PAD_RIGHT")

// ---- Career car lot (UICareerCarLot) and car unlock tiers ----

namespace carlot {
const uintptr_t kVtable  = 0x00795CF0;  // vtable_UICareerCarLot
const uintptr_t kNotify  = 0x004ED9E0;  // vtable[2] = maybe_CarLotScreen_HandleMessage: thiscall(screen, msg, a, b, c), RET 0x10
const size_t kPackageName = 0x04;       // char*
const size_t kReady       = 0x1BC;      // byte; the handler ignores ACCEPT while 0
const size_t kMode        = 0x4C;       // 1 browse, 2 buy into a free slot, 3 replace the only car, 4 garage full (choose), 5 pick the car to replace
const size_t kHighlighted = 0x58;       // highlighted list entry (same entry class as car select: +0x918 car type, +0x920 key)
const size_t kNewCarKey   = 0x64;       // mode 5: catalogue key of the car being bought (set when "choose another car" is picked)
const uint32_t kMsgClickAccept = 0x0C407210;  // the handler treats it like PAD_ACCEPT
}  // namespace carlot

const uintptr_t kCarUnlockTablePtr   = 0x00838438;  // 12-byte records {car name hash, race key, alternate race key} (GLOBALB chunk 0x34A1F)
const uintptr_t kCarUnlockCount      = 0x0083843C;
const uintptr_t kCarUnlockUseAlt     = 0x00864F24;  // FUN_00570A30 returns this: non-zero -> use the alternate race key
const uintptr_t kGRaceFindByKey      = 0x004FFC90;  // cdecl(race key) -> GRace* or 0
const size_t    kCarTypeNameHash     = 0xD0;        // car type entry: StringHash of its CARS\ folder name
const uintptr_t kLocalizedString     = 0x004FFA80;  // cdecl(string hash) -> const char* (language table)
const uintptr_t kShowMessageDialog   = 0x00558430;  // cdecl(callerPackageName, dialogPackage, messageHash, ...)
const uintptr_t kDialogCloseMessage  = 0x00838970;  // message sent to the caller when the dialog closes (the shop sets 0x34DC1BEC)
const uint32_t  kMsgNotEnoughCash    = 0xD1AC60BE;  // the parts shop's "not enough cash" message
const uint32_t  kMsgUndim            = 0x34DC1BEC;  // dialog closed -> screen UNDIMs
const uintptr_t kCarDBCatalogCount   = 0x0083B2D0;  // g_CarDB+0x540: number of car lot catalogue records (g_CarDB+0, 0x1C each, key at +8)
const size_t    kCatalogRecordSize   = 0x1C;

// Replaces one pointer (e.g. a vtable entry) after checking it holds `expected`.
inline bool PatchPointer(uintptr_t addr, uintptr_t expected, uintptr_t value) {
    if (!IsReadable(addr, 4) || *reinterpret_cast<uintptr_t*>(addr) != expected) return false;
    DWORD oldProtect;
    if (!VirtualProtect(reinterpret_cast<void*>(addr), 4, PAGE_READWRITE, &oldProtect)) return false;
    *reinterpret_cast<uintptr_t*>(addr) = value;
    VirtualProtect(reinterpret_cast<void*>(addr), 4, oldProtect, &oldProtect);
    return true;
}

// ---- Dialog texts, FE text objects, car select "keep" ----

const uintptr_t kFEFindObject      = 0x005379C0;  // cdecl(packageName, objectHash) -> FE object or 0
namespace feobj {
const size_t kType      = 0x18;   // 2 = text
const size_t kFlags     = 0x1C;   // 0x20: '^' is not a line break; 0x800 (or 0x20): '$NAME$' is not an icon (FUN_0051C110)
const size_t kMaxLength = 0x68;   // text objects: buffer capacity in characters; FEPrintf keeps at most (this - 1)
}  // namespace feobj
const uintptr_t kFEStringSetText   = 0x00535FE0;  // thiscall(textObject, const char*), RET 4: sets the text, growing the
                                                  // buffer when needed (FEPrintf caps at the capacity before calling it)
// In FE text, "$NAME$" draws an icon (FUN_0050BB10): e.g. $JOY_EVENT_FENG_PAD_UP$ = the player's own "up" button,
// $ICON_SPACER$ = a gap. A '$' followed by text is always the start of a token, so prices can't use '$'.
const uintptr_t kShowDialogFormatted = 0x00558790;  // cdecl(pkg, dlgPkg, a, b, yesMsg, noMsg, noMsg2, flag, textHash, ...):
                                                    // localized textHash is the printf format for the varargs
const uintptr_t kShowDialogText      = 0x00558680;  // cdecl(pkg, dlgPkg, a, b, yesMsg, noMsg, noMsg2, char 1, flag, format, va_list)
const uint32_t  kMsgSlotWord         = 0x93475966;  // "Slot" (car select title is "%s %d")
const uint32_t  kMsgTradeInOrChoose  = 0x3192F5D1;  // "Do you want to trade-in your %s ^for this stock %s?^Or do you want to choose one of your other cars?"
const uint32_t  kMsgTradeIn          = 0x85215BC6;  // "Do you want to trade-in your %s ^for this stock %s?"
const uint32_t  kMsgDriveThisCar     = 0xD05FC3A3;  // car select: "yes, drive it" (Career_SetCurrentCar(highlighted) + leave)
// ('^' is a line break in FE text, FUN_0050BC10; the game font has no arrow glyphs.)

// Replaces the rel32 of a JMP (E9) after checking it jumps to `expectedTarget` (tail calls).
inline bool RedirectJump(uintptr_t jmpSite, uintptr_t expectedTarget, const void* newTarget) {
    if (!IsReadable(jmpSite, 5)) return false;
    unsigned char* p = reinterpret_cast<unsigned char*>(jmpSite);
    int32_t rel;
    memcpy(&rel, p + 1, 4);
    if (p[0] != 0xE9 || jmpSite + 5 + rel != expectedTarget) return false;

    int32_t newRel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(newTarget) - (jmpSite + 5));
    DWORD oldProtect;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
    memcpy(p + 1, &newRel, 4);
    VirtualProtect(p, 5, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    return true;
}

// ---- Players ------------------------------
// The game loop (0x0060A240) runs the per-frame player update 0x00602650 on each of these.

namespace player {
const uintptr_t kList       = 0x008900B4;  // player pointers (2)
const uintptr_t kCount      = 0x008900BC;  // int
const size_t    kCar        = 0x004;       // player -> car
const size_t    kMode       = 0x22C;       // PLAYER_MODE: 0 NONE, 1 PAUSEMENU, 2 CAR_RACING, 3 DRAG_RACING, 8 PRERACE, 9 FINISH_RACE
const size_t    kController = 0x024;       // on the car: controller type 1 normal (human), 2 drag, 3/4 AI, 0 idle
}  // namespace player

// ---- Drag race steering --------------------
// See docs/addresses.md -> "Drag race steering".

namespace drag {
const uintptr_t kSwitchJne     = 0x00602A94;  // JNE in the per-frame player update (0x00602650): skips
                                              // Player_SwitchToDragController unless PLAYER_MODE == 3.
                                              // 75 07 -> EB 07 = the player never gets the drag controller.
const uintptr_t kIsDragRace    = 0x0089E7D8;  // byte: 1 during a drag race
// Force feedback (RealDriver_UpdateForceFeedback 0x00406460): at 0x004064CC it calls RaceOptions_IsDragRace
// (0x004018F0, ECX = 0x0089E7A0: byte +0x38 = 0x0089E7D8 or [0x00890108]); in a drag race the JNE at 0x004064D8
// jumps to 0x0040692A = all road/impact effects stopped, only the centering spring. 6-byte NOP = full force feedback.
const uintptr_t kFfbDragJne    = 0x004064D8;  // 0F 85 4C 04 00 00
// Drag Steering redirects this CALL to an empty hook (no drag controller).
const uintptr_t kSwitchToDrag  = 0x005F3010;  // Player_SwitchToDragController: thiscall(player), plain RET
const uintptr_t kSwitchToDragCallSites[] = {0x00602A98};  // the only CALL (player update, PLAYER_MODE == 3)
}  // namespace drag

// ---- Force feedback ------------------------
// See docs/addresses.md -> "Force feedback".

namespace ffb {
// Steering force: RealDriver_UpdateForceFeedback calls FF_SetConstantForceAtAngle (thiscall ECX = [0x00870760],
// args port, magnitude -10000..10000, angle 270; RET 0xC) from this one CALL. Magnitude = front tire lateral force,
// 0 below 1 m/s. The other 2 callers (0x005CD771, 0x005CDC34) are not the steering force.
const uintptr_t kSetConstantForce = 0x005C9550;
const uintptr_t kSteeringForceCall = 0x004065DB;
const int       kFullForce         = 10000;  // DirectInput force range used by the game
}  // namespace ffb

// ---- Gearbox, clutch and H-shifter ---------------
// See docs/addresses.md -> "Gearbox, clutch and H-shifter". The game has an unused shift mode 3 (manual with clutch).

namespace gearbox {
// Shift mode = the profile's transmission byte (+0x20: 0 auto / 1 manual; forced to 1 in drag races) passed to
// RealDriver_SetShiftMode (thiscall(driver, mode) -> AL, RET 4). Read through Player_GetTransmissionOption 0x005EB430
// at 0x004B5C2F / 0x00427FF7, and inlined at 0x004A7B78 (option changed), 0x005F3068, 0x005F30D6, 0x005F3E4D,
// 0x005F6E08, 0x005FAF70. So the setter is hooked
// through its only pointer, RealDriver vtable 0x00784FC8 slot +0x34.
const uintptr_t kSetShiftMode          = 0x00407280;
const uintptr_t kSetShiftModeVtblSlot  = 0x00784FFC;
// RealDriver per-frame update 0x0041E2C0 (vtable slot 1; returns early for drivers without a controller port).
// Both its exits call 0x00407400 (thiscall(driver, arg), RET 4) with ECX = driver; one of them always runs.
const uintptr_t kDriverFrameTail       = 0x00407400;
const uintptr_t kDriverFrameTailCalls[] = {0x0041E429, 0x0041E466};  // mode-3 path, other modes
const uintptr_t kIsDragRace            = 0x0089E7D8;  // byte: 1 during a drag race (same as drag::kIsDragRace)
// RealDriver_MapPedals 0x00407A30 (thiscall(driver, float dt), RET 4): pedal inputs -> throttle/brake outputs. Called
// once per driver update at 0x0041E332, before the gear logic. The game's reverse lives here (every shift mode):
// brake > 0.6 with throttle low sets the "reversing" flag (+0x2F0); below 0.5 m/s the brake then becomes the throttle
// and wanted gear = R (0x00407CBE); throttle with wanted gear R calls vtable+0x30 (wanted = 1st in modes 1/3) at
// 0x00407E5C. So in R the throttle "bogs" (wanted gear fights the shifter) and the brake drives.
// At the end, Race-finished autopilot (0x005F00F0 && state 6) sets throttle 0 / brake 0.5.
const uintptr_t kMapPedals             = 0x00407A30;
const uintptr_t kMapPedalsCalls[]      = {0x0041E332};

namespace driver {  // RealDriver offsets
const size_t kWantedGear = 0x1E4;  // int: 0 R, 1 N, 2 = 1st ...; read and written back by the car every physics tick
const size_t kCar        = 0x2A8;  // car
const size_t kPort       = 0x2AC;  // controller port, -1 = none
const size_t kStartHold  = 0x2B4;  // byte: car held at the start line (countdown); while set, the driver update forces
                                   // wanted gear = N every frame (0x0041E37E). Cleared at GO through 0x00410430(0),
                                   // which calls vtable+0x30 (0x00407310: wanted = 1st in modes 1/3, auto otherwise).
const size_t kShiftMode  = 0x2C0;  // 0 auto, 1 manual, 2 drag, 3 clutch, 4 clutch mode while a shift to/from N or R runs
const size_t kClutch     = 0x2F4;  // float: 1.0 pedal up (engaged) .. 0.0 pedal down; mode 3 only
const size_t kThrottleOut = 0x20C; // float: throttle the car gets (written by RealDriver_MapPedals, 0x005EB910)
const size_t kBrakeOut   = 0x214;  // float: brake the car gets (0x005EB8F0)
const size_t kThrottleIn = 0x2D4;  // float 0..1: throttle pedal
const size_t kBrakeIn    = 0x2DC;  // float 0..1: brake pedal
const size_t kReversing  = 0x2F0;  // byte: brake-to-reverse active (set/cleared inside RealDriver_MapPedals)
}  // namespace driver
namespace car {  // car offsets (driver+0x2A8)
const size_t kStats      = 0x020;  // -> +0x1C8 = top gear index - 1 (wanted gear may go up to it + 1)
const size_t kTopGear    = 0x1C8;
const size_t kParts      = 0x034;  // -> +0x4C = drivetrain
const size_t kDrivetrain = 0x04C;
const size_t kGear       = 0x434;  // int, same numbering as wanted gear
const size_t kVelocity   = 0x070;  // 3 floats, m/s (RealDriver_UpdateForceFeedback takes its length at 0x00406516)
}  // namespace car
namespace drivetrain {
const size_t kClutch     = 0x14;   // float engagement as last set by Drivetrain_SetClutch
const size_t kTarget     = 0x38;   // gear to engage when the clutch is fully down
const size_t kCurrent    = 0x3C;
const size_t kClutchState = 0x44;  // 3 engaged (>0.95), 1 disengaged (<0.05), 2 engaging, 0 disengaging
const size_t kPending    = 0x48;
const size_t kGrindStart = 0x5C;   // float game seconds; while non-zero the engine gets no power (0x005A163C writes
                                   // engine+0x80 = 0); cleared once now - this >= kGrindSeconds (0x005A164E)
const size_t kMode       = 0x60;   // copy of the shift mode (3 = clutch)
const size_t kEngine     = 0x20;   // -> engine; engine+0x80 = power factor (1.0 normal, 0 during the grind, 0.7 while
                                   // the automatic engages, >1 briefly after an upshift); only written by the update below
}  // namespace drivetrain
const size_t kEnginePowerFactor = 0x80;
// Drivetrain_UpdateClutchState: thiscall(drivetrain), no stack args, plain RET; once per physics step for every car.
// Its only pointer is the Drivetrain vtable slot 7.
const uintptr_t kDrivetrainUpdate         = 0x005A1400;
const uintptr_t kDrivetrainUpdateVtblSlot = 0x007A2524;
const uintptr_t kGameClock      = 0x0086518C;  // int ticks
const float     kGameClockScale = 0.00025f;    // ticks -> seconds (constant at 0x00784268)
const float     kGrindSeconds   = 0.45f;       // constant at 0x007A18C8
const int kModeManual = 1, kModeClutch = 3, kModeClutchShifting = 4;
const int kGearReverse = 0, kGearNeutral = 1, kGearFirst = 2;
}  // namespace gearbox

// ---- Sound output mode / cut surround -----------
// See docs/addresses.md -> "Audio output mode / cut surround sound". The PC sound library supports quad and 5.1,
// but the game only ever asks for mode 0 (mono) or 1 (stereo).

namespace snd {
// Snd_SetOutputMode: cdecl(int mode) -> int (0 ok, -5 not supported). 0 mono, 1 stereo, 2 quad, 3 5.1.
// Before the sound system runs it stores the mode for the init; while it runs it stores a pending mode that
// Snd_ApplyOutputMode (0x0072183A) picks up. Both game callers pass (audio mode != 0):
//   0x0046B2E5 / 0x0046B2F6  AudioSettings_SetAudioMode (boot: audio init 0x0047AAD0 calls it with 1, then opens the output)
//   0x00486C6F               world load (region loader step 3): g_AudioMode squashed to 0/1, then Snd_ApplyOutputMode
const uintptr_t kSetOutputMode = 0x00721681;
const uintptr_t kSetOutputModeCallSites[] = {0x0046B2E5, 0x0046B2F6, 0x00486C6F};
const int kModeMono = 0, kModeStereo = 1, kModeQuad = 2, kMode51 = 3;
const uintptr_t kAudioMode    = 0x0083AA50;  // int: FE option, 0 Mono / 1 Stereo (toggle wraps 0..1)
const uintptr_t kCaps         = 0x008B3FFC;  // bytes [mode] == 1: DirectSound accepted 1/2/4/6 channels (probe 0x00725DB5)
const uintptr_t kRunning      = 0x008B4608;  // byte: 1 once the sound system is running
const uintptr_t kModeStored   = 0x00818084;  // int: mode used by the next (re)init
const uintptr_t kChannels     = 0x008B44DF;  // byte: mixer speakers (without LFE)
const uintptr_t kLfe          = 0x008B44E0;  // byte: 1 = extra LFE channel (5.1)
const uintptr_t kOutChannels  = 0x008B47F9;  // byte: channels written to the DirectSound buffer (incl. LFE)
const uintptr_t kRate         = 0x008B44C6;  // u16: output sample rate
const uintptr_t kOutputSlots  = 0x008B46E8;  // byte: mixer channels incl. LFE (loop count of the gain CALL below)

// Voices: [kVoices] + n * kVoiceSize. Each voice is one channel of a playing sound.
const uintptr_t kVoices    = 0x008B466C;  // -> voice array
const size_t    kVoiceSize = 0x90;
const size_t    kVoiceAngle   = 0x1C;  // u16 direction, 0 = ahead, 0x4000 = 90 deg (clockwise: 0x2000 = front right)
const size_t    kVoiceVolume  = 0x48;  // float
const size_t    kVoiceLfeOnly = 0x4E;  // byte: 1 = sound goes to the subwoofer channel only
const size_t    kVoiceVolByte = 0x5F;  // signed char 0..127 (volume = byte * float / 127, 0x00724F74)
// Pan: Voice_UpdatePan 0x00724FC5 turns the angle into one gain per mixer channel (Snd_AngleToGains 0x00725878,
// cdecl(u16 angle, float* gains), only CALL 0x00725012; LFE gain 0 in 5.1). Voice_UpdateGains 0x00724F5D then sets
// volume * gain per channel through Snd_SetVoiceChannelGain 0x0072905D (cdecl(voice, channel, float), only CALL
// 0x00724FAC). It runs on every volume change (e.g. the game's per-frame SetVolume 0x0072412A).
const uintptr_t kAngleToGains       = 0x00725878;
const uintptr_t kAngleToGainsCall   = 0x00725012;
const uintptr_t kSetChannelGain     = 0x0072905D;
const uintptr_t kSetChannelGainCall = 0x00724FAC;
// Mixer channel -> speaker in 5.1 (speaker angles 0x007258E6 + output remap 0x00729574):
const int kCh51Centre = 0, kCh51FrontRight = 1, kCh51RearRight = 2, kCh51RearLeft = 3, kCh51FrontLeft = 4, kCh51Lfe = 5;
const size_t    kVoiceSampleRate = 0x20;  // u16 sample rate in Hz (22050, 24000, 28000, 32000, 44100 seen)
const size_t    kVoiceChannels = 0x23;  // byte: channels of the sound this voice belongs to (1 = mono)
// Mixer (the mix loop 0x007292BC): one record per voice, same index as the voice array.
const uintptr_t kMixVoices     = 0x008B4A00;  // -> records
const size_t    kMixVoiceSize  = 0x5C;
const uintptr_t kMixVoiceCount = 0x008B47F8;  // byte: number of records
const size_t    kMixState      = 0x00;  // byte: 2 = playing (only those are mixed)
const size_t    kMixGains      = 0x1C;  // float per output channel (written by Snd_SetVoiceChannelGain)
const size_t    kMixSends      = 0x38;  // -> float per FX bus (reverb sends)
const uintptr_t kFxBusCount    = 0x008180B8;  // int
const uintptr_t kFxBusActive   = 0x008B4804;  // -> int per bus, != 0 = in use
// Own car's engine: the player engine object (class table 0x0078AF30, built by 0x00473D30;
// AI engines use 0x0078AEE0) updates its two layers (accel GIN at +0xE0, decel GIN at +0xFC handle) in 0x00474310
// (its class slot 18) -> 0x00473E60 (1 layer) / 0x00474030 (2 layers). Both give every layer the same direction
// (the car's center seen from the camera, via an attached 3D position controller) through Snd_SetAzimuth
// cdecl(handle, azimuth, spread; caller pops), from these 3 CALLs only. There is no separate exhaust sound.
const uintptr_t kSetAzimuth = 0x00724093;
// Turbo blow-off. CARSFX_Turbo (ctor 0x00472480, vtable 0x0078AC10, slot 9
// update 0x0047CAD0) starts its one-shot blow-off when boost drops (state +0x180 1 -> 2) through SndBase_PlayOneShot
// (thiscall(obj, 0, 0, 0, 0, id), RET 0x14; only caller 0x0047CB9D). It stores the blow-off volume at +0xE8 = turbo
// tuning ([+0xE0]+8, or +0xC for the big variants 2/3 when boost +0xF8 > [0x00785510]) x boost, and the sound at +0xC0
// (only plays if +0xC0 was 0). Every frame vtable slot 10 (0x0045ECE0) sends min(+0xE8 x master >> 15 x fade, 0x7FFF)
// to the sound (+0x8). The turbo spool's volume is +0xE4.
const uintptr_t kPlayOneShot = 0x0045EE70;
const uintptr_t kPlayOneShotCalls[] = {0x0047CB9D};
const size_t    kTurboSound = 0xC0;      // playing one-shot (0 = none)
const size_t    kTurboBlowOffVol = 0xE8; // int: blow-off volume before the master volume (0..~0x7FFF)
const size_t    kTurboVariant = 0xEC;    // int: 1 small blow-off, 2/3 big ones
// Turbo spool (whistle while boost builds): CarSfxTurbo_UpdateSpool 0x00472770 (thiscall(obj, dt), RET 4), called
// twice per update (0x0047CB2C, 0x0047CBAF). With the spool sound playing (+0xBC != 0) it writes a fresh volume to
// +0xE4 every call (0x00472968 ... 0x004729B4); slot 10 sends min(+0xE4 x master >> 15, 0x7FFF) to it.
const uintptr_t kTurboSpool = 0x00472770;
const uintptr_t kTurboSpoolCalls[] = {0x0047CB2C, 0x0047CBAF};
const size_t    kTurboSpoolSound = 0xBC; // playing spool loop (0 = none: the call starts it, no volume written)
const size_t    kTurboSpoolVol = 0xE4;   // int: spool volume before the master volume
const uintptr_t kOwnEngineAzimuthCalls[] = {0x00473F5A, 0x0047412F, 0x00474285};
const uintptr_t kVoiceCount   = 0x008B460C;  // u16: voices in the voice array
const size_t    kVoiceHandle  = 0x00;  // int: handle of the sound (low byte = this voice's index), < 0 = free
const size_t    kVoiceGroup   = 0x26;  // byte: voices of one sound share it (0 = none)
const size_t    kVoiceActive  = 0x6D;  // byte: 1 = in use
const uintptr_t kChannelBuffers = 0x008B48F0;  // float* per output channel: the current mix block (<= 256 samples)
const float     kMixBias = 12582912.0f;  // mix buffer samples are stored + 1.5*2^23 (float->int trick); silence = bias
}  // namespace snd

// ---- Force feedback effects and steering input ------
// See docs/addresses.md -> "Force feedback effects and steering input". All FF functions are thiscall with
// ECX = FF object [0x00870760]; port = controller port (0/1). Forces and coefficients are 0..10000.

namespace wheel {
const uintptr_t kFFObject = 0x00870760;  // -> FF object; +port*0x110 = DIJOYSTATE2 of that port (axes -10000..10000)
const size_t    kJoyStateSize = 0x110;

// FF functions (RET = bytes of stack args popped)
const uintptr_t kSetSpring          = 0x005C9150;  // (port, offset, saturation, coefficient), RET 0x10
const uintptr_t kStopSpring         = 0x005BFE00;  // (port), RET 4
const uintptr_t kSetDamper          = 0x005C9590;  // (port, coefficient), RET 8
const uintptr_t kPlayJolt           = 0x005C9760;  // (port, magnitude), RET 8; square wave, 150 ms
const uintptr_t kSetRoadRumble      = 0x005C9FD0;  // (port, wave 0-4, magnitude, period ms), RET 0x10
const uintptr_t kSetNegativeDamper  = 0x005C9D90;  // (port, value -> coefficient -value), RET 8
const uintptr_t kPauseAll           = 0x005CDA10;  // (port), RET 4; pauses every effect (car in the air)
const uintptr_t kUpdateForceFeedback = 0x00406460; // RealDriver_UpdateForceFeedback: thiscall(steering, float curveOut, port), RET 8

// CALL sites inside RealDriver_UpdateForceFeedback (0x00406460), plus the menu spring
const uintptr_t kFullLockSpringCall   = 0x00406503;  // FF_SetSpring(port, 0, 10000, 10000) when |curve output| == 1.0
const uintptr_t kStopSpringCall       = 0x0040650B;  // FF_StopSpring(port) every frame otherwise (normal driving)
const uintptr_t kDamperCall           = 0x004065FD;  // FF_SetDamper(port, (1 - min(1, speed/13)) * 7058.8)
const uintptr_t kPauseAllCall         = 0x00406631;  // FF_PauseAll(port) when car+0x390 and +0x391 are both 0
const uintptr_t kCrashJoltCall        = 0x00406775;  // FF_PlayJolt: speed drop > 4 m/s in one frame
const uintptr_t kBumpJoltCall         = 0x004067A6;  // FF_PlayJolt: suspension hit
const uintptr_t kRoadRumbleCall       = 0x004068C5;  // FF_SetRoadRumble
const uintptr_t kNegativeDamperCall   = 0x004068F3;  // FF_SetNegativeDamper(port, 10000), surface 0xD above 23 m/s
const uintptr_t kOffFullLockSpringCall = 0x004069AC; // FF option off / drag branch: 100% spring at full lock
const uintptr_t kOffSpringCall        = 0x004069D6;  // FF option off / drag branch: 0x1B92 spring otherwise
const uintptr_t kMenuSpringCall       = 0x005CFFAA;  // Input_PollDevices: FF_SetSpring(port, 0, 7000, 7000) in menus
// Calls of RealDriver_UpdateForceFeedback: RealDriver (ECX = RealDriver+0x220) and DragDriver (ECX = +0x690)
const uintptr_t kUpdateForceFeedbackCalls[] = {0x0041E022, 0x004220CE};

// Steering: Steering_Update thiscall(steering = RealDriver+0x220, float input -1..1, float dt, int port) -> angle in AX,
// RET 0xC. One caller; input = RealDriver+0x2C8 (built from an 8-bit copy of the axis).
const uintptr_t kSteeringUpdate     = 0x0041DFC0;
const uintptr_t kSteeringUpdateCall = 0x0041E372;
const uintptr_t kIsFFWheel = 0x005C1000;  // Input_IsFFWheel: cdecl bool(int port); Steering_Update's wheel/pad switch (CALL 0x0041DFE5)
namespace steering {  // steering object offsets (RealDriver+0x220)
const size_t kCar        = 0x00;  // -> car (car+0x70 velocity, car+0x42C speed?, car+0x20 -> +0x270 per-car factor)
const size_t kSkipSpeedLock = 0x04;  // byte: player option +0x19, 1 = no speed-sensitive lock
const size_t kRange      = 0x08;  // float: hidden "Wheel Range" option (no slider on PC); input is clamped to it and divided by it
const size_t kFFDivisor  = 0x0C;  // float: "Road Feedback" option 2..20
}  // namespace steering
// Input device records (Input_ReadJoystickToBytes 0x005C1840): 6 records of 0x370 bytes
const uintptr_t kDeviceRecords    = 0x00874C40;
const int       kNumDeviceRecords = 6;
const size_t    kDeviceRecordSize = 0x370;
namespace device {
const size_t kPort       = 0x230;  // int, -1 = unused
const size_t kType       = 0x234;  // 0x13 = keyboard
const size_t kSteerAxis  = 0x344;  // int: byte offset of the steering axis inside DIJOYSTATE2
const size_t kSteerDir   = 0x354;  // signed char: axis direction
const size_t kSteerBound = 0x364;  // byte: steering axis bound
}  // namespace device
// The game's input: RealDriver+0x2C8 = -trunc(axis * 0.0127) * dir / 128 (about -axis * dir / 10079).
// Wheel response curve: 21 floats over -1..1 (table 0x007E9E88, only user Steering_ComputeWheel 0x00406202).
const uintptr_t kCurveData  = 0x007E92D8;
const int       kCurvePoints = 21;
// Speed-sensitive steering lock: 10 floats, max steering angle (degrees) over speed 0..160 (table 0x007E9EEC,
// only user 0x0040623A). 45, 15, 11, 8, 7, 7, 7, 7, 7, 7.
const uintptr_t kLockData   = 0x007E91A4;
const int       kLockPoints = 10;
const float     kMaxLockDegrees = 45.0f;
// Throttle/brake steering factor (the game's understeer model; nothing patches it, it is not an "assist"): the max angle is multiplied by
// 1 + table 0x007E9F14(speed) * k * (0.5 - 0.5*throttle + 0.25*(brake + handbrake)), k = the float at [0x00783AE0] (1.0,
// shared constant). Throttle/handbrake/brake = driver+0x20C/+0x210/+0x214. The FMUL at 0x00406294 (D8 0D E0 3A 78 00)
// reads k; its operand (0x00406296) can point at our own float instead. Table data (10 floats, also used by the pad
// path, never written): 1, 0.92, 0.78, 0.64, 0.54, 0.48, 0.42, 0.38, 0.34, 0.32.
const uintptr_t kPedalAssistFmul    = 0x00406294;
const uintptr_t kPedalAssistOperand = 0x00406296;
const uintptr_t kPedalAssistConst   = 0x00783AE0;
const uintptr_t kPedalWeightData    = 0x007E917C;
}  // namespace wheel

// ---- DVD / magazine covers ("showcases") ----
// Details: docs/addresses.md / structs.md -> "Showcases". Saved pictures: <save folder>\<profile> Magazine N\<same name>.
namespace covers {
namespace showcase {  // GShowcase record offsets (ev::kShowcaseTablePtr, ev::kShowcaseSize each)
const size_t kKey       = 0x20;  // StringHash(name)
const size_t kPhotoType = 0x24;  // 1/3 = magazine, 2/4 = DVD (1/2: the player poses the camera)
const size_t kDesc      = 0x28;  // desc hash: title/body texts and the cover art
const size_t kCash      = 0x26;  // int16 CashValue (500 for all)
}  // namespace showcase
// Photo pay: cash += showcase cash + trunc(stars x short [stage record data +4] + 0.005) (current stage's record +0x100;
// none post-game). Also tells the reward screen the total and adds the cover to the stage record.
const uintptr_t kPhotoCash     = 0x00541210;  // thiscall(g_Career, showcase), RET 4
const uintptr_t kPhotoCashCall = 0x00541941;  // the only CALL (FUN_00541930: auto photo and posed photo)
const uintptr_t kVisualRating  = 0x00528410;  // Career_GetCurrentCarVisualRating: thiscall(g_Career) -> ST0 stars, RET
const size_t    kStageDataPtr  = 0x100;       // stage record: GCareerStage data pointer
const size_t    kStarCash      = 0x04;        // GCareerStage: int16 photo pay per star
const uintptr_t kCurrentDesc   = 0x00837FD8;  // desc hash of the cover being shown (set by the gallery list on ACCEPT)
const uintptr_t kSaveDirName   = 0x007F66A0;  // char* "NFS Underground 2": save folder = CSIDL_LOCAL_APPDATA\<this>
const uintptr_t kFEManager     = 0x008384C4;  // pointer; [+8] = the FEng object (pads: fe::kPads / fe::kPadCount)
// The cover viewer screen (opened from Career -> Magazines / DVDs -> a cover)
const uintptr_t kViewerVtable  = 0x0079ACE8;
const uintptr_t kViewerNotify  = 0x00565B50;  // vtable[2]: thiscall(screen, msg, a, b, c), RET 0x10
const size_t    kViewerPackageName = 0x04;    // char*
const size_t    kViewerLoaded  = 0x50;        // byte: picture buffer allocated, load started
const size_t    kViewerShown   = 0x51;        // byte: picture loaded and uploaded to the texture
const uint32_t  kMsgViewerOpen = 0x35F8620B;  // first message: allocates the picture and starts loading the file
const uint32_t  kMsgLoadOk     = 0x42317A19;  // file loaded: buffer copied to the texture, shown
const uint32_t  kMsgLoadFailed = 0x2B46FE08;  // back to the list
const uint32_t  kMsgTick       = 0xC98356BA;  // every frame
// The picture: one global screen-capture object (ScreenCap_Create 0x005CE510)
const uintptr_t kScreenCap     = 0x00832DAC;
const size_t    kCapBuffer     = 0x08;        // void*: 512 x 512 x 4 bytes (B, G, R, A), = part B of the file
const size_t    kCapTexture    = 0x0C;
const uintptr_t kCapUpload     = 0x00488B10;  // thiscall(screenCap): copies the buffer into the texture, plain RET
const size_t    kPictureBytes  = 0x100000;
const uintptr_t kFEFindPackage = 0x0052CEF0;  // cdecl(packageName) -> package or 0 (also used by Virtual Garage)
// Earning covers after the career: race won -> covers earned per car model; a cover is offered (map marker) only
// while the player drives that car; photo taken -> done.
const uintptr_t kApplyRaceResult       = 0x0054C940;  // Career_ApplyRaceResult: fastcall(g_Career), plain RET
const uintptr_t kApplyResultCallSites[] = {0x0054C9DF, 0x0054CA43};  // Career_OnRaceFinished, FUN_0054CA40
const uintptr_t kApplyResultJumpSite   = 0x0054CA30;  // JMP thunk (called from 0x004F3A30 / 0x004F3AB7)
const size_t    kCareerRaceActive      = 0x0D25;      // byte: a race result is pending
const size_t    kCareerResultApplied   = 0x0D28;      // byte: set to 1 by Career_ApplyRaceResult when the player WON
const size_t    kCareerCurrentRace     = 0x8730;      // GRace* of the race being finished
const uintptr_t kShowcaseIsOffered     = 0x00501150;  // thiscall(g_Career+0x4210, showcase*) -> AL, RET 4
const uintptr_t kIsOfferedCallSites[]  = {0x005334A1, 0x0053319B, 0x0052793C};  // map markers, marker checks
const uintptr_t kIsOfferedJumpSite     = 0x00532AED;  // tail JMP in FUN_00532A50 (marker check)
const uintptr_t kShowcaseMarkDone      = 0x00533680;  // thiscall(0x0085EF50): showcase key at this+8; ends in a tail JMP, plain RET
const uintptr_t kMarkDoneCallSites[]   = {0x00498EBF, 0x004CEEB2};  // auto photo (ShowcaseDialog_TakePhoto), posed photo (PAD_ACCEPT)
const uintptr_t kRebuildMapMarkers     = 0x00533310;  // Career_RebuildMapMarkers: fastcall(g_Career), plain RET
// Rachel's text when a cover is earned (stock messages only, so saves stay mod-free) and the viewer counter.
const uintptr_t kInbox                 = 0x00860A60;  // g_Career+0x5D20: SMS inbox, 0x100 entries of 0x14 bytes
namespace inbox {
const size_t kEntrySize = 0x14;    // +0 message record*, +4 read (1), +5 auto-opened, +6 ?, +8/+0xC/+0x10 text hashes
const int    kMax       = 0x100;
const size_t kCount     = 0x1400;
const size_t kUnread    = 0x1404;  // SMSInbox_Add +1; reading a message -1 (0x00514DC0)
}  // namespace inbox
const uintptr_t kSmsTablePtr           = 0x00838454;  // SMS message records (0x14 each): +2 unlock type, +8 parameter
const uintptr_t kSmsCount              = 0x00838450;
const size_t    kSmsSize               = 0x14;
const uintptr_t kSmsFindMessage        = 0x00501310;  // cdecl(name hash) -> record or 0
const uintptr_t kSmsSendDvdCover       = 0x005299B0;  // thiscall(inbox, n), RET 4: adds the DVDUnlock (type 0xE) message
                                                      // with parameter n (SMS_DVD_COVER_n) unless already in the inbox
const uintptr_t kChallengeQueueSms     = 0x005338C0;  // WorldChallenge_QueueUnlockSMS: thiscall(inbox, challenge), RET 4:
                                                      // magazine: the challenge's SMS with SMS_MAG_GENERIC_BODY_1/2 + title
const uintptr_t kWorldChallengesPtr    = 0x00838424;  // g_WorldChallenges (0x18 each)
const uintptr_t kWorldChallengeCount   = 0x00838420;
namespace challenge {
const size_t kSize = 0x18;
const size_t kSms  = 0x08;  // SMS name hash
const size_t kDest = 0x0C;  // destination: showcase key for type 4
const size_t kType = 0x14;  // byte: 1 visual, 2 performance, 4 showcase
}  // namespace challenge
const uint32_t  kViewerCounterObj      = 0x47FF4E7C;  // UI_MagazineView.fng text object with no stock text (16 chars)
const uint32_t  kViewerBackObj         = 0xB8A7C6D0;  // UI_MagazineView.fng "Back" label (text hash 0x001335F0)
const uint32_t  kTextBack              = 0x001335F0;
}  // namespace covers

// ---- Particles, rain and frame timing ----------------
// See docs/addresses.md -> "Particles, rain and frame timing".

namespace particles {
const uintptr_t kFrameLimitDefault = 0x007FB710;  // float, seconds per frame: 1/120 (the game's own 120 fps cap)
const uintptr_t kFrameLimit        = 0x00865558;  // float, current limit (copied from the default at startup by 0x0077DCE0)
const uintptr_t kFrameDt           = 0x00865198;  // float, last frame time in s (Frame_LimitAndMeasure; menus use it)
const uintptr_t kParticlesEnabled  = 0x00870CEC;  // int g_ParticleSystemEnable (video options / registry)
const uintptr_t kRainEnabled       = 0x00870D10;  // int g_RainEnable
const uintptr_t kList              = 0x008357E0;  // the particle list every effect uses (emitter groups, linked)
const size_t    kGroupSprites      = 0x24;        // int: camera-facing particles in a group (list 0)
const size_t    kGroupOriented     = 0x28;        // int: oriented particles in a group (list 1)
const uintptr_t kRenderFrame       = 0x005D2850;  // Render_Frame: cdecl void(), whole frame incl. Present
const uintptr_t kRenderFrameCall   = 0x005814D6;  // in Game_MainLoop
const uintptr_t kAfterRenderCall   = 0x005814DB;  // CALL to the empty stub right after Render_Frame (frame end)
const uintptr_t kEmptyStub         = 0x004022C0;  // just RET (called all over the main loop)
// Main-loop CALLs that get a stack-neutral clock stub (in the order they run; then Render_Frame at kRenderFrameCall).
struct ClockSite { uintptr_t site, target; };
// Not 0x005811EE (loop top) / 0x005812ED (Frame_LimitAndMeasure): the 2025 WidescreenFix hooks both.
const ClockSite kMainLoopClock[7] = {
    {0x00581301, 0x00456940},
    {0x005813CA, 0x005EA360},  // sim dt (in the world; has its own catch-up busy-wait)
    {0x00581407, 0x00609CE0},  // world update (cars, AI, particles)
    {0x00581416, 0x00578E10},  // after the world update
    {0x00581484, 0x0060AEE0},
    {0x005814CC, 0x005CE850},
    {0x005814D1, 0x005CE8A0},  // creates + fills up to 10 managed vertex buffers for newly loaded meshes per frame
};
// Wrap Render_Frame only stack-neutrally (register-saving stub + JMP): a C++ wrapper (deeper stack, different
// return address) broke the main menu layout; a bare JMP did not.
const uintptr_t kRender            = 0x0048BDF0;  // Particles_Render: thiscall(list, view), RET 4
const uintptr_t kRenderCallMirror  = 0x005CAEC3;  // Render_World, view 3 section
const uintptr_t kRenderCallMain    = 0x005CB53C;  // Render_World, main view
const uintptr_t kUpdate            = 0x00492E10;  // Particles_Update: thiscall(list, float dt), RET 4
const uintptr_t kUpdateCallRace    = 0x0060A181;  // in-world update with the sim dt
const uintptr_t kOrientedDraw      = 0x005C7E80;  // stdcall(verts, texture, matrix, 0, 0), RET 0x14 -> Particle_DrawOrientedQuadUP
const uintptr_t kOrientedDrawCall  = 0x0048CC54;  // the only call from Particles_Render
const uintptr_t kBatchBegin        = 0x005BB320;  // SpriteBatch_Begin: thiscall(batch, int* quads), RET 4 (Lock DISCARD)
const uintptr_t kBatchBeginCall    = 0x005C0E5C;
const uintptr_t kBatchFlush        = 0x005C57E0;  // SpriteBatch_Flush: thiscall(batch), plain RET
const uintptr_t kBatchFlushCall    = 0x005CE154;  // the particle path (FUN_005CE120)
const uintptr_t kRain              = 0x0061F120;  // Rain_UpdateAndDraw: thiscall(rain), plain RET
const uintptr_t kRainCall          = 0x005CB46B;  // Render_World
}  // namespace particles

// ---- Race AI: catch-up and blocking -------------------------
// See docs/addresses.md -> "Race AI: catch-up (rubber band) and blocking / "PIT"".

namespace ai {
const uintptr_t kCatchUpOption   = 0x0089E7F8;  // int: Quick Race "Catch Up" (career sets 0, never turns it on)
const uintptr_t kCatchUpReads[]  = {0x004084B9, 0x0041757B};  // each A1 F8 E7 89 00 = MOV EAX,[kCatchUpOption]
const uintptr_t kIsDrift         = 0x0089E7D9;  // byte
const uintptr_t kIsStreetX       = 0x0089E7E3;  // byte
const uintptr_t kRaceMode        = 0x0083AAB4;  // int: 8 = career, others = Quick Race modes / online
// AI_FindCarToBlock: cdecl(ai, out worldObj*) -> block priority or -1. 0x0041CC55 = Block's "still valid?" check,
// the other two start a new block (AI_ChooseAction / AI_ChooseAction2). Each call is followed by ADD ESP,8.
const uintptr_t kFindCarToBlock            = 0x00414130;
const uintptr_t kFindCarToBlockCallSites[] = {0x0041CC55, 0x0042D36A, 0x0042D96A};
// D8 1D 3C 7D 7D 00 = FCOMP [0x007D7D3C] (shared 0.0): "gap >= 0 -> no block". The operand (+2) can point at -N.
const uintptr_t kBlockGapCompare = 0x004142DE;
const uintptr_t kZeroConstant    = 0x007D7D3C;
// Chains (verified in AI_FindCarToBlock / AI_UpdateCatchUpSpeedScale):
const size_t kAiVehicle      = 0x000;  // ai -> vehicle (+0x430 speed)
const size_t kVehicleCtrl    = 0x02C;  // vehicle -> AI controller
const size_t kCtrlCar        = 0x004;  // controller -> car (+0x14 racer info, +0x468 applied speed scale)
const size_t kCarRacer       = 0x014;
const size_t kCarSpeedScale  = 0x468;  // float, max(catch-up result, 1.0)
const size_t kCtrlRawScale   = 0x770;  // float, raw catch-up result (below 1.0 = slowed down)
const size_t kRacerSkill     = 0x901;  // int8, -2 for the player
const size_t kRacerBlockStyle = 0x902; // int8, index into the AI table's block priorities (GCareer opponent +0xB)
const size_t kRacerName      = 0x90C;  // char[16] (written by 0x006373E0; not yet seen in-game)
// World object (0x0089E970 table) -> human? Same test as WorldObj_IsHumanRacer 0x00614880: [[obj+0x1C]+0x14]+4 == 1.
const size_t kObjDriver      = 0x01C;
const size_t kDriverInfo     = 0x014;
const size_t kInfoType       = 0x004;  // int: 1 human, 2 AI
// Pass (overtake). AIActionPass vtable 0x007856D8; slot 2 = 0x0042B940 runs every frame: looks up its target entry
// (0x0060C120), gap = target distance - own distance along the route (0x0060BFF0, center to center); if gap <= 0.0
// (FCOMP [0x007D7D3C] at kPassGapCompare) or the side route planner 0x00429890 fails, it steers back to the racing
// line (0x00420D40) -- while the cars still overlap: the "PIT on the pass".
const uintptr_t kBrainFindEntry      = 0x0060C120;  // thiscall(brain, worldObj) -> entry or 0, RET 4
const uintptr_t kPassFindEntryCalls[] = {0x0042B95C};  // in Pass slot 2, right before the gap compare
const uintptr_t kPassGapCompare      = 0x0042B97D;  // D8 1D 3C 7D 7D 00
const uintptr_t kSetRacingLine       = 0x00420D40;  // thiscall(route, action+0x20, brain), RET 8
const uintptr_t kPassRacingLineCalls[] = {0x0042B9C9, 0x0042B9F8};  // planner failed / gap reached (or no target)
const uintptr_t kFindCarToPass       = 0x0042B5B0;  // AI_FindCarToPass: cdecl(brain, out worldObj*, out route) -> 100 or -1
const uintptr_t kFindCarToPassCallSites[] = {0x0042B858, 0x0042D3C5, 0x0042D9C5};  // Pass re-target, ChooseAction, ChooseAction2
// AI brain (the `ai` argument above): race-order entries of 0x30 bytes
const size_t kBrainCount     = 0x004;  // int
const size_t kBrainEntries   = 0x008;
const size_t kEntrySize      = 0x030;
const size_t kBrainOwnIndex  = 0x308;  // int8: its own entry
const size_t kEntryClosing   = 0x004;  // float
const size_t kEntryHalfLength = 0x00C; // float (AI_FindCarToBlock adds both to get bumper gaps)
const size_t kEntryDistance  = 0x010;  // float, distance along the route
const size_t kEntryHandle    = 0x02C;  // int16 index into the world object table 0x0089E970
const uintptr_t kWorldObjects = 0x0089E970;
const size_t kActionTarget   = 0x218;  // Pass/Block action: target world object
const size_t kActionRouteArg = 0x020;  // the action pointer passed to kSetRacingLine is action + this
}  // namespace ai

// ---- SMS inbox helpers (shared by Covers and UniqueParts) ----------------------------

// Takes `record` out of the inbox (if it is there), keeping the unread count right. True if it was there.
inline bool InboxRemove(uintptr_t record) {
    unsigned char* box = reinterpret_cast<unsigned char*>(covers::kInbox);
    int32_t& count = *reinterpret_cast<int32_t*>(box + covers::inbox::kCount);
    if (count < 0 || count > covers::inbox::kMax) return false;
    for (int i = 0; i < count; i++) {
        unsigned char* e = box + i * covers::inbox::kEntrySize;
        if (ReadAt<uintptr_t>(e, 0) != record) continue;
        if (e[4] == 0) *reinterpret_cast<int32_t*>(box + covers::inbox::kUnread) -= 1;  // it was unread
        memmove(e, e + covers::inbox::kEntrySize, (count - i - 1) * covers::inbox::kEntrySize);
        memset(box + (count - 1) * covers::inbox::kEntrySize, 0, covers::inbox::kEntrySize);
        count--;
        return true;
    }
    return false;
}

inline bool InboxHas(uintptr_t record) {
    const unsigned char* box = reinterpret_cast<const unsigned char*>(covers::kInbox);
    int count = ReadAt<int32_t>(box, covers::inbox::kCount);
    for (int i = 0; i < count && i < covers::inbox::kMax; i++)
        if (ReadAt<uintptr_t>(box + i * covers::inbox::kEntrySize, 0) == record) return true;
    return false;
}

// ---- Respawn at Garage (was Immersive Travel) ----------
// Details: docs/addresses.md -> "Immersive Travel". Free roam restarts at the saved spot g_Career+0x8744.. (in the save
// too; garage menus set 0x00863484 = 1, then 0x005404A0).
namespace travel {
const uintptr_t kCareerLoad            = 0x00529230;  // Career_LoadFromBuffer: thiscall(g_Career, buffer, size) -> next buffer, RET 8
const uintptr_t kCareerLoadCallSites[] = {0x0053450F};  // the only CALL (maybe_Profile_LoadFromBuffer)
const size_t    kSpotSaved   = 0x8744;  // g_Career byte: 1 = free roam starts at the spot below (0x00863484)
const size_t    kSpot        = 0x8750;  // 3 floats x, y, z (world meters) + 1 float (0), car+0x60..
const size_t    kSpotHeading = 0x8760;  // u16 heading (car+0x40C)
const size_t    kShopType    = 0x50;    // shop record: 0 = the garage (CRIB_1)
// Pause menu "Quit to Main Menu" (item vtable 0x00795528, action 0x004C2820: Yes = 0x30ED2368). The pause screen's
// Notify (0x004C2170) answers Yes with FUN_004A7CF0 (thiscall(screen), plain RET): main menu package, then
// FUN_005139C0 saves the car's spot (career only). Going back into the career does not reload the profile.
const uintptr_t kQuitToMenu            = 0x004A7CF0;
const uintptr_t kQuitToMenuCallSites[] = {0x004C21F7, 0x004C2444};  // both in the pause screen's Notify
}  // namespace travel

// ---- Unique performance parts after the career ----
// Details: docs/addresses.md -> "Unique performance parts" (+ "Unique parts after the career").
// The 3 stock timed challenges (world challenge type 2: STAGE_3_PERF_1, STAGE_4_PERF_2, STAGE_5_PERF_3) drive to
// CC_PERF_SHOP and offer 3 unique parts each. Nothing in that chain checks the stage once the challenge is in the
// active list, so post-game the feature puts one back in the list (after Career_RebuildActiveEvents emptied it).
namespace uniques {
const uintptr_t kGameMode           = 0x00850078;  // int: 1 = career mode
const uintptr_t kOutrunFinished     = 0x00541320;  // Career_OnOutrunFinished: thiscall(g_Career, char won), RET 4.
                                                   // Career mode only; outruns won +1 (stage record +0xDC), the
                                                   // outrun challenge SMS, rebuilds the map markers.
const uintptr_t kOutrunFinishedCall = 0x005F84D7;  // the only CALL (won = WORD [..+0x10] == 6, SETE AL)
const uintptr_t kChallengeList      = 0x00861E88;  // g_Career+0x7148: 32 active world challenges (pointers)
const int       kChallengeListSize  = 32;
const uintptr_t kChallengeState     = 0x0085B940;  // g_Career+0xC00: the running world challenge
namespace state {
const size_t kChallenge = 0x008;    // WorldChallenge* (0 = none running; = g_Career+0xC08)
const size_t kStep      = 0x11C;    // 1 running, 4 at the destination shop, 5 completing, 7 failed, 8 restart
}  // namespace state
namespace challenge {  // WorldChallenge record (covers::kWorldChallengesPtr, covers::challenge::kSize each)
const size_t kName    = 0x00;  // u16 offset into g_GCareerStrings (status entries use StringHash of it)
const size_t kTrigger = 0x02;  // u16 offset: start trigger name
const size_t kStage   = 0x04;  // int16
const size_t kTime    = 0x10;  // int, seconds
const size_t kParts   = 0x15;  // 3 bytes: unique perf part ids 1-10 (255 = none)
}  // namespace challenge
const uintptr_t kChallengeComplete  = 0x005419C0;  // WorldChallenge_Complete: fastcall(kChallengeState), no args
const uintptr_t kCompleteCallSites[] = {0x0056178E, 0x00561870};  // UniquePartScreen_Notify: declined, part picked
const uintptr_t kGrantPart          = 0x00521430;  // FECarSession_GrantUniquePerfPart: thiscall(g_FECarSession, cat), RET 4
const uintptr_t kGrantPartCallSites[] = {0x00561826};  // UniquePartScreen_Notify ACCEPT (performance part)
// [UniqueParts] RaceToShop = 0: entering the shop with a part waiting marks the challenge as arrived (state +8 = the
// challenge, +0x11C = 1) just before Career_OnEnterShop, whose end then sets step 4 + builds the part list, so
// Career_GetCurrentShop opens THE_UNIQUES_SHOP (the pick screen) as after the timed run.
const uintptr_t kEnterShop          = 0x0054CAF0;  // Career_OnEnterShop: thiscall(g_Career, shop record), RET 4
const uintptr_t kEnterShopCallSites[] = {0x00499497};  // the only CALL
const size_t    kShopKey            = 0x38;        // shop record (0xA0 bytes): key (= a challenge's destination +0xC)
// Magazine challenges (type 4, destination = showcase key): entering the showcase marker while it runs sets step 3
// (maybe_Career_OnEnterMarker 0x00532BF8); taking the photo calls WorldChallenge_CompleteIfArrived (0x00541960:
// step 3 -> status 1, cleared, markers rebuilt) right before Showcase_MarkDone (auto photo 0x00498EB5, posed photo
// 0x004CE994). While a challenge runs, Career_RebuildMapMarkers draws only its destination (FUN_00515060).
const uintptr_t kCompleteIfArrived  = 0x00541960;
const size_t    kSlotUniqueFlags    = 0x2F4;       // career slot: u32 per category, 1 = unique part installed
const int       kNumCategories      = 9;           // 0 Weight Reduction .. 8 Tires (9 is unused)
// Unique part id (challenge kParts byte) -> category, as UniquePerf_IdToCategory (0x00502240); -1 = none.
inline int PartCategory(int id) {
    static const int kMap[11] = {-1, 3, 7, 5, 2, 1, 0, 8, 6, 4, 9};
    return (id >= 1 && id <= 10) ? kMap[id] : -1;
}
inline const char* CategoryName(int cat) {  // English names (Perf_GetCategoryNameHash), for the log
    static const char* const kNames[] = {"Weight Reduction", "Transmission", "Nitrous", "Engine", "Turbo",
                                         "Suspension", "Brakes", "ECU", "Tires", "(unused)"};
    return (cat >= 0 && cat <= 9) ? kNames[cat] : "?";
}
// Visual uniques: 5 stock challenges of type 1, one part-slot id each (kParts byte), destination CC_BODYSHOP_1
// (vinyls: CC_GRAPHICS). WorldChallenge_BuildUniquePartList offers the car's level-4 parts of that slot (never
// filtered by what the car has); the pick is installed and charged (0x0055C230, 0x005289F0).
const uint8_t   kTypeVisual = 1, kTypePerf = 2;
const int       kNumVisualKinds = 5;
struct VisualKind {
    uint8_t     id;       // challenge part byte
    int         partSlot; // car part slot for the "car has options" lookup (-1 = always offered: rims, vinyls)
    const char* name;
};
inline const VisualKind& Visual(int k) {  // stage order: hoods (2), rims (3), spoilers (4), vinyls (4), widebody (5)
    static const VisualKind kKinds[kNumVisualKinds] = {
        {11, 9, "Hood"}, {13, -1, "Rims"}, {12, 0xC, "Spoiler"}, {62, -1, "Vinyl"}, {22, 6, "Widebody Kit"}};
    return kKinds[k];
}
const uintptr_t kGetCarTypeIndex = 0x00503AE0;  // CarRecord vtable[1]: thiscall(slot) -> car type index or -1, plain RET
const uintptr_t kPartDb       = 0x008A2B68;  // ECX for the part lookups
const uintptr_t kPartFirst    = 0x0061BA30;  // thiscall(partDb, carType, partSlot, 0, level) -> part or 0, RET 0x10
const int       kUniqueLevel  = 4;           // level of the unique parts (as the list builder asks, 0x00534072)

// The world challenge running now (g_Career+0xC08), or 0.
inline uintptr_t RunningChallenge() {
    return *reinterpret_cast<const volatile uintptr_t*>(kChallengeState + state::kChallenge);
}

// Puts world challenge `c` into the active list (its start marker then appears with the next map markers).
// 1 = added, 0 = already there, -1 = list full. Main thread, right after Career_RebuildActiveEvents.
inline int AddActiveChallenge(uintptr_t c) {
    uintptr_t* list = reinterpret_cast<uintptr_t*>(kChallengeList);
    int free = -1;
    for (int k = 0; k < kChallengeListSize; k++) {
        if (list[k] == c) return 0;
        if (!list[k] && free < 0) free = k;
    }
    if (free < 0) return -1;
    list[free] = c;
    return 1;
}
}  // namespace uniques

#ifdef BAYVIEW_FOREVER
}  // namespace
#endif
}  // namespace nfsu2
