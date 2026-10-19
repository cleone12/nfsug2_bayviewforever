// GarageLogger (debug, [Debug] GarageLogger = 1): read-only. Once a second logs changes to the 5 career garage slots,
// cash and the current car, and saves a raw copy of each changed slot to BayviewForever\garage-logger-dumps\.

#define BF_TAG "GarageLogger"
#include "nfsu2.h"

using namespace nfsu2;

namespace {

const CodeCheck kCodeChecks[] = {
    {0x005164ED, {0x83, 0xFF, 0x05}, 3, "CMP EDI,5 in CarDB_InitCareerSlots"},
    {0x0053292B, {0xB9, 0x90, 0xAD, 0x83, 0x00}, 5, "MOV ECX,0x83AD90 in maybe_Career_Reset"},
    {0x005329CC, {0xB9, 0xD8, 0x0E, 0x84, 0x00}, 5, "MOV ECX,0x840ED8 in maybe_Career_Reset"},
};

// +0x000 is the vtable (a code address); it is skipped when looking for changes.
const size_t kCompareStart = 0x004;
const size_t kCompareEnd   = 0x7F6;

const int   kMaxDumps     = 1000;
const DWORD kPollInterval = 1000;  // ms

char g_dumpDir[MAX_PATH];
int  g_dumpCount = 0;

bool CheckGameVersion() {
    if (!ExeMd5Ok()) return false;
    if (!CodeMatches(kCodeChecks, sizeof(kCodeChecks) / sizeof(kCodeChecks[0]))) return false;
    Log("Code checks OK.");
    if (!IsReadable(kCareerSlots, kSlotSize * kMaxSlots) || !IsReadable(kCareerSlotCount, 4) ||
        !IsReadable(kCash, 4) || !IsReadable(kCurrentSlotHash, 4)) {
        Log("STOP: garage memory is not readable.");
        return false;
    }
    return true;
}

struct Snapshot {
    int32_t       cash;
    uint32_t      currentSlotHash;
    int32_t       slotCount;
    unsigned char slots[kMaxSlots][kSlotSize];
};

void TakeSnapshot(Snapshot& s) {
    s.cash            = *reinterpret_cast<volatile int32_t*>(kCash);
    s.currentSlotHash = *reinterpret_cast<volatile uint32_t*>(kCurrentSlotHash);
    s.slotCount       = *reinterpret_cast<volatile int32_t*>(kCareerSlotCount);
    memcpy(s.slots, reinterpret_cast<const void*>(kCareerSlots), sizeof(s.slots));
}

int CurrentSlotIndex(const Snapshot& s) {
    for (int i = 0; i < kMaxSlots; i++)
        if (s.currentSlotHash != 0 && ReadAt<uint32_t>(s.slots[i], slot::kHash) == s.currentSlotHash) return i;
    return -1;
}

// Writes the whole slot (including the vtable) to garage-logger-dumps\NNNN_slotN.bin.
// Returns the file name, or "" if no dump was written.
const char* DumpSlot(int n, const unsigned char* s) {
    static char name[64];
    if (g_dumpCount >= kMaxDumps) return "";
    snprintf(name, sizeof(name), "%04d_slot%d.bin", g_dumpCount, n);
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s%s", g_dumpDir, name);
    FILE* f = fopen(path, "wb");
    if (!f) return "";
    fwrite(s, 1, kSlotSize, f);
    fclose(f);
    g_dumpCount++;
    return name;
}

DWORD WINAPI WorkerThread(LPVOID) {
    Log("==== Garage Logger started (debug, read-only) ====");
    if (!CheckGameVersion()) {
        Log("Doing nothing.");
        return 0;
    }

    char dir[MAX_PATH];
    GetDataDir(dir);
    snprintf(g_dumpDir, sizeof(g_dumpDir), "%sgarage-logger-dumps\\", dir);
    CreateDirectoryA(g_dumpDir, nullptr);
    Log("Watching the garage. Slot dumps go to %s", g_dumpDir);

    static Snapshot prev, cur;
    TakeSnapshot(prev);
    Log("Initial: cash=%d current slot=%d slot count=%d", prev.cash, CurrentSlotIndex(prev), prev.slotCount);
    for (int i = 0; i < kMaxSlots; i++) LogSlot(i, prev.slots[i]);

    for (;;) {
        Sleep(kPollInterval);
        TakeSnapshot(cur);

        if (cur.cash != prev.cash) Log("Cash: %d -> %d", prev.cash, cur.cash);
        if (cur.currentSlotHash != prev.currentSlotHash)
            Log("Current car: slot %d -> slot %d (hash %08X)", CurrentSlotIndex(prev), CurrentSlotIndex(cur),
                cur.currentSlotHash);
        if (cur.slotCount != prev.slotCount) Log("Slot count: %d -> %d", prev.slotCount, cur.slotCount);

        for (int i = 0; i < kMaxSlots; i++) {
            if (memcmp(cur.slots[i] + kCompareStart, prev.slots[i] + kCompareStart, kCompareEnd - kCompareStart) != 0) {
                Log("Slot %d changed:", i);
                const char* dump = DumpSlot(i, cur.slots[i]);
                char suffix[96] = "";
                if (dump[0]) snprintf(suffix, sizeof(suffix), "  dump: %s", dump);
                LogSlot(i, cur.slots[i], suffix);
            }
        }
        memcpy(&prev, &cur, sizeof(prev));
    }
}

}  // namespace

void bf::StartGarageLogger() {  // the work runs on its own thread (it starts once DllMain has returned)
    HANDLE thread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
}
