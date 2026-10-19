// Endless Career: after the career, Career_RebuildActiveEvents (0x00527D30) leaves the map empty. After the game's
// rebuild (only if the career is finished) this puts a rotating set of races back:
//   - [EndlessCareer.Regions] race slots per district; a district's races differ in type where possible, every type
//     is on the map somewhere, a won race is replaced in the same district.
//   - Each slot is a track variant (track x direction, docs/tracks.txt) from the district's own rotation (every variant
//     once before repeats; endless-career\<profile>\rotation.txt), put into a "carrier" race of the same family (venue
//     type, icon, opponents, traffic stay right), on the race marker nearest its start line, never two on one marker.
//   - Map icon: X for a track not won yet (the sponsor-race icon), circle for repeats; URL races keep theirs.
//   - Laps from the track length and [Laps]; payout from [Rewards]; opponents' car level and skill from [AI].
//   - Each win returns one collected money pickup ([Pickups]).
// All changes are to the in-memory race table and are undone for a profile whose career isn't finished. The only
// thing saved: a returned pickup's "collected" flag (g_Career+0x8768).
// Hooks: 3 CALLs to Career_RebuildActiveEvents, 2 to Career_OnRaceWon, 1 to Career_OnRaceLost (log), 2 to
// RaceSession_Init (the game sets up an already-won race as a "replay" with no result; reset for our races).
// Calls bf::UniquePartsAfterRebuild / bf::CoversAfterRebuild after its own work. Track list: track_data.h
// (tools/dump_tracks.py --header).

#define BF_TAG "EndlessCareer"
#include "nfsu2.h"
#include "track_data.h"

#include <cmath>
#include <cstdlib>

using namespace nfsu2;
using tracks::kVariants;
using tracks::kMarkers;
using tracks::kPickups;

namespace {

const int kMinStage = 2;     // stage 0-1 races (prologue, tutorials) are never used as carriers
const int kMaxRaces = 2000;  // sanity limit for the race table (stock: 224)
const int kMaxLaps  = 10;
const int kMaxSkill = 9;     // the AI's per-skill table has 10 entries (0..9); stock skills are 0..9
const int kNumVariants = sizeof(kVariants) / sizeof(kVariants[0]);
const int kNumMarkers  = sizeof(kMarkers) / sizeof(kMarkers[0]);
const int kMaxPickups  = 256;  // collected flags are indexed by a byte
const int kNumDistricts = 5;   // tracks::kDistrictNames 0..4 (5 = "special", never used by a variant)
const int kMaxPerDistrict = 4;
const int kMaxMapSlots = kNumDistricts * kMaxPerDistrict;

// Race types (for variety per district and the log).
enum Type { kTCircuit, kTSprint, kTDrag, kTDrift, kTStreetX, kNumTypes };
const char* const kTypeNames[kNumTypes] = {"circuit", "sprint", "drag", "drift", "street X"};

int TypeOfFamily(int f) {
    switch (f) {
        case tracks::kFCircuit:
        case tracks::kFSpeedway: return kTCircuit;  // URL races are circuit races on the speedways
        case tracks::kFSprint: return kTSprint;
        case tracks::kFDrag: return kTDrag;
        case tracks::kFDriftLot:
        case tracks::kFDriftHill: return kTDrift;
        case tracks::kFStreetX: return kTStreetX;
        default: return -1;
    }
}

// Groups for the per-type settings ([Laps] ...Scale, [AI] ...Offset).
enum Group { kGCircuit, kGURL, kGSprint, kGDrag, kGDrift, kGStreetX, kNumGroups };
const char* const kGroupNames[kNumGroups] = {"Circuit", "URL", "Sprint", "Drag", "Drift", "StreetX"};

int GroupOfFamily(int f) {
    switch (f) {
        case tracks::kFCircuit: return kGCircuit;
        case tracks::kFSpeedway: return kGURL;
        case tracks::kFSprint: return kGSprint;
        case tracks::kFDrag: return kGDrag;
        case tracks::kFDriftLot:
        case tracks::kFDriftHill: return kGDrift;
        default: return kGStreetX;
    }
}

// Rough average speed (m/s) of a late-game car per family, for lap counts and the log (measured in play).
float g_speed[tracks::kNumFamilies] = {45, 45, 60, 20, 25, 35, 50};

// Payout classes (each has its own stage-5 payout level).
enum Kind { kCircuit, kSprint, kDrag, kDrift, kStreetX, kURL, kSponsor, kSUV, kNumKinds };
const char* const kKindNames[kNumKinds] = {"circuit", "sprint", "drag", "drift", "street X", "URL", "sponsor", "SUV"};

const CodeCheck kCodeChecks[] = {
    {0x00527D30, {0x81, 0xEC, 0xA0, 0x01, 0x00, 0x00, 0x8B, 0xD1}, 8, "start of Career_RebuildActiveEvents"},
    {0x00527D93, {0x8B, 0x8A, 0x08, 0x0C, 0x00, 0x00}, 6, "MOV ECX,[EDX+0xC08] in Career_RebuildActiveEvents"},
    {0x00541420, {0x51, 0x53, 0x56, 0x8B, 0xF1, 0x8A, 0x86, 0x26}, 8, "start of Career_OnRaceWon"},
    {0x0054152D, {0x5F, 0x5E, 0x5B, 0x59, 0xC2, 0x04, 0x00}, 7, "RET 4 of Career_OnRaceWon"},
    {0x005414D7, {0x8B, 0x4F, 0x30, 0x01, 0x8E, 0x34, 0x71, 0x00}, 8, "cash += race+0x30 in Career_OnRaceWon"},
    {0x005007A0, {0x56, 0x8B, 0x74, 0x24, 0x08, 0x8B, 0xC6, 0xE8}, 8, "start of EventList_AddActive"},
    {0x005007CF, {0xC2, 0x04, 0x00}, 3, "RET 4 of EventList_AddActive"},
    {0x00503AE0, {0x8B, 0x49, 0x20, 0x51, 0xE8, 0x47, 0xC6, 0x10}, 8, "GetCarTypeIndex -> CarTypes_FindByNameHash"},
    {0x00503AF4, {0x8B, 0x80, 0x40, 0x08, 0x00, 0x00}, 6, "MOV EAX,[EAX+0x840] in GetCarTypeIndex"},
    {0x00513880, {0x8B, 0x81, 0xEC, 0x78, 0x00, 0x00, 0x8B, 0x50}, 8, "Career_IsFinished reads [ECX+0x78EC]"},
    {0x00500B80, {0x56, 0x8B, 0xB1, 0xFC, 0x7A, 0x00, 0x00}, 7, "MOV ESI,[ECX+0x7AFC] in Career_IsEventWon"},
    {0x00513A71, {0x0F, 0xB7, 0x46, 0x04}, 4, "outro movie read (race+0x04) in the post-race movie picker"},
    {0x00512C9A, {0x89, 0x0D, 0xE0, 0x83, 0x83, 0x00}, 6, "MOV [g_GRaces],ECX in GCareer_LoadChunk"},
    {0x00532EA0, {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x28, 0x0D, 0x00}, 8, "start of RaceSession_Init"},
    {0x005330B9, {0xC6, 0x46, 0x04, 0x01, 0x5E, 0x5B, 0x8B, 0xE5}, 8, "replay flag = 1 in RaceSession_Init"},
    {0x0053309B, {0x5D, 0xC2, 0x04, 0x00}, 4, "RET 4 of RaceSession_Init (1)"},
    {0x005330C1, {0x5D, 0xC2, 0x04, 0x00}, 4, "RET 4 of RaceSession_Init (2)"},
    {0x0054C943, {0x8A, 0x87, 0x25, 0x0D, 0x00, 0x00}, 6, "MOV AL,[EDI+0xD25] in Career_ApplyRaceResult"},
    {0x0054C94D, {0x8A, 0x87, 0x28, 0x0D, 0x00, 0x00}, 6, "MOV AL,[EDI+0xD28] in Career_ApplyRaceResult"},
    {0x00541425, {0x8A, 0x86, 0x26, 0x0D, 0x00, 0x00}, 6, "MOV AL,[ESI+0xD26] (pays) in Career_OnRaceWon"},
    {0x00526138, {0x83, 0xC6, 0x42}, 3, "ADD ESI,0x42 (first opponent) in the race AI setup"},
    {0x00526157, {0x8A, 0x06, 0x84, 0xC0, 0x75, 0x0E, 0x0F, 0xBE}, 8, "mode 0 -> fixed level [ESI+1] in the race AI setup"},
    {0x0052616B, {0x3C, 0x03, 0x75, 0x1B, 0x8B, 0x0D, 0x38, 0x28}, 8, "mode 3 -> level from the current stage"},
    {0x005261E6, {0x0F, 0xBE, 0x54, 0x0E, 0x06, 0x0F, 0xBE, 0x46}, 8, "skill [ESI+difficulty+6] in the race AI setup"},
    // Tracks and markers
    {0x005D3EB4, {0x81, 0x38, 0x01, 0x42, 0x03, 0x00}, 6, "track info loader checks chunk 0x34201"},
    {0x005D3EBF, {0x89, 0x0D, 0xA0, 0x3D, 0x88, 0x00}, 6, "MOV [g_TrackInfos],ECX in the track info loader"},
    {0x005D3ED8, {0x89, 0x0D, 0xA4, 0x3D, 0x88, 0x00}, 6, "MOV [g_TrackInfoCount],ECX in the track info loader"},
    {0x004FFC50, {0x56, 0x6A, 0x00, 0x68, 0x00, 0xE0, 0x00, 0x00}, 8, "start of WorldTrigger_FindByHash"},
    {0x004FFC70, {0x39, 0x70, 0x30}, 3, "CMP [EAX+0x30],ESI (trigger name hash) in WorldTrigger_FindByHash"},
    {0x00533534, {0x8B, 0x56, 0x28, 0x52, 0xE8, 0x13, 0xC7, 0xFC}, 8, "map markers: WorldTrigger_FindByHash(race+0x28)"},
    {0x00528500, {0x56, 0x8B, 0xF1, 0x8B, 0x8E, 0xFC, 0x7A, 0x00}, 8, "start of Career_OnRaceLost"},
    {0x00528551, {0xC2, 0x04, 0x00}, 3, "RET 4 of Career_OnRaceLost"},
    // Map icon X (sponsor byte) and money pickups
    {0x004B06D0, {0x80, 0x79, 0x34, 0x01}, 4, "world map: race +0x34 == 1 -> X icon"},
    {0x004AC49F, {0x80, 0x79, 0x34, 0x01}, 4, "map indicator: race +0x34 == 1 -> X icon"},
    {0x00512D85, {0x89, 0x15, 0x30, 0x84, 0x83, 0x00}, 6, "MOV [g_MoneyPickups],EDX in GCareer_LoadChunk"},
    {0x00512D96, {0x89, 0x15, 0x34, 0x84, 0x83, 0x00}, 6, "MOV [g_MoneyPickupCount],EDX in GCareer_LoadChunk"},
    {0x005141B0, {0x51, 0x53, 0x56, 0x57, 0x8D, 0xB1, 0x00, 0x08}, 8, "start of Career_BuildLiveMoneyPickups"},
    {0x00514271, {0x5F, 0x5E, 0x5B, 0x59, 0xC3}, 5, "plain RET of Career_BuildLiveMoneyPickups"},
    {0x0052837B, {0x0F, 0xB7, 0x06, 0x03, 0xD0}, 5, "cash += u16 [pickup] in MoneyPickup_Collect"},
    {0x0052838A, {0xC6, 0x84, 0x0A, 0x68, 0x87, 0x00, 0x00, 0x01}, 8, "collected flag [career+index+0x8768] = 1"},
};

volatile LONG g_versionOk = 0;  // set by the MD5 thread; until then the hooks only pass through

// ---- Settings (scripts\BayviewForever.ini, [EndlessCareer.*] sections) -----------

float   g_targetSeconds = 240.0f;   // [Laps] TargetMinutes: circuits and URL races aim at this race time
float   g_driftSeconds  = 150.0f;   // [Laps] DriftTargetMinutes: lot/stadium drifts aim at this race time
float   g_lengthScale   = 1.0f;     // [Laps] LengthScale / 100: every race type
float   g_groupScale[kNumGroups] = {1, 1, 1, 1, 1, 1};  // [Laps] <Group>Scale / 100
int     g_carLevel = 10;            // [AI] CarLevel 0..10
int     g_skillBase[3] = {3, 7, 9}; // [AI] Easy / Medium / Hard
int     g_skillOffset[kNumGroups];  // [AI] <Group>Offset
int     g_perDistrict[kNumDistricts] = {2, 2, 2, 2, 2};  // [Regions]
bool    g_iconX = true;             // [Map] NewTrackX
bool    g_pickupsOn = true;         // [Pickups] Enabled
int     g_pickupCash = 500;         // [Pickups] Cash
int     g_pickupsPerWin = 1;        // [Pickups] PerWin
float   g_pickupSpacing = 75.0f;    // [Pickups] MinSpacing (m)
int     g_rewardPercent = 100;      // [Rewards] Percent: % of the payout below
// [Rewards] <Kind>: fixed payout, 0 = automatic (best stage-5 payout of the kind). URL $3,000 (single races, not
// tournaments); Sponsor 0 = paid like a normal race of its type (Endless Career reuses sponsor slots).
int     g_rewardFixed[kNumKinds] = {0, 0, 0, 0, 0, 3000, 0, 0};
int     g_rewardGrowPerWin = 0;     // [Rewards] GrowPerWin: % more per post-game win so far
int     g_rewardGrowMax = 100;      // [Rewards] GrowMax: at most this % more
int     g_force[64];                // variants the test switch puts on the map first
int     g_forceCount = 0;
bool    g_forceUsed[64];

int FindVariant(int track, int reverse) {
    for (int v = 0; v < kNumVariants; v++)
        if (kVariants[v].track == track && kVariants[v].reverse == reverse) return v;
    return -1;
}

int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

float ReadFloat(const char* section, const char* key, float def, float lo, float hi, const char* path) {
    char buf[64], defText[32];
    snprintf(defText, sizeof(defText), "%g", def);
    GetPrivateProfileStringA(section, key, defText, buf, sizeof(buf), path);
    float v = static_cast<float>(atof(buf));
    return (v >= lo && v <= hi) ? v : def;
}

int ReadInt(const char* section, const char* key, int def, int lo, int hi, const char* path) {
    char buf[64];
    GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), path);
    if (!buf[0]) return def;
    char* end;
    long v = strtol(buf, &end, 10);  // accepts "+2" and "-1"
    return (end != buf && v >= lo && v <= hi) ? static_cast<int>(v) : def;
}

void LoadSettings() {
    char path[MAX_PATH];
    bf::IniPath(path);

    g_lengthScale = ReadInt("EndlessCareer.Laps", "LengthScale", 100, 25, 300, path) / 100.0f;
    for (int g = 0; g < kNumGroups; g++) {
        char key[32];
        snprintf(key, sizeof(key), "%sScale", kGroupNames[g]);
        g_groupScale[g] = ReadInt("EndlessCareer.Laps", key, 100, 25, 300, path) / 100.0f;
    }
    g_targetSeconds = ReadFloat("EndlessCareer.Laps", "TargetMinutes", 4.0f, 1.0f, 15.0f, path) * 60.0f;
    g_driftSeconds  = ReadFloat("EndlessCareer.Laps", "DriftTargetMinutes", 2.5f, 0.5f, 15.0f, path) * 60.0f;
    g_speed[tracks::kFCircuit]  = ReadFloat("EndlessCareer.Laps", "CircuitSpeed", 45, 10, 100, path);
    g_speed[tracks::kFSpeedway] = ReadFloat("EndlessCareer.Laps", "SpeedwaySpeed", 50, 10, 100, path);
    g_speed[tracks::kFDriftLot] = ReadFloat("EndlessCareer.Laps", "DriftSpeed", 20, 5, 100, path);

    g_carLevel = ReadInt("EndlessCareer.AI", "CarLevel", 10, 0, 10, path);
    static const char* const kDiff[3] = {"Easy", "Medium", "Hard"};
    for (int d = 0; d < 3; d++) g_skillBase[d] = ReadInt("EndlessCareer.AI", kDiff[d], g_skillBase[d], 0, kMaxSkill, path);
    for (int g = 0; g < kNumGroups; g++) {
        char key[32];
        snprintf(key, sizeof(key), "%sOffset", kGroupNames[g]);
        g_skillOffset[g] = ReadInt("EndlessCareer.AI", key, 0, -9, 9, path);
    }

    static const char* const kRegionKeys[kNumDistricts] = {"JacksonHeights", "CityCore", "BeaconHill", "CoalHarbor", "Airport"};
    for (int d = 0; d < kNumDistricts; d++) g_perDistrict[d] = ReadInt("EndlessCareer.Regions", kRegionKeys[d], 2, 0, kMaxPerDistrict, path);

    g_iconX = ReadInt("EndlessCareer.Map", "NewTrackX", 1, 0, 1, path) != 0;
    g_pickupsOn = ReadInt("EndlessCareer.Pickups", "Enabled", 1, 0, 1, path) != 0;
    g_pickupCash = ReadInt("EndlessCareer.Pickups", "Cash", 500, 0, 65535, path);
    g_pickupsPerWin = ReadInt("EndlessCareer.Pickups", "PerWin", 1, 0, 10, path);
    g_pickupSpacing = ReadFloat("EndlessCareer.Pickups", "MinSpacing", 75, 0, 1000, path);
    static const char* const kRewardKeys[kNumKinds] = {"Circuit", "Sprint", "Drag", "Drift", "StreetX", "URL",
                                                       "Sponsor", "SUV"};
    g_rewardPercent = ReadInt("EndlessCareer.Rewards", "Percent", 100, 0, 1000, path);
    for (int k = 0; k < kNumKinds; k++)
        g_rewardFixed[k] = ReadInt("EndlessCareer.Rewards", kRewardKeys[k], g_rewardFixed[k], 0, 1000000, path);
    g_rewardGrowPerWin = ReadInt("EndlessCareer.Rewards", "GrowPerWin", 0, 0, 100, path);
    g_rewardGrowMax = ReadInt("EndlessCareer.Rewards", "GrowMax", 100, 0, 1000, path);

    // [Test] Force = 4011R, 4602F, ...  (track ID + F/R): these come up first, once each.
    char buf[512];
    GetPrivateProfileStringA("EndlessCareer.Test", "Force", "", buf, sizeof(buf), path);
    for (char* p = buf; *p && g_forceCount < 64;) {
        while (*p == ' ' || *p == ',' || *p == ';') p++;
        if (!*p) break;
        int track = static_cast<int>(strtol(p, &p, 10));
        int reverse = (*p == 'R' || *p == 'r') ? 1 : 0;
        if (*p == 'R' || *p == 'r' || *p == 'F' || *p == 'f') p++;
        int idx = FindVariant(track, reverse);
        if (idx >= 0)
            g_force[g_forceCount++] = idx;
        else
            Log("Settings: [Test] Force: no track variant %d%c, ignored.", track, reverse ? 'R' : 'F');
        while (*p && *p != ',' && *p != ' ' && *p != ';') p++;
    }

    if (!bf::IniFound()) Log("Settings: no BayviewForever.ini, defaults.");
    Log("Settings [Laps]: length %.0f%% (circuit %.0f%%, URL %.0f%%, sprint %.0f%%, drag %.0f%%, drift %.0f%%, street X %.0f%%), "
        "circuits/URL aim at %.1f min, lot/stadium drifts at %.1f min; speeds circuit %.0f, speedway %.0f, drift %.0f m/s.",
        g_lengthScale * 100, g_groupScale[kGCircuit] * 100, g_groupScale[kGURL] * 100, g_groupScale[kGSprint] * 100,
        g_groupScale[kGDrag] * 100, g_groupScale[kGDrift] * 100, g_groupScale[kGStreetX] * 100, g_targetSeconds / 60,
        g_driftSeconds / 60, g_speed[tracks::kFCircuit], g_speed[tracks::kFSpeedway], g_speed[tracks::kFDriftLot]);
    Log("Settings [AI]: car level %d, skill Easy %d / Medium %d / Hard %d, offsets circuit %+d, URL %+d, sprint %+d, "
        "drag %+d, drift %+d, street X %+d.", g_carLevel, g_skillBase[0], g_skillBase[1], g_skillBase[2],
        g_skillOffset[kGCircuit], g_skillOffset[kGURL], g_skillOffset[kGSprint], g_skillOffset[kGDrag],
        g_skillOffset[kGDrift], g_skillOffset[kGStreetX]);
    Log("Settings [Regions]: Jackson Heights %d, City Core %d, Beacon Hill %d, Coal Harbor %d, Airport %d races. "
        "[Map] X icon for new tracks: %s. [Pickups] %s, $%d each, %d per win, at least %.0f m apart. %d forced test variant(s).",
        g_perDistrict[0], g_perDistrict[1], g_perDistrict[2], g_perDistrict[3], g_perDistrict[4], g_iconX ? "on" : "off",
        g_pickupsOn ? "on" : "off", g_pickupCash, g_pickupsPerWin, g_pickupSpacing, g_forceCount);
    char fixed[256] = "";
    int len = 0;
    for (int k = 0; k < kNumKinds; k++)
        if (g_rewardFixed[k]) len += snprintf(fixed + len, sizeof(fixed) - len, " %s $%d", kKindNames[k], g_rewardFixed[k]);
    Log("Settings [Rewards]: %d%% of the payout, +%d%% per post-game win (at most +%d%%); fixed:%s; sponsor races %s.",
        g_rewardPercent, g_rewardGrowPerWin, g_rewardGrowMax, len ? fixed : " none (automatic)",
        g_rewardFixed[kSponsor] ? "fixed" : "paid like a normal race of their type");
}

// ---- Race table ------------------------------------------------------------

const unsigned char* g_races     = nullptr;
int                  g_raceCount = 0;
const char*          g_strings   = nullptr;
int                  g_stringsSize = 0;

// Fixed-size arrays only: nothing in the hook path may allocate or throw.
signed char g_carrierFamily[kMaxRaces];  // family of a race that may carry a variant, else -1
int         g_level[kNumKinds][2];       // stage-5 payout per kind, [not hidden / hidden]
bool        g_variantOk[kNumVariants];   // track exists in the game's track table with this length/direction
uint16_t    g_markerName[kNumMarkers];   // string offset of the marker's name (0 = marker not usable)
bool        g_markerWarned[kNumMarkers];

const size_t kOppBytes = 5 * ev::race::kOpponentSize;
const size_t kLegBytes = 16;

struct Original {
    bool          saved;
    int32_t       cash;
    uint16_t      intro, outro, triggerName;
    uint32_t      trigger;
    uint8_t       hidden, numLegs, icon;
    unsigned char legs[kLegBytes];
    unsigned char opponents[kOppBytes];
};
Original g_orig[kMaxRaces];

unsigned char* RaceAt(int i) { return const_cast<unsigned char*>(g_races) + i * ev::kRaceSize; }

const char* RaceName(int i) {
    uint16_t off = ReadAt<uint16_t>(RaceAt(i), ev::race::kName);
    if (off >= g_stringsSize || !memchr(g_strings + off, 0, g_stringsSize - off)) return "?";
    return g_strings + off;
}

int KindOf(const unsigned char* r, uint8_t icon) {
    if (r[ev::race::kIsSUV]) return kSUV;
    if (icon == 2) return kURL;
    if (icon == 1) return kSponsor;
    switch (r[ev::race::kBehavior]) {
        case 0: return kCircuit;
        case 1: return kSprint;
        case 2: return kStreetX;
        case 4: return kDrag;
        case 5: return kDrift;
        default: return -1;  // 3 = open world (prologue drives)
    }
}

// Kind from the race's ORIGINAL icon (the plugin changes +0x34 for the X/circle icon).
int KindOfRace(int i) {
    const unsigned char* r = RaceAt(i);
    return KindOf(r, g_orig[i].saved ? g_orig[i].icon : r[ev::race::kIcon]);
}

int TypeOf(const unsigned char* r) {
    switch (r[ev::race::kBehavior]) {
        case 0: return kTCircuit;
        case 1: return kTSprint;
        case 2: return kTStreetX;
        case 4: return kTDrag;
        case 5: return kTDrift;
        default: return -1;
    }
}

unsigned char* Opponent(unsigned char* r, int n) { return r + ev::race::kOpponents + n * ev::race::kOpponentSize; }

int NumOpponents(const unsigned char* r) {
    int n = static_cast<signed char>(r[ev::race::kNumOpp]);
    return n < 0 ? 0 : (n > 5 ? 5 : n);
}

int FamilyOfTrack(int track) {
    for (const auto& v : kVariants)
        if (v.track == track) return v.family;
    return -1;
}

// Checks the built-in track list against the game's own track table (g_TrackInfos).
void CheckVariants() {
    const unsigned char* table = reinterpret_cast<const unsigned char*>(*reinterpret_cast<volatile uintptr_t*>(ev::kTrackInfosPtr));
    int count = *reinterpret_cast<volatile int32_t*>(ev::kTrackInfoCount);
    if (!table || count <= 0 || count > 1000 || !IsReadable(reinterpret_cast<uintptr_t>(table), count * ev::kTrackInfoSize)) {
        Log("Track table not readable (pointer %p, count %d): no track variants.", table, count);
        return;
    }
    int ok = 0;
    for (int v = 0; v < kNumVariants; v++) {
        const tracks::Variant& var = kVariants[v];
        const unsigned char* rec = nullptr;
        for (int i = 0; i < count && !rec; i++)
            if (ReadAt<uint16_t>(table + i * ev::kTrackInfoSize, ev::trackinfo::kId) == var.track) rec = table + i * ev::kTrackInfoSize;
        if (!rec) {
            Log("  track %d is not in the game's track table: left out.", var.track);
            continue;
        }
        uint32_t length = ReadAt<uint32_t>(rec, ev::trackinfo::kLength);
        if (length != var.length || (var.reverse && !rec[ev::trackinfo::kReverseExists])) {
            Log("  track %d%c differs from the built-in list (length %u, expected %u; reverse %u): left out.", var.track,
                var.reverse ? 'R' : 'F', length, var.length, rec[ev::trackinfo::kReverseExists]);
            continue;
        }
        if (var.district >= kNumDistricts) continue;
        g_variantOk[v] = true;
        ok++;
    }
    Log("Track variants: %d of %d match the game's track table.", ok, kNumVariants);
}

// Loads the race table once the game has it, and sorts races into carriers per family.
bool EnsureTables() {
    if (g_races) return true;
    const unsigned char* races = reinterpret_cast<const unsigned char*>(*reinterpret_cast<volatile uintptr_t*>(ev::kRaceTablePtr));
    int count       = *reinterpret_cast<volatile int32_t*>(ev::kRaceCount);
    const char* str = reinterpret_cast<const char*>(*reinterpret_cast<volatile uintptr_t*>(ev::kStringsPtr));
    int strSize     = *reinterpret_cast<volatile int32_t*>(ev::kStringsSize);
    if (!races || count <= 0 || count > kMaxRaces || !str || strSize <= 0 || strSize > 0x100000) return false;
    if (!IsReadable(reinterpret_cast<uintptr_t>(races), count * ev::kRaceSize) ||
        !IsReadable(reinterpret_cast<uintptr_t>(str), strSize))
        return false;

    g_races = races;
    g_raceCount = count;
    g_strings = str;
    g_stringsSize = strSize;
    CheckVariants();

    int carriers[tracks::kNumFamilies] = {};
    for (int i = 0; i < count; i++) {
        unsigned char* r = RaceAt(i);
        g_carrierFamily[i] = -1;

        // Marker names: the race table holds each EventTrigger's name (+0x06) next to its hash (+0x28).
        uint32_t trig = ReadAt<uint32_t>(r, ev::race::kTrigger);
        uint16_t trigName = ReadAt<uint16_t>(r, ev::race::kTriggerName);
        for (int m = 0; m < kNumMarkers; m++)
            if (kMarkers[m].hash == trig && trigName && trigName < g_stringsSize) g_markerName[m] = trigName;

        int kind = KindOf(r, r[ev::race::kIcon]), type = TypeOf(r);
        if (kind < 0 || type < 0) continue;
        bool isFinal = ReadAt<uint32_t>(r, ev::race::kKey) == ev::kFinalRaceKey;
        if (r[ev::race::kStage] == 5) {
            int& level = g_level[kind][r[ev::race::kHidden] ? 1 : 0];
            int cash = ReadAt<int32_t>(r, ev::race::kCash);
            if (cash > level && !isFinal) level = cash;  // Caleb's final race ($10,000) is not a normal circuit
        }
        if (r[ev::race::kStage] < kMinStage || isFinal) continue;
        if (ReadAt<uint32_t>(r, ev::race::kForcedCar) != 0 || r[ev::race::kDriveToGPS]) continue;
        if (r[ev::race::kNumLegs] == 0) continue;
        int family = FamilyOfTrack(ReadAt<uint16_t>(r, ev::race::kLegs));
        if (family < 0 || TypeOfFamily(family) != type) continue;  // e.g. a mod that turned a drag into a circuit
        g_carrierFamily[i] = static_cast<signed char>(family);
        carriers[family]++;
    }
    // Prologue markers are left out: a prologue race showed no map icon (cause unknown).
    int markers = 0;
    for (int m = 0; m < kNumMarkers; m++) {
        if (!strncmp(kMarkers[m].name, "CIRCUIT_", 8) || !strncmp(kMarkers[m].name, "EVENT_LOCATOR_", 14)) g_markerName[m] = 0;
        if (g_markerName[m]) markers++;
    }
    Log("Race table: %d races. Carriers per family (stages %d-5):", count, kMinStage);
    for (int f = 0; f < tracks::kNumFamilies; f++) Log("  %-15s %3d races", tracks::kFamilyNames[f], carriers[f]);
    Log("Race markers usable: %d of %d.", markers, kNumMarkers);
    Log("Stage-5 payout per kind:");
    for (int k = 0; k < kNumKinds; k++) {
        if (g_level[k][1] == 0) g_level[k][1] = g_level[k][0];
        Log("  %-8s at least $%d (hidden ones $%d)", kKindNames[k], g_level[k][0], g_level[k][1]);
    }
    return true;
}

// ---- Career state ----------------------------------------------------------

unsigned char* Career() { return reinterpret_cast<unsigned char*>(ev::kCareer); }

int StatusOf(uint32_t key) {
    int n = *reinterpret_cast<volatile int32_t*>(ev::kStatusCount);
    if (n < 0 || n > ev::kMaxStatus) return -1;
    const unsigned char* list = reinterpret_cast<const unsigned char*>(ev::kStatusList);
    for (int i = 0; i < n; i++) {
        const unsigned char* e = list + i * ev::kStatusEntrySize;
        if (ReadAt<uint32_t>(e, ev::status::kKey) == key) return e[ev::status::kState];
    }
    return -1;
}

// Same test as Career_IsFinished (0x00513880): stage 5's last event is won.
bool CareerFinished() {
    uintptr_t stage5 = ReadAt<uintptr_t>(Career(), ev::kCareerStage5Data);
    if (!stage5 || !IsReadable(stage5, ev::kStageDataSize)) return false;
    return StatusOf(ReadAt<uint32_t>(reinterpret_cast<const unsigned char*>(stage5), ev::stagedata::kLastEvent)) == 1;
}

// Same test as the game's SUV check in EventList_PickRandomCandidate.
bool CurrentCarIsSUV() {
    if (*reinterpret_cast<volatile uint8_t*>(ev::kCurrentCarBought) == 0) return false;
    uint32_t hash = *reinterpret_cast<volatile uint32_t*>(kCurrentSlotHash);
    for (int n = 0; n < kMaxSlots; n++) {
        const unsigned char* s = SlotPtr(n);
        if (ReadAt<uint32_t>(s, slot::kHash) != hash || ReadAt<uint32_t>(s, slot::kInUse) == 0) continue;
        using FindFn = uintptr_t(__cdecl*)(uint32_t);
        uintptr_t entry = reinterpret_cast<FindFn>(ev::kCarTypesFindByName)(ReadAt<uint32_t>(s, slot::kCarModel));
        if (!entry) return false;
        int index = *reinterpret_cast<int32_t*>(entry + ev::kCarTypeIndex);
        uintptr_t table = *reinterpret_cast<volatile uintptr_t*>(ev::kCarTypesTablePtr);
        if (!table || index < 0) return false;
        return *reinterpret_cast<uint8_t*>(table + index * ev::kCarTypeSize + ev::kCarTypeIsSUV) != 0;
    }
    return false;
}

// True if the world has this marker (game function, main thread only). Logs once if it moved.
bool MarkerInWorld(int m) {
    using FindFn = const unsigned char*(__cdecl*)(uint32_t);
    const unsigned char* t = reinterpret_cast<FindFn>(ev::kWorldTriggerFind)(kMarkers[m].hash);
    if (!t) return false;
    float x = ReadAt<float>(t, ev::trigger::kX), y = ReadAt<float>(t, ev::trigger::kY);
    float dx = x - kMarkers[m].x, dy = y - kMarkers[m].y;
    if (dx * dx + dy * dy > 100.0f && !g_markerWarned[m]) {
        g_markerWarned[m] = true;
        Log("Note: marker %s is at (%.0f, %.0f) in the game, built-in list says (%d, %d).", kMarkers[m].name, x, y,
            kMarkers[m].x, kMarkers[m].y);
    }
    return true;
}

// ---- Rotation (endless-career\<profile>\rotation.txt) ---------------------

bool     g_done[kNumVariants];        // won in the current round of its district
uint16_t g_wins[kNumVariants];        // wins in Endless Career so far (0 = X icon)
bool     g_pickupDone[kMaxPickups];   // money pickup returned in the current pickup round
char     g_rotProfile[kProfileNameSize + 1];

bool RotationPath(char out[MAX_PATH]) {
    const char* name = reinterpret_cast<const char*>(kProfile1Name);
    char safe[kProfileNameSize + 1];
    size_t n = 0;
    for (; n < kProfileNameSize && name[n]; n++) {
        char c = name[n];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_';
        safe[n] = ok ? c : '_';
    }
    safe[n] = '\0';
    if (n == 0) return false;
    char dir[MAX_PATH], root[MAX_PATH];
    GetDataDir(dir);
    snprintf(root, sizeof(root), "%sendless-career\\", dir);
    CreateDirectoryA(root, nullptr);
    snprintf(out, MAX_PATH, "%s%s\\", root, safe);
    CreateDirectoryA(out, nullptr);
    strncat(out, "rotation.txt", MAX_PATH - strlen(out) - 1);
    return true;
}

// Loads the rotation when a different profile is active.
void EnsureRotation() {
    const char* name = reinterpret_cast<const char*>(kProfile1Name);
    if (!strncmp(name, g_rotProfile, kProfileNameSize)) return;
    memset(g_done, 0, sizeof(g_done));
    memset(g_wins, 0, sizeof(g_wins));
    memset(g_pickupDone, 0, sizeof(g_pickupDone));
    strncpy(g_rotProfile, name, kProfileNameSize);
    g_rotProfile[kProfileNameSize] = '\0';
    char path[MAX_PATH];
    if (!RotationPath(path)) return;
    FILE* f = fopen(path, "r");
    int done = 0, won = 0, pickups = 0;
    if (f) {
        char line[64];
        while (fgets(line, sizeof(line), f)) {
            int track, count, index;
            char dir;
            if (sscanf(line, "done %d%c", &track, &dir) == 2) {
                int v = FindVariant(track, dir == 'R' ? 1 : 0);
                if (v >= 0 && !g_done[v]) {
                    g_done[v] = true;
                    done++;
                }
                // older files only had "done" lines: those tracks were won once.
                if (v >= 0 && g_wins[v] == 0) g_wins[v] = 1;
            } else if (sscanf(line, "won %d%c %d", &track, &dir, &count) == 3) {
                int v = FindVariant(track, dir == 'R' ? 1 : 0);
                if (v >= 0 && count > 0) g_wins[v] = static_cast<uint16_t>(count > 60000 ? 60000 : count);
            } else if (sscanf(line, "pickup %d", &index) == 1 && index >= 0 && index < kMaxPickups) {
                g_pickupDone[index] = true;
                pickups++;
            }
        }
        fclose(f);
    }
    for (int v = 0; v < kNumVariants; v++) won += g_wins[v] > 0;
    Log("Profile \"%s\": %d track variant(s) won so far, %d done in the current district rounds, %d money pickup(s) "
        "returned in this round (%s).", g_rotProfile, won, done, pickups, path);
}

void SaveRotation() {
    char path[MAX_PATH];
    if (!RotationPath(path)) return;
    FILE* f = fopen(path, "w");
    if (!f) {
        Log("Could not write %s.", path);
        return;
    }
    fprintf(f, "# NFSU2 Endless Career rotation (track ID + F/R). Delete this file to start over.\n"
               "# won <track> <n>: wins in Endless Career (X icon on the map until the first win)\n"
               "# done <track>: won in the current round of its district (a district starts a new round when all are done)\n"
               "# pickup <index>: money pickup returned in the current pickup round\n");
    for (int v = 0; v < kNumVariants; v++)
        if (g_wins[v]) fprintf(f, "won %d%c %u\n", kVariants[v].track, kVariants[v].reverse ? 'R' : 'F', g_wins[v]);
    for (int v = 0; v < kNumVariants; v++)
        if (g_done[v]) fprintf(f, "done %d%c\n", kVariants[v].track, kVariants[v].reverse ? 'R' : 'F');
    for (int i = 0; i < kMaxPickups; i++)
        if (g_pickupDone[i]) fprintf(f, "pickup %d\n", i);
    fclose(f);
}

// ---- Picks (one per map slot; slots belong to districts) -------------------

struct Pick {
    int race;     // carrier race index, -1 = empty slot
    int variant;
    int marker;
    int laps;
};
struct Slot {
    int  district;
    Pick pick;
};
Slot     g_slots[kMaxMapSlots];
int      g_slotCount = 0;
bool     g_active    = false;   // endless mode on (career finished)
uint32_t g_lastWon   = 0;       // carrier not picked again right away
int      g_lastVariant = -1;    // variant not picked again right away
uint32_t g_rng       = 0;
const unsigned char* g_startedRace = nullptr;  // race of the last RaceSession_Init (for the race time)
DWORD    g_startTick = 0;

uint32_t Random() {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

void BuildSlots() {
    g_slotCount = 0;
    for (int d = 0; d < kNumDistricts; d++)
        for (int k = 0; k < g_perDistrict[d] && g_slotCount < kMaxMapSlots; k++) g_slots[g_slotCount++] = Slot{d, Pick{-1, -1, -1, 0}};
}

void ClearPicks() {
    for (int s = 0; s < g_slotCount; s++) g_slots[s].pick = Pick{-1, -1, -1, 0};
}

bool RaceInUse(int i) {
    for (int s = 0; s < g_slotCount; s++)
        if (g_slots[s].pick.race == i) return true;
    return false;
}

bool VariantInUse(int v) {
    for (int s = 0; s < g_slotCount; s++)
        if (g_slots[s].pick.race >= 0 && g_slots[s].pick.variant == v) return true;
    return false;
}

bool MarkerInUse(int m) {
    for (int s = 0; s < g_slotCount; s++)
        if (g_slots[s].pick.race >= 0 && g_slots[s].pick.marker == m) return true;
    return false;
}

// Number of races of this type on the map (district < 0: anywhere).
int TypeCount(int type, int district) {
    int n = 0;
    for (int s = 0; s < g_slotCount; s++) {
        const Pick& p = g_slots[s].pick;
        if (p.race >= 0 && TypeOfFamily(kVariants[p.variant].family) == type && (district < 0 || g_slots[s].district == district)) n++;
    }
    return n;
}

bool CarrierOk(int i, int family, bool suv) {
    if (g_carrierFamily[i] != family || RaceInUse(i)) return false;
    const unsigned char* r = RaceAt(i);
    if (ReadAt<uint32_t>(r, ev::race::kKey) == g_lastWon) return false;
    return suv || !r[ev::race::kIsSUV];
}

int PickCarrier(int family, bool suv) {
    static int candidates[kMaxRaces];
    int n = 0;
    for (int i = 0; i < g_raceCount; i++)
        if (CarrierOk(i, family, suv)) candidates[n++] = i;
    return n ? candidates[Random() % n] : -1;
}

bool HasCarrier(int family, bool suv) {
    for (int i = 0; i < g_raceCount; i++)
        if (CarrierOk(i, family, suv)) return true;
    return false;
}

// Nearest usable marker to the variant's start line that no other race uses.
int NearestMarker(int v, int* distance) {
    int best = -1;
    float bestD = 0;
    for (int m = 0; m < kNumMarkers; m++) {
        if (!g_markerName[m] || MarkerInUse(m)) continue;
        float dx = static_cast<float>(kMarkers[m].x - kVariants[v].x), dy = static_cast<float>(kMarkers[m].y - kVariants[v].y);
        float d = dx * dx + dy * dy;
        if (best >= 0 && d >= bestD) continue;
        if (!MarkerInWorld(m)) continue;
        best = m;
        bestD = d;
    }
    if (distance) *distance = best >= 0 ? static_cast<int>(sqrtf(bestD)) : 0;
    return best;
}

bool VariantCandidate(int v, int district, bool suv) {
    return g_variantOk[v] && kVariants[v].district == district && v != g_lastVariant && !VariantInUse(v) &&
           HasCarrier(kVariants[v].family, suv);
}

// Picks a variant for an empty slot in this district (see the priority below).
int PickVariant(int district, bool suv) {
    for (int k = 0; k < g_forceCount; k++)  // test switch first
        if (!g_forceUsed[k] && VariantCandidate(g_force[k], district, suv)) {
            g_forceUsed[k] = true;
            Log("Test switch: %d%c comes up now.", kVariants[g_force[k]].track, kVariants[g_force[k]].reverse ? 'R' : 'F');
            return g_force[k];
        }
    // A district whose tracks are all done starts a new round.
    bool anyLeft = false, anyDone = false;
    for (int v = 0; v < kNumVariants; v++)
        if (g_variantOk[v] && kVariants[v].district == district) (g_done[v] ? anyDone : anyLeft) = true;
    if (!anyLeft && anyDone) {
        Log("All %s track variants done: %s starts a new round.", tracks::kDistrictNames[district],
            tracks::kDistrictNames[district]);
        for (int v = 0; v < kNumVariants; v++)
            if (kVariants[v].district == district) g_done[v] = false;
        SaveRotation();
    }
    // Priority: a race type that is nowhere on the map, then a track not done in this round, then a type this
    // district doesn't have yet. (Offline simulation: all 5 types always on the map; ~1 pick in 6 repeats a track
    // early, mostly street X / drifts, which only a few districts have.)
    static int candidates[kNumVariants];
    int bestTier = 99, n = 0;
    for (int v = 0; v < kNumVariants; v++) {
        if (!VariantCandidate(v, district, suv)) continue;
        int type = TypeOfFamily(kVariants[v].family);
        int tier = (TypeCount(type, -1) > 0 ? 4 : 0) + (g_done[v] ? 2 : 0) + (TypeCount(type, district) > 0 ? 1 : 0);
        if (tier < bestTier) {
            bestTier = tier;
            n = 0;
        }
        if (tier == bestTier) candidates[n++] = v;
    }
    if (n) return candidates[Random() % n];
    return -1;
}

int LapsFor(int v, int race) {
    const tracks::Variant& var = kVariants[v];
    float scale = g_lengthScale * g_groupScale[GroupOfFamily(var.family)];
    if (var.family == tracks::kFCircuit || var.family == tracks::kFSpeedway) {
        int laps = static_cast<int>(g_targetSeconds * scale * g_speed[var.family] / var.length + 0.5f);
        return Clamp(laps, 2, kMaxLaps);  // never a 1-lap circuit (untested in this game)
    }
    if (var.family == tracks::kFDriftLot) {
        int laps = static_cast<int>(g_driftSeconds * scale * g_speed[var.family] / var.length + 0.5f);
        return Clamp(laps, 1, kMaxLaps);
    }
    int laps = g_orig[race].saved ? g_orig[race].legs[3] : RaceAt(race)[ev::race::kLegs + 3];  // the race's own lap count
    if (laps > 1) laps = static_cast<int>(laps * scale + 0.5f);  // street X (sprints, drags, downhill drifts are 1 run)
    return Clamp(laps, 1, kMaxLaps);
}

// ---- Changes to the race table (in memory only) ---------------------------

void SaveOriginal(int i) {
    unsigned char* r = RaceAt(i);
    Original& o = g_orig[i];
    if (o.saved) return;
    o.saved       = true;
    o.cash        = ReadAt<int32_t>(r, ev::race::kCash);
    o.intro       = ReadAt<uint16_t>(r, ev::race::kIntro);
    o.outro       = ReadAt<uint16_t>(r, ev::race::kOutro);
    o.triggerName = ReadAt<uint16_t>(r, ev::race::kTriggerName);
    o.trigger     = ReadAt<uint32_t>(r, ev::race::kTrigger);
    o.hidden      = r[ev::race::kHidden];
    o.numLegs     = r[ev::race::kNumLegs];
    o.icon        = r[ev::race::kIcon];
    memcpy(o.legs, r + ev::race::kLegs, kLegBytes);
    memcpy(o.opponents, r + ev::race::kOpponents, kOppBytes);
}

// [Rewards]: base = the fixed amount for the race type, or else the race's own payout raised to the best stage-5 payout
// of its type; times Percent, plus GrowPerWin % for every post-game win so far (up to GrowMax %).
int TotalWins() {
    int total = 0;
    for (int v = 0; v < kNumVariants; v++) total += g_wins[v];
    return total;
}

int32_t RewardFor(const unsigned char* r, int kind, int32_t own, bool hidden) {
    long long base = own;
    if (kind == kSponsor && g_rewardFixed[kSponsor] == 0) {
        // Like a normal race of the same type: its fixed payout or its stage-5 level; the sponsor's own payout is
        // ignored (otherwise it always wins the max below).
        const int k = KindOf(r, 0);
        if (k >= 0 && k < kNumKinds && k != kSponsor) {
            kind = k;
            own = 0;
            hidden = false;
        }
    }
    if (kind >= 0 && kind < kNumKinds) {
        // Races hidden in the career ($4,000 there) are paid like a normal race: own payout and hidden level ignored.
        if (hidden) own = 0;
        const int level = g_level[kind][0];
        base = g_rewardFixed[kind] > 0 ? g_rewardFixed[kind] : (own > level ? own : level);
    }
    long long grow = static_cast<long long>(TotalWins()) * g_rewardGrowPerWin;
    if (grow > g_rewardGrowMax) grow = g_rewardGrowMax;
    long long cash = base * g_rewardPercent / 100 * (100 + grow) / 100;
    if (cash < 0) cash = 0;
    if (cash > 10000000) cash = 10000000;
    return static_cast<int32_t>(cash);
}

void ApplyChanges(const Pick& p) {
    SaveOriginal(p.race);
    unsigned char* r = RaceAt(p.race);
    const Original& o = g_orig[p.race];
    const tracks::Variant& var = kVariants[p.variant];

    unsigned char legs[kLegBytes] = {};
    uint16_t track = var.track;
    memcpy(legs, &track, 2);
    legs[2] = var.reverse;
    legs[3] = static_cast<unsigned char>(p.laps);
    memcpy(r + ev::race::kLegs, legs, kLegBytes);
    r[ev::race::kNumLegs] = 1;
    uint32_t hash = kMarkers[p.marker].hash;
    memcpy(r + ev::race::kTrigger, &hash, 4);
    memcpy(r + ev::race::kTriggerName, &g_markerName[p.marker], 2);
    // Map icon: X (sponsor style) for a track not won yet, circle for a repeat. URL races keep the URL icon.
    if (o.icon != 2) r[ev::race::kIcon] = g_iconX ? (g_wins[p.variant] == 0 ? 1 : 3) : o.icon;

    const int kind = KindOf(r, o.icon);
    int32_t cash = RewardFor(r, kind, o.cash, o.hidden != 0);
    uint16_t none = 0;
    memcpy(r + ev::race::kCash, &cash, 4);
    memcpy(r + ev::race::kIntro, &none, 2);
    memcpy(r + ev::race::kOutro, &none, 2);
    r[ev::race::kHidden] = 0;
    int offset = g_skillOffset[GroupOfFamily(var.family)];
    for (int n = 0; n < NumOpponents(r); n++) {
        unsigned char* op = Opponent(r, n);
        op[ev::opp::kMode]  = 0;  // fixed car level instead of "from the current stage"
        op[ev::opp::kLevel] = static_cast<unsigned char>(g_carLevel);
        for (int d = 0; d < 3; d++) op[ev::opp::kSkill + d] = static_cast<unsigned char>(Clamp(g_skillBase[d] + offset, 0, kMaxSkill));
    }
}

// ---- Money pickups ---------------------------------------------------------

unsigned char* g_pickups = nullptr;   // g_MoneyPickups
int            g_pickupCount = 0;
uint16_t       g_pickupCashOrig[kMaxPickups];
bool           g_pickupCashSaved = false;

bool EnsurePickups() {
    if (g_pickups) return true;
    unsigned char* table = reinterpret_cast<unsigned char*>(*reinterpret_cast<volatile uintptr_t*>(ev::kMoneyPickupsPtr));
    int count = *reinterpret_cast<volatile int32_t*>(ev::kMoneyPickupCount);
    if (!table || count <= 0 || count > kMaxPickups || !IsReadable(reinterpret_cast<uintptr_t>(table), count * ev::kMoneyPickupSize))
        return false;
    g_pickups = table;
    g_pickupCount = count;
    int known = 0;
    for (int i = 0; i < count; i++)
        for (const auto& b : kPickups)
            if (b.hash == ReadAt<uint32_t>(table + i * ev::kMoneyPickupSize, ev::pickup::kTrigger)) known++;
    Log("Money pickups: %d in the game (%d with a known position).", count, known);
    return true;
}

const tracks::Pickup* PickupPos(const unsigned char* rec) {
    uint32_t h = ReadAt<uint32_t>(rec, ev::pickup::kTrigger);
    for (const auto& b : kPickups)
        if (b.hash == h) return &b;
    return nullptr;
}

// The game's own availability test without the collected flag (MoneyPickup_IsAvailable 0x00513890).
bool PickupConditionMet(const unsigned char* rec) {
    uint8_t cond = rec[ev::pickup::kCondition];
    int32_t value = ReadAt<int32_t>(rec, ev::pickup::kValue);
    if (cond == 1) return *reinterpret_cast<volatile int32_t*>(ev::kCurrentStage) > value;
    if (cond == 2) return StatusOf(static_cast<uint32_t>(value)) == 1;
    return true;
}

bool PickupCollected(const unsigned char* rec) { return Career()[ev::kCareerPickupFlags + rec[ev::pickup::kIndex]] != 0; }

void SetPickupCash(bool endless) {
    if (!EnsurePickups()) return;
    if (endless && g_pickupsOn) {
        for (int i = 0; i < g_pickupCount; i++) {
            unsigned char* rec = g_pickups + i * ev::kMoneyPickupSize;
            if (!g_pickupCashSaved) g_pickupCashOrig[i] = ReadAt<uint16_t>(rec, ev::pickup::kCash);
            uint16_t cash = static_cast<uint16_t>(g_pickupCash);
            memcpy(rec + ev::pickup::kCash, &cash, 2);
        }
        g_pickupCashSaved = true;
    } else if (g_pickupCashSaved) {
        for (int i = 0; i < g_pickupCount; i++) memcpy(g_pickups + i * ev::kMoneyPickupSize + ev::pickup::kCash, &g_pickupCashOrig[i], 2);
        g_pickupCashSaved = false;
    }
}

// Brings one collected money pickup back: rotating (each once per round), never within MinSpacing of a pickup
// that is already out. Clears the game's collected flag and rebuilds the game's list of live pickups.
void ReturnPickup() {
    if (!g_pickupsOn || !EnsurePickups()) return;
    static int candidates[kMaxPickups];
    for (int pass = 0; pass < 2; pass++) {
        int n = 0, collected = 0;
        for (int i = 0; i < g_pickupCount; i++) {
            const unsigned char* rec = g_pickups + i * ev::kMoneyPickupSize;
            if (!PickupCollected(rec) || !PickupConditionMet(rec)) continue;
            collected++;
            if (g_pickupDone[rec[ev::pickup::kIndex]]) continue;
            const tracks::Pickup* pos = PickupPos(rec);
            if (!pos) continue;
            bool tooClose = false;
            for (int j = 0; j < g_pickupCount && !tooClose; j++) {
                const unsigned char* other = g_pickups + j * ev::kMoneyPickupSize;
                if (j == i || PickupCollected(other) || !PickupConditionMet(other)) continue;
                const tracks::Pickup* op = PickupPos(other);
                if (!op) continue;
                float dx = static_cast<float>(pos->x - op->x), dy = static_cast<float>(pos->y - op->y);
                tooClose = dx * dx + dy * dy < g_pickupSpacing * g_pickupSpacing;
            }
            if (!tooClose) candidates[n++] = i;
        }
        if (n) {
            unsigned char* rec = g_pickups + candidates[Random() % n] * ev::kMoneyPickupSize;
            const tracks::Pickup* pos = PickupPos(rec);
            Career()[ev::kCareerPickupFlags + rec[ev::pickup::kIndex]] = 0;
            g_pickupDone[rec[ev::pickup::kIndex]] = true;
            reinterpret_cast<void(__fastcall*)(void*)>(ev::kBuildLiveMoneyPickups)(Career());
            if (pos)
                Log("Money pickup #%u is back at (%d, %d) in %s ($%u).", rec[ev::pickup::kIndex], pos->x, pos->y,
                    tracks::kDistrictNames[pos->district], ReadAt<uint16_t>(rec, ev::pickup::kCash));
            return;
        }
        if (collected == 0) {
            Log("Money pickups: none collected, so none to bring back.");
            return;
        }
        if (pass == 0) {  // every collected one was returned this round already (or is too close): new round
            bool anyDone = false;
            for (bool d : g_pickupDone) anyDone |= d;
            if (!anyDone) break;
            Log("Money pickups: all returned once, a new pickup round starts.");
            memset(g_pickupDone, 0, sizeof(g_pickupDone));
        }
    }
    Log("Money pickups: none can come back right now (all collected ones are next to a pickup that is out).");
}

void RestoreAll() {
    int n = 0;
    for (int i = 0; i < g_raceCount; i++) {
        Original& o = g_orig[i];
        if (!o.saved) continue;
        unsigned char* r = RaceAt(i);
        memcpy(r + ev::race::kCash, &o.cash, 4);
        memcpy(r + ev::race::kIntro, &o.intro, 2);
        memcpy(r + ev::race::kOutro, &o.outro, 2);
        memcpy(r + ev::race::kTriggerName, &o.triggerName, 2);
        memcpy(r + ev::race::kTrigger, &o.trigger, 4);
        r[ev::race::kHidden] = o.hidden;
        r[ev::race::kNumLegs] = o.numLegs;
        r[ev::race::kIcon] = o.icon;
        memcpy(r + ev::race::kLegs, o.legs, kLegBytes);
        memcpy(r + ev::race::kOpponents, o.opponents, kOppBytes);
        o.saved = false;
        n++;
    }
    SetPickupCash(false);
    if (n) Log("Restored the original data of %d races.", n);
}

float EstimatedSeconds(const Pick& p) {
    const tracks::Variant& var = kVariants[p.variant];
    return var.length * p.laps / g_speed[var.family];
}

void LogPick(const Slot& s, int distance) {
    const Pick& p = s.pick;
    const tracks::Variant& var = kVariants[p.variant];
    const tracks::Marker& m = kMarkers[p.marker];
    const unsigned char* r = RaceAt(p.race);
    float secs = EstimatedSeconds(p);
    char icon = r[ev::race::kIcon] == 1 ? 'X' : (r[ev::race::kIcon] == 2 ? 'U' : 'O');
    Log("  %-15s %c %-8s %-24s %d%c x%-2d %5.1f km ~%d:%02d | %-9s %4d m from the start | %s (%s, $%d, %d opponents)",
        tracks::kDistrictNames[s.district], icon, kTypeNames[TypeOfFamily(var.family)], var.name, var.track,
        var.reverse ? 'R' : 'F', p.laps, var.length * p.laps / 1000.0f, static_cast<int>(secs) / 60,
        static_cast<int>(secs) % 60, m.name, distance, RaceName(p.race), kKindNames[KindOfRace(p.race)],
        ReadAt<int32_t>(r, ev::race::kCash), NumOpponents(r));
}

// Runs right after the game rebuilt (and, post-career, emptied) the event list.
void AfterRebuild() {
    if (!EnsureTables()) return;

    if (!CareerFinished()) {
        if (g_active) {
            Log("Career not finished in this profile: endless mode OFF.");
            g_active = false;
        }
        ClearPicks();
        RestoreAll();
        return;
    }
    if (ReadAt<int32_t>(Career(), ev::kCareerSpecialMode) != 0) return;  // the game shows no events in this mode
    if (!g_active) {
        Log("Career finished: endless mode ON.");
        g_active = true;
        SetPickupCash(true);
    }
    EnsureRotation();

    bool suv = CurrentCarIsSUV();
    bool logged = false;
    for (int s = 0; s < g_slotCount; s++) {
        Pick& p = g_slots[s].pick;
        if (p.race >= 0 && !suv && RaceAt(p.race)[ev::race::kIsSUV]) p.race = -1;  // car changed: no SUV race
        if (p.race >= 0) continue;
        int v = PickVariant(g_slots[s].district, suv);
        if (v < 0) continue;
        int distance = 0;
        int m = NearestMarker(v, &distance);
        int race = PickCarrier(kVariants[v].family, suv);
        if (m < 0 || race < 0) continue;
        p = Pick{race, v, m, 0};
        p.laps = LapsFor(v, race);
        ApplyChanges(p);
        if (!logged) {
            Log("New race(s) on the map (X = track not won yet, O = repeat, U = URL):");
            logged = true;
        }
        LogPick(g_slots[s], distance);
    }

    using AddFn = void(__fastcall*)(void* list, void* edx, const void* race);
    void* list = Career() + ev::kCareerEventList;
    const uintptr_t* active = reinterpret_cast<const uintptr_t*>(ev::kActiveEvents);
    for (int s = 0; s < g_slotCount; s++) {
        const Pick& p = g_slots[s].pick;
        if (p.race < 0) continue;
        bool present = false;
        for (int j = 0; j < ev::kMaxActive; j++)
            if (active[j] == reinterpret_cast<uintptr_t>(RaceAt(p.race))) present = true;
        if (!present) reinterpret_cast<AddFn>(ev::kEventListAddActive)(list, nullptr, RaceAt(p.race));
    }
}

// ---- Hooks -----------------------------------------------------------------

// Career_RebuildActiveEvents is __fastcall(career) with no stack arguments.
void __fastcall Hook_Rebuild(void* career) {
    reinterpret_cast<void(__fastcall*)(void*)>(ev::kRebuildActiveEvents)(career);
    if (g_versionOk && career == Career()) AfterRebuild();
    if (career == Career() && bf::FeatureOn(bf::kUniqueParts)) bf::UniquePartsAfterRebuild();  // checks itself
    if (career == Career() && bf::FeatureOn(bf::kCovers)) bf::CoversAfterRebuild();            // checks itself
}

// Seconds since RaceSession_Init of this race (includes loading), or -1 if another race was started since.
int RaceSeconds(int race) {
    return g_startedRace == RaceAt(race) ? static_cast<int>((GetTickCount() - g_startTick) / 1000) : -1;
}

// Career_OnRaceWon is __thiscall(career, raceKey); __fastcall with an unused EDX has the same layout.
void __fastcall Hook_RaceWon(void* career, void* edx, uint32_t key) {
    bool track = g_versionOk && career == Career() && g_active && g_races;
    int32_t cashBefore = *reinterpret_cast<volatile int32_t*>(kCash);
    Pick won{-1, -1, -1, 0};
    int district = -1;
    if (track) {
        for (int s = 0; s < g_slotCount; s++) {
            Pick& p = g_slots[s].pick;
            if (p.race >= 0 && ReadAt<uint32_t>(RaceAt(p.race), ev::race::kKey) == key) {
                won = p;
                district = g_slots[s].district;
                p.race = -1;  // the rebuild that follows picks a replacement in the same district
            }
        }
        g_lastWon = key;
    }
    reinterpret_cast<void(__fastcall*)(void*, void*, uint32_t)>(ev::kOnRaceWon)(career, edx, key);
    if (!track) return;
    int32_t cashAfter = *reinterpret_cast<volatile int32_t*>(kCash);
    if (won.race < 0) {
        Log("WON a race that was not one of ours (key %08X): cash %d -> %d", key, cashBefore, cashAfter);
        return;
    }
    const tracks::Variant& var = kVariants[won.variant];
    int secs = RaceSeconds(won.race);
    int est = static_cast<int>(EstimatedSeconds(won));
    Log("WON %s = %s %d%c x%d (%.1f km, %s): cash %d -> %d (%+d). Race time incl. loading %d:%02d (estimated %d:%02d).",
        RaceName(won.race), var.name, var.track, var.reverse ? 'R' : 'F', won.laps, var.length * won.laps / 1000.0f,
        tracks::kDistrictNames[district], cashBefore, cashAfter, cashAfter - cashBefore, secs < 0 ? 0 : secs / 60,
        secs < 0 ? 0 : secs % 60, est / 60, est % 60);
    g_lastVariant = won.variant;
    g_done[won.variant] = true;
    if (g_wins[won.variant] < 60000) g_wins[won.variant]++;
    for (int k = 0; k < g_pickupsPerWin; k++) ReturnPickup();
    SaveRotation();
}

// Career_OnRaceLost is __thiscall(career, raceKey), RET 4. Only logs: a lost race stays on the map for another try.
void __fastcall Hook_RaceLost(void* career, void* edx, uint32_t key) {
    reinterpret_cast<void(__fastcall*)(void*, void*, uint32_t)>(ev::kOnRaceLost)(career, edx, key);
    if (!g_versionOk || career != Career() || !g_active || !g_races) return;
    for (int s = 0; s < g_slotCount; s++) {
        const Pick& p = g_slots[s].pick;
        if (p.race >= 0 && ReadAt<uint32_t>(RaceAt(p.race), ev::race::kKey) == key) {
            const tracks::Variant& var = kVariants[p.variant];
            int secs = RaceSeconds(p.race);
            int est = static_cast<int>(EstimatedSeconds(p));
            Log("LOST %s = %s %d%c x%d (%.1f km): stays on the map. Race time incl. loading %d:%02d (estimated %d:%02d).",
                RaceName(p.race), var.name, var.track, var.reverse ? 'R' : 'F', p.laps, var.length * p.laps / 1000.0f,
                secs < 0 ? 0 : secs / 60, secs < 0 ? 0 : secs % 60, est / 60, est % 60);
            return;
        }
    }
    Log("LOST a race that was not one of ours (key %08X).", key);
}

// RaceSession_Init is __thiscall(session = career+0xD24, race), RET 4. For a race the player already won it sets
// "pays" = 0 and "replay" = 1, which makes Career_ApplyRaceResult ignore the result. For our races, undo that.
void __fastcall Hook_SessionInit(unsigned char* sessionObj, void* edx, const unsigned char* race) {
    reinterpret_cast<void(__fastcall*)(void*, void*, const void*)>(ev::kRaceSessionInit)(sessionObj, edx, race);
    if (!g_versionOk || !g_active || !g_races || sessionObj != Career() + ev::kCareerRaceSession) return;
    g_startedRace = race;
    g_startTick = GetTickCount();
    for (int s = 0; s < g_slotCount; s++) {
        const Pick& p = g_slots[s].pick;
        if (p.race >= 0 && RaceAt(p.race) == race) {
            const tracks::Variant& var = kVariants[p.variant];
            Log("Starting %s = %s %d%c x%d: replay=%d pays=%d counts=%d -> treated as a fresh race.", RaceName(p.race),
                var.name, var.track, var.reverse ? 'R' : 'F', p.laps, sessionObj[ev::session::kReplay],
                sessionObj[ev::session::kPays], sessionObj[ev::session::kCounts]);
            sessionObj[ev::session::kReplay] = 0;
            sessionObj[ev::session::kPays]   = 1;
            return;
        }
    }
    Log("Starting a race that is not one of ours; left as the game set it up.");
}

// All call sites must be "CALL expected" before any of them is patched.
bool CallTargetIs(uintptr_t site, uintptr_t target) {
    if (!IsReadable(site, 5)) return false;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(site);
    int32_t rel;
    memcpy(&rel, p + 1, 4);
    return p[0] == 0xE8 && site + 5 + rel == target;
}

DWORD WINAPI VersionCheckThread(LPVOID) {
    if (ExeMd5Ok()) {
        InterlockedExchange(&g_versionOk, 1);
        Log("Active. Waiting for a profile whose career is finished.");
    } else {
        Log("The hooks stay installed but only pass calls through to the game.");
    }
    return 0;
}

void Init() {
    Log("==== Endless Career started ====");
    LoadSettings();
    BuildSlots();

    if (!CodeMatches(kCodeChecks, sizeof(kCodeChecks) / sizeof(kCodeChecks[0]))) {
        Log("Not hooking anything.");
        return;
    }
    for (uintptr_t site : ev::kRebuildCallSites)
        if (!CallTargetIs(site, ev::kRebuildActiveEvents)) {
            Log("STOP: 0x%08X is not CALL Career_RebuildActiveEvents. Not hooking anything.", static_cast<unsigned>(site));
            return;
        }
    for (uintptr_t site : ev::kOnRaceWonCallSites)
        if (!CallTargetIs(site, ev::kOnRaceWon)) {
            Log("STOP: 0x%08X is not CALL Career_OnRaceWon. Not hooking anything.", static_cast<unsigned>(site));
            return;
        }
    for (uintptr_t site : ev::kOnRaceLostCallSites)
        if (!CallTargetIs(site, ev::kOnRaceLost)) {
            Log("STOP: 0x%08X is not CALL Career_OnRaceLost. Not hooking anything.", static_cast<unsigned>(site));
            return;
        }
    for (uintptr_t site : ev::kRaceSessionInitCallSites)
        if (!CallTargetIs(site, ev::kRaceSessionInit)) {
            Log("STOP: 0x%08X is not CALL RaceSession_Init. Not hooking anything.", static_cast<unsigned>(site));
            return;
        }
    for (uintptr_t site : ev::kRebuildCallSites)
        if (RedirectCall(site, ev::kRebuildActiveEvents, reinterpret_cast<const void*>(&Hook_Rebuild)))
            Log("Hooked Career_RebuildActiveEvents call at 0x%08X.", static_cast<unsigned>(site));
    for (uintptr_t site : ev::kOnRaceWonCallSites)
        if (RedirectCall(site, ev::kOnRaceWon, reinterpret_cast<const void*>(&Hook_RaceWon)))
            Log("Hooked Career_OnRaceWon call at 0x%08X.", static_cast<unsigned>(site));
    for (uintptr_t site : ev::kOnRaceLostCallSites)
        if (RedirectCall(site, ev::kOnRaceLost, reinterpret_cast<const void*>(&Hook_RaceLost)))
            Log("Hooked Career_OnRaceLost call at 0x%08X.", static_cast<unsigned>(site));
    for (uintptr_t site : ev::kRaceSessionInitCallSites)
        if (RedirectCall(site, ev::kRaceSessionInit, reinterpret_cast<const void*>(&Hook_SessionInit)))
            Log("Hooked RaceSession_Init call at 0x%08X.", static_cast<unsigned>(site));

    ClearPicks();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    g_rng = static_cast<uint32_t>(t.QuadPart) ^ GetTickCount() ^ 0x9E3779B9u;
    if (g_rng == 0) g_rng = 1;

    // CryptoAPI must not run inside DllMain, so the MD5 check runs on its own thread.
    HANDLE thread = CreateThread(nullptr, 0, VersionCheckThread, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
}

}  // namespace

void bf::StartEndlessCareer() { Init(); }  // patches the CALLs before the game's own code starts running
