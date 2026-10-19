// Respawn at Garage (optional; was Immersive Travel): free roam starts at the garage after a profile load or Quit to
// Main Menu. Shops and races still return you where you were; the pause menu's Go To Garage is the game's own.
//
// The garage spot is the position the game itself saves when you drive into a shop (g_Career+0x8750..0x8760, same
// format and heading encoding). A built-in spot is used unless BayviewForever\garage-spot.txt exists; driving into
// the garage (CRIB_1) rewrites that file.
// Hooks: CALL 0x0053450F (Career_LoadFromBuffer), 0x00499497 (Career_OnEnterShop, chained with UniqueParts),
// 0x004C21F7 / 0x004C2444 (pause menu quit). Only acts while the player owns a career car.
// Undo: [Features] RespawnAtGarage = 0.

#define BF_TAG "RespawnAtGarage"
#include "nfsu2.h"

using namespace nfsu2;

namespace {

volatile LONG g_versionOk = 0;
bool          g_hooksOk   = false;

// Built-in garage spot (recorded in the game; the map is the same for everyone). garage-spot.txt, if present, wins.
const bool     kHaveBuiltIn = true;
const float    kBuiltIn[4]  = {656.0490f, -102.1455f, 15.8103f, 0.0f};
const uint16_t kBuiltInHeading = 28576;

// ---- Code checks ---------------------------------------------------------------------

const CodeCheck kCodeChecks[] = {
    // Career_LoadFromBuffer: thiscall(g_Career, buffer, size), RET 8; reads the start spot
    {0x0053450A, {0xB9, 0x40, 0xAD, 0x85, 0x00, 0xE8, 0x1C, 0x4D}, 8, "MOV ECX,g_Career; CALL Career_LoadFromBuffer"},
    {0x00529312, {0x88, 0x93, 0x44, 0x87, 0x00, 0x00}, 6, "load: g_Career+0x8744 (spot saved)"},
    {0x00529338, {0x8D, 0x93, 0x50, 0x87, 0x00, 0x00}, 6, "load: g_Career+0x8750 (spot)"},
    {0x00529359, {0x66, 0x89, 0x8B, 0x60, 0x87, 0x00, 0x00}, 7, "load: g_Career+0x8760 (heading)"},
    {0x0052940B, {0xC2, 0x08, 0x00}, 3, "RET 8 of Career_LoadFromBuffer"},
    // The free-roam start reads the spot (0x005404A0)
    {0x0054069E, {0x38, 0x1D, 0x84, 0x34, 0x86, 0x00}, 6, "free roam start: CMP [0x00863484] (spot saved)"},
    {0x005406AE, {0xD9, 0x05, 0x90, 0x34, 0x86, 0x00}, 6, "free roam start: FLD [0x00863490] (x)"},
    {0x005406BF, {0x66, 0x8B, 0x15, 0xA0, 0x34, 0x86, 0x00}, 7, "free roam start: MOV DX,[0x008634A0] (heading)"},
    // Career_OnEnterShop saves the spot (car+0x60.., +0x40C) and keeps the shop at +0x8734
    {0x0054CBFC, {0xC6, 0x86, 0x44, 0x87, 0x00, 0x00, 0x01}, 7, "enter shop: g_Career+0x8744 = 1"},
    {0x0049948D, {0xB9, 0x40, 0xAD, 0x85, 0x00}, 5, "MOV ECX,g_Career before Career_OnEnterShop"},
    {0x0054CC93, {0xC2, 0x04, 0x00}, 3, "RET 4 of Career_OnEnterShop"},
    // Pause menu quit: Yes 0x30ED2368 -> FUN_004A7CF0 (thiscall(screen), saves the spot via FUN_005139C0, plain RET)
    {0x004C28C5, {0x68, 0x68, 0x23, 0xED, 0x30}, 5, "Quit item: PUSH Yes 0x30ED2368"},
    {0x004C21F5, {0x8B, 0xCE, 0xE8, 0xF4, 0x5A, 0xFE, 0xFF}, 7, "pause Notify: MOV ECX,ESI; CALL quit"},
    {0x004C2442, {0x8B, 0xCE, 0xE8, 0xA7, 0x58, 0xFE, 0xFF}, 7, "pause Notify (deferred): MOV ECX,ESI; CALL quit"},
    {0x004A7D2F, {0xE8, 0x8C, 0xBC, 0x06, 0x00}, 5, "quit: CALL FUN_005139C0 (saves the spot)"},
    {0x004A7D52, {0xC3}, 1, "plain RET of the quit"},
};

// ---- The garage spot -------------------------------------------------------------------

struct Spot {
    bool     known;
    float    pos[4];
    uint16_t heading;
};
Spot g_spot = {};

void SpotPath(char out[MAX_PATH]) {
    char dir[MAX_PATH];
    GetDataDir(dir);
    snprintf(out, MAX_PATH, "%sgarage-spot.txt", dir);
}

void LoadSpot() {
    char path[MAX_PATH], line[256];
    SpotPath(path);
    FILE* f = fopen(path, "r");
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            float x, y, z, w;
            unsigned h;
            if (sscanf(line, "spot %f %f %f %f %u", &x, &y, &z, &w, &h) == 5 && h <= 0xFFFF) {
                g_spot = Spot{true, {x, y, z, w}, static_cast<uint16_t>(h)};
            }
        }
        fclose(f);
    }
    if (g_spot.known) {
        Log("Garage spot from garage-spot.txt: %.2f %.2f %.2f, heading %u.", g_spot.pos[0], g_spot.pos[1],
            g_spot.pos[2], g_spot.heading);
    } else if (kHaveBuiltIn) {
        g_spot = Spot{true, {kBuiltIn[0], kBuiltIn[1], kBuiltIn[2], kBuiltIn[3]}, kBuiltInHeading};
        Log("Garage spot: built in (%.2f %.2f %.2f, heading %u).", g_spot.pos[0], g_spot.pos[1], g_spot.pos[2],
            g_spot.heading);
    } else {
        Log("Garage spot not known yet: drive into your garage once and it is remembered for every load.");
    }
}

void SaveSpot() {
    char path[MAX_PATH];
    SpotPath(path);
    FILE* f = fopen(path, "w");
    if (!f) {
        Log("COULD NOT write %s.", path);
        return;
    }
    fprintf(f, "; BayviewForever Respawn at Garage: where a profile load starts free roam (the spot the game saves when\n"
               "; you drive into your garage). Delete this file and drive into the garage again to take a new one.\n");
    fprintf(f, "spot %.4f %.4f %.4f %.4f %u\n", g_spot.pos[0], g_spot.pos[1], g_spot.pos[2], g_spot.pos[3],
            g_spot.heading);
    fclose(f);
}

unsigned char* Career() { return reinterpret_cast<unsigned char*>(ev::kCareer); }

// Makes free roam start at the garage. False (nothing written) without a career car or a known spot.
bool PutStartAtGarage() {
    if (!g_spot.known || !*reinterpret_cast<const volatile uint8_t*>(ev::kCurrentCarBought)) return false;
    unsigned char* c = Career();
    c[travel::kSpotSaved] = 1;
    memcpy(c + travel::kSpot, g_spot.pos, sizeof(g_spot.pos));
    memcpy(c + travel::kSpotHeading, &g_spot.heading, sizeof(g_spot.heading));
    return true;
}

bool HasCareerCar() { return *reinterpret_cast<const volatile uint8_t*>(ev::kCurrentCarBought) != 0; }

// ---- Hooks ---------------------------------------------------------------------------

// Replaces the CALL to Career_LoadFromBuffer (thiscall(g_Career, buffer, size) -> next buffer, RET 8).
void* __fastcall Hook_CareerLoad(void* career, void* edx, void* buffer, int size) {
    using Fn = void*(__fastcall*)(void*, void*, void*, int);
    void* next = reinterpret_cast<Fn>(travel::kCareerLoad)(career, edx, buffer, size);
    if (g_versionOk && reinterpret_cast<uintptr_t>(career) == ev::kCareer) {
        unsigned char* c = Career();
        float old[3];
        memcpy(old, c + travel::kSpot, sizeof(old));
        if (!HasCareerCar()) {
            Log("Profile loaded: no career car yet, start spot left as saved.");
        } else if (!g_spot.known) {
            Log("Profile loaded: start spot left as saved (%.0f %.0f), the garage spot is not known yet.", old[0], old[1]);
        } else {
            c[travel::kSpotSaved] = 1;
            memcpy(c + travel::kSpot, g_spot.pos, sizeof(g_spot.pos));
            memcpy(c + travel::kSpotHeading, &g_spot.heading, sizeof(g_spot.heading));
            Log("Profile loaded: free roam starts at the garage (saved spot was %.0f %.0f).", old[0], old[1]);
        }
    }
    return next;
}

// Replaces both CALLs to the pause menu's quit (thiscall(screen), plain RET). The game saves the car's spot inside it;
// afterwards the spot becomes the garage, so going back into the career starts there.
void __fastcall Hook_QuitToMenu(void* screen, void* edx) {
    using Fn = void(__fastcall*)(void*, void*);
    reinterpret_cast<Fn>(travel::kQuitToMenu)(screen, edx);
    if (g_versionOk && *reinterpret_cast<const int32_t*>(uniques::kGameMode) == 1) {
        float old[3];
        memcpy(old, Career() + travel::kSpot, sizeof(old));
        if (PutStartAtGarage())
            Log("Quit to the main menu: free roam will start at the garage (left at %.0f %.0f).", old[0], old[1]);
        else
            Log("Quit to the main menu: start spot left as saved (%s).", g_spot.known ? "no career car" : "garage spot not known yet");
    }
}

uintptr_t g_nextEnterShop = uniques::kEnterShop;

// Replaces the CALL to Career_OnEnterShop (thiscall(g_Career, shop record), RET 4), chained with UniqueParts.
void __fastcall Hook_EnterShop(void* career, void* edx, const unsigned char* shop) {
    using Fn = void(__fastcall*)(void*, void*, const unsigned char*);
    reinterpret_cast<Fn>(g_nextEnterShop)(career, edx, shop);
    if (g_versionOk && reinterpret_cast<uintptr_t>(career) == ev::kCareer && shop && shop[travel::kShopType] == 0 &&
        Career()[travel::kSpotSaved]) {
        Spot s = {true, {}, 0};
        memcpy(s.pos, Career() + travel::kSpot, sizeof(s.pos));
        memcpy(&s.heading, Career() + travel::kSpotHeading, sizeof(s.heading));
        bool changed = !g_spot.known || memcmp(s.pos, g_spot.pos, sizeof(s.pos)) != 0 || s.heading != g_spot.heading;
        Log("Drove into the garage at %.2f %.2f %.2f, heading %u%s.", s.pos[0], s.pos[1], s.pos[2], s.heading,
            changed ? ": remembered as the start spot" : "");
        if (changed) {
            g_spot = s;
            SaveSpot();
        }
    }
}

DWORD WINAPI VersionCheckThread(LPVOID) {
    bool ok = ExeMd5Ok() && g_hooksOk;
    InterlockedExchange(&g_versionOk, ok ? 1 : 0);
    if (!ok) Log("The hooks (if any) stay installed but only pass calls through to the game.");
    else Log("Ready. Profile loads and Quit to Main Menu restart free roam at the garage.");
    return 0;
}

void Init() {
    Log("==== RespawnAtGarage started (profile loads and Quit to Main Menu restart at the garage) ====");
    if (!CodeMatches(kCodeChecks, sizeof(kCodeChecks) / sizeof(kCodeChecks[0]))) {
        Log("Not hooking anything.");
        return;
    }
    LoadSpot();
    bool b = RedirectCall(travel::kCareerLoadCallSites[0], travel::kCareerLoad,
                          reinterpret_cast<const void*>(&Hook_CareerLoad));
    Log("%s career load call at 0x%08X.", b ? "Hooked" : "COULD NOT hook",
        static_cast<unsigned>(travel::kCareerLoadCallSites[0]));
    bool c = ChainCall(uniques::kEnterShopCallSites[0], uniques::kEnterShop,
                       reinterpret_cast<const void*>(&Hook_EnterShop), &g_nextEnterShop);
    Log("%s shop entered call at 0x%08X.", c ? "Hooked" : "COULD NOT hook",
        static_cast<unsigned>(uniques::kEnterShopCallSites[0]));
    bool d = true;
    for (uintptr_t site : travel::kQuitToMenuCallSites) {
        bool one = RedirectCall(site, travel::kQuitToMenu, reinterpret_cast<const void*>(&Hook_QuitToMenu));
        Log("%s quit to main menu call at 0x%08X.", one ? "Hooked" : "COULD NOT hook", static_cast<unsigned>(site));
        d &= one;
    }
    g_hooksOk = b && c && d;
    HANDLE thread = CreateThread(nullptr, 0, VersionCheckThread, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
}

}  // namespace

void bf::StartRespawnAtGarage() { Init(); }
