// Drag Steering: normal steering and normal force feedback in drag races.
//
// Steering: the player update (0x00602650) calls Player_SwitchToDragController (CALL 0x00602A98) every drag frame,
// which swaps in the lane-change "drag controller". The CALL goes to an empty hook, so the player keeps the normal
// controller. Opponents are not affected.
// Force feedback: the FF update (0x00406460) stops every effect except a centering spring in drags; the JNE at
// 0x004064D8 is NOPed.
// Settings: [DragSteering]. Undo: [Features] DragSteering = 0 (in memory only).

#define BF_TAG "DragSteering"
#include "nfsu2.h"

using namespace nfsu2;

namespace {

const DWORD kPollInterval = 250;  // ms between state checks for the log

bool g_dragSteering = true;  // [DragSteering] NormalSteering
bool g_dragFfb      = true;  // [DragSteering] ForceFeedback


const CodeCheck kCodeChecks[] = {
    {0x00602A5D, {0xBF, 0x03, 0x00, 0x00, 0x00}, 5, "MOV EDI,3 (PLAYER_MODE_DRAG_RACING) in the player update"},
    {0x00602A79, {0x83, 0x78, 0x24, 0x02, 0x8B, 0x86, 0x2C, 0x02}, 8, "controller type == 2 / read PLAYER_MODE"},
    {0x00602A83, {0x75, 0x0D, 0x3B, 0xC7}, 4, "JNE / CMP EAX,EDI (switch back to normal)"},
    {0x00602A92, {0x3B, 0xC7, 0x75, 0x07, 0x8B, 0xCE, 0xE8, 0x73}, 8, "CMP EAX,EDI; JNE; CALL Player_SwitchToDragController"},
    {0x00602A9D, {0x38, 0x1D, 0xD8, 0xE7, 0x89, 0x00}, 6, "CMP [is drag race 0x0089E7D8],BL"},
    {0x0060A240, {0x8B, 0x0C, 0xB5, 0xB4, 0x00, 0x89, 0x00, 0xE8}, 8, "player list 0x008900B4 -> player update"},
    {0x0060A24C, {0xA1, 0xBC, 0x00, 0x89, 0x00}, 5, "player count 0x008900BC"},
    {0x004064BF, {0xE8, 0x5C, 0x4F, 0x1E, 0x00, 0x85, 0xC0, 0x0F}, 8, "force feedback: CALL 0x005EB420 (player option)"},
    {0x004064CC, {0xB9, 0xA0, 0xE7, 0x89, 0x00, 0xE8, 0x1A, 0xB4}, 8, "force feedback: CALL RaceOptions_IsDragRace"},
    {0x004064D6, {0x84, 0xC0, 0x0F, 0x85, 0x4C, 0x04, 0x00, 0x00}, 8, "force feedback: TEST AL,AL; JNE 0x0040692A"},
    {0x004018F0, {0x8A, 0x41, 0x38, 0x84, 0xC0, 0x75, 0x0C, 0xA1}, 8, "RaceOptions_IsDragRace reads [ECX+0x38]"},
    // Player_SwitchToDragController: thiscall(player), checks car+0x24, plain RET
    {0x005F3010, {0x57, 0x8B, 0xF9, 0x8B, 0x4F, 0x04, 0x85, 0xC9}, 8, "prologue of Player_SwitchToDragController"},
    {0x005F3076, {0xC3}, 1, "plain RET of Player_SwitchToDragController"},
};

const char* ModeName(int mode) {
    static const char* const kNames[] = {"NONE", "PAUSEMENU", "CAR_RACING", "DRAG_RACING", "AUTO_PILOT",
                                         "TURRET_SELECTOR", "FROZEN_ZONE", "AI_DEMO", "PRERACE", "FINISH_RACE"};
    return (mode >= 0 && mode < 10) ? kNames[mode] : "?";
}

const char* ControllerName(int type) {
    switch (type) {
        case 0: return "idle";
        case 1: return "normal";
        case 2: return "DRAG (lanes)";
        case 3: case 4: return "AI";
        case 5: return "other";
        default: return "?";
    }
}

// Writes `len` bytes of code at addr. True if they read back correctly.
bool PatchCode(uintptr_t addr, const unsigned char* bytes, size_t len) {
    unsigned char* p = reinterpret_cast<unsigned char*>(addr);
    DWORD oldProtect;
    if (!VirtualProtect(p, len, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
    memcpy(p, bytes, len);
    VirtualProtect(p, len, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), p, len);
    return memcmp(p, bytes, len) == 0;
}

const unsigned char kNop6[] = {0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00};  // one 6-byte NOP instruction

// ---- Settings ---------------------------------------------------------------

void LoadSettings() {
    char path[MAX_PATH];
    bf::IniPath(path);
    g_dragSteering = GetPrivateProfileIntA("DragSteering", "NormalSteering", 1, path) != 0;
    g_dragFfb = GetPrivateProfileIntA("DragSteering", "ForceFeedback", 1, path) != 0;
    Log("Settings%s: NormalSteering=%d ForceFeedback=%d", bf::IniFound() ? "" : " (defaults)", g_dragSteering,
        g_dragFfb);
}

// Replaces the CALL to Player_SwitchToDragController (thiscall(player), plain RET): runs every frame of a drag race
// while the player isn't on the drag controller. It never switches, so the player keeps the normal controller.
void __fastcall Hook_SwitchToDrag(void* /*player*/) {}


// ---- State log ----------------------------------------------------------------

struct State {
    int count;
    int dragRace;
    int mode;
    int controller;
    bool operator!=(const State& o) const {
        return count != o.count || dragRace != o.dragRace || mode != o.mode || controller != o.controller;
    }
};

// Player 1's state, or -1 for anything that can't be read right now.
State ReadState() {
    State s = {-1, -1, -1, -1};
    if (IsReadable(drag::kIsDragRace, 1)) s.dragRace = *reinterpret_cast<const unsigned char*>(drag::kIsDragRace);
    if (!IsReadable(player::kCount, 4)) return s;
    s.count = *reinterpret_cast<const int*>(player::kCount);
    if (s.count <= 0 || s.count > 2 || !IsReadable(player::kList, 4)) return s;
    const unsigned char* p1 = *reinterpret_cast<unsigned char* const*>(player::kList);
    if (!p1 || !IsReadable(reinterpret_cast<uintptr_t>(p1), player::kMode + 4)) return s;
    s.mode = ReadAt<int>(p1, player::kMode);
    const unsigned char* car = ReadAt<const unsigned char*>(p1, player::kCar);
    if (car && IsReadable(reinterpret_cast<uintptr_t>(car), player::kController + 4))
        s.controller = ReadAt<int>(car, player::kController);
    return s;
}

void MonitorLoop() {
    State last = {-2, -2, -2, -2};
    for (;;) {
        State s = ReadState();
        if (s != last) {
            const char* note = "";
            if (s.mode == 3 && s.controller == 1) note = "  <- drag race on the normal controller (patch working)";
            if (s.controller == 2) note = "  <- DRAG controller (lane steering)";
            Log("players=%d dragRace=%d player1 mode=%d (%s) controller=%d (%s)%s", s.count, s.dragRace, s.mode,
                ModeName(s.mode), s.controller, ControllerName(s.controller), note);
            last = s;
        }
        Sleep(kPollInterval);
    }
}

DWORD WINAPI WorkerThread(LPVOID) {
    if (!ExeMd5Ok()) {
        Log("Not patching anything.");
        return 0;
    }
    if (!CodeMatches(kCodeChecks, sizeof(kCodeChecks) / sizeof(kCodeChecks[0]))) {
        Log("Not patching anything.");
        return 0;
    }
    LoadSettings();
    if (g_dragSteering) {
        if (RedirectCall(drag::kSwitchToDragCallSites[0], drag::kSwitchToDrag, reinterpret_cast<const void*>(&Hook_SwitchToDrag)))
            Log("Hooked the drag controller switch at 0x%08X (never switches): drag races keep normal steering.",
                static_cast<unsigned>(drag::kSwitchToDragCallSites[0]));
        else
            Log("Could not hook the drag controller switch at 0x%08X.", static_cast<unsigned>(drag::kSwitchToDragCallSites[0]));
    }
    if (g_dragFfb) {
        if (PatchCode(drag::kFfbDragJne, kNop6, sizeof(kNop6)))
            Log("Patched 0x%08X: JNE -> NOP. Drag races get normal force feedback.", static_cast<unsigned>(drag::kFfbDragJne));
        else
            Log("Could not write the force feedback patch at 0x%08X.", static_cast<unsigned>(drag::kFfbDragJne));
    }
    Log("Watching player 1's mode and controller (logged when they change).");
    MonitorLoop();
    return 0;
}

void Init() {
    Log("==== Drag Steering started ====");
    // CryptoAPI must not run inside DllMain, so the MD5 check (and everything after it) runs on its own thread.
    HANDLE thread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
}

}  // namespace

void bf::StartDragSteering() { Init(); }
