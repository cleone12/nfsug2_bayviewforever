// Race AI: fewer "PIT maneuvers" and an optional Catch Up switch ([RaceAI]).
//
// The AI's Block behavior (AI_FindCarToBlock 0x00414130) moves into the line of the car behind it as soon as its tail
// is clear, and Block outranks Pass, so right after passing the player it swerves across the player's nose.
//   BlockMinGap     the "gap >= 0 -> no block" compare at 0x004142DE reads -BlockMinGap instead of 0.0 (every car).
//   BlockDelay      hook on the two CALLs that start a block (0x0042D36A, 0x0042D96A): wait before blocking the
//                   player. BlockPlayer = 0: never block the player. BlockStartDelay: no blocks early in a race.
//   PassClearGap    the Pass action (0x0042B940) steers back to the racing line when the centers are level; its
//                   compare at 0x0042B97D reads our value, set per frame from both cars' half-lengths by the hook on
//                   CALL 0x0042B95C. Only when the AI passes the player (for every car, AIs fanned out like cops).
//   DisableCatchUp  the two reads of the Catch Up option (0x004084B9, 0x0041757B) become XOR EAX,EAX. Career races
//                   do use catch-up (RaceOptions_Reset 0x005ED5F0 sets it to 1).
// The race log only reads memory (every pointer checked). Undo: [Features] RaceAI = 0.

#define BF_TAG "RaceAI"
#include "nfsu2.h"

#include <cstdlib>

using namespace nfsu2;

namespace {


// Settings
float g_minGap         = 0.0f;  // [AI] BlockMinGap (m)
float g_delay          = 0.0f;  // [AI] BlockDelay (s)
bool  g_blockPlayer    = true;  // [AI] BlockPlayer
bool  g_disableCatchUp = true;  // [AI] DisableCatchUp
float g_passClearGap   = 0.0f;  // [AI] PassClearGap (m)
float g_startGrace     = 0.0f;  // [AI] BlockStartDelay (s)
const float kNearPlayer = 25.0f;  // m along the route: AI-vs-AI events this close to the player are logged

// The block-gap compare reads this instead of the game's 0.0 (address patched into the instruction).
float g_negMinGap = 0.0f;
// The Pass action's "back to the racing line?" compare reads this (set per frame by HookPassFindEntry).
float g_passCompare = 0.0f;

const CodeCheck kCodeChecks[] = {
    // Catch Up option reads
    {0x004084B9, {0xA1, 0xF8, 0xE7, 0x89, 0x00, 0x85, 0xC0}, 7, "AI_CatchUpRubberBand: MOV EAX,[CatchUp]; TEST EAX,EAX"},
    {0x0041757B, {0xA1, 0xF8, 0xE7, 0x89, 0x00, 0x85, 0xC0}, 7, "AI_UpdateCatchUpSpeedScale: MOV EAX,[CatchUp]; TEST EAX,EAX"},
    // AI_FindCarToBlock: start, gap compare, the chains the log follows
    {0x00414130, {0x83, 0xEC, 0x20, 0x8B, 0x44, 0x24, 0x28, 0x53}, 8, "AI_FindCarToBlock prologue"},
    {0x00414150, {0x8B, 0x3E, 0x8B, 0x47, 0x2C}, 5, "AI_FindCarToBlock: vehicle = [ai]; ctrl = [vehicle+0x2C]"},
    {0x0041428F, {0x8B, 0x46, 0x04, 0x8B, 0x57, 0x04, 0x8B, 0x48}, 8, "AI_FindCarToBlock: car = [ctrl+4]; racer = [car+0x14]"},
    {0x004142A1, {0x0F, 0xBE, 0x91, 0x02, 0x09, 0x00, 0x00}, 7, "AI_FindCarToBlock: block style = racer+0x902"},
    {0x004142DA, {0xD9, 0x44, 0x24, 0x10, 0xD8, 0x1D, 0x3C, 0x7D}, 8, "AI_FindCarToBlock: FLD gap; FCOMP [0x007D7D3C]"},
    {0x004142E2, {0x7D, 0x00, 0xDF, 0xE0, 0xF6, 0xC4, 0x05, 0x0F}, 8, "AI_FindCarToBlock: ...; FNSTSW; TEST AH,5; JP"},
    // What follows each CALL (cdecl, 2 args, -1 = no target)
    {0x0041CC5A, {0x83, 0xC4, 0x08, 0x83, 0xF8, 0xFF, 0x7E, 0x20}, 8, "Block still valid: ADD ESP,8; CMP EAX,-1"},
    {0x0042D36F, {0x8B, 0xD8, 0x83, 0xC4, 0x08, 0x83, 0xFB, 0xFF}, 8, "AI_ChooseAction: MOV EBX,EAX; ADD ESP,8; CMP EBX,-1"},
    {0x0042D96F, {0x8B, 0xD8, 0x83, 0xC4, 0x08, 0x83, 0xFB, 0xFF}, 8, "AI_ChooseAction2: MOV EBX,EAX; ADD ESP,8; CMP EBX,-1"},
    // WorldObj_IsHumanRacer: [[obj+0x1C]+0x14]+4 == 1 (the plugin repeats this test itself)
    {0x00614880, {0x8B, 0x49, 0x1C, 0x32, 0xC0, 0x85, 0xC9, 0x74}, 8, "WorldObj_IsHumanRacer: [obj+0x1C]"},
    {0x00614889, {0x8B, 0x41, 0x14, 0x83, 0x78, 0x04, 0x01}, 7, "WorldObj_IsHumanRacer: [..+0x14]+4 == 1"},
    // AI_UpdateCatchUpSpeedScale writes car+0x468 (car = [ctrl+4]) and ctrl+0x770
    {0x0041773B, {0x8B, 0x53, 0x04, 0xD9, 0x9A, 0x68, 0x04, 0x00}, 8, "catch-up result -> [[ctrl+4]+0x468]"},
    {0x00417744, {0x5F, 0xD9, 0x9B, 0x70, 0x07, 0x00, 0x00}, 7, "catch-up raw result -> [ctrl+0x770]"},
    // Pass action slot 2 (0x0042B940): target entry, gap compare, the two racing-line fallbacks
    {0x0042B948, {0x8B, 0x86, 0x18, 0x02, 0x00, 0x00, 0x57, 0x8B}, 8, "Pass: target = [action+0x218]"},
    {0x0042B950, {0x7C, 0x24, 0x24, 0x50, 0x8B, 0xCF, 0x8D, 0x9F}, 8, "Pass: PUSH target; ECX = brain; route = brain+0x30C"},
    {0x0042B97D, {0xD8, 0x1D, 0x3C, 0x7D, 0x7D, 0x00, 0xDF, 0xE0}, 8, "Pass: FCOMP gap,[0.0]; FNSTSW"},
    {0x0042B985, {0xF6, 0xC4, 0x41, 0x75, 0x60}, 5, "Pass: TEST AH,41; JNE racing line"},
    {0x0042B9A4, {0x8D, 0x46, 0x20, 0x55, 0x50, 0x8D, 0x4C, 0x24}, 8, "Pass: plan route around target"},
    {0x0042B9EE, {0x50, 0xEB, 0x05, 0x57, 0x8D, 0x4E, 0x20, 0x51}, 8, "Pass: racing line args (action+0x20, brain)"},
    {0x0042B9F6, {0x8B, 0xCB, 0xE8}, 3, "Pass: ECX = route; CALL racing line"},
    // Brain helpers: entry lookup and gap (entries 0x30 bytes from brain+8, own index brain+0x308, distance +0x10)
    {0x0060BFF0, {0x8B, 0x44, 0x24, 0x04, 0xD9, 0x40, 0x10, 0x0F}, 8, "gap: FLD [entry+0x10]"},
    {0x0060BFF8, {0xBE, 0x81, 0x08, 0x03, 0x00, 0x00, 0x8D, 0x14}, 8, "gap: own index = brain+0x308"},
    {0x0060C000, {0x40, 0xC1, 0xE2, 0x04, 0xD8, 0x64, 0x0A, 0x18}, 8, "gap: FSUB [brain+idx*0x30+0x18]"},
    {0x0060C12C, {0x8B, 0x7C, 0x24, 0x10, 0x8D, 0x51, 0x34, 0x0F}, 8, "entry lookup: handles at brain+0x34"},
    {0x0060C134, {0xBF, 0x1A, 0x39, 0x3C, 0x9D, 0x70, 0xE9, 0x89}, 8, "entry lookup: world object table 0x0089E970"},
    // AI_FindCarToPass callers (cdecl, 3 args)
    {0x0042B85D, {0x83, 0xC4, 0x0C, 0x83, 0xF8, 0xFF}, 6, "Pass re-target: ADD ESP,0xC; CMP EAX,-1"},
    {0x0042D3CA, {0x8B, 0xD8, 0x83, 0xC4, 0x0C, 0x83, 0xFB, 0xFF}, 8, "AI_ChooseAction: pass result"},
    {0x0042D9CA, {0x8B, 0xD8, 0x83, 0xC4, 0x0C, 0x83, 0xFB, 0xFF}, 8, "AI_ChooseAction2: pass result"},
    // Game clock (4000 ticks per second)
    {0x00576D01, {0xC7, 0x05, 0x8C, 0x51, 0x86, 0x00, 0xA0, 0x0F}, 8, "game clock reset to 4000 ticks (1 s)"},
};

// Writes `len` bytes of code at addr. True if they read back correctly.
bool PatchCode(uintptr_t addr, const void* bytes, size_t len) {
    unsigned char* p = reinterpret_cast<unsigned char*>(addr);
    DWORD oldProtect;
    if (!VirtualProtect(p, len, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
    memcpy(p, bytes, len);
    VirtualProtect(p, len, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), p, len);
    return memcmp(p, bytes, len) == 0;
}

// ---- Settings ---------------------------------------------------------------

float IniFloat(const char* section, const char* key, float def, const char* path) {
    char buf[64];
    GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), path);
    if (!buf[0]) return def;
    return static_cast<float>(atof(buf));
}

void LoadSettings() {
    char path[MAX_PATH];
    bf::IniPath(path);
    const bool found = bf::IniFound();
    g_minGap = IniFloat("RaceAI", "BlockMinGap", 0.0f, path);
    g_delay = IniFloat("RaceAI", "BlockDelay", 0.0f, path);
    g_blockPlayer = GetPrivateProfileIntA("RaceAI", "BlockPlayer", 1, path) != 0;
    g_disableCatchUp = GetPrivateProfileIntA("RaceAI", "DisableCatchUp", 1, path) != 0;
    g_passClearGap = IniFloat("RaceAI", "PassClearGap", 0.0f, path);
    if (g_passClearGap < 0.0f) g_passClearGap = 0.0f;
    if (g_passClearGap > 10.0f) g_passClearGap = 10.0f;  // the game ends a pass once the AI is 10 m clear anyway
    g_startGrace = IniFloat("RaceAI", "BlockStartDelay", 0.0f, path);
    if (g_startGrace < 0.0f) g_startGrace = 0.0f;
    if (g_startGrace > 120.0f) g_startGrace = 120.0f;
    if (g_minGap < 0.0f) g_minGap = 0.0f;
    if (g_minGap > 50.0f) g_minGap = 50.0f;  // the search only looks 50 m back anyway
    if (g_delay < 0.0f) g_delay = 0.0f;
    if (g_delay > 30.0f) g_delay = 30.0f;
    Log("Settings (%s): BlockMinGap=%.1f m, BlockDelay=%.2f s, BlockStartDelay=%.0f s, BlockPlayer=%d, "
        "PassClearGap=%.1f m (player only), DisableCatchUp=%d",
        found ? "BayviewForever.ini" : "file not found, defaults", g_minGap, g_delay, g_startGrace, g_blockPlayer, g_passClearGap,
        g_disableCatchUp);
}

// ---- Safe reads (log only) ----------------------------------------------------

template <typename T>
bool Peek(uintptr_t addr, T& out) {
    if (!addr || !IsReadable(addr, sizeof(T))) return false;
    memcpy(&out, reinterpret_cast<const void*>(addr), sizeof(T));
    return true;
}

uintptr_t PeekPtr(uintptr_t addr) {
    uintptr_t v = 0;
    return Peek(addr, v) ? v : 0;
}

int GameClock() {
    int t = 0;
    Peek(gearbox::kGameClock, t);
    return t;
}

const int kTicksPerSecond = 4000;

// Same test as WorldObj_IsHumanRacer (0x00614880).
bool IsHumanObject(uintptr_t obj) {
    uintptr_t driver = PeekPtr(obj + ai::kObjDriver);
    if (!driver) return false;
    uintptr_t info = PeekPtr(driver + ai::kDriverInfo);
    int type = 0;
    return info && Peek(info + ai::kInfoType, type) && type == 1;
}

// ---- Per-AI state and race log ------------------------------------------------

struct AiState {
    uintptr_t ai;
    uintptr_t ctrl, car, racer;
    char      name[24];
    int       skill, style;
    int       calls;           // decision searches this race
    int       lastCall;        // clock of the last search
    long long intervalSum;     // ticks between searches (to learn how often the AI decides)
    int       intervals;
    int       firstSeen;       // clock when the player became its block target in this stretch (0 = not now)
    int       lastSeen;
    bool      heldLogged, blockLogged, refusedLogged;
    int       playerBlocks;    // stretches in which it was allowed to block the player
    int       held;            // stretches the delay held back
    int       aiBlocks;        // block starts on other AIs
    float     maxBoost;        // car+0x468 (applied, >= 1.0)
    float     minRaw, maxRaw;  // ctrl+0x770
    float     loggedBoost;
    int       passFirst, passLast;  // player as its pass target (clock), for once-per-pass log lines
    uintptr_t passAction;           // the Pass action whose racing-line moment was logged
    int       passReasons;          // bit 0 gap reached, bit 1 no room
    int       passes;               // passes on the player
    int       mergeOverlap;         // back to the racing line while still overlapping the player
    float     worstMerge;           // smallest tail-to-nose gap at that moment (negative = overlap)
    int       graceRefused;         // blocks refused by BlockStartDelay
    bool      graceLogged;
    int       aiPassLast;           // clock of the last logged AI-vs-AI pass start near the player
    uintptr_t aiPassAction;         // AI-vs-AI pass whose racing-line moment was logged
    int       aiPassReasons;
    int       nearAiPasses, nearAiBlocks;
};

const int kMaxAi = 16;
AiState g_ai[kMaxAi];
int     g_aiCount       = 0;
int     g_raceNumber    = 0;
int     g_lastClock     = 0;
DWORD   g_lastRealTime  = 0;
int     g_nextSummary   = 0;
int     g_raceStart     = 0;
int     g_logLines      = 0;
const int kMaxLogLines  = 4000;

// Limited log for per-event lines, so a long session can't fill the disk.
template <typename... A>
void EventLog(const char* fmt, A... args) {
    if (g_logLines >= kMaxLogLines) return;
    if (++g_logLines == kMaxLogLines) {
        Log("(event log limit reached, only summaries from now on)");
        return;
    }
    Log(fmt, args...);
}

float Seconds(int ticks) { return ticks / static_cast<float>(kTicksPerSecond); }

void ReadName(AiState& s) {
    snprintf(s.name, sizeof(s.name), "AI#%d", static_cast<int>(&s - g_ai) + 1);
    char buf[17] = {0};
    if (!s.racer || !IsReadable(s.racer + ai::kRacerName, 16)) return;
    memcpy(buf, reinterpret_cast<const void*>(s.racer + ai::kRacerName), 16);
    int n = 0;
    for (; n < 16 && buf[n]; n++)
        if (buf[n] < 0x20 || buf[n] > 0x7E) return;  // not text: keep AI#n
    if (n == 0) return;
    snprintf(s.name, sizeof(s.name), "%s", buf);
}

AiState& StateFor(uintptr_t aiPtr) {
    for (int i = 0; i < g_aiCount; i++)
        if (g_ai[i].ai == aiPtr) return g_ai[i];
    AiState& s = g_ai[g_aiCount < kMaxAi ? g_aiCount++ : kMaxAi - 1];
    memset(&s, 0, sizeof(s));
    s.ai = aiPtr;
    uintptr_t vehicle = PeekPtr(aiPtr + ai::kAiVehicle);
    s.ctrl = vehicle ? PeekPtr(vehicle + ai::kVehicleCtrl) : 0;
    s.car = s.ctrl ? PeekPtr(s.ctrl + ai::kCtrlCar) : 0;
    s.racer = s.car ? PeekPtr(s.car + ai::kCarRacer) : 0;
    signed char v = 0;
    s.skill = (s.racer && Peek(s.racer + ai::kRacerSkill, v)) ? v : -99;
    s.style = (s.racer && Peek(s.racer + ai::kRacerBlockStyle, v)) ? v : -99;
    s.minRaw = 99.0f;
    s.maxRaw = -99.0f;
    s.loggedBoost = 1.0f;
    s.worstMerge = 99.0f;
    ReadName(s);
    Log("  %s: skill %d, block style %d (ai %08X, car %08X)", s.name, s.skill, s.style,
        static_cast<unsigned>(aiPtr), static_cast<unsigned>(s.car));
    return s;
}

void LogSummary(const char* when, int now) {
    if (g_aiCount == 0) return;
    Log("Race %d summary (%s, race clock %.0f s):", g_raceNumber, when, Seconds(now));
    for (int i = 0; i < g_aiCount; i++) {
        const AiState& s = g_ai[i];
        float every = s.intervals ? Seconds(static_cast<int>(s.intervalSum / s.intervals)) * 1000.0f : 0.0f;
        char merge[32] = "-";
        if (s.worstMerge < 98.0f) snprintf(merge, sizeof(merge), "%.1f m", s.worstMerge);
        Log("  %-16s searches %5d (every %4.0f ms) | blocks on player %d, held back %d | blocks on AIs %d | "
            "passes on player %d, back to line overlapping %d, closest %s | near you: AI passes %d, AI blocks %d | "
            "start delay refused %d | catch-up boost max x%.3f (raw %.3f..%.3f)",
            s.name, s.calls, every, s.playerBlocks, s.held, s.aiBlocks, s.passes, s.mergeOverlap, merge,
            s.nearAiPasses, s.nearAiBlocks, s.graceRefused, s.maxBoost, s.minRaw, s.maxRaw);
    }
}

void StartRace(int now) {
    g_raceNumber++;
    g_aiCount = 0;
    int catchUp = -1, mode = -1;
    unsigned char drag = 0, drift = 0, streetX = 0;
    Peek(ai::kCatchUpOption, catchUp);
    Peek(ai::kRaceMode, mode);
    Peek(drag::kIsDragRace, drag);
    Peek(ai::kIsDrift, drift);
    Peek(ai::kIsStreetX, streetX);
    Log("---- Race %d ---- game mode %d (%s), Catch Up option = %d%s, drag %d, drift %d, street X %d",
        g_raceNumber, mode, mode == 8 ? "career" : "not career", catchUp,
        g_disableCatchUp ? " (ignored: DisableCatchUp=1)" : "", drag, drift, streetX);
    g_nextSummary = now + 30 * kTicksPerSecond;
    g_raceStart = now;
}

// Called on every search (game's main thread). Detects a new race and samples the AI's catch-up boost.
AiState& Track(uintptr_t aiPtr, int now) {
    DWORD real = GetTickCount();
    // New race: the game clock went back, or no AI searched for 60 s of real time (loading, menus, free roam).
    if (g_raceNumber == 0 || now + kTicksPerSecond < g_lastClock || real - g_lastRealTime > 60000) {
        if (g_raceNumber > 0) LogSummary("end", g_lastClock);
        StartRace(now);
    }
    g_lastClock = now;
    g_lastRealTime = real;

    AiState& s = StateFor(aiPtr);
    float boost = 0.0f, raw = 0.0f;
    if (s.car && Peek(s.car + ai::kCarSpeedScale, boost)) {
        if (boost > s.maxBoost) s.maxBoost = boost;
        if (boost > 1.001f && boost >= s.loggedBoost + 0.02f) {
            EventLog("CATCH-UP: %s speed boost x%.3f at %.1f s", s.name, boost, Seconds(now));
            s.loggedBoost = boost;
        }
    }
    if (s.ctrl && Peek(s.ctrl + ai::kCtrlRawScale, raw)) {
        if (raw < s.minRaw) s.minRaw = raw;
        if (raw > s.maxRaw) s.maxRaw = raw;
    }
    if (now >= g_nextSummary) {
        LogSummary("so far", now);
        g_nextSummary = now + 30 * kTicksPerSecond;
    }
    return s;
}

// ---- Hooks ----------------------------------------------------------------------

using FindFn = int(__cdecl*)(void* ai, void** outTarget);
const FindFn kOrigFind = reinterpret_cast<FindFn>(ai::kFindCarToBlock);

bool AiNearPlayer(uintptr_t brain, float& ahead);  // below

// The two CALLs that START a block.
int __cdecl HookFindToStart(void* aiPtr, void** outTarget) {
    int result = kOrigFind(aiPtr, outTarget);
    int now = GameClock();
    AiState& s = Track(reinterpret_cast<uintptr_t>(aiPtr), now);
    if (s.calls > 0) {
        s.intervalSum += now - s.lastCall;
        s.intervals++;
    }
    s.calls++;
    s.lastCall = now;

    uintptr_t target = outTarget ? reinterpret_cast<uintptr_t>(*outTarget) : 0;
    if (result == -1 || !target) return result;
    bool human = IsHumanObject(target);

    // No blocks at all (on anyone) in the first BlockStartDelay seconds of the race.
    if (now - g_raceStart < static_cast<int>(g_startGrace * kTicksPerSecond)) {
        s.graceRefused++;
        if (!s.graceLogged) {
            EventLog("START: %s wanted to block %s at %.1f s (%.1f s into the race), not allowed yet", s.name,
                     human ? "the player" : "an AI", Seconds(now), Seconds(now - g_raceStart));
            s.graceLogged = true;
        }
        *outTarget = nullptr;
        return -1;
    }
    if (!human) {
        s.aiBlocks++;
        float ahead = 0.0f;
        if (AiNearPlayer(reinterpret_cast<uintptr_t>(aiPtr), ahead)) {
            s.nearAiBlocks++;
            EventLog("AI-BLOCK: %s blocks another AI at %.1f s, %.1f m %s you", s.name, Seconds(now),
                     ahead < 0.0f ? -ahead : ahead, ahead < 0.0f ? "behind" : "ahead of");
        }
        return result;
    }

    // The player is its block target. A new stretch starts after 1 s without being the target.
    if (s.firstSeen == 0 || now - s.lastSeen > kTicksPerSecond) {
        s.firstSeen = now;
        s.heldLogged = s.blockLogged = s.refusedLogged = false;
    }
    s.lastSeen = now;

    if (!g_blockPlayer) {
        if (!s.refusedLogged) {
            EventLog("NO BLOCK: %s wanted to block the player at %.1f s (BlockPlayer=0)", s.name, Seconds(now));
            s.refusedLogged = true;
        }
        *outTarget = nullptr;
        return -1;
    }
    int waited = now - s.firstSeen;
    if (waited < static_cast<int>(g_delay * kTicksPerSecond)) {
        if (!s.heldLogged) {
            EventLog("HOLD: %s wants to block the player at %.1f s, waiting %.2f s", s.name, Seconds(now), g_delay);
            s.heldLogged = true;
            s.held++;
        }
        *outTarget = nullptr;
        return -1;
    }
    if (!s.blockLogged) {
        EventLog("BLOCK: %s blocks the player at %.1f s (target for %.2f s)", s.name, Seconds(now), Seconds(waited));
        s.blockLogged = true;
        s.playerBlocks++;
    }
    return result;
}

// The Block action's "still valid?" check: keeps the stretch alive while the block runs.
int __cdecl HookFindStillValid(void* aiPtr, void** outTarget) {
    int result = kOrigFind(aiPtr, outTarget);
    if (result == -1 || !outTarget || !*outTarget) return result;
    int now = GameClock();
    AiState& s = Track(reinterpret_cast<uintptr_t>(aiPtr), now);
    if (IsHumanObject(reinterpret_cast<uintptr_t>(*outTarget)) && s.firstSeen != 0) s.lastSeen = now;
    return result;
}

// ---- Pass (overtake) ----------------------------------------------------------------

// Entry of the brain's race-order list for a world object, or 0. Plain reads, like 0x0060C120.
uintptr_t EntryFor(uintptr_t brain, uintptr_t obj) {
    int count = 0;
    if (!obj || !Peek(brain + ai::kBrainCount, count) || count <= 0 || count > 16) return 0;
    for (int i = 0; i < count; i++) {
        uintptr_t e = brain + ai::kBrainEntries + i * ai::kEntrySize;
        short h = 0;
        if (Peek(e + ai::kEntryHandle, h) && h > 0 && h < 128 && PeekPtr(ai::kWorldObjects + h * 4) == obj) return e;
    }
    return 0;
}

uintptr_t OwnEntry(uintptr_t brain) {
    signed char idx = -1;
    if (!Peek(brain + ai::kBrainOwnIndex, idx) || idx < 0 || idx >= 16) return 0;
    return brain + ai::kBrainEntries + idx * ai::kEntrySize;
}

float HalfLength(uintptr_t entry) {
    float h = 0.0f;
    return (Peek(entry + ai::kEntryHalfLength, h) && h > 0.5f && h < 10.0f) ? h : 2.25f;
}

// Tail-to-nose gap between a passing AI and its target: > 0 = the AI is clear ahead, < 0 = they overlap.
bool ClearGap(uintptr_t brain, uintptr_t target, float& out) {
    uintptr_t e = EntryFor(brain, target), own = OwnEntry(brain);
    float de = 0.0f, dOwn = 0.0f;
    if (!e || !own || !Peek(e + ai::kEntryDistance, de) || !Peek(own + ai::kEntryDistance, dOwn)) return false;
    out = (dOwn - de) - HalfLength(e) - HalfLength(own);
    return true;
}

// True if the player is within kNearPlayer m of this AI along the route; ahead = AI distance - player distance.
bool AiNearPlayer(uintptr_t brain, float& ahead) {
    int count = 0;
    uintptr_t own = OwnEntry(brain);
    float dOwn = 0.0f;
    if (!own || !Peek(own + ai::kEntryDistance, dOwn) || !Peek(brain + ai::kBrainCount, count) || count <= 0 || count > 16)
        return false;
    for (int i = 0; i < count; i++) {
        uintptr_t e = brain + ai::kBrainEntries + i * ai::kEntrySize;
        short h = 0;
        float d = 0.0f;
        if (e == own || !Peek(e + ai::kEntryHandle, h) || h <= 0 || h >= 128) continue;
        if (!IsHumanObject(PeekPtr(ai::kWorldObjects + h * 4)) || !Peek(e + ai::kEntryDistance, d)) continue;
        ahead = dOwn - d;
        return ahead > -kNearPlayer && ahead < kNearPlayer;
    }
    return false;
}

using FindEntryFn = uintptr_t(__thiscall*)(void* brain, void* obj);
const FindEntryFn kOrigFindEntry = reinterpret_cast<FindEntryFn>(ai::kBrainFindEntry);

// CALL 0x0042B95C in Pass slot 2: returns the target entry, and sets the number the next compare reads.
// Game: back to the racing line when gap (center to center) <= 0. Here: when gap <= -(both half-lengths + clear gap).
uintptr_t __fastcall HookPassFindEntry(void* brain, void* /*edx*/, void* obj) {
    uintptr_t entry = kOrigFindEntry(brain, obj);
    g_passCompare = 0.0f;
    uintptr_t own = OwnEntry(reinterpret_cast<uintptr_t>(brain));
    // Only when it passes the player: AI-vs-AI passes held wide spread the AIs across the road (AI round 2).
    if (entry && own && IsHumanObject(reinterpret_cast<uintptr_t>(obj)))
        g_passCompare = -(HalfLength(entry) + HalfLength(own) + g_passClearGap);
    return entry;
}

using RacingLineFn = uintptr_t(__thiscall*)(void* route, void* actionArg, void* brain);
const RacingLineFn kOrigRacingLine = reinterpret_cast<RacingLineFn>(ai::kSetRacingLine);

// Logs (once per pass and reason) when an AI passing the player steers back to its racing line.
void LogRacingLine(void* actionArg, void* brainPtr, int reason) {
    uintptr_t brain = reinterpret_cast<uintptr_t>(brainPtr);
    uintptr_t action = reinterpret_cast<uintptr_t>(actionArg) - ai::kActionRouteArg;
    uintptr_t target = PeekPtr(action + ai::kActionTarget);
    if (!target) return;
    int now = GameClock();
    if (!IsHumanObject(target)) {
        // AI passing another AI: logged when it happens within kNearPlayer m of the player.
        float ahead = 0.0f;
        if (!AiNearPlayer(brain, ahead)) return;
        AiState& s = Track(brain, now);
        if (s.aiPassAction != action) {
            s.aiPassAction = action;
            s.aiPassReasons = 0;
        }
        if (s.aiPassReasons & (1 << reason)) return;
        s.aiPassReasons |= 1 << reason;
        EventLog("AI-PASS: %s (passing another AI) %s at %.1f s, %.1f m %s you", s.name,
                 reason == 0 ? "steers back to its racing line" : "finds no room, takes its racing line", Seconds(now),
                 ahead < 0.0f ? -ahead : ahead, ahead < 0.0f ? "behind" : "ahead of");
        return;
    }
    AiState& s = Track(brain, now);
    if (s.passAction != action) {
        s.passAction = action;
        s.passReasons = 0;
    }
    if (s.passReasons & (1 << reason)) return;
    s.passReasons |= 1 << reason;
    float gap = 0.0f;
    bool haveGap = ClearGap(brain, target, gap);
    if (reason == 0 && haveGap) {
        if (gap < 0.0f) s.mergeOverlap++;
        if (gap < s.worstMerge) s.worstMerge = gap;
    }
    if (!haveGap)
        EventLog("PASS: %s %s at %.1f s (gap unknown)", s.name,
                 reason == 0 ? "steers back to its racing line beside the player" : "finds no room around the player, takes its racing line",
                 Seconds(now));
    else
        EventLog("PASS: %s %s at %.1f s: its tail %.1f m %s your nose", s.name,
                 reason == 0 ? "steers back to its racing line beside the player" : "finds no room around the player, takes its racing line",
                 Seconds(now), gap < 0.0f ? -gap : gap,
                 gap >= 0.0f ? "ahead of" : (gap > -4.0f * HalfLength(OwnEntry(brain)) ? "BEHIND (overlapping)" : "behind"));
}

// 0x0042B9C9: the side route planner found no room.
uintptr_t __fastcall HookRacingLineNoRoom(void* route, void* /*edx*/, void* actionArg, void* brain) {
    LogRacingLine(actionArg, brain, 1);
    return kOrigRacingLine(route, actionArg, brain);
}

// 0x0042B9F8: gap reached (or no target).
uintptr_t __fastcall HookRacingLineGap(void* route, void* /*edx*/, void* actionArg, void* brain) {
    LogRacingLine(actionArg, brain, 0);
    return kOrigRacingLine(route, actionArg, brain);
}

using FindPassFn = int(__cdecl*)(void* brain, void** outTarget, void* outRoute);
const FindPassFn kOrigFindPass = reinterpret_cast<FindPassFn>(ai::kFindCarToPass);

// The three CALLs to AI_FindCarToPass: logs when an AI starts passing the player.
int __cdecl HookFindToPass(void* brain, void** outTarget, void* outRoute) {
    int result = kOrigFindPass(brain, outTarget, outRoute);
    uintptr_t target = outTarget ? reinterpret_cast<uintptr_t>(*outTarget) : 0;
    if (result == -1 || !target) return result;
    int now = GameClock();
    if (!IsHumanObject(target)) {
        float ahead = 0.0f;
        if (!AiNearPlayer(reinterpret_cast<uintptr_t>(brain), ahead)) return result;
        AiState& s = Track(reinterpret_cast<uintptr_t>(brain), now);
        if (s.aiPassLast == 0 || now - s.aiPassLast > 2 * kTicksPerSecond) {
            s.nearAiPasses++;
            EventLog("AI-PASS: %s starts passing another AI at %.1f s, %.1f m %s you", s.name, Seconds(now),
                     ahead < 0.0f ? -ahead : ahead, ahead < 0.0f ? "behind" : "ahead of");
        }
        s.aiPassLast = now;
        return result;
    }
    AiState& s = Track(reinterpret_cast<uintptr_t>(brain), now);
    if (s.passFirst == 0 || now - s.passLast > 2 * kTicksPerSecond) {
        s.passFirst = now;
        s.passes++;
        float gap = 0.0f;
        if (ClearGap(reinterpret_cast<uintptr_t>(brain), target, gap))
            EventLog("PASS: %s starts passing the player at %.1f s (its nose %.1f m behind your tail)", s.name,
                     Seconds(now), -gap);
        else
            EventLog("PASS: %s starts passing the player at %.1f s", s.name, Seconds(now));
    }
    s.passLast = now;
    return result;
}

// ---- Startup ----------------------------------------------------------------------

DWORD WINAPI WorkerThread(LPVOID) {
    if (!ExeMd5Ok() || !CodeMatches(kCodeChecks, sizeof(kCodeChecks) / sizeof(kCodeChecks[0]))) {
        Log("Not patching anything.");
        return 0;
    }
    LoadSettings();

    if (g_disableCatchUp) {
        const unsigned char kXorEax[] = {0x31, 0xC0, 0x90, 0x90, 0x90};  // XOR EAX,EAX + 3 NOPs
        for (uintptr_t site : ai::kCatchUpReads) {
            if (PatchCode(site, kXorEax, sizeof(kXorEax)))
                Log("Patched 0x%08X: AI reads Catch Up as 0.", static_cast<unsigned>(site));
            else
                Log("Could not patch the Catch Up read at 0x%08X.", static_cast<unsigned>(site));
        }
    }

    if (g_minGap > 0.0f) {
        g_negMinGap = -g_minGap;
        uintptr_t operand = reinterpret_cast<uintptr_t>(&g_negMinGap);
        if (PatchCode(ai::kBlockGapCompare + 2, &operand, 4))
            Log("Patched 0x%08X: AIs block only cars more than %.1f m behind them.",
                static_cast<unsigned>(ai::kBlockGapCompare), g_minGap);
        else
            Log("Could not patch the block gap compare at 0x%08X.", static_cast<unsigned>(ai::kBlockGapCompare));
    }

    if (g_passClearGap > 0.0f) {
        uintptr_t operand = reinterpret_cast<uintptr_t>(&g_passCompare);
        if (RedirectCall(ai::kPassFindEntryCalls[0], ai::kBrainFindEntry, reinterpret_cast<const void*>(&HookPassFindEntry)) &&
            PatchCode(ai::kPassGapCompare + 2, &operand, 4))
            Log("Patched 0x%08X: AIs passing the player keep their line until their tail is %.1f m ahead of the player.",
                static_cast<unsigned>(ai::kPassGapCompare), g_passClearGap);
        else
            Log("Could not patch the pass gap compare at 0x%08X.", static_cast<unsigned>(ai::kPassGapCompare));
    }
    int passHooks = 0;
    if (RedirectCall(ai::kPassRacingLineCalls[0], ai::kSetRacingLine, reinterpret_cast<const void*>(&HookRacingLineNoRoom))) passHooks++;
    if (RedirectCall(ai::kPassRacingLineCalls[1], ai::kSetRacingLine, reinterpret_cast<const void*>(&HookRacingLineGap))) passHooks++;
    for (uintptr_t site : ai::kFindCarToPassCallSites)
        if (RedirectCall(site, ai::kFindCarToPass, reinterpret_cast<const void*>(&HookFindToPass))) passHooks++;
    Log("Hooked %d of 5 pass calls (log).", passHooks);

    const void* hooks[] = {reinterpret_cast<const void*>(&HookFindStillValid),
                           reinterpret_cast<const void*>(&HookFindToStart),
                           reinterpret_cast<const void*>(&HookFindToStart)};
    int hooked = 0;
    for (size_t i = 0; i < 3; i++) {
        if (RedirectCall(ai::kFindCarToBlockCallSites[i], ai::kFindCarToBlock, hooks[i]))
            hooked++;
        else
            Log("Could not hook the CALL at 0x%08X.", static_cast<unsigned>(ai::kFindCarToBlockCallSites[i]));
    }
    Log("Hooked %d of 3 calls to AI_FindCarToBlock. Waiting for a race.", hooked);
    return 0;
}

void Init() {
    Log("==== Race AI started ====");
    // CryptoAPI must not run inside DllMain, so the MD5 check (and everything after it) runs on its own thread.
    HANDLE thread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
}

}  // namespace

void bf::StartRaceAI() { Init(); }
