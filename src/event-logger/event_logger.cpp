// EventLogger (debug, [Debug] EventLogger = 1): read-only. Writes the race table once (event-logger-races.txt), then
// logs stage, cash, the events on the map, event status changes and race markers driven into; each profile load also
// writes the whole status list to event-logger-status-NN.txt (all in BayviewForever\).

#define BF_TAG "EventLogger"
#include "nfsu2.h"

using namespace nfsu2;

namespace {

const CodeCheck kCodeChecks[] = {
    {0x00512C9A, {0x89, 0x0D, 0xE0, 0x83, 0x83, 0x00}, 6, "MOV [g_GRaces],ECX in GCareer_LoadChunk"},
    {0x00512CA0, {0x89, 0x15, 0xE4, 0x83, 0x83, 0x00}, 6, "MOV [g_GRaceCount],EDX in GCareer_LoadChunk"},
    {0x00527D30, {0x81, 0xEC, 0xA0, 0x01, 0x00, 0x00, 0x8B, 0xD1}, 8, "start of Career_RebuildActiveEvents"},
    {0x00500B80, {0x56, 0x8B, 0xB1, 0xFC, 0x7A, 0x00, 0x00}, 7, "MOV ESI,[ECX+0x7AFC] in Career_IsEventWon"},
    {0x00532B22, {0xC7, 0x86, 0x1C, 0x87, 0x00, 0x00}, 6, "MOV [ESI+0x871C],0 in maybe_Career_OnEnterMarker"},
    {0x005414BD, {0x8D, 0x8C, 0x30, 0xD8, 0x72, 0x00, 0x00}, 7, "LEA ECX,[EAX+ESI+0x72D8] in Career_OnRaceWon"},
    {0x00532871, {0xB9, 0x80, 0x01, 0x00, 0x00}, 5, "MOV ECX,0x180 in maybe_Career_Reset"},
};

const DWORD kPollInterval = 500;  // ms
const int   kMaxDumps     = 50;   // status-list dump files per session

// ---- Static tables (loaded from GLOBALB) ---------------------------------

struct Tables {
    const unsigned char* races;
    int                  raceCount;
    const unsigned char* showcases;
    int                  showcaseCount;
    const unsigned char* stages;
    int                  stageCount;
    const char*          strings;
    int                  stringsSize;
};

Tables g_t;

template <typename T>
T ReadGlobal(uintptr_t addr) {
    return *reinterpret_cast<volatile T*>(addr);
}

// True once the game has loaded the GCareer tables and they look sane.
bool TablesReady(Tables& t) {
    t.races         = reinterpret_cast<const unsigned char*>(ReadGlobal<uintptr_t>(ev::kRaceTablePtr));
    t.raceCount     = ReadGlobal<int32_t>(ev::kRaceCount);
    t.showcases     = reinterpret_cast<const unsigned char*>(ReadGlobal<uintptr_t>(ev::kShowcaseTablePtr));
    t.showcaseCount = ReadGlobal<int32_t>(ev::kShowcaseCount);
    t.stages        = reinterpret_cast<const unsigned char*>(ReadGlobal<uintptr_t>(ev::kStageTablePtr));
    t.stageCount    = ReadGlobal<int32_t>(ev::kStageCount);
    t.strings       = reinterpret_cast<const char*>(ReadGlobal<uintptr_t>(ev::kStringsPtr));
    t.stringsSize   = ReadGlobal<int32_t>(ev::kStringsSize);

    if (!t.races || t.raceCount <= 0 || t.raceCount > 2000) return false;
    if (!t.strings || t.stringsSize <= 0 || t.stringsSize > 0x100000) return false;
    if (t.showcaseCount < 0 || t.showcaseCount > 1000 || t.stageCount < 0 || t.stageCount > 32) return false;
    if (!IsReadable(reinterpret_cast<uintptr_t>(t.races), t.raceCount * ev::kRaceSize)) return false;
    if (!IsReadable(reinterpret_cast<uintptr_t>(t.strings), t.stringsSize)) return false;
    if (t.showcaseCount && !IsReadable(reinterpret_cast<uintptr_t>(t.showcases), t.showcaseCount * ev::kShowcaseSize))
        return false;
    if (t.stageCount && !IsReadable(reinterpret_cast<uintptr_t>(t.stages), t.stageCount * ev::kStageDataSize))
        return false;
    return true;
}

const char* Str(uint16_t off) {
    if (off >= g_t.stringsSize) return "?";
    if (!memchr(g_t.strings + off, 0, g_t.stringsSize - off)) return "?";
    return g_t.strings + off;
}

const unsigned char* RaceAt(int i) { return g_t.races + i * ev::kRaceSize; }

const char* RaceName(const unsigned char* r) { return Str(ReadAt<uint16_t>(r, ev::race::kName)); }

// Returns the race if p points exactly at a record of the race table.
const unsigned char* AsRace(uintptr_t p) {
    uintptr_t base = reinterpret_cast<uintptr_t>(g_t.races);
    if (p < base || p >= base + g_t.raceCount * ev::kRaceSize) return nullptr;
    if ((p - base) % ev::kRaceSize != 0) return nullptr;
    return reinterpret_cast<const unsigned char*>(p);
}

const unsigned char* RaceByKey(uint32_t key) {
    for (int i = 0; i < g_t.raceCount; i++)
        if (ReadAt<uint32_t>(RaceAt(i), ev::race::kKey) == key) return RaceAt(i);
    return nullptr;
}

const char* BehaviorName(unsigned b) {
    static const char* const kNames[] = {"Circuit", "Sprint", "StreetX", "OpenWorld", "Drag", "Drift"};
    return b < 6 ? kNames[b] : "?";
}

const char* IconName(unsigned i) {
    static const char* const kNames[] = {"none", "sponsor", "URL", "regular"};
    return i < 4 ? kNames[i] : "?";
}

// "S2_SPRINT_3 (stage 2 Sprint, $300)"
const char* DescribeRace(const unsigned char* r, char* buf, size_t size) {
    snprintf(buf, size, "%s (stage %d %s%s%s, $%d)", RaceName(r), static_cast<signed char>(r[ev::race::kStage]),
             BehaviorName(r[ev::race::kBehavior]), r[ev::race::kIsSUV] ? " SUV" : "",
             r[ev::race::kHidden] ? " hidden" : "", ReadAt<int32_t>(r, ev::race::kCash));
    return buf;
}

// Name for any status-list key: a race, a showcase (DVD/magazine), or just the hash.
const char* DescribeKey(uint32_t key, char* buf, size_t size) {
    if (const unsigned char* r = RaceByKey(key)) return DescribeRace(r, buf, size);
    for (int i = 0; i < g_t.showcaseCount; i++) {
        const unsigned char* s = g_t.showcases + i * ev::kShowcaseSize;
        if (ReadAt<uint32_t>(s, 0x20) == key) {
            snprintf(buf, size, "showcase %.32s (stage %d)", reinterpret_cast<const char*>(s), s[0x25]);
            return buf;
        }
    }
    snprintf(buf, size, "key %08X (not a race or showcase)", key);
    return buf;
}

const char* StateName(unsigned s) {
    switch (s) {
        case 0: return "none";
        case 1: return "WON";
        case 2: return "in progress";
        case 4: return "lost";
        case 5: return "shop found";
        default: return "?";
    }
}

// Writes every race as the game sees it, so we can compare with docs/races.txt
// (and spot a modded GLOBALB on the PC).
void WriteRaceFile() {
    char dir[MAX_PATH], path[MAX_PATH];
    GetDataDir(dir);
    snprintf(path, sizeof(path), "%sevent-logger-races.txt", dir);
    FILE* f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "idx name                   stage type      icon    hidden   cash legs(track/rev/laps)       opp key\n");
    for (int i = 0; i < g_t.raceCount; i++) {
        const unsigned char* r = RaceAt(i);
        char legs[64] = "";
        int numLegs = r[ev::race::kNumLegs];
        for (int k = 0; k < numLegs && k < 4; k++) {
            const unsigned char* leg = r + ev::race::kLegs + k * 4;
            size_t len = strlen(legs);
            snprintf(legs + len, sizeof(legs) - len, "%X%s/%d ", ReadAt<uint16_t>(leg, 0), (leg[2] & 1) ? "r" : "",
                     leg[3]);
        }
        fprintf(f, "%3d %-22s %5d %-9s %-7s %6d %6d %-26s %3d %08X\n", i, RaceName(r),
                static_cast<signed char>(r[ev::race::kStage]), BehaviorName(r[ev::race::kBehavior]),
                IconName(r[ev::race::kIcon]), r[ev::race::kHidden], ReadAt<int32_t>(r, ev::race::kCash), legs,
                r[ev::race::kNumOpp], ReadAt<uint32_t>(r, ev::race::kKey));
    }
    fclose(f);
    Log("Wrote all %d races to %s", g_t.raceCount, path);
}

void LogStageTable() {
    for (int i = 0; i < g_t.stageCount; i++) {
        const unsigned char* s = g_t.stages + i * ev::kStageDataSize;
        const unsigned char* last = RaceByKey(ReadAt<uint32_t>(s, ev::stagedata::kLastEvent));
        const unsigned char* m = s + ev::stagedata::kMaxShown;
        Log("  stage data %d: id=%d last event=%s max shown circuit/drag/streetX/drift/sprint=%d/%d/%d/%d/%d", i,
            s[ev::stagedata::kId], last ? RaceName(last) : "-", m[0], m[1], m[2], m[3], m[4]);
    }
}

// ---- Per-profile state ----------------------------------------------------

struct Snapshot {
    int32_t       stage;
    int32_t       cash;
    int32_t       statusCount;
    uintptr_t     active[ev::kMaxActive];
    unsigned char status[ev::kMaxStatus * ev::kStatusEntrySize];
    unsigned char stageComplete[ev::kNumStageRecords];
    uintptr_t     stageData[ev::kNumStageRecords];
    uintptr_t     enteredRace;
};

void TakeSnapshot(Snapshot& s) {
    s.stage       = ReadGlobal<int32_t>(ev::kCurrentStage);
    s.cash        = ReadGlobal<int32_t>(kCash);
    s.statusCount = ReadGlobal<int32_t>(ev::kStatusCount);
    memcpy(s.active, reinterpret_cast<const void*>(ev::kActiveEvents), sizeof(s.active));
    memcpy(s.status, reinterpret_cast<const void*>(ev::kStatusList), sizeof(s.status));
    for (int i = 0; i < ev::kNumStageRecords; i++) {
        const unsigned char* rec = reinterpret_cast<const unsigned char*>(ev::kStageRecords + i * ev::kStageRecordSize);
        s.stageComplete[i] = rec[ev::stagerec::kComplete];
        s.stageData[i]     = ReadAt<uintptr_t>(rec, ev::stagerec::kData);
    }
    s.enteredRace = ReadGlobal<uintptr_t>(ev::kEnteredRace);
}

int ValidStatusCount(const Snapshot& s) {
    if (s.statusCount < 0) return 0;
    return s.statusCount > ev::kMaxStatus ? ev::kMaxStatus : s.statusCount;
}

const unsigned char* StatusEntry(const Snapshot& s, int i) { return s.status + i * ev::kStatusEntrySize; }

int StatusOf(const Snapshot& s, uint32_t key) {
    for (int i = 0; i < ValidStatusCount(s); i++)
        if (ReadAt<uint32_t>(StatusEntry(s, i), ev::status::kKey) == key) return StatusEntry(s, i)[ev::status::kState];
    return -1;  // no entry
}

// Same test as Career_IsFinished: is stage 5's last event (the final Caleb race) won?
bool CareerFinished(const Snapshot& s) {
    for (int i = 0; i < g_t.stageCount; i++) {
        const unsigned char* d = g_t.stages + i * ev::kStageDataSize;
        if (d[ev::stagedata::kId] == 5) return StatusOf(s, ReadAt<uint32_t>(d, ev::stagedata::kLastEvent)) == 1;
    }
    return false;
}

int StageIdOf(uintptr_t stageData) {
    for (int i = 0; i < g_t.stageCount; i++)
        if (reinterpret_cast<uintptr_t>(g_t.stages + i * ev::kStageDataSize) == stageData)
            return g_t.stages[i * ev::kStageDataSize + ev::stagedata::kId];
    return -1;
}

void LogStages(const Snapshot& s) {
    for (int i = 0; i < ev::kNumStageRecords; i++)
        Log("  stage record %d: complete=%d data=stage %d", i, s.stageComplete[i], StageIdOf(s.stageData[i]));
}

bool InList(const uintptr_t* list, uintptr_t p) {
    for (int i = 0; i < ev::kMaxActive; i++)
        if (list[i] == p) return true;
    return false;
}

void LogActiveList(const Snapshot& s) {
    int n = 0;
    for (int i = 0; i < ev::kMaxActive; i++)
        if (s.active[i]) n++;
    Log("Events on the map: %d", n);
    char buf[160];
    for (int i = 0; i < ev::kMaxActive; i++) {
        if (!s.active[i]) continue;
        const unsigned char* r = AsRace(s.active[i]);
        if (r) Log("  [%2d] %s", i, DescribeRace(r, buf, sizeof(buf)));
        else Log("  [%2d] pointer %08X is not in the race table!", i, static_cast<unsigned>(s.active[i]));
    }
}

void LogActiveChanges(const Snapshot& prev, const Snapshot& cur) {
    char buf[160];
    for (int i = 0; i < ev::kMaxActive; i++) {
        if (prev.active[i] && !InList(cur.active, prev.active[i])) {
            const unsigned char* r = AsRace(prev.active[i]);
            Log("Map: removed %s", r ? DescribeRace(r, buf, sizeof(buf)) : "(not a race)");
        }
    }
    for (int i = 0; i < ev::kMaxActive; i++) {
        if (cur.active[i] && !InList(prev.active, cur.active[i])) {
            const unsigned char* r = AsRace(cur.active[i]);
            Log("Map: added %s", r ? DescribeRace(r, buf, sizeof(buf)) : "(not a race)");
        }
    }
}

int  g_dumpCount = 0;

// Summary in the log + the full list in event-logger-status-NN.txt.
void LogStatusList(const Snapshot& s) {
    int n = ValidStatusCount(s);
    int wonPerStage[8] = {0}, racesWon = 0, otherWon = 0, showcasesDone = 0, started = 0;
    for (int i = 0; i < n; i++) {
        const unsigned char* e = StatusEntry(s, i);
        const unsigned char* r = RaceByKey(ReadAt<uint32_t>(e, ev::status::kKey));
        int state = e[ev::status::kState];
        if (state == 1 && r) {
            racesWon++;
            int st = static_cast<signed char>(r[ev::race::kStage]);
            if (st >= 0 && st < 8) wonPerStage[st]++;
        } else if (state == 1) {
            otherWon++;
        }
        if (state == 2) started++;
        if (state == 5) showcasesDone++;
    }
    Log("Status list: %d entries (count field %d). Races won: %d (per stage 0-5: %d %d %d %d %d %d), "
        "other keys won: %d, in progress: %d, shops found: %d",
        n, s.statusCount, racesWon, wonPerStage[0], wonPerStage[1], wonPerStage[2], wonPerStage[3], wonPerStage[4],
        wonPerStage[5], otherWon, started, showcasesDone);

    if (g_dumpCount >= kMaxDumps) return;
    char dir[MAX_PATH], path[MAX_PATH];
    GetDataDir(dir);
    snprintf(path, sizeof(path), "%sevent-logger-status-%02d.txt", dir, g_dumpCount);
    FILE* f = fopen(path, "w");
    if (!f) return;
    g_dumpCount++;
    fprintf(f, "Event status list: %d entries, current stage %d\n", n, s.stage);
    fprintf(f, "idx key      bytes            discovered state          what\n");
    char buf[160];
    for (int i = 0; i < n; i++) {
        const unsigned char* e = StatusEntry(s, i);
        uint32_t key = ReadAt<uint32_t>(e, ev::status::kKey);
        fprintf(f, "%3d %08X %02X %02X %02X %02X      %d          %d %-13s %s\n", i, key, e[4], e[5], e[6], e[7],
                e[ev::status::kDiscovered], e[ev::status::kState], StateName(e[ev::status::kState]),
                DescribeKey(key, buf, sizeof(buf)));
    }
    fclose(f);
    Log("Full status list written to %s", path);
}

void LogStatusChanges(const Snapshot& prev, const Snapshot& cur) {
    char buf[160];
    int n = ValidStatusCount(cur);
    for (int i = 0; i < n; i++) {
        const unsigned char* e = StatusEntry(cur, i);
        uint32_t key = ReadAt<uint32_t>(e, ev::status::kKey);
        if (i >= ValidStatusCount(prev)) {
            Log("Status: NEW entry %d: %s  discovered=%d state=%d (%s)  bytes %02X %02X %02X %02X", i,
                DescribeKey(key, buf, sizeof(buf)), e[ev::status::kDiscovered], e[ev::status::kState],
                StateName(e[ev::status::kState]), e[4], e[5], e[6], e[7]);
            continue;
        }
        const unsigned char* p = StatusEntry(prev, i);
        if (memcmp(e, p, ev::kStatusEntrySize) != 0) {
            Log("Status: entry %d: %s  discovered %d->%d  state %d->%d (%s)  bytes %02X %02X %02X %02X -> %02X %02X %02X %02X",
                i, DescribeKey(key, buf, sizeof(buf)), p[ev::status::kDiscovered], e[ev::status::kDiscovered],
                p[ev::status::kState], e[ev::status::kState], StateName(e[ev::status::kState]), p[4], p[5], p[6], p[7],
                e[4], e[5], e[6], e[7]);
        }
    }
}

// A profile load (or new career) replaces the whole list instead of changing one entry.
bool LooksLikeReload(const Snapshot& prev, const Snapshot& cur) {
    int a = ValidStatusCount(prev), b = ValidStatusCount(cur);
    if (b < a || b - a > 4) return true;
    for (int i = 0; i < a && i < 4; i++)
        if (ReadAt<uint32_t>(StatusEntry(prev, i), 0) != ReadAt<uint32_t>(StatusEntry(cur, i), 0)) return true;
    return false;
}

bool CheckGameVersion() {
    if (!ExeMd5Ok()) return false;
    if (!CodeMatches(kCodeChecks, sizeof(kCodeChecks) / sizeof(kCodeChecks[0]))) return false;
    Log("Code checks OK.");
    const struct {
        uintptr_t   addr;
        size_t      len;
        const char* what;
    } kRanges[] = {
        {ev::kRaceTablePtr, 0x50, "GCareer table pointers"},
        {ev::kActiveEvents, ev::kMaxActive * 4, "active event list"},
        {ev::kStageRecords, ev::kNumStageRecords * ev::kStageRecordSize, "stage records"},
        {ev::kStatusList, ev::kMaxStatus * ev::kStatusEntrySize, "event status list"},
        {ev::kEnteredRace, 4, "entered race"},
        {kCash, 4, "cash"},
    };
    for (const auto& r : kRanges) {
        if (!IsReadable(r.addr, r.len)) {
            Log("STOP: %s at 0x%08X (+0x%X) is not readable.", r.what, static_cast<unsigned>(r.addr),
                static_cast<unsigned>(r.len));
            return false;
        }
    }
    return true;
}

DWORD WINAPI WorkerThread(LPVOID) {
    Log("==== Event Logger started (debug, read-only) ====");
    if (!CheckGameVersion()) {
        Log("Doing nothing.");
        return 0;
    }

    Log("Waiting for the game to load the career tables...");
    while (!TablesReady(g_t)) Sleep(kPollInterval);
    Log("Career tables: %d races at %08X, %d showcases, %d stages, strings %d bytes.", g_t.raceCount,
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(g_t.races)), g_t.showcaseCount, g_t.stageCount,
        g_t.stringsSize);
    LogStageTable();
    WriteRaceFile();

    static Snapshot prev, cur, check;
    TakeSnapshot(prev);
    bool finished = CareerFinished(prev);
    Log("Initial: stage=%d cash=%d career finished=%s", prev.stage, prev.cash, finished ? "yes" : "no");
    LogStages(prev);
    LogStatusList(prev);
    LogActiveList(prev);

    for (;;) {
        Sleep(kPollInterval);
        TakeSnapshot(cur);

        // The game may be halfway through rebuilding the map list; only trust it if it holds still.
        if (memcmp(cur.active, prev.active, sizeof(cur.active)) != 0) {
            Sleep(50);
            TakeSnapshot(check);
            if (memcmp(cur.active, check.active, sizeof(cur.active)) != 0) continue;
        }

        if (cur.stage != prev.stage) Log("Current stage: %d -> %d", prev.stage, cur.stage);
        if (cur.cash != prev.cash) Log("Cash: %d -> %d (%+d)", prev.cash, cur.cash, cur.cash - prev.cash);
        if (memcmp(cur.stageComplete, prev.stageComplete, sizeof(cur.stageComplete)) != 0 ||
            memcmp(cur.stageData, prev.stageData, sizeof(cur.stageData)) != 0) {
            Log("Stage records changed:");
            LogStages(cur);
        }

        if (cur.enteredRace != prev.enteredRace) {
            char buf[160];
            const unsigned char* r = AsRace(cur.enteredRace);
            if (cur.enteredRace == 0) Log("Marker: left the race marker.");
            else Log("Marker: drove into %s", r ? DescribeRace(r, buf, sizeof(buf)) : "(pointer not in race table)");
        }

        bool statusChanged = cur.statusCount != prev.statusCount || memcmp(cur.status, prev.status, sizeof(cur.status)) != 0;
        if (statusChanged) {
            if (LooksLikeReload(prev, cur)) {
                Log("Status list replaced (profile load or new career):");
                LogStatusList(cur);
            } else {
                LogStatusChanges(prev, cur);
            }
        }

        bool nowFinished = CareerFinished(cur);
        if (nowFinished != finished) {
            Log("Career finished: %s -> %s", finished ? "yes" : "no", nowFinished ? "yes" : "no");
            finished = nowFinished;
        }

        if (memcmp(cur.active, prev.active, sizeof(cur.active)) != 0) {
            LogActiveChanges(prev, cur);
            LogActiveList(cur);
        }

        memcpy(&prev, &cur, sizeof(prev));
    }
}

}  // namespace

void bf::StartEventLogger() {  // the work runs on its own thread (it starts once DllMain has returned)
    HANDLE thread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
}
