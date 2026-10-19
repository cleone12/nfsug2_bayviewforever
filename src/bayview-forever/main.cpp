// BayviewForever: Need for Speed: Underground 2 (PC, v1.2) mod, all features in one .asi.
//
// Features (each in its own folder under src/, switched on/off in scripts\BayviewForever.ini [Features]):
//   VirtualGarage   src/virtual-garage/     stored cars beyond the 5 garage slots, Rachel's cars
//   EndlessCareer   src/endless-career/     post-game races that keep coming back
//   EventLogger     src/event-logger/       debug, off by default: read-only career event / status / map list log
//   GarageLogger    src/garage-logger/      debug, off by default: read-only garage slot log + slot dumps
//   Covers          src/covers/             post-game DVD / magazine covers earned per car, cover archive
//   DragSteering    src/drag-steering/      normal steering and force feedback in drag races
//   Wheel           src/wheel-enhancements/ steering wheel force feedback and steering response
//   HShifter        src/h-shifter/          H-shifter + clutch pedal (the game's hidden clutch mode)
//   Surround        src/surround/           the cut 5.1 / quad sound output
//   ParticleFix     src/particle-fix/       particle stutter fix + timing log
//   RaceAI          src/race-ai/            AI blocking / PIT tamer, Catch Up off switch
//   RespawnAtGarage src/respawn-at-garage/  optional: profile loads and Quit to Main Menu restart at the garage
//   UniqueParts     src/unique-parts/       post-game unique performance + visual parts earned per car by outruns
//
// This file: the log (<game>\BayviewForever\BayviewForever.log, fresh each run, previous run kept; lines tagged by
// feature, written by a log thread), moving files older versions left in the game folder into BayviewForever\, the
// refusal to run next to the old separate NFSU2<Name>.asi plugins or a second copy, the [Features] switches, and the
// one-time exe MD5 check. Undo: remove BayviewForever.asi (everything is in memory).

#define BF_TAG "Main"
#include "nfsu2.h"

#include <string>
#include <vector>

namespace {

HMODULE g_module = nullptr;
char g_iniPath[MAX_PATH] = "";
bool g_iniFound = false;
bool g_on[bf::kNumFeatures] = {};

CRITICAL_SECTION g_logLock;
FILE* g_file = nullptr;
long g_logBytes = 0;
const long kMaxLogBytes = 50L * 1024 * 1024;  // a runaway log stops here instead of filling the disk

CRITICAL_SECTION g_md5Lock;
int g_md5State = 0;  // 0 not checked yet, 1 v1.2, 2 not v1.2

struct FeatureInfo {
    const char* key;  // [Features] key and the prefix of its ini sections
    void (*start)();
    int defaultOn;    // used when the [Features] key is missing (optional gameplay changes default to off)
};
const FeatureInfo kFeatures[bf::kNumFeatures] = {
    {"Covers", bf::StartCovers, 1},
    {"DragSteering", bf::StartDragSteering, 1},
    {"EndlessCareer", bf::StartEndlessCareer, 1},
    {"EventLogger", bf::StartEventLogger, 0},
    {"GarageLogger", bf::StartGarageLogger, 0},
    {"HShifter", bf::StartHShifter, 1},
    {"ParticleFix", bf::StartParticleFix, 1},
    {"RaceAI", bf::StartRaceAI, 1},
    {"RespawnAtGarage", bf::StartRespawnAtGarage, 0},
    {"Surround", bf::StartSurround, 1},
    {"UniqueParts", bf::StartUniqueParts, 1},
    {"VirtualGarage", bf::StartVirtualGarage, 1},
    {"Wheel", bf::StartWheel, 1},
};

// The separate plugins BayviewForever replaces (any version: NFSU2Covers-v0.3.1.asi, NFSU2Covers.asi, ...).
const char* const kOldPlugins[] = {
    "NFSU2Covers",       "NFSU2DragSteering", "NFSU2EndlessCareer",       "NFSU2HShifter",
    "NFSU2ParticleFix",  "NFSU2RaceAI",       "NFSU2Surround",            "NFSU2VirtualGarage",
    "NFSU2WheelEnhancements",
};
// The old read-only mapping tools: now the EventLogger / GarageLogger debug features. They patch nothing, so an old copy
// only gets a note (it would log twice), not a STOP.
const char* const kOldTools[] = {"NFSU2EventLogger", "NFSU2GarageLogger"};

// Log lines waiting for the log thread (only it, or startup / exit code, writes the file).
const int kPendingSize = 512 * 1024;
char g_pending[kPendingSize];
int  g_pendingLen = 0, g_pendingDropped = 0;
HANDLE g_logThread = nullptr;
CRITICAL_SECTION g_writeLock;  // one writer at a time

// atExit: the process is ending and other threads are gone (maybe while holding a lock), so never wait for one.
void FlushLog(bool atExit = false) {
    static char out[kPendingSize];
    if (atExit) {
        if (!TryEnterCriticalSection(&g_writeLock)) return;
        if (!TryEnterCriticalSection(&g_logLock)) {
            LeaveCriticalSection(&g_writeLock);
            return;
        }
    } else {
        EnterCriticalSection(&g_writeLock);
        EnterCriticalSection(&g_logLock);
    }
    const int len = g_pendingLen, dropped = g_pendingDropped;
    memcpy(out, g_pending, len);
    g_pendingLen = 0;
    g_pendingDropped = 0;
    const bool full = g_logBytes >= kMaxLogBytes;
    static bool fullNoted = false;
    LeaveCriticalSection(&g_logLock);
    if (g_file && (len || dropped)) {
        fwrite(out, 1, len, g_file);
        if (dropped) fprintf(g_file, "(%d log line(s) dropped: more than 512 KB in 0.1 s)\n", dropped);
        if (full && !fullNoted) {
            fprintf(g_file, "(log size limit reached: nothing more is logged this run)\n");
            fullNoted = true;
        }
        fflush(g_file);
    }
    LeaveCriticalSection(&g_writeLock);
}

DWORD WINAPI LogThread(LPVOID) {
    for (;;) {
        Sleep(100);
        FlushLog();
    }
}

void OpenLogFile() {
    char game[MAX_PATH], dir[MAX_PATH], path[MAX_PATH], previous[MAX_PATH], old[MAX_PATH];
    nfsu2::GetGameDir(game);
    nfsu2::GetDataDir(dir);
    snprintf(path, sizeof(path), "%sBayviewForever.log", dir);
    snprintf(previous, sizeof(previous), "%sBayviewForever.previous.log", dir);
    // Older versions kept the log in the game folder: the last one there becomes the previous log.
    snprintf(old, sizeof(old), "%sBayviewForever.previous.log", game);
    DeleteFileA(old);
    snprintf(old, sizeof(old), "%sBayviewForever.log", game);
    if (GetFileAttributesA(old) != INVALID_FILE_ATTRIBUTES && GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES)
        MoveFileExA(old, previous, MOVEFILE_REPLACE_EXISTING);
    else
        MoveFileExA(path, previous, MOVEFILE_REPLACE_EXISTING);  // keep the last run
    g_file = fopen(path, "w");
}

bool Exists(const char* path) { return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES; }
bool IsDir(const char* path) {
    const DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

// If cover-archive couldn't be moved, Covers starts a fresh covers-state.txt in BayviewForever\. Merges the two: the
// old file keeps its history, the new file's "wins" counts are added; the result replaces the new file.
bool MergeCoversState(const char* oldPath, const char* newPath) {
    std::vector<std::string> lines;
    char buf[512];
    FILE* f = fopen(oldPath, "r");
    if (!f) return false;
    while (fgets(buf, sizeof(buf), f)) lines.push_back(buf);
    fclose(f);
    int added = 0;
    if ((f = fopen(newPath, "r"))) {
        while (fgets(buf, sizeof(buf), f)) {
            unsigned hash;
            int wins;
            if (sscanf(buf, "wins %x %d", &hash, &wins) != 2) continue;
            bool found = false;
            for (std::string& l : lines) {
                unsigned h;
                int w;
                if (sscanf(l.c_str(), "wins %x %d", &h, &w) == 2 && h == hash) {
                    const char* comment = strchr(l.c_str(), ';');
                    char merged[256];
                    snprintf(merged, sizeof(merged), "wins %08X %d %s", h, w + wins, comment ? comment : "\n");
                    l = merged;
                    found = true;
                }
            }
            if (!found) lines.push_back(buf);
            added += wins;
        }
        fclose(f);
    }
    if (!(f = fopen(newPath, "w"))) return false;
    for (const std::string& l : lines) fputs(l.c_str(), f);
    fclose(f);
    DeleteFileA(oldPath);
    nfsu2::Log("  merged %s into the new covers-state.txt: old history kept, %d win(s) counted since added.", oldPath,
               added);
    return true;
}

// Moves the contents of folder `from` into folder `to` (which exists), file by file; sub-folders that don't exist in
// `to` are moved whole. Files that exist in both stay where they are (logged), except covers-state.txt (merged).
// Empty folders left behind are removed. Returns the number of entries that could not be moved.
int MergeFolder(const char* from, const char* to, int& moved) {
    char pattern[MAX_PATH], src[MAX_PATH], dst[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", from);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int left = 0;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        snprintf(src, sizeof(src), "%s\\%s", from, fd.cFileName);
        snprintf(dst, sizeof(dst), "%s\\%s", to, fd.cFileName);
        const bool dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (!Exists(dst)) {
            if (MoveFileA(src, dst)) {
                moved++;
            } else if (dir && CreateDirectoryA(dst, nullptr)) {
                left += MergeFolder(src, dst, moved);
            } else {
                nfsu2::Log("  could not move %s (error %lu)", src, GetLastError());
                left++;
            }
        } else if (dir && IsDir(dst)) {
            left += MergeFolder(src, dst, moved);
        } else if (!_stricmp(fd.cFileName, "covers-state.txt") && MergeCoversState(src, dst)) {
            moved++;
        } else {
            nfsu2::Log("  kept %s where it is: %s exists too", src, dst);
            left++;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    RemoveDirectoryA(from);  // only works once it's empty
    return left;
}

// Older versions wrote into the game folder. Moves those files into BayviewForever\ (a rename on the same drive). A
// folder that can't be renamed whole, or exists in both places, is merged file by file, retried every start.
void MoveOldFiles() {
    static const struct { const char* from; const char* to; } kMoves[] = {
        {"virtual-garage", "virtual-garage"},
        {"endless-career", "endless-career"},
        {"cover-archive", "cover-archive"},
        {"BayviewForever-garage.txt", "garage-spot.txt"},
        {"BayviewForever-DUPLICATE.log", "BayviewForever-DUPLICATE.log"},
        {"garage-logger-dumps", "garage-logger-dumps"},
        {"event-logger-races.txt", "event-logger-races.txt"},
    };
    char game[MAX_PATH], dir[MAX_PATH], from[MAX_PATH], to[MAX_PATH];
    nfsu2::GetGameDir(game);
    nfsu2::GetDataDir(dir);
    for (const auto& m : kMoves) {
        snprintf(from, sizeof(from), "%s%s", game, m.from);
        snprintf(to, sizeof(to), "%s%s", dir, m.to);
        if (!Exists(from)) continue;
        if (!Exists(to) && MoveFileA(from, to)) {
            nfsu2::Log("Moved %s from the game folder to BayviewForever\\%s.", m.from, m.to);
        } else if (IsDir(from) && (IsDir(to) || CreateDirectoryA(to, nullptr))) {
            int moved = 0;
            const int left = MergeFolder(from, to, moved);
            nfsu2::Log("Merged the old %s into BayviewForever\\%s: %d moved, %d left in the game folder%s.", m.from,
                       m.to, moved, left, left ? " (tried again next start)" : "");
        } else {
            nfsu2::Log("Could not move %s into BayviewForever\\ (error %lu); the mod uses the one in BayviewForever\\.",
                       m.from, GetLastError());
        }
    }
    snprintf(from, sizeof(from), "%sevent-logger-status-*.txt", game);  // debug dumps (rewritten every run)
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(from, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            snprintf(from, sizeof(from), "%s%s", game, fd.cFileName);
            snprintf(to, sizeof(to), "%s%s", dir, fd.cFileName);
            MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
}

// Folders the ASI loader loads plugins from: the game folder, scripts\, plugins\ and our own folder.
int PluginFolders(char out[4][MAX_PATH]) {
    char dir[MAX_PATH];
    nfsu2::GetGameDir(dir);
    int n = 0;
    snprintf(out[n++], MAX_PATH, "%s", dir);
    snprintf(out[n++], MAX_PATH, "%sscripts\\", dir);
    snprintf(out[n++], MAX_PATH, "%splugins\\", dir);
    char self[MAX_PATH];
    if (GetModuleFileNameA(g_module, self, MAX_PATH)) {
        char* slash = strrchr(self, '\\');
        if (slash) slash[1] = '\0';
        bool known = false;
        for (int i = 0; i < n; i++) known |= _stricmp(out[i], self) == 0;
        if (!known) snprintf(out[n++], MAX_PATH, "%s", self);
    }
    return n;
}

// Logs every <name>*.asi in the plugin folders; returns how many were found.
int FindAsi(const char* const* names, size_t count, const char* what) {
    char folders[4][MAX_PATH];
    const int nf = PluginFolders(folders);
    int found = 0;
    for (int f = 0; f < nf; f++) {
        for (size_t i = 0; i < count; i++) {
            char pattern[MAX_PATH];
            snprintf(pattern, sizeof(pattern), "%s%s*.asi", folders[f], names[i]);
            WIN32_FIND_DATAA fd;
            HANDLE h = FindFirstFileA(pattern, &fd);
            if (h == INVALID_HANDLE_VALUE) continue;
            do {
                nfsu2::Log("%s %s%s", what, folders[f], fd.cFileName);
                found++;
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
    return found;
}

// A second BayviewForever (a renamed copy) must not touch the log the first one writes.
bool AnotherCopyLoaded() {
    char name[64];
    snprintf(name, sizeof(name), "Local\\BayviewForever-%lu", GetCurrentProcessId());
    CreateMutexA(nullptr, FALSE, name);  // the handle stays open for the game's lifetime
    if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
    char dir[MAX_PATH], path[MAX_PATH], self[MAX_PATH] = "?";
    nfsu2::GetDataDir(dir);
    snprintf(path, sizeof(path), "%sBayviewForever-DUPLICATE.log", dir);
    GetModuleFileNameA(g_module, self, MAX_PATH);
    if (FILE* f = fopen(path, "a")) {
        SYSTEMTIME t;
        GetLocalTime(&t);
        fprintf(f, "[%02d:%02d:%02d] %s is a second copy of BayviewForever; it does nothing. Keep only one.\n",
                t.wHour, t.wMinute, t.wSecond, self);
        fclose(f);
    }
    return true;
}

void ReadFeatureSwitches() {
    char line[512];
    int len = 0;
    for (int i = 0; i < bf::kNumFeatures; i++) {
        int def = kFeatures[i].defaultOn;
        if (i == bf::kRespawnAtGarage)  // its name until 0.2.6
            def = static_cast<int>(GetPrivateProfileIntA("Features", "ImmersiveTravel", def, g_iniPath));
        g_on[i] = GetPrivateProfileIntA("Features", kFeatures[i].key, def, g_iniPath) != 0;
        // The debug tools' switches live in [Debug] ([Features] still works).
        if (i == bf::kEventLogger || i == bf::kGarageLogger)
            g_on[i] = GetPrivateProfileIntA("Debug", kFeatures[i].key, g_on[i] ? 1 : 0, g_iniPath) != 0;
        len += snprintf(line + len, sizeof(line) - len, "%s%s %s", i ? ", " : "", kFeatures[i].key,
                        g_on[i] ? "on" : "OFF");
    }
    nfsu2::Log("Features: %s.", line);
}

void Start() {
    InitializeCriticalSection(&g_logLock);
    InitializeCriticalSection(&g_writeLock);
    InitializeCriticalSection(&g_md5Lock);
    OpenLogFile();
    nfsu2::Log("==== BayviewForever v%s started ====", BF_VERSION);
    MoveOldFiles();
    char self[MAX_PATH] = "?";
    GetModuleFileNameA(g_module, self, MAX_PATH);
    nfsu2::Log("Loaded from %s", self);
    snprintf(g_iniPath, sizeof(g_iniPath), "%s", self);
    char* slash = strrchr(g_iniPath, '\\');
    snprintf(slash ? slash + 1 : g_iniPath, MAX_PATH - (slash ? slash + 1 - g_iniPath : 0), "BayviewForever.ini");
    g_iniFound = GetFileAttributesA(g_iniPath) != INVALID_FILE_ATTRIBUTES;
    nfsu2::Log("Settings: %s%s", g_iniPath, g_iniFound ? "" : " (NOT FOUND: every feature uses its defaults)");
    if (FILE* f = g_iniFound ? fopen(g_iniPath, "r") : nullptr) {  // the first line labels the ini
        char first[160] = "";
        if (fgets(first, sizeof(first), f)) first[strcspn(first, "\r\n")] = '\0';
        fclose(f);
        nfsu2::Log("Settings file's first line: %s", first);
    }

    if (FindAsi(kOldPlugins, sizeof(kOldPlugins) / sizeof(kOldPlugins[0]), "STOP: old separate plugin found:")) {
        nfsu2::Log("STOP: BayviewForever replaces those plugins, and both would patch the same game code. Delete them "
                   "(every NFSU2<Name>.asi listed above, and its .ini) and start the game again. BayviewForever does "
                   "nothing this run.");
        return;
    }
    FindAsi(kOldTools, sizeof(kOldTools) / sizeof(kOldTools[0]),
            "Note: old mapping tool found (now built in: [Features] EventLogger / GarageLogger; delete the old .asi):");

    ReadFeatureSwitches();
    for (int i = 0; i < bf::kNumFeatures; i++)
        if (g_on[i]) kFeatures[i].start();
}

}  // namespace

// ---- Services for the features (bayview_forever.h) ----------------------------------

HMODULE bf::Module() { return g_module; }

bool bf::FeatureOn(Feature f) { return f >= 0 && f < kNumFeatures && g_on[f]; }

void bf::IniPath(char out[MAX_PATH]) { snprintf(out, MAX_PATH, "%s", g_iniPath); }

bool bf::IniFound() { return g_iniFound; }

// Lines are collected in memory and written by the log thread every 100 ms, so the game thread never waits for the
// disk (writing directly caused hitches). A crash loses at most ~0.1 s of log.
void bf::LogV(const char* tag, const char* fmt, va_list args) {
    if (!g_file) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    char line[1200];
    int n = snprintf(line, sizeof(line), "[%02d:%02d:%02d] [%s] ", t.wHour, t.wMinute, t.wSecond, tag);
    int m = vsnprintf(line + n, sizeof(line) - n - 1, fmt, args);
    n += m < 0 ? 0 : (m < static_cast<int>(sizeof(line)) - n - 1 ? m : static_cast<int>(sizeof(line)) - n - 2);
    line[n++] = '\n';
    EnterCriticalSection(&g_logLock);  // features log from the game thread and their own threads
    if (g_logBytes < kMaxLogBytes) {
        g_logBytes += n;
        if (g_pendingLen + n <= kPendingSize) {
            memcpy(g_pending + g_pendingLen, line, n);
            g_pendingLen += n;
        } else {
            g_pendingDropped++;
        }
    }
    LeaveCriticalSection(&g_logLock);
    if (!g_logThread) FlushLog();  // before the log thread runs (startup): write at once
}

bool bf::ExeMd5Ok() {
    EnterCriticalSection(&g_md5Lock);
    if (g_md5State == 0) g_md5State = nfsu2::ExeMd5Check() ? 1 : 2;
    const bool ok = g_md5State == 1;
    LeaveCriticalSection(&g_md5Lock);
    return ok;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
        if (AnotherCopyLoaded()) return TRUE;
        Start();  // the features patch their CALLs and vtable entries here, before the game's own code runs
        g_logThread = CreateThread(nullptr, 0, LogThread, nullptr, 0, nullptr);  // runs once DllMain has returned
    } else if (reason == DLL_PROCESS_DETACH && g_file) {
        FlushLog(true);  // the last lines (the log thread is gone at process exit)
    }
    return TRUE;
}
