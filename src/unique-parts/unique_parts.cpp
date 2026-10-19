// UniqueParts: after the career, every [UniqueParts] OutrunsPerPart outrun wins with a car model earn it a unique part,
// taking turns between performance and visual. Performance: the stock STAGE_3/4/5_PERF challenge with the most parts
// the car lacks (all 9 collected = every reward is visual). Visual: the 5 stock VISUAL challenges in turn (hood, rims,
// spoiler, vinyl, widebody), skipping kinds the car has no unique parts for (SUVs: no widebody) and a start marker an
// Endless Career race is using right now. Visual rewards never run out: the pick screen always offers all of the
// kind, so the car can be re-styled. While driving that car the challenge is on the map; picking or declining uses
// the reward up. RaceToShop = 0: no timed run, the pick screen opens on the next visit to the challenge's shop.
//
// The stock challenge chain has no stage checks except the unlock step, so after Career_RebuildActiveEvents the
// challenge is put into the active world-challenge list (g_Career+0x7148). Endless Career owns those CALLs and calls
// bf::UniquePartsAfterRebuild(), so this needs EndlessCareer = 1.
// Hooks: CALL 0x005F84D7 (Career_OnOutrunFinished: count), 0x00499497 (Career_OnEnterShop, chained with
// RespawnAtGarage; RaceToShop = 0), 0x00561826 (FECarSession_GrantUniquePerfPart: log), 0x0056178E / 0x00561870
// (WorldChallenge_Complete: use up the reward). Before the career is finished every hook only passes through.
// State per profile: BayviewForever\endless-career\<profile>\unique-parts.txt. Undo: [Features] UniqueParts = 0.

#define BF_TAG "UniqueParts"
#include "nfsu2.h"

using namespace nfsu2;

namespace {

volatile LONG g_versionOk = 0;  // set by the MD5 thread; until then every hook only passes through
bool          g_hooksOk   = false;

const int kMaxVisualWaiting = 5;  // visual rewards waiting per car at most

// ---- Settings (scripts\BayviewForever.ini, [UniqueParts]) ---------------------------------

int  g_every      = 10;    // outrun wins with one car per unique part (0 = never)
bool g_raceToShop = true;  // false = no timed run: the pick screen opens when entering the shop
bool g_message    = true;  // Rachel's text when a part is earned (timed run only)

void LoadSettings() {
    char path[MAX_PATH];
    bf::IniPath(path);
    int every = static_cast<int>(GetPrivateProfileIntA("UniqueParts", "OutrunsPerPart", 10, path));
    g_every   = every < 0 ? 0 : (every > 1000 ? 1000 : every);
    g_raceToShop = GetPrivateProfileIntA("UniqueParts", "RaceToShop", 1, path) != 0;
    g_message = GetPrivateProfileIntA("UniqueParts", "Message", 1, path) != 0;
    Log("Settings %s: a unique part every %d outrun win(s) per car%s (performance and visual in turn), %s, "
        "message %s.", bf::IniFound() ? "from BayviewForever.ini" : "(no BayviewForever.ini, defaults)", g_every,
        g_every ? "" : " (= never)",
        g_raceToShop ? "timed race to the shop" : "no race: pick it at the shop",
        !g_raceToShop ? "off (RaceToShop = 0)" : (g_message ? "on" : "off"));
}

// ---- Code checks ---------------------------------------------------------------------

const CodeCheck kCodeChecks[] = {
    // The outrun result: the only CALL to Career_OnOutrunFinished, its prologue and RET 4
    {0x005F84CE, {0xB9, 0x40, 0xAD, 0x85, 0x00, 0x0F, 0x94, 0xC0}, 8, "MOV ECX,g_Career; SETE AL (outrun won)"},
    {0x005F84D7, {0xE8, 0x44, 0x8E, 0xF4, 0xFF}, 5, "CALL Career_OnOutrunFinished"},
    {0x00541320, {0xA1, 0x78, 0x00, 0x85, 0x00, 0x56, 0x8B, 0xF1}, 8, "Career_OnOutrunFinished: game mode 0x00850078"},
    {0x0054141D, {0xC2, 0x04, 0x00}, 3, "RET 4 of Career_OnOutrunFinished"},
    // The active world-challenge list (g_Career+0x7148, 32 entries) and who reads it
    {0x00527E88, {0x8D, 0x8B, 0x48, 0x71, 0x00, 0x00, 0xE8, 0xDD}, 8, "LEA ECX,[career+0x7148]; CALL CheckUnlock"},
    {0x0053365C, {0x8D, 0x8E, 0x48, 0x71, 0x00, 0x00}, 6, "map markers: LEA ECX,[career+0x7148]"},
    {0x005335B8, {0x83, 0xFD, 0x20}, 3, "map markers: CMP EBP,0x20 (32 challenges)"},
    {0x00527D93, {0x8B, 0x8A, 0x08, 0x0C, 0x00, 0x00}, 6, "MOV ECX,[EDX+0xC08] (challenge running)"},
    // World challenges table (count, pointer) and the strings
    {0x00527E41, {0x8B, 0x15, 0x20, 0x84, 0x83, 0x00}, 6, "world challenge count at 0x00838420"},
    {0x00527E5B, {0x8B, 0x0D, 0x24, 0x84, 0x83, 0x00}, 6, "world challenges at 0x00838424"},
    {0x005419C0, {0x8B, 0x15, 0x28, 0x84, 0x83, 0x00}, 6, "WorldChallenge_Complete: MOV EDX,[g_GCareerStrings]"},
    // WorldChallenge_Start / _Complete: challenge at state+8, step at state+0x11C
    {0x00533C4D, {0x89, 0x7E, 0x08}, 3, "WorldChallenge_Start: state+8 = challenge"},
    {0x005419C9, {0x8B, 0x46, 0x08}, 3, "WorldChallenge_Complete: challenge = state+8"},
    {0x00541A04, {0xE9, 0x97, 0x78, 0xFB, 0xFF}, 5, "WorldChallenge_Complete ends with JMP 0x004F92A0"},
    {0x00561782, {0xB9, 0x40, 0xB9, 0x85, 0x00}, 5, "MOV ECX,0x0085B940 before Complete (declined)"},
    {0x0056186B, {0xB9, 0x40, 0xB9, 0x85, 0x00}, 5, "MOV ECX,0x0085B940 before Complete (part picked)"},
    // Granting: ECX = g_FECarSession, RET 4; the part list checks the car's slot +0x2F4 + cat*4
    {0x00561821, {0xB9, 0x80, 0x89, 0x83, 0x00}, 5, "MOV ECX,g_FECarSession before the grant"},
    {0x00521430, {0x8B, 0x44, 0x24, 0x04, 0x56, 0x57, 0x6A, 0x01}, 8, "prologue of FECarSession_GrantUniquePerfPart"},
    {0x00521473, {0xC2, 0x04, 0x00}, 3, "RET 4 of FECarSession_GrantUniquePerfPart"},
    {0x005340DD, {0x8B, 0x8C, 0x86, 0xF4, 0x02, 0x00, 0x00}, 7, "part list: MOV ECX,[slot+cat*4+0x2F4]"},
    // Visual part list: type 1 branch, vinyl id 0x3E, rims id 0x0D, other ids -> part slot, lookup at level 4
    {0x00533DA9, {0x83, 0xFA, 0x01, 0x0F, 0x85}, 5, "part list: CMP EDX,1 (visual)"},
    {0x00533DB2, {0x83, 0xF8, 0x3E}, 3, "part list: CMP EAX,0x3E (vinyls)"},
    {0x00534061, {0xE8, 0x5A, 0xE0, 0xFC, 0xFF}, 5, "part list: CALL part id -> part slot (0x005020C0)"},
    {0x00534072, {0x6A, 0x04, 0x6A, 0x00, 0x8B, 0xF0, 0x57, 0x56}, 8, "part list: PUSH 4; PUSH 0; PUSH slot; PUSH car type"},
    {0x0053407A, {0xB9, 0x68, 0x2B, 0x8A, 0x00, 0xE8, 0xAC, 0x79}, 8, "part list: MOV ECX,partDb; CALL first part"},
    {0x0061BA30, {0x8B, 0x44, 0x24, 0x10, 0x8B, 0x54, 0x24, 0x0C}, 8, "prologue of the first-part lookup"},
    {0x0061BA4B, {0xC2, 0x10, 0x00}, 3, "RET 0x10 of the first-part lookup"},
    {0x00503AE0, {0x8B, 0x49, 0x20, 0x51, 0xE8, 0x47, 0xC6, 0x10}, 8, "CarRecord GetCarTypeIndex (model at +0x20)"},
    {0x00503AF4, {0x8B, 0x80, 0x40, 0x08, 0x00, 0x00, 0xC3}, 7, "GetCarTypeIndex: entry+0x840, plain RET"},
    // Messages and markers
    {0x005338C0, {0x51, 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x10, 0x8B}, 8, "prologue of WorldChallenge_QueueUnlockSMS"},
    {0x00533AF5, {0xB9, 0x60, 0x0A, 0x86, 0x00, 0xE8, 0xC1, 0xFD}, 8, "MOV ECX,inbox; CALL WorldChallenge_QueueUnlockSMS"},
    {0x00501310, {0x55, 0x8B, 0x6C, 0x24, 0x08, 0x85, 0xED, 0x75}, 8, "prologue of SMS_FindMessage"},
    {0x00533310, {0x83, 0xEC, 0x20, 0x56, 0x8B, 0xF1}, 6, "prologue of Career_RebuildMapMarkers"},
    {0x00528431, {0x8B, 0x89, 0x40, 0x87, 0x00, 0x00}, 6, "current car slot hash at g_Career+0x8740"},
    // A race on the map wins a shared trigger (FUN_00527780: race +0x28 vs the 64 active events at g_Career+0x71C8)
    {0x0052781C, {0xE8, 0x5F, 0xFF, 0xFF, 0xFF}, 5, "WorldTrigger_Classify: races first (CALL 0x00527780)"},
    // RaceToShop = 0: Career_OnEnterShop (thiscall(g_Career, shop), RET 4) ends with the challenge check
    {0x0049948D, {0xB9, 0x40, 0xAD, 0x85, 0x00}, 5, "MOV ECX,g_Career before Career_OnEnterShop"},
    {0x0054CAF6, {0x8B, 0x7D, 0x08, 0x8B, 0x47, 0x38}, 6, "Career_OnEnterShop: shop key at +0x38"},
    {0x0054CC61, {0x8B, 0x86, 0x08, 0x0C, 0x00, 0x00}, 6, "Career_OnEnterShop: MOV EAX,[career+0xC08]"},
    {0x0054CC74, {0x8B, 0x57, 0x38}, 3, "Career_OnEnterShop: shop key vs challenge destination"},
    {0x0054CC80, {0xC7, 0x81, 0x1C, 0x01, 0x00, 0x00, 0x04, 0x00}, 8, "Career_OnEnterShop: step = 4"},
    {0x0054CC93, {0xC2, 0x04, 0x00}, 3, "RET 4 of Career_OnEnterShop"},
    {0x00533C50, {0xC7, 0x86, 0x1C, 0x01, 0x00, 0x00, 0x01, 0x00}, 8, "WorldChallenge_Start: step = 1"},
    {0x00533C80, {0xD9, 0x5E, 0x04}, 3, "WorldChallenge_Start: timer at state+4"},
};

// ---- Game data -----------------------------------------------------------------------

bool CareerFinished() {  // same test as Covers / Virtual Garage
    if (*reinterpret_cast<volatile int32_t*>(ev::kCurrentStage) >= 6) return true;
    uintptr_t stage5 = *reinterpret_cast<volatile uintptr_t*>(ev::kCareer + ev::kCareerStage5Data);
    if (!stage5 || !IsReadable(stage5 + 0x28, 4)) return false;
    using Fn = unsigned char(__fastcall*)(uintptr_t);
    return reinterpret_cast<Fn>(kCareerIsFinished)(ev::kCareer) != 0;
}

// The career slot of the car being driven (occupied), or nullptr.
const unsigned char* CurrentSlot() {
    uint32_t hash = *reinterpret_cast<const uint32_t*>(kCurrentSlotHash);
    for (int n = 0; n < kMaxSlots; n++) {
        const unsigned char* s = SlotPtr(n);
        if (ReadAt<uint32_t>(s, slot::kHash) == hash && s[slot::kOccupied]) return s;
    }
    return nullptr;
}

uint32_t ModelOf(const unsigned char* s) { return s ? ReadAt<uint32_t>(s, slot::kCarModel) : 0; }

bool HasUnique(const unsigned char* s, int cat) {
    return cat >= 0 && ReadAt<uint32_t>(s, uniques::kSlotUniqueFlags + cat * 4) != 0;
}

int MissingParts(const unsigned char* s) {
    int n = 0;
    for (int cat = 0; cat < uniques::kNumCategories; cat++) n += !HasUnique(s, cat);
    return n;
}

const char* GameString(uint16_t offset) {
    uintptr_t base = *reinterpret_cast<volatile uintptr_t*>(ev::kStringsPtr);
    if (!base || !IsReadable(base + offset, 1)) return "?";
    return reinterpret_cast<const char*>(base + offset);
}

const char* ChallengeName(const unsigned char* c) { return GameString(ReadAt<uint16_t>(c, uniques::challenge::kName)); }

// The game's world challenges of `type` (2 performance, 1 visual), in table order. Returns how many.
int Challenges(uint8_t type, const unsigned char* out[8]) {
    int count = *reinterpret_cast<const int32_t*>(covers::kWorldChallengeCount);
    uintptr_t t = *reinterpret_cast<const uintptr_t*>(covers::kWorldChallengesPtr);
    if (count <= 0 || count > 1000 || !t || !IsReadable(t, count * covers::challenge::kSize)) return 0;
    int n = 0;
    for (int i = 0; i < count && n < 8; i++) {
        const unsigned char* c = reinterpret_cast<const unsigned char*>(t + i * covers::challenge::kSize);
        if (c[covers::challenge::kType] == type) out[n++] = c;
    }
    return n;
}

// 2 = one of the performance challenges, 1 = a visual one, 0 = neither.
int ChallengeKind(uintptr_t p) {
    static const uint8_t kTypes[] = {uniques::kTypePerf, uniques::kTypeVisual};
    for (uint8_t type : kTypes) {
        const unsigned char* list[8];
        int n = Challenges(type, list);
        for (int i = 0; i < n; i++)
            if (reinterpret_cast<uintptr_t>(list[i]) == p) return type;
    }
    return 0;
}

// How many of challenge c's parts the car in slot s doesn't have yet.
int MissingIn(const unsigned char* c, const unsigned char* s) {
    int n = 0;
    for (int k = 0; k < 3; k++) {
        int cat = uniques::PartCategory(c[uniques::challenge::kParts + k]);
        if (cat >= 0 && cat < uniques::kNumCategories && !HasUnique(s, cat)) n++;
    }
    return n;
}

// "Engine, Transmission, Tires" (the parts challenge c offers; the ones the car has already in brackets).
void PartList(const unsigned char* c, const unsigned char* s, char* out, size_t size) {
    out[0] = '\0';
    size_t len = 0;
    for (int k = 0; k < 3; k++) {
        int cat = uniques::PartCategory(c[uniques::challenge::kParts + k]);
        if (cat < 0) continue;
        bool has = s && HasUnique(s, cat);
        len += snprintf(out + len, len < size ? size - len : 0, "%s%s%s%s", len ? ", " : "", has ? "(" : "",
                        uniques::CategoryName(cat), has ? ")" : "");
    }
}

// The performance challenge to offer the car in slot s: the one with the most parts it lacks (first on a tie).
const unsigned char* PickPerfChallenge(const unsigned char* s) {
    const unsigned char* list[8];
    int n = Challenges(uniques::kTypePerf, list);
    const unsigned char* best = nullptr;
    int bestMissing = 0;
    for (int i = 0; i < n; i++) {
        int m = MissingIn(list[i], s);
        if (m > bestMissing) {
            best = list[i];
            bestMissing = m;
        }
    }
    return best;
}

// ---- Visual uniques ----------------------------------------------------------------------

// Index into uniques::Visual() of challenge c (by its part byte), or -1.
int VisualKindOf(const unsigned char* c) {
    for (int k = 0; k < uniques::kNumVisualKinds; k++)
        if (c[uniques::challenge::kParts] == uniques::Visual(k).id) return k;
    return -1;
}

const unsigned char* VisualChallenge(int kind) {
    const unsigned char* list[8];
    int n = Challenges(uniques::kTypeVisual, list);
    for (int i = 0; i < n; i++)
        if (VisualKindOf(list[i]) == kind) return list[i];
    return nullptr;
}

// Whether the car in slot s has unique parts of this kind (hoods, spoilers, widebody kits: asks the part database
// as the pick screen does; rims and vinyls always have some). Main thread.
bool CarHasVisual(const unsigned char* s, int kind) {
    int partSlot = uniques::Visual(kind).partSlot;
    if (partSlot < 0) return true;
    using TypeFn = int(__fastcall*)(const unsigned char*);  // thiscall(slot), plain RET
    int carType = reinterpret_cast<TypeFn>(uniques::kGetCarTypeIndex)(s);
    if (carType < 0) return false;
    using PartFn = uintptr_t(__fastcall*)(uintptr_t, void*, int, int, int, int);  // thiscall, RET 0x10
    return reinterpret_cast<PartFn>(uniques::kPartFirst)(uniques::kPartDb, nullptr, carType, partSlot, 0,
                                                         uniques::kUniqueLevel) != 0;
}

// True if an event on the map right now sits on challenge c's start trigger (the race would win the marker).
bool StartTriggerTaken(const unsigned char* c) {
    uint32_t trigger = StringHash(GameString(ReadAt<uint16_t>(c, uniques::challenge::kTrigger)));
    const uintptr_t* events = reinterpret_cast<const uintptr_t*>(ev::kActiveEvents);
    for (int i = 0; i < 64; i++) {
        uintptr_t r = events[i];
        if (r && IsReadable(r + ev::race::kTrigger, 4) &&
            *reinterpret_cast<const uint32_t*>(r + ev::race::kTrigger) == trigger)
            return true;
    }
    return false;
}

bool AnyVisualFor(const unsigned char* s) {
    for (int k = 0; k < uniques::kNumVisualKinds; k++)
        if (VisualChallenge(k) && CarHasVisual(s, k)) return true;
    return false;
}

// The visual challenge to offer next, starting at kind `next`: one the car has parts for and (if `free`) whose start
// marker no race is using.
const unsigned char* PickVisualChallenge(const unsigned char* s, int next, bool free) {
    for (int i = 0; i < uniques::kNumVisualKinds; i++) {
        int k = (next + i) % uniques::kNumVisualKinds;
        const unsigned char* c = VisualChallenge(k);
        if (c && CarHasVisual(s, k) && !(free && StartTriggerTaken(c))) return c;
    }
    return nullptr;
}

// ---- Per-profile state (endless-career\<profile>\unique-parts.txt) ----------------------------

const int kMaxCars = 64;
struct State {
    char     profile[33];  // "" = nothing loaded
    int      count;
    uint32_t model[kMaxCars];
    int      outruns[kMaxCars];      // post-game outrun wins with this car model
    int      waitingPerf[kMaxCars];  // performance rewards earned, not collected yet
    int      waitingVis[kMaxCars];   // visual rewards earned, not collected yet
    int      earned[kMaxCars];       // rewards earned so far (even = performance's turn next)
    int      nextVisual[kMaxCars];   // the visual kind whose turn is next (uniques::Visual index)
};
State g_state;

bool ProfileName(char out[33]) {
    memcpy(out, reinterpret_cast<const void*>(kPlayer1Profile), 32);
    out[32] = '\0';
    if (!out[0]) return false;
    for (const char* c = out; *c; c++)
        if (strchr("\\/:*?\"<>|", *c)) return false;
    return true;
}

void StatePath(const char* profile, char out[MAX_PATH]) {
    char dir[MAX_PATH];
    GetDataDir(dir);
    snprintf(out, MAX_PATH, "%sendless-career", dir);
    CreateDirectoryA(out, nullptr);
    size_t n = strlen(out);
    snprintf(out + n, MAX_PATH - n, "\\%s", profile);
    CreateDirectoryA(out, nullptr);
    n = strlen(out);
    snprintf(out + n, MAX_PATH - n, "\\unique-parts.txt");
}

void SaveState() {
    if (!g_state.profile[0]) return;
    char path[MAX_PATH], tmp[MAX_PATH];
    StatePath(g_state.profile, path);
    snprintf(tmp, sizeof(tmp), "%s.new", path);
    FILE* f = fopen(tmp, "w");
    if (!f) {
        Log("COULD NOT write %s.", tmp);
        return;
    }
    fprintf(f, "; BayviewForever unique parts for profile \"%s\". Written by the mod; edit only while the game is closed.\n"
               "; car <model hash> <post-game outrun wins> <performance parts waiting> <visual parts waiting>\n"
               ";     <rewards earned> <next visual kind: 0 hood, 1 rims, 2 spoiler, 3 vinyl, 4 widebody>\n",
            g_state.profile);
    for (int i = 0; i < g_state.count; i++)
        fprintf(f, "car %08X %d %d %d %d %d ; %s\n", static_cast<unsigned>(g_state.model[i]), g_state.outruns[i],
                g_state.waitingPerf[i], g_state.waitingVis[i], g_state.earned[i], g_state.nextVisual[i],
                CarName(g_state.model[i]));
    bool ok = fclose(f) == 0 && MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING);
    if (!ok) Log("COULD NOT replace %s (error %lu).", path, GetLastError());
}

void LoadState(const char* profile) {
    memset(&g_state, 0, sizeof(g_state));
    snprintf(g_state.profile, sizeof(g_state.profile), "%s", profile);
    char path[MAX_PATH], line[256];
    StatePath(profile, path);
    FILE* f = fopen(path, "r");
    if (!f) {
        Log("Profile \"%s\": no unique-parts.txt yet (no post-game outrun wins counted).", profile);
        return;
    }
    auto clampInt = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
    int perf = 0, vis = 0;
    while (fgets(line, sizeof(line), f)) {
        unsigned m = 0;
        int o = 0, wp = 0, wv = 0, e = 0, nv = 0;
        int got = sscanf(line, "car %x %d %d %d %d %d", &m, &o, &wp, &wv, &e, &nv);
        if (got < 3 || g_state.count >= kMaxCars) continue;
        if (got < 6) {  // a 0.2.x line (performance parts only)
            wv = 0;
            e = g_every > 0 ? o / g_every : 0;
            nv = 0;
        }
        int i = g_state.count++;
        g_state.model[i] = m;
        g_state.outruns[i] = clampInt(o, 0, 1000000);
        g_state.waitingPerf[i] = clampInt(wp, 0, uniques::kNumCategories);
        g_state.waitingVis[i] = clampInt(wv, 0, kMaxVisualWaiting);
        g_state.earned[i] = clampInt(e, 0, 1000000);
        g_state.nextVisual[i] = clampInt(nv, 0, uniques::kNumVisualKinds - 1);
        perf += g_state.waitingPerf[i];
        vis += g_state.waitingVis[i];
    }
    fclose(f);
    Log("Profile \"%s\": loaded unique-parts.txt (%d car(s), %d performance and %d visual part(s) waiting).", profile,
        g_state.count, perf, vis);
}

bool EnsureState() {
    char profile[33];
    if (!ProfileName(profile)) return false;
    if (strcmp(profile, g_state.profile) != 0) LoadState(profile);
    return true;
}

// Index of `model` in g_state (added if `add`), or -1.
int CarIndex(uint32_t model, bool add) {
    for (int i = 0; i < g_state.count; i++)
        if (g_state.model[i] == model) return i;
    if (!add || !model || g_state.count >= kMaxCars) return -1;
    int i = g_state.count++;
    g_state.model[i] = model;
    g_state.outruns[i] = g_state.waitingPerf[i] = g_state.waitingVis[i] = g_state.earned[i] = 0;
    g_state.nextVisual[i] = 0;
    return i;
}

// ---- Earning (outrun won) -----------------------------------------------------------------

void RebuildMarkers() {
    using Fn = void(__fastcall*)(uintptr_t);
    reinterpret_cast<Fn>(covers::kRebuildMapMarkers)(ev::kCareer);
}

// Rachel's stock text for challenge c (taken out of the inbox first if it is there from the career).
void SendMessage(const unsigned char* c) {
    using FindFn = uintptr_t(__cdecl*)(uint32_t);
    uintptr_t record = reinterpret_cast<FindFn>(covers::kSmsFindMessage)(ReadAt<uint32_t>(c, covers::challenge::kSms));
    if (!record) {
        Log("  No message: %s's message is missing.", ChallengeName(c));
        return;
    }
    bool was = InboxRemove(record);
    using Fn = void(__fastcall*)(uintptr_t, void*, const unsigned char*);  // thiscall(inbox, challenge), RET 4
    reinterpret_cast<Fn>(covers::kChallengeQueueSms)(covers::kInbox, nullptr, c);
    Log("  Rachel's message (%s)%s: %s.", ChallengeName(c), was ? ", re-sent" : "",
        InboxHas(record) ? "in the inbox" : "NOT delivered (inbox full?)");
}

void OnOutrunWon() {
    if (!CareerFinished() || !EnsureState()) return;
    const unsigned char* s = CurrentSlot();
    uint32_t model = ModelOf(s);
    int i = CarIndex(model, true);
    if (i < 0) {
        Log("Outrun won, but the car being driven is unknown: not counted.");
        return;
    }
    int wins = ++g_state.outruns[i];
    int missing = MissingParts(s);
    Log("Outrun won with the %s: %d post-game outrun win(s) with this car (waiting: %d performance, %d visual; %d of "
        "9 performance parts still to get).", CarName(model), wins, g_state.waitingPerf[i], g_state.waitingVis[i],
        missing);
    int kind = 0;  // what was earned: uniques::kTypePerf / kTypeVisual, 0 = nothing
    if (g_every > 0 && wins % g_every == 0) {
        bool perfOk = g_state.waitingPerf[i] < missing;
        bool visOk = g_state.waitingVis[i] < kMaxVisualWaiting && AnyVisualFor(s);
        bool perfTurn = g_state.earned[i] % 2 == 0;
        if (perfTurn) kind = perfOk ? uniques::kTypePerf : (visOk ? uniques::kTypeVisual : 0);
        else          kind = visOk ? uniques::kTypeVisual : (perfOk ? uniques::kTypePerf : 0);
        if (!kind) {
            Log("  %d wins, but the %s already has every reward it can hold waiting: nothing earned.", wins,
                CarName(model));
        } else {
            g_state.earned[i]++;
            if (kind == uniques::kTypePerf) g_state.waitingPerf[i]++;
            else g_state.waitingVis[i]++;
            Log("  EARNED a unique %s part for the %s (%s's turn%s; waiting now: %d performance, %d visual). Its "
                "challenge shows on the map while you drive that car.", kind == uniques::kTypePerf ? "performance"
                : "visual", CarName(model), perfTurn ? "performance" : "visual",
                (kind == uniques::kTypePerf) == perfTurn ? "" : ", the other kind has nothing left to give",
                g_state.waitingPerf[i], g_state.waitingVis[i]);
        }
    }
    SaveState();
    if (kind && g_raceToShop) {
        RebuildMarkers();  // puts the challenge on the map (bf::UniquePartsAfterRebuild)
        const unsigned char* c = kind == uniques::kTypePerf ? PickPerfChallenge(s)
                                                            : PickVisualChallenge(s, g_state.nextVisual[i], true);
        if (g_message && c) SendMessage(c);
    } else if (kind) {
        Log("  Drive into the %s with the %s to pick it.", kind == uniques::kTypePerf ? "performance shop"
            : "body shop (graphics shop for vinyls)", CarName(model));
    }
}

// ---- Hooks ---------------------------------------------------------------------------

// Replaces the CALL to Career_OnOutrunFinished (thiscall(g_Career, char won), RET 4).
void __fastcall Hook_OutrunFinished(void* career, void* edx, uint32_t won) {
    using Fn = void(__fastcall*)(void*, void*, uint32_t);
    reinterpret_cast<Fn>(uniques::kOutrunFinished)(career, edx, won);
    if (g_versionOk && reinterpret_cast<uintptr_t>(career) == ev::kCareer && (won & 0xFF) &&
        *reinterpret_cast<const int32_t*>(uniques::kGameMode) == 1)
        OnOutrunWon();
}

uintptr_t g_nextEnterShop = uniques::kEnterShop;

// Replaces the CALL to Career_OnEnterShop (thiscall(g_Career, shop record), RET 4), chained with RespawnAtGarage.
// RaceToShop = 0: a reward waiting for this car whose challenge leads to this shop -> the challenge counts as run and
// arrived, so the game's own code right after opens the pick screen. Performance first, then visual.
void __fastcall Hook_EnterShop(void* career, void* edx, const unsigned char* shop) {
    if (g_versionOk && !g_raceToShop && reinterpret_cast<uintptr_t>(career) == ev::kCareer && shop &&
        !uniques::RunningChallenge() && CareerFinished() && EnsureState()) {
        const unsigned char* s = CurrentSlot();
        uint32_t model = ModelOf(s);
        int i = CarIndex(model, false);
        uint32_t key = ReadAt<uint32_t>(shop, uniques::kShopKey);
        const unsigned char* c = nullptr;
        if (i >= 0 && g_state.waitingPerf[i] > 0) {
            c = PickPerfChallenge(s);
            if (c && ReadAt<uint32_t>(c, covers::challenge::kDest) != key) c = nullptr;
        }
        if (!c && i >= 0 && g_state.waitingVis[i] > 0) {
            for (int n = 0; n < uniques::kNumVisualKinds && !c; n++) {  // the next kind in turn that leads here
                int k = (g_state.nextVisual[i] + n) % uniques::kNumVisualKinds;
                const unsigned char* v = VisualChallenge(k);
                if (v && CarHasVisual(s, k) && ReadAt<uint32_t>(v, covers::challenge::kDest) == key) c = v;
            }
        }
        if (c) {
            unsigned char* st = reinterpret_cast<unsigned char*>(uniques::kChallengeState);
            st[0] = 0;  // as WorldChallenge_Start
            *reinterpret_cast<float*>(st + 4) = static_cast<float>(ReadAt<int32_t>(c, uniques::challenge::kTime));
            *reinterpret_cast<const unsigned char**>(st + uniques::state::kChallenge) = c;
            *reinterpret_cast<int32_t*>(st + uniques::state::kStep) = 1;
            if (c[covers::challenge::kType] == uniques::kTypePerf) {
                char parts[96];
                PartList(c, s, parts, sizeof(parts));
                Log("Performance shop with the %s: unique part pick screen (%s; offers %s).", CarName(model),
                    ChallengeName(c), parts);
            } else {
                Log("Shop with the %s: unique %s pick screen (%s).", CarName(model),
                    uniques::Visual(VisualKindOf(c)).name, ChallengeName(c));
            }
        }
    }
    using Fn = void(__fastcall*)(void*, void*, const unsigned char*);
    reinterpret_cast<Fn>(g_nextEnterShop)(career, edx, shop);  // the game's, or RespawnAtGarage's hook (ChainCall)
}

int g_granted = -1;  // performance category granted on the unique part screen just now (-1 = none)

// Replaces the CALL to FECarSession_GrantUniquePerfPart (thiscall(g_FECarSession, category), RET 4).
void __fastcall Hook_GrantPart(void* session, void* edx, int cat) {
    using Fn = void(__fastcall*)(void*, void*, int);
    reinterpret_cast<Fn>(uniques::kGrantPart)(session, edx, cat);
    g_granted = cat;
}

// The pick screen is done (picked or declined). The earned reward is used up BEFORE the game completes the
// challenge, because completing rebuilds the markers.
void OnPickDone(void* state, bool picked) {
    uintptr_t chal = *reinterpret_cast<const uintptr_t*>(reinterpret_cast<uintptr_t>(state) + uniques::state::kChallenge);
    int type = ChallengeKind(chal);
    if (!g_versionOk || reinterpret_cast<uintptr_t>(state) != uniques::kChallengeState || !type || !CareerFinished() ||
        !EnsureState())
        return;
    const unsigned char* c = reinterpret_cast<const unsigned char*>(chal);
    uint32_t model = ModelOf(CurrentSlot());
    int i = CarIndex(model, false);
    const char* name = ChallengeName(c);
    int* waiting = i < 0 ? nullptr : (type == uniques::kTypePerf ? &g_state.waitingPerf[i] : &g_state.waitingVis[i]);
    char what[48];
    if (type == uniques::kTypePerf)
        snprintf(what, sizeof(what), "the unique %s", g_granted >= 0 ? uniques::CategoryName(g_granted) : "part");
    else
        snprintf(what, sizeof(what), "a unique %s", uniques::Visual(VisualKindOf(c) < 0 ? 0 : VisualKindOf(c)).name);
    if (!waiting || *waiting <= 0) {
        Log("%s done with the %s, but no reward was waiting for it (%s).", name, CarName(model),
            picked ? what : "declined");
        return;
    }
    (*waiting)--;
    if (type == uniques::kTypeVisual && VisualKindOf(c) >= 0)
        g_state.nextVisual[i] = (VisualKindOf(c) + 1) % uniques::kNumVisualKinds;
    SaveState();
    if (picked)
        Log("%s done: the %s got %s (%d %s reward(s) still waiting for it).", name, CarName(model), what, *waiting,
            type == uniques::kTypePerf ? "performance" : "visual");
    else
        Log("%s done: declined (BACK) with the %s; the reward is used up (%d still waiting).", name, CarName(model),
            *waiting);
}

// Replace the 2 CALLs to WorldChallenge_Complete (fastcall(state), no args) on the unique part screen.
void __fastcall Hook_CompleteDeclined(void* state) {
    OnPickDone(state, false);
    g_granted = -1;
    using Fn = void(__fastcall*)(void*);
    reinterpret_cast<Fn>(uniques::kChallengeComplete)(state);
}

void __fastcall Hook_CompletePicked(void* state) {
    OnPickDone(state, true);
    g_granted = -1;
    using Fn = void(__fastcall*)(void*);
    reinterpret_cast<Fn>(uniques::kChallengeComplete)(state);
}

// For the log: what the map showed after the last rebuild.
const unsigned char* g_lastPerf   = nullptr;
const unsigned char* g_lastVisual = nullptr;
uint32_t             g_lastModel  = 0;

const unsigned char* AddToMap(const unsigned char* c) {
    if (c && uniques::AddActiveChallenge(reinterpret_cast<uintptr_t>(c)) < 0) {
        Log("The world challenge list is full; %s not added.", ChallengeName(c));
        return nullptr;
    }
    return c;
}

}  // namespace

// Called by Endless Career right after Career_RebuildActiveEvents (and its own races). Main thread.
void bf::UniquePartsAfterRebuild() {
    if (!g_versionOk || !g_raceToShop || !CareerFinished()) return;
    if (uniques::RunningChallenge()) return;
    if (!EnsureState()) return;
    const unsigned char* s = CurrentSlot();
    uint32_t model = ModelOf(s);
    int i = CarIndex(model, false);
    const unsigned char* perf = (i >= 0 && g_state.waitingPerf[i] > 0) ? AddToMap(PickPerfChallenge(s)) : nullptr;
    const unsigned char* vis = (i >= 0 && g_state.waitingVis[i] > 0)
                                   ? AddToMap(PickVisualChallenge(s, g_state.nextVisual[i], true)) : nullptr;
    if (perf != g_lastPerf || vis != g_lastVisual || model != g_lastModel) {
        if (perf) {
            char parts[96];
            PartList(perf, s, parts, sizeof(parts));
            Log("On the map for the %s: %s (%d s to the performance shop; offers %s).", CarName(model),
                ChallengeName(perf), ReadAt<int32_t>(perf, uniques::challenge::kTime), parts);
        }
        if (vis) {
            const uniques::VisualKind& k = uniques::Visual(VisualKindOf(vis));  // >= 0: PickVisualChallenge's
            Log("On the map for the %s: %s (%d s to the %s; unique %s).", CarName(model), ChallengeName(vis),
                ReadAt<int32_t>(vis, uniques::challenge::kTime), k.id == 62 ? "graphics shop" : "body shop", k.name);
        } else if (i >= 0 && g_state.waitingVis[i] > 0) {
            Log("A visual reward is waiting for the %s, but every start marker it could use has a race on it right "
                "now; it comes back when the races move.", CarName(model));
        }
        if (!perf && !vis && (g_lastPerf || g_lastVisual))
            Log("No unique part challenge on the map now (%s: %d performance, %d visual waiting).", CarName(model),
                i >= 0 ? g_state.waitingPerf[i] : 0, i >= 0 ? g_state.waitingVis[i] : 0);
        g_lastPerf = perf;
        g_lastVisual = vis;
        g_lastModel = model;
    }
}

namespace {

DWORD WINAPI VersionCheckThread(LPVOID) {
    bool ok = ExeMd5Ok() && g_hooksOk;
    InterlockedExchange(&g_versionOk, ok ? 1 : 0);
    if (!ok) Log("The hooks (if any) stay installed but only pass calls through to the game.");
    else Log("Ready. After the career, every %d outrun win(s) with a car earn it a unique part (performance and "
             "visual in turn).", g_every);
    return 0;
}

void Init() {
    Log("==== UniqueParts started (post-game unique performance and visual parts, earned per car by outrun wins) ====");
    LoadSettings();
    if (!bf::FeatureOn(bf::kEndlessCareer)) {
        Log("STOP: needs EndlessCareer = 1 in [Features] (it puts the challenge on the map after Endless Career's "
            "rebuild). Not hooking anything.");
        return;
    }
    if (!CodeMatches(kCodeChecks, sizeof(kCodeChecks) / sizeof(kCodeChecks[0]))) {
        Log("Not hooking anything.");
        return;
    }
    struct Site {
        uintptr_t   site, target;
        const void* hook;
        const char* what;
    };
    const Site sites[] = {
        {uniques::kOutrunFinishedCall, uniques::kOutrunFinished, reinterpret_cast<const void*>(&Hook_OutrunFinished), "outrun result"},
        {uniques::kGrantPartCallSites[0], uniques::kGrantPart, reinterpret_cast<const void*>(&Hook_GrantPart), "part granted"},
        {uniques::kEnterShopCallSites[0], uniques::kEnterShop, reinterpret_cast<const void*>(&Hook_EnterShop), "shop entered"},
        {uniques::kCompleteCallSites[0], uniques::kChallengeComplete, reinterpret_cast<const void*>(&Hook_CompleteDeclined), "challenge done (declined)"},
        {uniques::kCompleteCallSites[1], uniques::kChallengeComplete, reinterpret_cast<const void*>(&Hook_CompletePicked), "challenge done (picked)"},
    };
    bool ok = true;
    for (const Site& s : sites) {
        bool one = s.target == uniques::kEnterShop ? ChainCall(s.site, s.target, s.hook, &g_nextEnterShop)
                                                   : RedirectCall(s.site, s.target, s.hook);
        Log("%s %s call at 0x%08X.", one ? "Hooked" : "COULD NOT hook", s.what, static_cast<unsigned>(s.site));
        ok &= one;
    }
    g_hooksOk = ok;
    HANDLE thread = CreateThread(nullptr, 0, VersionCheckThread, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
}

}  // namespace

void bf::StartUniqueParts() { Init(); }
