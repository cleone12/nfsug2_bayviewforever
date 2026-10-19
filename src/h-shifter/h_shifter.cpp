// H-Shifter: H-pattern shifter and clutch pedal through the game's hidden "manual with clutch" shift mode 3
// (RealDriver+0x2C0: 0 auto, 1 manual, 2 drag, 3 clutch, 4 = mode 3 during a shift into/out of N or R). In mode 3 the
// driver's clutch (+0x2F4, 1.0 = up) goes to Drivetrain_SetClutch each frame and gears only engage with the clutch
// fully down. See docs/addresses.md -> "Gearbox, clutch and H-shifter".
//
//   - RealDriver_SetShiftMode (0x00407280) is replaced in its only pointer (vtable slot 0x00784FFC): Manual becomes
//     mode 3 wherever the game sets it (the race start inlines that code in many places). ClutchMode = 0 keeps mode 1.
//   - Each frame, at the end of the human driver's update (CALLs 0x0041E429 / 0x0041E466), the wheel is read through
//     the mod's own non-exclusive DirectInput device; clutch -> +0x2F4, lever -> wanted gear +0x1E4. Nothing is written
//     until the pedal / a shifter button has been seen once, so a wrong mapping can't strand the car in neutral.
//   - During a shift the clutch is held open and the gear is requested only once the drivetrain reports "disengaged",
//     so the game never refuses it; a shift without the pedal (below ClutchThreshold) gets a bog: engine power
//     (engine+0x80, scaled after Drivetrain_UpdateClutchState, vtable slot 0x007A2524) drops to BogPower, holds for
//     BogHold, recovers over BogRecover. Shifts into neutral are never punished.
//   - BrakeOnlyBrakes: see "Pedals" below.
// DirectInput is loaded from the system folder at runtime (the game folder's dinput8.dll is the ASI loader).
// Undo: [Features] HShifter = 0.

#define DIRECTINPUT_VERSION 0x0800
#define BF_TAG "HShifter"
#include "nfsu2.h"
#include <dinput.h>
#include <cctype>

using namespace nfsu2;
namespace gb = nfsu2::gearbox;

namespace {

HMODULE g_module = nullptr;

const CodeCheck kCodeChecks[] = {
    // Shift mode setter (hooked through its vtable slot; the slot value is checked by PatchPointer)
    {0x00407280, {0x8B, 0x54, 0x24, 0x04, 0x83, 0xFA, 0x03, 0xB8}, 8, "start of RealDriver_SetShiftMode: EDX = mode; CMP EDX, 3"},
    {0x004072A0, {0xC7, 0x81, 0xC0, 0x02, 0x00, 0x00, 0x03}, 7, "RealDriver_SetShiftMode: mode 3 -> driver+0x2C0"},
    {0x00407304, {0xC2, 0x04, 0x00, 0x32, 0xC0, 0xC2, 0x04, 0x00}, 8, "RealDriver_SetShiftMode: RET 4 (AL result)"},
    {0x005F3046, {0x8A, 0x0D, 0xD8, 0xE7, 0x89, 0x00}, 6, "inlined option read: drag flag 0x0089E7D8"},
    // Driver per-frame update
    {0x0041E2C4, {0x83, 0xBE, 0xAC, 0x02, 0x00, 0x00, 0xFF}, 7, "driver update: skip drivers without a controller port"},
    {0x0041E3AE, {0x8B, 0x41, 0x34, 0x8B, 0x78, 0x4C, 0x8B, 0x86}, 8, "driver update: drivetrain = [[car+0x34]+0x4C]"},
    {0x0041E3B4, {0x8B, 0x86, 0xC0, 0x02, 0x00, 0x00}, 6, "driver update: reads shift mode driver+0x2C0"},
    {0x0041E3C4, {0x8B, 0x89, 0x34, 0x04, 0x00, 0x00}, 6, "driver update: reads car gear car+0x434"},
    {0x0041E413, {0x8B, 0x8E, 0xF4, 0x02, 0x00, 0x00, 0x51, 0x8B}, 8, "driver update: PUSH clutch driver+0x2F4 for Drivetrain_SetClutch"},
    {0x0041E426, {0x52, 0x8B, 0xCE, 0xE8, 0xD2, 0x8F, 0xFE, 0xFF}, 8, "driver update (mode 3 exit): ECX = driver; CALL 0x00407400"},
    {0x0041E463, {0x52, 0x8B, 0xCE, 0xE8, 0x95, 0x8F, 0xFE, 0xFF}, 8, "driver update (other exit): ECX = driver; CALL 0x00407400"},
    {0x00407400, {0x51, 0x56, 0x57, 0x33, 0xFF, 0x57, 0x8B, 0xF1}, 8, "start of 0x00407400 (thiscall)"},
    {0x004076B2, {0xC2, 0x04, 0x00}, 3, "RET 4 of 0x00407400"},
    // Gear and clutch fields
    {0x00407FE9, {0x8B, 0x90, 0xA8, 0x02, 0x00, 0x00, 0x8B, 0x8A}, 8, "shift up: car = driver+0x2A8"},
    {0x00407FF5, {0x8B, 0x52, 0x20, 0x8B, 0x92, 0xC8, 0x01, 0x00}, 8, "shift up: top gear = [[car+0x20]+0x1C8]"},
    {0x00594910, {0xD9, 0x44, 0x24, 0x04, 0xBA, 0x03, 0x00, 0x00}, 8, "start of Drivetrain_SetClutch"},
    {0x005AAC3C, {0x83, 0x7E, 0x60, 0x03, 0x75, 0x5F, 0x8B, 0x46}, 8, "Drivetrain_RequestGear: clutch mode branch"},
    {0x005AAC49, {0x8B, 0x56, 0x44, 0xB9, 0x01, 0x00, 0x00, 0x00}, 8, "Drivetrain_RequestGear: needs clutch state 1"},
    {0x005ABDA9, {0xFF, 0x57, 0x20, 0x8B, 0x4E, 0x4C, 0x50, 0xE8}, 8, "per tick: wanted gear -> Drivetrain_RequestGear"},
    {0x005ABDB5, {0x8B, 0x4E, 0x04, 0x50, 0xFF, 0x57, 0x28}, 7, "per tick: result -> wanted gear"},
    // Grind power cut (drivetrain+0x5C) used for the missed-clutch penalty
    {0x005A163C, {0x89, 0x99, 0x80, 0x00, 0x00, 0x00, 0xDB, 0x05}, 8, "grind: engine+0x80 = 0; FILD game clock"},
    {0x005A1400, {0x53, 0x55, 0x56, 0x8B, 0xF1, 0xE8, 0xE6, 0x31}, 8, "start of Drivetrain_UpdateClutchState (ECX only)"},
    {0x005A1665, {0x5E, 0x5D, 0x5B, 0xC3}, 4, "Drivetrain_UpdateClutchState: plain RET"},
    {0x005A16E5, {0x5E, 0x5D, 0x5B, 0xC3}, 4, "Drivetrain_UpdateClutchState: plain RET (2)"},
    {0x005A1639, {0x8B, 0x4E, 0x20, 0x89, 0x99, 0x80, 0x00, 0x00}, 8, "power factor = [drivetrain+0x20]+0x80"},
    {0x00784268, {0x6F, 0x12, 0x83, 0x39}, 4, "constant 0.00025 (game clock ticks -> seconds)"},
    // RealDriver_MapPedals (BrakeOnlyBrakes): the brake-to-reverse logic reads the brake pedal and the flag
    {0x00407A30, {0x83, 0xEC, 0x0C, 0xA1, 0x28, 0x3B, 0x78, 0x00}, 8, "start of RealDriver_MapPedals"},
    {0x00407AF2, {0xD9, 0x86, 0xDC, 0x02, 0x00, 0x00}, 6, "MapPedals: reads the brake pedal driver+0x2DC"},
    {0x00407B18, {0xC6, 0x86, 0xF0, 0x02, 0x00, 0x00, 0x01}, 7, "MapPedals: reversing flag driver+0x2F0 = 1"},
    {0x00407CBE, {0xC7, 0x86, 0xE4, 0x01, 0x00, 0x00, 0x00}, 7, "MapPedals: wanted gear = R (brake reverses)"},
    {0x00407E4E, {0x8B, 0x86, 0xE4, 0x01, 0x00, 0x00, 0x85, 0xC0}, 8, "MapPedals: wanted gear R + throttle -> vtable+0x30"},
    {0x005EB8F0, {0x8B, 0x44, 0x24, 0x04, 0x89, 0x81, 0x14, 0x02}, 8, "brake output setter: driver+0x214"},
    {0x005EB910, {0x8B, 0x44, 0x24, 0x04, 0x89, 0x81, 0x0C, 0x02}, 8, "throttle output setter: driver+0x20C"},
    // Start hold
    {0x0041E37E, {0x8A, 0x86, 0xB4, 0x02, 0x00, 0x00, 0x84, 0xC0}, 8, "driver update: start hold flag driver+0x2B4"},
    {0x0041E38F, {0xC7, 0x86, 0xE4, 0x01, 0x00, 0x00, 0x01, 0x00}, 8, "driver update: start hold -> wanted gear = N"},
    // Players
    {0x0060A240, {0x8B, 0x0C, 0xB5, 0xB4, 0x00, 0x89, 0x00, 0xE8}, 8, "player list 0x008900B4 -> player update"},
    {0x0060A24C, {0xA1, 0xBC, 0x00, 0x89, 0x00}, 5, "player count 0x008900BC"},
};

// ---- Settings -----------------------------------------------------------------

const int kNumAxes = 8;
const char* const kAxisNames[kNumAxes] = {"X", "Y", "Z", "RX", "RY", "RZ", "SLIDER0", "SLIDER1"};
const int kAxisMax = 10000;  // every axis is set to the range 0..kAxisMax

struct Settings {
    bool clutchMode = true;          // [Gearbox] ClutchMode: Manual -> manual with clutch
    bool dragRaces = true;           // [Gearbox] DragRaces: clutch mode in drag races too (needs DragSteering)
    bool brakeOnlyBrakes = true;     // [Gearbox] BrakeOnlyBrakes: with the shifter, the brake never reverses
    char deviceName[64] = "G27";     // [Device] Name: part of the controller's name
    int gearButton[7] = {15, 9, 10, 11, 12, 13, 14};  // [Shifter] Reverse, Gear1..Gear6: button numbers (1 = first), 0 = none
    int clutchAxis = -1;             // [Clutch] Axis: index into kAxisNames, -1 = AUTO (the axis named "Clutch")
    int clutchUp = kAxisMax;         // [Clutch] UpValue / DownValue: 0..10000 (G27: up 10000, down 0)
    int clutchDown = 0;
    float topDeadzone = 0.05f;       // [Clutch] TopDeadzone / BottomDeadzone, percent of pedal travel
    float bottomDeadzone = 0.10f;
    float clutchThreshold = 0.50f;   // [Shifting] ClutchThreshold: pedal travel that counts as "used the clutch"
    float bogPower = 0.0f;           // [Shifting] BogPower (%): engine power right after a missed-clutch shift
    float bogHold = 0.4f;            // [Shifting] BogHold (ms): time at that power
    float bogRecover = 1.0f;         // [Shifting] BogRecover (ms): time to ramp back to full power
    bool logInput = false;           // [Log] Input
    bool logGearbox = false;         // [Log] Gearbox: gearbox state and every shift (Input = 1 turns them on too)
} g_set;

int ReadIniPercent(const char* section, const char* key, int def, const char* path) {
    int v = static_cast<int>(GetPrivateProfileIntA(section, key, def, path));
    return v < 0 ? 0 : (v > 45 ? 45 : v);
}

void LoadSettings() {
    char path[MAX_PATH];
    bf::IniPath(path);

    g_set.clutchMode = GetPrivateProfileIntA("HShifter.Gearbox", "ClutchMode", 1, path) != 0;
    g_set.dragRaces = GetPrivateProfileIntA("HShifter.Gearbox", "DragRaces", 1, path) != 0;
    g_set.brakeOnlyBrakes = GetPrivateProfileIntA("HShifter.Gearbox", "BrakeOnlyBrakes", 1, path) != 0;
    GetPrivateProfileStringA("HShifter.Device", "Name", "G27", g_set.deviceName, sizeof(g_set.deviceName), path);
    static const char* const kGearKeys[7] = {"Reverse", "Gear1", "Gear2", "Gear3", "Gear4", "Gear5", "Gear6"};
    for (int i = 0; i < 7; i++) {
        int b = static_cast<int>(GetPrivateProfileIntA("HShifter.Shifter", kGearKeys[i], g_set.gearButton[i], path));
        g_set.gearButton[i] = (b >= 0 && b <= 128) ? b : 0;
    }
    char axis[32];
    GetPrivateProfileStringA("HShifter.Clutch", "Axis", "AUTO", axis, sizeof(axis), path);
    for (int i = 0; i < kNumAxes; i++)
        if (_stricmp(axis, kAxisNames[i]) == 0) g_set.clutchAxis = i;
    g_set.clutchUp = static_cast<int>(GetPrivateProfileIntA("HShifter.Clutch", "UpValue", kAxisMax, path));
    g_set.clutchDown = static_cast<int>(GetPrivateProfileIntA("HShifter.Clutch", "DownValue", 0, path));
    if (g_set.clutchUp < 0 || g_set.clutchUp > kAxisMax || g_set.clutchDown < 0 || g_set.clutchDown > kAxisMax ||
        g_set.clutchUp == g_set.clutchDown) {
        Log("  (bad clutch UpValue/DownValue, using 10000/0)");
        g_set.clutchUp = kAxisMax;
        g_set.clutchDown = 0;
    }
    g_set.topDeadzone = ReadIniPercent("HShifter.Clutch", "TopDeadzone", 5, path) / 100.0f;
    g_set.bottomDeadzone = ReadIniPercent("HShifter.Clutch", "BottomDeadzone", 10, path) / 100.0f;
    int threshold = static_cast<int>(GetPrivateProfileIntA("HShifter.Shifting", "ClutchThreshold", 50, path));
    g_set.clutchThreshold = (threshold < 1 ? 1 : threshold > 100 ? 100 : threshold) / 100.0f;
    auto readClamped = [&](const char* key, int def, int lo, int hi) {
        int v = static_cast<int>(GetPrivateProfileIntA("HShifter.Shifting", key, def, path));
        return v < lo ? lo : v > hi ? hi : v;
    };
    g_set.bogPower = readClamped("BogPower", 0, 0, 100) / 100.0f;
    g_set.bogHold = readClamped("BogHold", 400, 0, 5000) / 1000.0f;
    g_set.bogRecover = readClamped("BogRecover", 1000, 0, 10000) / 1000.0f;
    g_set.logInput = GetPrivateProfileIntA("HShifter.Log", "Input", 0, path) != 0;
    g_set.logGearbox = GetPrivateProfileIntA("HShifter.Log", "Gearbox", 0, path) != 0;

    Log("Settings%s:", bf::IniFound() ? "" : " (defaults)");
    Log("  ClutchMode=%d  DragRaces=%d  BrakeOnlyBrakes=%d  Device=\"%s\"  LogInput=%d  LogGearbox=%d", g_set.clutchMode,
        g_set.dragRaces, g_set.brakeOnlyBrakes, g_set.deviceName, g_set.logInput, g_set.logGearbox);
    Log("  Shifter buttons: R=%d 1=%d 2=%d 3=%d 4=%d 5=%d 6=%d", g_set.gearButton[0], g_set.gearButton[1],
        g_set.gearButton[2], g_set.gearButton[3], g_set.gearButton[4], g_set.gearButton[5], g_set.gearButton[6]);
    Log("  Clutch: axis %s, up %d, down %d, deadzones top %d%% bottom %d%%",
        g_set.clutchAxis < 0 ? "AUTO" : kAxisNames[g_set.clutchAxis], g_set.clutchUp, g_set.clutchDown,
        static_cast<int>(g_set.topDeadzone * 100 + 0.5f), static_cast<int>(g_set.bottomDeadzone * 100 + 0.5f));
    Log("  Shifting: clutch counts from %d%% pedal travel; missed clutch = %d%% power for %d ms, back to full in %d ms",
        static_cast<int>(g_set.clutchThreshold * 100 + 0.5f), static_cast<int>(g_set.bogPower * 100 + 0.5f),
        static_cast<int>(g_set.bogHold * 1000 + 0.5f), static_cast<int>(g_set.bogRecover * 1000 + 0.5f));
}

// The input log can get long (the wheel axis moves all the time), so it stops after this many lines.
// Only with [Log] Gearbox or Input on.
const int kMaxDetailLines = 40000;
int g_detailLines = 0;
#define LOG_DETAIL(...)                                                                   \
    do {                                                                                  \
        if ((g_set.logGearbox || g_set.logInput) && g_detailLines < kMaxDetailLines) {    \
            if (++g_detailLines == kMaxDetailLines) Log("(log limit reached: no more input/gearbox lines)"); \
            else Log(__VA_ARGS__);                                                        \
        }                                                                                 \
    } while (0)

// ---- DirectInput ----------------------------------------------------------------

typedef HRESULT(WINAPI* DirectInput8CreateFn)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

IDirectInput8A* g_di = nullptr;
IDirectInputDevice8A* g_dev = nullptr;
int g_initAttempts = 0;
DWORD g_nextInitTry = 0;
DIJOYSTATE2 g_state;
DIJOYSTATE2 g_prev;
bool g_havePrev = false;
LONG g_axisLogged[kNumAxes];
DWORD g_axisLoggedAt[kNumAxes];

bool ContainsNoCase(const char* hay, const char* needle) {
    if (!*needle) return true;
    for (; *hay; hay++) {
        const char* h = hay;
        const char* n = needle;
        while (*h && *n && tolower(static_cast<unsigned char>(*h)) == tolower(static_cast<unsigned char>(*n))) h++, n++;
        if (!*n) return true;
    }
    return false;
}

struct EnumResult {
    GUID match;
    GUID first;
    bool haveMatch = false;
    bool haveFirst = false;
};

BOOL CALLBACK EnumDeviceCb(LPCDIDEVICEINSTANCEA inst, LPVOID ctx) {
    EnumResult* r = static_cast<EnumResult*>(ctx);
    Log("  game controller: \"%s\" (instance \"%s\")", inst->tszProductName, inst->tszInstanceName);
    if (!r->haveFirst) {
        r->first = inst->guidInstance;
        r->haveFirst = true;
    }
    if (!r->haveMatch && (ContainsNoCase(inst->tszProductName, g_set.deviceName) ||
                          ContainsNoCase(inst->tszInstanceName, g_set.deviceName))) {
        r->match = inst->guidInstance;
        r->haveMatch = true;
    }
    return DIENUM_CONTINUE;
}

const int kFallbackClutchAxis = 7;  // SLIDER1: the G27's clutch
int g_clutchAxis = kFallbackClutchAxis;

// Logs which device axis sits in each of our 8 slots and picks the clutch axis. Use GetObjectInfo(DIPH_BYOFFSET):
// EnumObjects' dwOfs doesn't match the data format.
void MapAxes() {
    int byName = -1;
    for (int a = 0; a < kNumAxes; a++) {
        DIDEVICEOBJECTINSTANCEA obj;
        obj.dwSize = sizeof(obj);
        if (FAILED(g_dev->GetObjectInfo(&obj, a * 4, DIPH_BYOFFSET))) continue;
        Log("  %s = \"%s\"", kAxisNames[a], obj.tszName);
        if (byName < 0 && ContainsNoCase(obj.tszName, "clutch")) byName = a;
    }
    if (g_set.clutchAxis >= 0)
        g_clutchAxis = g_set.clutchAxis;
    else if (byName >= 0)
        g_clutchAxis = byName;
    else
        g_clutchAxis = kFallbackClutchAxis;
    Log("  clutch axis: %s (%s)", kAxisNames[g_clutchAxis],
        g_set.clutchAxis >= 0 ? "from the ini" : byName >= 0 ? "found by name" : "no axis named Clutch, default");
}

BOOL CALLBACK FindGameWindowCb(HWND hwnd, LPARAM out) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd) && !GetWindow(hwnd, GW_OWNER)) {
        *reinterpret_cast<HWND*>(out) = hwnd;
        return FALSE;
    }
    return TRUE;
}

void ReleaseInput() {
    if (g_dev) {
        g_dev->Unacquire();
        g_dev->Release();
        g_dev = nullptr;
    }
    if (g_di) {
        g_di->Release();
        g_di = nullptr;
    }
}

// Opens the controller. Runs on the game's main thread, the first time a human driver is updated; retried a few
// times (every 5 s) if it fails.
bool InitInput() {
    if (g_dev) return true;
    if (g_initAttempts >= 5 || GetTickCount() < g_nextInitTry) return false;
    g_initAttempts++;
    g_nextInitTry = GetTickCount() + 5000;
    Log("Opening the controller (attempt %d)...", g_initAttempts);

    char dll[MAX_PATH];
    GetSystemDirectoryA(dll, MAX_PATH);
    strncat(dll, "\\dinput8.dll", MAX_PATH - strlen(dll) - 1);
    HMODULE lib = LoadLibraryA(dll);
    DirectInput8CreateFn create =
        lib ? reinterpret_cast<DirectInput8CreateFn>(reinterpret_cast<void*>(GetProcAddress(lib, "DirectInput8Create"))) : nullptr;
    if (!create) {
        Log("  could not load %s", dll);
        return false;
    }
    HRESULT hr = create(g_module, DIRECTINPUT_VERSION, IID_IDirectInput8A, reinterpret_cast<void**>(&g_di), nullptr);
    if (FAILED(hr)) {
        Log("  DirectInput8Create failed (0x%08lX)", hr);
        g_di = nullptr;
        return false;
    }
    EnumResult found;
    g_di->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumDeviceCb, &found, DIEDFL_ATTACHEDONLY);
    if (!found.haveFirst) {
        Log("  no game controller found");
        ReleaseInput();
        return false;
    }
    if (!found.haveMatch) Log("  nothing named \"%s\": using the first controller", g_set.deviceName);

    HWND window = nullptr;
    EnumWindows(FindGameWindowCb, reinterpret_cast<LPARAM>(&window));
    if ((hr = g_di->CreateDevice(found.haveMatch ? found.match : found.first, &g_dev, nullptr)) != DI_OK) {
        Log("  CreateDevice failed (0x%08lX)", hr);
        g_dev = nullptr;
        ReleaseInput();
        return false;
    }
    if (FAILED(hr = g_dev->SetDataFormat(&c_dfDIJoystick2)) ||
        FAILED(hr = g_dev->SetCooperativeLevel(window, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE))) {
        Log("  device setup failed (0x%08lX, window %p)", hr, static_cast<void*>(window));
        ReleaseInput();
        return false;
    }
    DIPROPRANGE range;
    range.diph.dwSize = sizeof(range);
    range.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    range.diph.dwObj = 0;
    range.diph.dwHow = DIPH_DEVICE;
    range.lMin = 0;
    range.lMax = kAxisMax;
    g_dev->SetProperty(DIPROP_RANGE, &range.diph);

    DIDEVICEINSTANCEA info;
    info.dwSize = sizeof(info);
    DIDEVCAPS caps;
    caps.dwSize = sizeof(caps);
    if (SUCCEEDED(g_dev->GetDeviceInfo(&info)) && SUCCEEDED(g_dev->GetCapabilities(&caps)))
        Log("  using \"%s\": %lu axes, %lu buttons, %lu POV hats", info.tszProductName, caps.dwAxes, caps.dwButtons,
            caps.dwPOVs);
    MapAxes();
    hr = g_dev->Acquire();
    Log("  acquire: 0x%08lX (non-exclusive, background)", hr);
    return true;
}

// Reads the controller into g_state. False if there is no controller (yet).
bool PollInput() {
    if (!InitInput()) return false;
    if (FAILED(g_dev->Poll())) {
        g_dev->Acquire();
        g_dev->Poll();
    }
    HRESULT hr = g_dev->GetDeviceState(sizeof(g_state), &g_state);
    if (FAILED(hr)) {
        static DWORD lastComplaint = 0;
        if (GetTickCount() - lastComplaint > 10000) {
            lastComplaint = GetTickCount();
            Log("GetDeviceState failed (0x%08lX)", hr);
        }
        return false;
    }
    return true;
}

LONG AxisValue(const DIJOYSTATE2& s, int axis) {
    return reinterpret_cast<const LONG*>(&s)[axis];  // lX lY lZ lRx lRy lRz rglSlider[0] rglSlider[1]
}

// Logs button / POV / axis changes, so the log shows which number each shifter gear and pedal has.
void LogInputChanges() {
    const DWORD now = GetTickCount();
    if (!g_havePrev) {
        char line[512];
        int len = snprintf(line, sizeof(line), "Input at start:");
        for (int a = 0; a < kNumAxes; a++) {
            len += snprintf(line + len, sizeof(line) - len, " %s=%ld", kAxisNames[a], AxisValue(g_state, a));
            g_axisLogged[a] = AxisValue(g_state, a);
            g_axisLoggedAt[a] = now;
        }
        len += snprintf(line + len, sizeof(line) - len, " | buttons held:");
        for (int b = 0; b < 128 && len < static_cast<int>(sizeof(line)) - 8; b++)
            if (g_state.rgbButtons[b] & 0x80) len += snprintf(line + len, sizeof(line) - len, " %d", b + 1);
        Log("%s", line);
        g_prev = g_state;
        g_havePrev = true;
        return;
    }
    if (!g_set.logInput) return;
    for (int b = 0; b < 128; b++) {
        const bool was = g_prev.rgbButtons[b] & 0x80, is = g_state.rgbButtons[b] & 0x80;
        if (was != is) LOG_DETAIL("Button %d %s", b + 1, is ? "pressed" : "released");
    }
    for (int p = 0; p < 4; p++)
        if (g_prev.rgdwPOV[p] != g_state.rgdwPOV[p])
            LOG_DETAIL("POV %d = %ld", p, static_cast<long>(static_cast<int32_t>(g_state.rgdwPOV[p])));
    for (int a = 0; a < kNumAxes; a++) {
        const LONG v = AxisValue(g_state, a);
        const LONG d = v - g_axisLogged[a];
        const bool atEnd = (v <= 50 || v >= kAxisMax - 50) && d != 0;
        if ((d >= 1000 || d <= -1000 || atEnd) && now - g_axisLoggedAt[a] >= 200) {
            LOG_DETAIL("Axis %s = %ld", kAxisNames[a], v);
            g_axisLogged[a] = v;
            g_axisLoggedAt[a] = now;
        }
    }
    g_prev = g_state;
}

// ---- Clutch pedal and shifter -------------------------------------------------------

bool g_clutchActive = false;  // the pedal has been pressed well past halfway at least once

// Pedal travel 0 (up) .. 1 (fully pressed). No learning: the G27 reports 5000 for every pedal until its first real
// report, so a "first reading" can't be trusted. Up/down values come from the ini (default up 10000, down 0).
float PedalTravel(LONG raw) {
    float travel = static_cast<float>(raw - g_set.clutchUp) / static_cast<float>(g_set.clutchDown - g_set.clutchUp);
    if (travel < 0) travel = 0;
    if (travel > 1) travel = 1;
    if (!g_clutchActive && travel > 0.6f) {  // > 0.6: the G27's dummy 5000 reading is exactly halfway
        g_clutchActive = true;
        Log("Clutch pedal seen (%s = %ld): the plugin now drives the game's clutch.", kAxisNames[g_clutchAxis], raw);
    }
    return travel;
}

// Clutch engagement for the game: 1.0 pedal up .. 0.0 fully disengaged (bottom deadzone).
float ClutchEngagement(float travel) {
    const float top = g_set.topDeadzone, bottom = g_set.bottomDeadzone;
    if (travel <= top) return 1.0f;
    if (travel >= 1.0f - bottom) return 0.0f;
    return 1.0f - (travel - top) / (1.0f - top - bottom);
}

bool g_shifterActive = false;  // a shifter button has been seen pressed at least once

bool ButtonHeld(int number) {
    return number >= 1 && number <= 128 && (g_state.rgbButtons[number - 1] & 0x80);
}

// The game's gear number for the lever position: 0 R, 1 N, 2 = 1st ... 7 = 6th.
int LeverGear() {
    if (ButtonHeld(g_set.gearButton[0])) return gb::kGearReverse;
    for (int g = 1; g <= 6; g++)
        if (ButtonHeld(g_set.gearButton[g])) return gb::kGearNeutral + g;
    return gb::kGearNeutral;
}

const char* GearName(int gear) {
    static const char* const kNames[] = {"R", "N", "1", "2", "3", "4", "5", "6", "7", "8"};
    if (gear >= 0 && gear <= 9) return kNames[gear];
    if (gear == -1) return "auto";
    if (gear == -2) return "drag";
    return "?";
}

// ---- Per-frame driver hook ---------------------------------------------------------------

// Only player 1's driver (split screen: the other player's driver is left alone).
bool IsPlayerOneDriver(const unsigned char* drv) {
    static bool logged = false;
    const uintptr_t car = ReadAt<uintptr_t>(drv, gb::driver::kCar);
    int count = IsReadable(player::kCount, 4) ? *reinterpret_cast<const int*>(player::kCount) : 0;
    uintptr_t p1Car = 0;
    if (count >= 1 && IsReadable(player::kList, 4)) {
        const uintptr_t p1 = *reinterpret_cast<const uintptr_t*>(player::kList);
        if (p1 && IsReadable(p1 + player::kCar, 4)) p1Car = *reinterpret_cast<const uintptr_t*>(p1 + player::kCar);
    }
    const bool yes = count <= 1 || car == p1Car;
    if (!logged) {
        logged = true;
        Log("First driver update: driver %p car %p port %d; players %d, player 1 car %p -> %s",
            static_cast<const void*>(drv), reinterpret_cast<void*>(car), ReadAt<int>(drv, gb::driver::kPort), count,
            reinterpret_cast<void*>(p1Car), yes ? "player 1" : "not player 1");
    }
    return yes;
}

struct GearState {
    int mode, wanted, carGear, current, target, pending, clutchState, grinding, lever;
};
GearState g_lastLogged = {-99, -99, -99, -99, -99, -99, -99, -99, -99};

void LogGearState(const unsigned char* drv, const unsigned char* dt, int lever, float pedal) {
    const unsigned char* car = reinterpret_cast<const unsigned char*>(ReadAt<uintptr_t>(drv, gb::driver::kCar));
    GearState s;
    s.mode = ReadAt<int>(drv, gb::driver::kShiftMode);
    s.wanted = ReadAt<int>(drv, gb::driver::kWantedGear);
    s.carGear = ReadAt<int>(car, gb::car::kGear);
    s.current = dt ? ReadAt<int>(dt, gb::drivetrain::kCurrent) : -99;
    s.target = dt ? ReadAt<int>(dt, gb::drivetrain::kTarget) : -99;
    s.pending = dt ? ReadAt<int>(dt, gb::drivetrain::kPending) : -99;
    s.clutchState = dt ? ReadAt<int>(dt, gb::drivetrain::kClutchState) : -99;
    s.grinding = dt ? ReadAt<float>(dt, gb::drivetrain::kGrindStart) != 0.0f : -99;
    s.lever = lever;
    if (memcmp(&s, &g_lastLogged, sizeof(s)) == 0) return;
    if (s.mode != g_lastLogged.mode) LOG_DETAIL("Shift mode now %d", s.mode);
    g_lastLogged = s;
    LOG_DETAIL("Gearbox: mode %d | wanted %s | car gear %s | drivetrain %s -> %s pending %d | clutch state %d (%.2f, "
               "driver %.2f, pedal %.2f) | grind %d | lever %s",
               s.mode, GearName(s.wanted), GearName(s.carGear), GearName(s.current), GearName(s.target), s.pending,
               s.clutchState, dt ? ReadAt<float>(dt, gb::drivetrain::kClutch) : -1.0f,
               ReadAt<float>(drv, gb::driver::kClutch), pedal, s.grinding, lever < 0 ? "-" : GearName(lever));
}

int ClampToTopGear(const unsigned char* car, int gear) {
    const uintptr_t stats = ReadAt<uintptr_t>(car, gb::car::kStats);
    if (stats && IsReadable(stats + gb::car::kTopGear, 4)) {
        const int top = *reinterpret_cast<const int*>(stats + gb::car::kTopGear) + 1;
        if (gear > top) gear = top;  // 6th on a 5-speed car = 5th
    }
    return gear;
}


float GameSeconds() {
    return static_cast<float>(*reinterpret_cast<const int*>(gb::kGameClock)) * gb::kGameClockScale;
}

// ---- Missed-clutch bog ---------------------------------------------------------------------------
// The power factor (engine+0x80) is written only inside Drivetrain_UpdateClutchState, once per physics step, so the
// plugin scales it right after that function returns (hooked through its vtable slot).

struct Bog {
    unsigned char* dt = nullptr;  // player's drivetrain; nullptr = no bog
    float start = 0;              // game seconds
} g_bog;

bool g_bogLoggedFirstStep = false;

// Power multiplier for the bog at game time `now`, or 1 when it's over.
float BogFactor(float now) {
    const float t = now - g_bog.start;
    if (t < 0) return 1.0f;  // clock reset (new race)
    if (t < g_set.bogHold) return g_set.bogPower;
    if (g_set.bogRecover <= 0 || t >= g_set.bogHold + g_set.bogRecover) return 1.0f;
    const float r = (t - g_set.bogHold) / g_set.bogRecover;
    return g_set.bogPower + (1.0f - g_set.bogPower) * r;
}

typedef void(__fastcall* DrivetrainUpdateFn)(void* dt);

void __fastcall Hook_DrivetrainUpdate(void* dtv) {
    reinterpret_cast<DrivetrainUpdateFn>(gb::kDrivetrainUpdate)(dtv);
    unsigned char* dt = static_cast<unsigned char*>(dtv);
    if (!g_bog.dt || dt != g_bog.dt) return;  // every car runs through here; only the player's bog matters
    const float factor = BogFactor(GameSeconds());
    if (factor >= 1.0f) {
        g_bog.dt = nullptr;
        return;
    }
    const uintptr_t engine = ReadAt<uintptr_t>(dt, gb::drivetrain::kEngine);
    if (!engine || !IsReadable(engine + gb::kEnginePowerFactor, 4)) return;
    float* power = reinterpret_cast<float*>(engine + gb::kEnginePowerFactor);
    if (!g_bogLoggedFirstStep) {
        g_bogLoggedFirstStep = true;
        LOG_DETAIL("Bog: first physics step, power factor %.2f x %.2f", *power, factor);
    }
    *power *= factor;
}

// One gear change: from the frame the lever's gear differs from the engaged gear until the drivetrain has it.
struct Shift {
    bool active = false;
    int from = 0, to = 0;
    float maxTravel = 0;  // deepest clutch pedal travel seen during the shift
    DWORD startTick = 0;
    bool stuck = false;   // took too long: the clutch is back on the pedal
} g_shift;
const DWORD kShiftTimeout = 1500;  // ms

// Mode 3 with the shifter in use. The game refuses a gear request unless the clutch is fully disengaged (state 1), with
// a 0.45 s power cut that a per-frame request keeps renewing, so the clutch is held open during a shift and the gear is
// requested only at state 1.
void ClutchModeFrame(unsigned char* drv, const unsigned char* car, unsigned char* dt, int lever, float travel,
                     float pedal) {
    const int target = ClampToTopGear(car, lever);
    const int current = ReadAt<int>(dt, gb::drivetrain::kCurrent);
    float clutch = g_clutchActive ? pedal : 1.0f;
    int wanted = target;
    if (target != current) {
        if (!g_shift.active || g_shift.to != target) {
            if (!g_shift.active) {
                g_shift.from = current;
                g_shift.maxTravel = 0;
            }
            g_shift.startTick = GetTickCount();
            g_shift.stuck = false;
            g_shift.active = true;
            g_shift.to = target;
        }
        if (travel > g_shift.maxTravel) g_shift.maxTravel = travel;
        if (!g_shift.stuck && GetTickCount() - g_shift.startTick > kShiftTimeout) {
            g_shift.stuck = true;
            LOG_DETAIL("Shift %s -> %s did not complete in %lu ms: clutch back on the pedal (clutch state %d)",
                       GearName(g_shift.from), GearName(target), kShiftTimeout,
                       ReadAt<int>(dt, gb::drivetrain::kClutchState));
        }
        if (!g_shift.stuck) {
            clutch = 0.0f;
            // A request while the clutch is still engaged would be refused (and start the game's own grind), so ask
            // only once the drivetrain has seen the open clutch (it reads driver+0x2F4 at the start of the next
            // driver update).
            if (ReadAt<int>(dt, gb::drivetrain::kClutchState) != 1) wanted = current;
        }
    } else if (g_shift.active) {
        g_shift.active = false;
        const bool usedClutch = !g_clutchActive || g_shift.maxTravel >= g_set.clutchThreshold;
        const bool punish = !usedClutch && target != gb::kGearNeutral && g_set.bogPower < 1.0f &&
                            g_set.bogHold + g_set.bogRecover > 0;
        if (punish) {
            g_bog.start = GameSeconds();
            g_bog.dt = dt;
            g_bogLoggedFirstStep = false;
        }
        LOG_DETAIL("Shift %s -> %s in %lu ms: clutch pedal %d%%%s", GearName(g_shift.from), GearName(target),
                   GetTickCount() - g_shift.startTick, static_cast<int>(g_shift.maxTravel * 100 + 0.5f),
                   punish ? " -> missed clutch, bog" : usedClutch ? "" : " (into neutral: no penalty)");
    }
    memcpy(drv + gb::driver::kClutch, &clutch, sizeof(clutch));
    memcpy(drv + gb::driver::kWantedGear, &wanted, sizeof(wanted));
}

void OnDriverFrame(unsigned char* drv) {
    if (!drv || !IsReadable(reinterpret_cast<uintptr_t>(drv), gb::driver::kClutch + 4)) return;
    if (!IsPlayerOneDriver(drv)) return;
    unsigned char* car = reinterpret_cast<unsigned char*>(ReadAt<uintptr_t>(drv, gb::driver::kCar));
    if (!car || !IsReadable(reinterpret_cast<uintptr_t>(car), gb::car::kGear + 4)) return;
    unsigned char* dt = nullptr;
    const uintptr_t parts = ReadAt<uintptr_t>(car, gb::car::kParts);
    if (parts && IsReadable(parts + gb::car::kDrivetrain, 4)) {
        dt = reinterpret_cast<unsigned char*>(*reinterpret_cast<const uintptr_t*>(parts + gb::car::kDrivetrain));
        if (dt && !IsReadable(reinterpret_cast<uintptr_t>(dt), gb::drivetrain::kMode + 4)) dt = nullptr;
    }

    int lever = -1;
    float pedal = -1.0f;
    if (PollInput()) {
        LogInputChanges();
        lever = LeverGear();
        if (!g_shifterActive && lever != gb::kGearNeutral) {
            g_shifterActive = true;
            Log("Shifter seen (gear %s): the plugin now sets the wanted gear.", GearName(lever));
        }
        const float travel = PedalTravel(AxisValue(g_state, g_clutchAxis));
        pedal = ClutchEngagement(travel);

        const int mode = ReadAt<int>(drv, gb::driver::kShiftMode);
        // Start line: the game holds neutral (driver+0x2B4) so the engine can be revved; only the clutch passes through
        // until GO (writing the lever's gear fights the hold).
        static bool wasHeld = false;
        const bool held = drv[gb::driver::kStartHold] != 0;
        if (held != wasHeld) {
            LOG_DETAIL("Start hold %s", held ? "on (game keeps neutral)" : "off (GO: the lever decides)");
            wasHeld = held;
        }
        if (held) {
            g_shift.active = false;
            if (mode == gb::kModeClutch && g_clutchActive) memcpy(drv + gb::driver::kClutch, &pedal, sizeof(pedal));
        } else if (mode == gb::kModeClutch && g_shifterActive && dt) {
            ClutchModeFrame(drv, car, dt, lever, travel, pedal);
        } else if (mode == gb::kModeClutch || mode == gb::kModeClutchShifting ||
                   (!g_set.clutchMode && mode == gb::kModeManual)) {
            // Mode 4 (the game's own N/R shift), no shifter seen yet, or no clutch mode.
            if (mode != gb::kModeManual && g_clutchActive) memcpy(drv + gb::driver::kClutch, &pedal, sizeof(pedal));
            if (g_shifterActive) {
                const int wanted = ClampToTopGear(car, lever);
                memcpy(drv + gb::driver::kWantedGear, &wanted, sizeof(wanted));
            }
        }
    }
    LogGearState(drv, dt, lever, pedal);
}

// ---- Pedals (BrakeOnlyBrakes) ----------------------------------------------------------------
//
// RealDriver_MapPedals (CALL 0x0041E332) has the game's arcade reverse: brake hard below 0.5 m/s and the brake becomes
// the throttle with wanted gear R; throttle with wanted gear R asks for 1st. That fights the lever. While the shifter
// sets the gear, the hook hides the brake pedal from that code, keeps the lever's wanted gear and puts the brake back
// on as a plain brake. The race-finished autopilot (throttle 0, brake 0.5) is left alone.

typedef void(__thiscall* MapPedalsFn)(void* driver, float dt);

bool ShifterOwnsGear(const unsigned char* drv) {
    if (!g_shifterActive) return false;
    const int mode = ReadAt<int>(drv, gb::driver::kShiftMode);
    return mode == gb::kModeClutch || mode == gb::kModeClutchShifting || (!g_set.clutchMode && mode == gb::kModeManual);
}

// Replaces the CALL to RealDriver_MapPedals in the driver update (ECX = driver, one float on the stack, RET 4).
void __fastcall Hook_MapPedals(void* drvPtr, void* /*edx*/, float dt) {
    unsigned char* drv = static_cast<unsigned char*>(drvPtr);
    const MapPedalsFn original = reinterpret_cast<MapPedalsFn>(gb::kMapPedals);
    if (!g_set.brakeOnlyBrakes || !drv || !IsReadable(reinterpret_cast<uintptr_t>(drv), gb::driver::kClutch + 4) ||
        !ShifterOwnsGear(drv) || !IsPlayerOneDriver(drv)) {
        original(drvPtr, dt);
        return;
    }
    const float brake = ReadAt<float>(drv, gb::driver::kBrakeIn);
    const int wanted = ReadAt<int>(drv, gb::driver::kWantedGear);
    const float zero = 0.0f;
    memcpy(drv + gb::driver::kBrakeIn, &zero, sizeof(zero));
    drv[gb::driver::kReversing] = 0;
    original(drvPtr, dt);
    memcpy(drv + gb::driver::kBrakeIn, &brake, sizeof(brake));
    memcpy(drv + gb::driver::kWantedGear, &wanted, sizeof(wanted));
    const bool autopilot = ReadAt<float>(drv, gb::driver::kThrottleOut) == 0.0f &&
                           ReadAt<float>(drv, gb::driver::kBrakeOut) == 0.5f;
    if (!autopilot) memcpy(drv + gb::driver::kBrakeOut, &brake, sizeof(brake));
    static bool logged = false;
    if (!logged && brake > 0.6f && ReadAt<int>(drv, gb::driver::kWantedGear) != gb::kGearReverse) {
        logged = true;
        LOG_DETAIL("Brake pedal %.0f%% with the shifter: brake only (the game's brake-to-reverse is off).", brake * 100);
    }
}

typedef void(__thiscall* DriverFrameTailFn)(void* driver, uint32_t arg);

// Replaces both CALLs to 0x00407400 at the end of the driver update (ECX = driver, one stack arg, RET 4).
void __fastcall Hook_DriverFrameTail(void* drv, void* /*edx*/, uint32_t arg) {
    OnDriverFrame(static_cast<unsigned char*>(drv));
    reinterpret_cast<DriverFrameTailFn>(gb::kDriverFrameTail)(drv, arg);
}

// ---- Shift mode ------------------------------------------------------------------------------

typedef uint32_t(__thiscall* SetShiftModeFn)(void* driver, int mode);

// Replaces RealDriver_SetShiftMode in the RealDriver vtable (thiscall(driver, mode) -> AL, RET 4). Every place that
// applies the transmission option goes through it: race start (several copies), drags (their inner RealDriver)
// and changing the option during a race.
uint32_t __fastcall Hook_SetShiftMode(void* drv, void* /*edx*/, int mode) {
    const bool drag = IsReadable(gb::kIsDragRace, 1) && *reinterpret_cast<const unsigned char*>(gb::kIsDragRace) == 1;
    int newMode = mode;
    if (g_set.clutchMode && mode == gb::kModeManual && (!drag || g_set.dragRaces)) newMode = gb::kModeClutch;
    LOG_DETAIL("Set shift mode %d%s for driver %p -> %d%s", mode, drag ? " (drag race)" : "", drv, newMode,
               newMode == gb::kModeClutch ? " (manual with clutch)" : "");
    g_lastLogged.mode = -99;
    return reinterpret_cast<SetShiftModeFn>(gb::kSetShiftMode)(drv, newMode);
}

// ---- Startup ---------------------------------------------------------------------------------

DWORD WINAPI WorkerThread(LPVOID) {
    if (!ExeMd5Ok() || !CodeMatches(kCodeChecks, sizeof(kCodeChecks) / sizeof(kCodeChecks[0]))) {
        Log("Not changing anything.");
        return 0;
    }
    LoadSettings();
    for (uintptr_t site : gb::kDriverFrameTailCalls) {
        if (!RedirectCall(site, gb::kDriverFrameTail, reinterpret_cast<const void*>(&Hook_DriverFrameTail))) {
            Log("STOP: could not hook the driver update CALL at 0x%08X: another plugin already hooked it.",
                static_cast<unsigned>(site));
            return 0;  // (the first one may be hooked; it only reads input and logs until the clutch mode is on)
        }
    }
    Log("Hooked the driver update (CALLs at 0x%08X and 0x%08X).", static_cast<unsigned>(gb::kDriverFrameTailCalls[0]),
        static_cast<unsigned>(gb::kDriverFrameTailCalls[1]));
    if (g_set.brakeOnlyBrakes) {
        if (RedirectCall(gb::kMapPedalsCalls[0], gb::kMapPedals, reinterpret_cast<const void*>(&Hook_MapPedals)))
            Log("Hooked RealDriver_MapPedals (CALL at 0x%08X): with the shifter the brake only brakes, the throttle "
                "drives in reverse too.", static_cast<unsigned>(gb::kMapPedalsCalls[0]));
        else
            Log("Could not hook RealDriver_MapPedals (CALL at 0x%08X): the game's brake-to-reverse stays.",
                static_cast<unsigned>(gb::kMapPedalsCalls[0]));
    }
    if (!PatchPointer(gb::kSetShiftModeVtblSlot, gb::kSetShiftMode, reinterpret_cast<uintptr_t>(&Hook_SetShiftMode))) {
        Log("STOP: could not hook RealDriver_SetShiftMode (vtable slot 0x%08X).",
            static_cast<unsigned>(gb::kSetShiftModeVtblSlot));
        return 0;
    }
    if (!PatchPointer(gb::kDrivetrainUpdateVtblSlot, gb::kDrivetrainUpdate,
                      reinterpret_cast<uintptr_t>(&Hook_DrivetrainUpdate))) {
        Log("Could not hook Drivetrain_UpdateClutchState (vtable slot 0x%08X): no missed-clutch bog.",
            static_cast<unsigned>(gb::kDrivetrainUpdateVtblSlot));
    } else {
        Log("Hooked Drivetrain_UpdateClutchState (vtable slot 0x%08X) for the missed-clutch bog.",
            static_cast<unsigned>(gb::kDrivetrainUpdateVtblSlot));
    }
    Log("Hooked RealDriver_SetShiftMode (vtable slot 0x%08X): Manual -> %s.",
        static_cast<unsigned>(gb::kSetShiftModeVtblSlot),
        g_set.clutchMode ? "manual with clutch (mode 3)" : "manual (mode 1, no clutch)");
    return 0;
}

void Init() {
    Log("==== H-Shifter started ====");
    // CryptoAPI must not run inside DllMain, so the MD5 check (and everything after it) runs on its own thread.
    HANDLE thread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
}

}  // namespace

void bf::StartHShifter() {
    g_module = bf::Module();  // DirectInput8Create's instance handle
    Init();
}
