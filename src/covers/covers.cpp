// Covers: DVD and magazine covers after the career.
//
//  - Archive: opening a cover in Career -> Magazines / DVDs copies the game's picture file (if this exact picture is
//    new) to BayviewForever\cover-archive\<profile>\<cover>\NNN.cover (oldest = 001). The save folder is never written.
//  - Browsing: in the viewer, UP = older version, DOWN = newer (only the viewer's picture buffer [0x00832DB4] and its
//    texture upload change). Counter = 2 puts "PHOTO 2/3  UP/DOWN" on its own line under "Back" (same line pushes
//    "Back" out of its frame; Counter = 1's spare text object sits above the screen).
//  - Earning: every race won counts for the car model driven; every MagazineEveryWins / DVDEveryWins wins earn the
//    least recently shot cover of that kind. Its marker shows only while driving that car (Showcase_IsOffered hook);
//    MagazineChallenge = 1 adds the magazine's stock timed run (put in by bf::CoversAfterRebuild, called by Endless
//    Career). Rachel re-sends the game's own SMS for the cover (stock records only, so saves stay mod-free).
//    Covers repeat endlessly (the next one earned is the one shot longest ago). Taking the photo (Showcase_MarkDone)
//    archives the old picture first. Photo saving is the game's own: answering its save screen from the mod crashed
//    the game. Pay: CashPerStar x the car's visual rating (stars), via the showcase's cash value for that one call.
// State per profile: cover-archive\<profile>\covers-state.txt. Input: the menu engine's pad bits ([[0x008384C4]+8]).
// Hooks: cover viewer Notify (vtable 0x0079ACF0); Career_ApplyRaceResult (0x0054C9DF, 0x0054CA43, JMP 0x0054CA30);
// Showcase_IsOffered (0x005334A1, 0x0053319B, 0x0052793C, JMP 0x00532AED); Showcase_MarkDone (0x00498EBF, 0x004CEEB2);
// photo pay (0x00541941).
// Before the career is finished every hook only passes through. Undo: [Features] Covers = 0.

#define BF_TAG "Covers"
#include "nfsu2.h"

#include <shlobj.h>

using namespace nfsu2;

namespace {

volatile LONG g_versionOk = 0;      // set by the MD5 thread; until then the hook only passes through
bool          g_hooksOk   = false;

// ---- Settings (scripts\BayviewForever.ini, [Covers.*] sections) ---------------------------

bool g_testBrowseAll = false;
bool g_detailedLog   = false;
int  g_magEvery      = 3;   // wins with one car per magazine cover (0 = never)
int  g_dvdEvery      = 10;  // wins with one car per DVD cover (0 = never)
int  g_maxWaiting    = 3;   // covers waiting per car at most
bool g_message       = true;  // Rachel's text when a cover is earned
bool g_magChallenge  = true;  // magazines: the stock timed run to the photo shoot first (needs EndlessCareer)
int  g_counter       = 2;   // viewer counter: 0 off, 1 spare text object (off-screen, y = -366), 2 after the "Back" label
int  g_cashPerStar   = 500; // post-game photo pay = this x the car's stars (at least 1); 0 = the game's pay

void LoadSettings() {
    char path[MAX_PATH];
    bf::IniPath(path);
    g_testBrowseAll = GetPrivateProfileIntA("Covers.Log", "TestBrowseAll",  // old place: [Covers.Viewer]
                                            GetPrivateProfileIntA("Covers.Viewer", "TestBrowseAll", 0, path), path) != 0;
    g_detailedLog   = GetPrivateProfileIntA("Covers.Log", "Detailed", 0, path) != 0;
    auto clampInt = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
    g_magEvery   = clampInt(static_cast<int>(GetPrivateProfileIntA("Covers.Earn", "MagazineEveryWins", 3, path)), 0, 1000);
    g_dvdEvery   = clampInt(static_cast<int>(GetPrivateProfileIntA("Covers.Earn", "DVDEveryWins", 10, path)), 0, 1000);
    g_maxWaiting = clampInt(static_cast<int>(GetPrivateProfileIntA("Covers.Earn", "MaxWaitingPerCar", 3, path)), 1, 30);
    g_message    = GetPrivateProfileIntA("Covers.Earn", "Message", 1, path) != 0;
    g_magChallenge = GetPrivateProfileIntA("Covers.Earn", "MagazineChallenge", 1, path) != 0;
    if (g_magChallenge && !bf::FeatureOn(bf::kEndlessCareer)) {
        Log("MagazineChallenge needs EndlessCareer = 1 (it puts the challenge on the map after Endless Career's "
            "rebuild); magazines go straight to the photo shoot.");
        g_magChallenge = false;
    }
    g_counter    = clampInt(static_cast<int>(GetPrivateProfileIntA("Covers.Viewer", "Counter", 2, path)), 0, 2);
    g_cashPerStar = clampInt(static_cast<int>(GetPrivateProfileIntA("Covers.Earn", "CashPerStar", 500, path)), 0, 3000);
    char pay[48];
    if (g_cashPerStar) snprintf(pay, sizeof(pay), "$%d x the car's stars", g_cashPerStar);
    else snprintf(pay, sizeof(pay), "the game's");
    Log("Settings %s: magazine every %d win(s) (%s), DVD every %d win(s), at most %d waiting per car, message %s; "
        "photo pay %s; viewer counter %d, test browse-all %s, detailed log %s.",
        bf::IniFound() ? "from BayviewForever.ini" : "(no BayviewForever.ini, defaults)", g_magEvery,
        g_magChallenge ? "timed run to the shoot first" : "straight to the shoot", g_dvdEvery, g_maxWaiting,
        g_message ? "on" : "off", pay, g_counter,
        g_testBrowseAll ? "ON" : "off", g_detailedLog ? "on" : "off");
}

// ---- Code checks ---------------------------------------------------------------------

const CodeCheck kCodeChecks[] = {
    // The cover viewer screen: vtable entry, prologue, messages it handles, return size
    {0x0079ACF0, {0x50, 0x5B, 0x56, 0x00}, 4, "vtable of the cover viewer [2] = Notify 0x00565B50"},
    {0x00565B50, {0x8B, 0x44, 0x24, 0x04, 0x3D, 0x64, 0xB3, 0x1A}, 8, "prologue of the cover viewer Notify"},
    {0x00565B74, {0x3D, 0x0B, 0x62, 0xF8, 0x35}, 5, "viewer: CMP EAX,0x35F8620B (open)"},
    {0x00565B7B, {0x3D, 0x19, 0x7A, 0x31, 0x42}, 5, "viewer: CMP EAX,0x42317A19 (picture loaded)"},
    {0x00565B86, {0xB9, 0xAC, 0x2D, 0x83, 0x00, 0xE8, 0x80, 0x2F}, 8, "viewer: MOV ECX,ScreenCap; CALL upload"},
    {0x00565B9F, {0xC6, 0x46, 0x51, 0x01}, 4, "viewer: screen+0x51 = 1 (picture shown)"},
    {0x00565BDA, {0xC6, 0x46, 0x50, 0x01}, 4, "viewer: screen+0x50 = 1 (picture buffer made)"},
    {0x00565BDE, {0x8B, 0x3D, 0xB4, 0x2D, 0x83, 0x00}, 6, "viewer: MOV EDI,[ScreenCap+8] (picture buffer)"},
    {0x00565CB6, {0x3D, 0xBA, 0x56, 0x83, 0xC9}, 5, "viewer: CMP EAX,0xC98356BA (every frame)"},
    {0x00565D81, {0xC2, 0x10, 0x00}, 3, "RET 0x10 of the cover viewer Notify"},
    // ScreenCap upload (buffer -> texture)
    {0x00488B10, {0x53, 0x56, 0x57, 0x8B, 0xD9, 0x8B, 0x4B, 0x0C}, 8, "prologue of the ScreenCap upload"},
    {0x00488B25, {0x8B, 0x73, 0x08}, 3, "upload copies from ScreenCap+8"},
    {0x00488B44, {0xC3}, 1, "RET of the ScreenCap upload"},
    // Which cover is shown: the gallery list stores showcase+0x28 in 0x00837FD8
    {0x0053DE90, {0x8B, 0x40, 0x28}, 3, "gallery: desc hash = showcase+0x28"},
    {0x0053DE93, {0xA3, 0xD8, 0x7F, 0x83, 0x00}, 5, "gallery: stores the desc hash in 0x00837FD8"},
    {0x00510BB2, {0x8A, 0x42, 0x24, 0x3C, 0x01}, 5, "Showcase_FormatPictureName: photo type at +0x24"},
    // Menu engine and its pads
    {0x00558EE0, {0xA1, 0xC4, 0x84, 0x83, 0x00}, 5, "MOV EAX,[FE manager 0x008384C4]"},
    {0x00558EE9, {0x8B, 0x48, 0x08}, 3, "FEng object = [FE manager+8]"},
    {0x00558C67, {0x8B, 0x85, 0xD4, 0x00, 0x00, 0x00}, 6, "FEng pad count at +0xD4"},
    {0x00558C91, {0x8B, 0x55, 0x08}, 3, "FEng pad table at +0x8"},
    {0x00558C96, {0x69, 0xC9, 0x88, 0x00, 0x00, 0x00}, 6, "pad entries are 0x88 bytes"},
    // FE_FindPackage (object list logging) and the save folder name
    {0x0052CEF0, {0xA1, 0xD0, 0x84, 0x83, 0x00, 0x85, 0xC0, 0x56}, 8, "prologue of FE_FindPackage"},
    {0x007F66A0, {0xF4, 0xE8, 0x78, 0x00}, 4, "pointer to \"NFS Underground 2\" (save folder name)"},
    // Race result: Career_ApplyRaceResult, its flags, its JMP thunk (the 2 CALL sites are checked by name)
    {0x0054C940, {0x57, 0x8B, 0xF9, 0x8A, 0x87, 0x25, 0x0D, 0x00}, 8, "Career_ApplyRaceResult reads +0xD25"},
    {0x0054C94D, {0x8A, 0x87, 0x28, 0x0D, 0x00, 0x00}, 6, "Career_ApplyRaceResult reads +0xD28"},
    {0x0054C99D, {0x8B, 0x87, 0x30, 0x87, 0x00, 0x00}, 6, "Career_ApplyRaceResult: race at +0x8730"},
    {0x0054C9AE, {0x5F, 0xC3}, 2, "plain RET of Career_ApplyRaceResult"},
    {0x0054CA30, {0xE9, 0x0B, 0xFF, 0xFF, 0xFF}, 5, "JMP Career_ApplyRaceResult (thunk)"},
    // Showcase_IsOffered: prologue, RET 4, the tail JMP site
    {0x00501150, {0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x8B, 0x48}, 8, "prologue of Showcase_IsOffered"},
    {0x00501176, {0xC2, 0x04, 0x00}, 3, "RET 4 of Showcase_IsOffered"},
    {0x00532AED, {0xE9, 0x5E, 0xE6, 0xFC, 0xFF}, 5, "JMP Showcase_IsOffered (FUN_00532A50)"},
    // Showcase_MarkDone: ECX = 0x0085EF50, key at this+8, tail JMP to a plain RET
    {0x0053368F, {0x8B, 0xD9}, 2, "Showcase_MarkDone: EBX = this"},
    {0x005336A9, {0x8B, 0x73, 0x08}, 3, "Showcase_MarkDone: key = this+8"},
    {0x0053373C, {0xE9, 0x5F, 0x5B, 0xFC, 0xFF}, 5, "Showcase_MarkDone ends with JMP 0x004F92A0"},
    {0x00498EBA, {0xB9, 0x50, 0xEF, 0x85, 0x00}, 5, "MOV ECX,0x0085EF50 before Showcase_MarkDone (auto photo)"},
    // Career_RebuildMapMarkers prologue, the current car slot hash
    {0x00533310, {0x83, 0xEC, 0x20, 0x56, 0x8B, 0xF1}, 6, "prologue of Career_RebuildMapMarkers"},
    {0x00528431, {0x8B, 0x89, 0x40, 0x87, 0x00, 0x00}, 6, "current car slot hash at g_Career+0x8740"},
    // Messages: the inbox at g_Career+0x5D20 and the game's two cover senders
    {0x0053326D, {0x8D, 0x8F, 0x20, 0x5D, 0x00, 0x00, 0xE8, 0x38}, 8, "LEA ECX,[career+0x5D20]; CALL DVD cover SMS"},
    {0x00533AF5, {0xB9, 0x60, 0x0A, 0x86, 0x00, 0xE8, 0xC1, 0xFD}, 8, "MOV ECX,inbox; CALL WorldChallenge_QueueUnlockSMS"},
    {0x005299B0, {0x51, 0xA1, 0x50, 0x84, 0x83, 0x00, 0x53, 0x33}, 8, "prologue of the DVD cover SMS sender"},
    {0x005299DB, {0x80, 0x7F, 0x02, 0x0E}, 4, "DVD SMS: unlock type 0x0E"},
    {0x005299E5, {0x39, 0x4F, 0x08}, 3, "DVD SMS: parameter at +8 = n"},
    {0x00529A34, {0xC2, 0x04, 0x00}, 3, "RET 4 of the DVD cover SMS sender"},
    {0x005338C0, {0x51, 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x10, 0x8B}, 8, "prologue of WorldChallenge_QueueUnlockSMS"},
    {0x005338C7, {0x8B, 0x47, 0x08}, 3, "challenge SMS hash at +8"},
    {0x00533929, {0x8B, 0x47, 0x0C}, 3, "challenge destination at +0xC"},
    {0x00533A35, {0xC2, 0x04, 0x00}, 3, "RET 4 of WorldChallenge_QueueUnlockSMS"},
    {0x00501310, {0x55, 0x8B, 0x6C, 0x24, 0x08, 0x85, 0xED, 0x75}, 8, "prologue of SMS_FindMessage"},
    {0x0050131D, {0xA1, 0x50, 0x84, 0x83, 0x00}, 5, "SMS count at 0x00838450"},
    {0x00501330, {0xA1, 0x54, 0x84, 0x83, 0x00}, 5, "SMS records at 0x00838454"},
    {0x00529605, {0x8B, 0x87, 0x00, 0x14, 0x00, 0x00, 0x3D, 0x00}, 8, "inbox count at +0x1400"},
    {0x0052960B, {0x3D, 0x00, 0x01, 0x00, 0x00}, 5, "inbox holds 0x100 entries"},
    {0x00529612, {0x8D, 0x0C, 0x80}, 3, "inbox entries are 0x14 bytes"},
    {0x0052963C, {0xFF, 0x87, 0x04, 0x14, 0x00, 0x00}, 6, "inbox unread count at +0x1404"},
    {0x00514DC6, {0x80, 0x78, 0x04, 0x01, 0x74, 0x34, 0xFF, 0x89}, 8, "reading a message: entry+4 = 1, unread - 1"},
    {0x00527E41, {0x8B, 0x15, 0x20, 0x84, 0x83, 0x00}, 6, "world challenge count at 0x00838420"},
    {0x00527E5B, {0x8B, 0x0D, 0x24, 0x84, 0x83, 0x00}, 6, "world challenges at 0x00838424"},
    // Photo pay (FUN_00541210): showcase cash + stars x the stage's per-star pay; its only CALL
    {0x0054193C, {0xB9, 0x40, 0xAD, 0x85, 0x00, 0xE8, 0xCA, 0xF8}, 8, "MOV ECX,g_Career; CALL photo pay"},
    {0x00541221, {0x0F, 0xBF, 0x7B, 0x26, 0xE8, 0xE6, 0x71, 0xFE}, 8, "photo pay: MOVSX EDI,[showcase+0x26]; CALL stars"},
    {0x0054122A, {0x8B, 0x86, 0xF8, 0x7A, 0x00, 0x00}, 6, "photo pay: current stage at +0x7AF8"},
    {0x00541239, {0x69, 0xC0, 0x04, 0x01, 0x00, 0x00, 0x8D, 0x84}, 8, "photo pay: stage record = +0x72D8 + n*0x104"},
    {0x0054124A, {0x8B, 0x80, 0x00, 0x01, 0x00, 0x00}, 6, "photo pay: stage data at record+0x100"},
    {0x00541254, {0x0F, 0xBF, 0x40, 0x04}, 4, "photo pay: per-star pay = short [data+4]"},
    {0x0054126A, {0xD8, 0xC9, 0xD8, 0x05, 0xC0, 0xBF, 0x79, 0x00}, 8, "photo pay: x stars + [0x0079BFC0]"},
    {0x0079BFC0, {0x0A, 0xD7, 0xA3, 0x3B}, 4, "0.005f"},
    {0x00541272, {0xE8, 0x5D, 0xAC, 0x21, 0x00}, 5, "photo pay: CALL _ftol (truncates)"},
    {0x0054127F, {0x8D, 0x0C, 0x38}, 3, "photo pay: total = bonus + showcase cash"},
    {0x005412C2, {0xC2, 0x04, 0x00}, 3, "RET 4 of the photo pay"},
    {0x00528410, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0}, 6, "prologue of Career_GetCurrentCarVisualRating"},
    {0x005284AD, {0xC3}, 1, "plain RET of Career_GetCurrentCarVisualRating"},
    // FE text and the game's language
    {0x005379C0, {0x56, 0x8B, 0x74, 0x24, 0x08, 0x85, 0xF6, 0x57}, 8, "prologue of FE_FindObject"},
    {0x00535FE0, {0x53, 0x56, 0x57, 0x8B, 0xF9, 0x8B, 0x47, 0x64}, 8, "prologue of FEString_SetText (obj+0x64 text)"},
    {0x00536046, {0xC2, 0x04, 0x00}, 3, "RET 4 of FEString_SetText"},
    {0x004FFA80, {0x8B, 0x54, 0x24, 0x04, 0xE8, 0x47, 0xFF, 0xFF}, 8, "prologue of the localized string lookup"},
};

// ---- Cover files (MC02 container, see docs/structs.md) ----------------------------------

const size_t   kHeaderSize = 0x24;                         // 0x1C header + 8 bytes part A
const size_t   kFileSize   = kHeaderSize + covers::kPictureBytes;
const uint32_t kMagic      = 0x4D433032;                   // "20CM" in the file

// Same checksum as the game's MC02_Checksum (0x006FE173): CRC-32 table (poly 0x04C11DB7, MSB first),
// initial value = the first 4 bytes inverted, no final padding.
uint32_t g_crcTable[256];

void InitCrcTable() {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i << 24;
        for (int k = 0; k < 8; k++) c = (c & 0x80000000u) ? (c << 1) ^ 0x04C11DB7u : (c << 1);
        g_crcTable[i] = c;
    }
}

uint32_t Mc02Checksum(const unsigned char* p, size_t n) {
    if (n < 4) return 0;
    uint32_t u = ~(static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 |
                   static_cast<uint32_t>(p[2]) << 8 | p[3]);
    for (size_t i = 4; i < n; i++) u = ((u << 8) | p[i]) ^ g_crcTable[u >> 24];
    return ~u;
}

// True if `d` (n bytes) is a complete, undamaged cover picture file.
bool ValidCover(const unsigned char* d, size_t n, const char** why) {
    *why = "";
    if (n != kFileSize) { *why = "wrong size"; return false; }
    if (ReadAt<uint32_t>(d, 0) != kMagic || ReadAt<uint32_t>(d, 4) != kFileSize || ReadAt<uint32_t>(d, 8) != 8 ||
        ReadAt<uint32_t>(d, 0xC) != covers::kPictureBytes) {
        *why = "not a cover picture (header)";
        return false;
    }
    if (Mc02Checksum(d + 0x1C, 8) != ReadAt<uint32_t>(d, 0x10) ||
        Mc02Checksum(d + kHeaderSize, covers::kPictureBytes) != ReadAt<uint32_t>(d, 0x14) ||
        Mc02Checksum(d, 0x18) != ReadAt<uint32_t>(d, 0x18)) {
        *why = "checksum mismatch";
        return false;
    }
    return true;
}

unsigned char g_file[kFileSize];  // one whole cover file (main thread only)

// Reads a whole file of exactly kFileSize bytes into g_file.
bool ReadCoverFile(const char* path, const char** why) {
    *why = "";
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) { *why = "cannot open"; return false; }
    DWORD size = GetFileSize(f, nullptr), got = 0;
    bool ok = size == kFileSize && ReadFile(f, g_file, kFileSize, &got, nullptr) && got == kFileSize;
    CloseHandle(f);
    if (!ok) { *why = size == kFileSize ? "read error" : "wrong size"; return false; }
    return ValidCover(g_file, kFileSize, why);
}

bool WriteWholeFile(const char* path, const void* a, size_t na, const void* b, size_t nb) {
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD w1 = 0, w2 = 0;
    bool ok = WriteFile(f, a, static_cast<DWORD>(na), &w1, nullptr) && w1 == na;
    if (ok && b) ok = WriteFile(f, b, static_cast<DWORD>(nb), &w2, nullptr) && w2 == nb;
    CloseHandle(f);
    if (!ok) DeleteFileA(path);
    return ok;
}

// ---- Paths and names ------------------------------------------------------------------

// Player 1's profile name; false if none is loaded or it can't be a folder name.
bool ProfileName(char out[33]) {
    memcpy(out, reinterpret_cast<const void*>(kPlayer1Profile), 32);
    out[32] = '\0';
    if (!out[0]) return false;
    for (const char* c = out; *c; c++)
        if (strchr("\\/:*?\"<>|", *c)) return false;
    return true;
}

// The game's save folder: CSIDL_LOCAL_APPDATA\NFS Underground 2 (FUN_004A9C40), no trailing backslash.
bool SaveFolder(char out[MAX_PATH]) {
    static char cached[MAX_PATH] = "";
    if (!cached[0]) {
        char base[MAX_PATH];
        const char* name = *reinterpret_cast<const char* const*>(covers::kSaveDirName);
        if (!name || !IsReadable(reinterpret_cast<uintptr_t>(name), 2)) return false;
        if (FAILED(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, base))) return false;
        snprintf(cached, sizeof(cached), "%s\\%s", base, name);
        Log("Save folder: %s", cached);
    }
    snprintf(out, MAX_PATH, "%s", cached);
    return true;
}

// The showcase table (g_GShowcases); count 0 if it isn't loaded.
const int kMaxShowcases = 64;
int ShowcaseTable(const unsigned char** table) {
    int count = *reinterpret_cast<const int32_t*>(ev::kShowcaseCount);
    uintptr_t t = *reinterpret_cast<const uintptr_t*>(ev::kShowcaseTablePtr);
    if (count <= 0 || count > kMaxShowcases || !t || !IsReadable(t, count * ev::kShowcaseSize)) return 0;
    *table = reinterpret_cast<const unsigned char*>(t);
    return count;
}

// 1 = magazine, 2 = DVD, 0 = neither.
int CoverKind(const unsigned char* showcase) {
    unsigned char type = showcase[covers::showcase::kPhotoType];
    return (type == 1 || type == 3) ? 1 : (type == 2 || type == 4) ? 2 : 0;
}

// "Magazine 7" / "DVD 3" for showcase i, counted like Showcase_FormatPictureName (0x00510B80).
bool CoverNameAt(int index, char out[32]) {
    const unsigned char* table = nullptr;
    int count = ShowcaseTable(&table);
    if (index < 0 || index >= count) return false;
    int n[3] = {0, 0, 0};
    for (int i = 0; i <= index; i++) n[CoverKind(table + i * ev::kShowcaseSize)]++;
    int kind = CoverKind(table + index * ev::kShowcaseSize);
    if (!kind) return false;
    snprintf(out, 32, kind == 1 ? "Magazine %d" : "DVD %d", n[kind]);
    return true;
}

int ShowcaseIndexByField(size_t offset, uint32_t value) {
    const unsigned char* table = nullptr;
    int count = ShowcaseTable(&table);
    for (int i = 0; i < count; i++)
        if (ReadAt<uint32_t>(table + i * ev::kShowcaseSize, offset) == value) return i;
    return -1;
}

int ShowcaseIndexOf(const void* showcase) {
    const unsigned char* table = nullptr;
    int count = ShowcaseTable(&table);
    uintptr_t p = reinterpret_cast<uintptr_t>(showcase), t = reinterpret_cast<uintptr_t>(table);
    if (!count || p < t || p >= t + count * ev::kShowcaseSize || (p - t) % ev::kShowcaseSize) return -1;
    return static_cast<int>((p - t) / ev::kShowcaseSize);
}

// The cover being viewed (desc hash from 0x00837FD8).
bool CoverName(uint32_t desc, char out[32], int* showcaseIndex) {
    *showcaseIndex = ShowcaseIndexByField(covers::showcase::kDesc, desc);
    return CoverNameAt(*showcaseIndex, out);
}

// <game>\BayviewForever\cover-archive\<profile>\<cover>\ (created on demand).
bool ArchiveDir(const char* profile, const char* cover, char out[MAX_PATH]) {
    char dir[MAX_PATH];
    GetDataDir(dir);
    snprintf(out, MAX_PATH, "%scover-archive", dir);
    CreateDirectoryA(out, nullptr);
    size_t n = strlen(out);
    snprintf(out + n, MAX_PATH - n, "\\%s", profile);
    CreateDirectoryA(out, nullptr);
    n = strlen(out);
    snprintf(out + n, MAX_PATH - n, "\\%s", cover);
    CreateDirectoryA(out, nullptr);
    n = strlen(out);
    snprintf(out + n, MAX_PATH - n, "\\");
    return GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES;
}

// ---- Versions of the cover being viewed ---------------------------------------------------

const int kMaxVersions = 512;
struct Version {
    char path[MAX_PATH];
    char label[48];  // for the log, e.g. "archive 002" or "test: DVD 3"
};
Version g_versions[kMaxVersions];
int     g_versionCount = 0;
int     g_currentPos   = -1;  // the version the game itself shows
int     g_pos          = -1;  // the version on screen

// Numbers of the archived versions (NNN.cover), sorted oldest first.
int ListArchive(const char* dir, int* numbers, int max) {
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s*.cover", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    int n = 0;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            int num = 0;
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && sscanf(fd.cFileName, "%d.cover", &num) == 1 &&
                num > 0 && n < max)
                numbers[n++] = num;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    for (int i = 1; i < n; i++)  // insertion sort
        for (int j = i; j > 0 && numbers[j - 1] > numbers[j]; j--) {
            int t = numbers[j]; numbers[j] = numbers[j - 1]; numbers[j - 1] = t;
        }
    return n;
}

void AddVersion(const char* path, const char* label) {
    if (g_versionCount >= kMaxVersions) return;
    snprintf(g_versions[g_versionCount].path, MAX_PATH, "%s", path);
    snprintf(g_versions[g_versionCount].label, sizeof(g_versions[0].label), "%s", label);
    g_versionCount++;
}

// Reads the game's current picture of `cover`, archives it if it is new, and builds the version list.
void PrepareCover(const char* profile, const char* cover) {
    g_versionCount = 0;
    g_currentPos = g_pos = -1;
    char save[MAX_PATH], gameFile[MAX_PATH], dir[MAX_PATH];
    if (!SaveFolder(save)) {
        Log("  Could not find the save folder; browsing is off for this cover.");
        return;
    }
    snprintf(gameFile, sizeof(gameFile), "%s\\%s %s\\%s %s", save, profile, cover, profile, cover);
    if (!ArchiveDir(profile, cover, dir)) {
        Log("  Could not create the archive folder %s; browsing is off for this cover.", dir);
        return;
    }

    const char* why;
    bool haveGame = ReadCoverFile(gameFile, &why);
    if (!haveGame) Log("  The game's picture %s: %s (not archived).", gameFile, why);

    static int numbers[kMaxVersions];
    int n = ListArchive(dir, numbers, kMaxVersions);
    int match = -1;
    for (int i = 0; i < n; i++) {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%s%03d.cover", dir, numbers[i]);
        AddVersion(path, "");
        snprintf(g_versions[g_versionCount - 1].label, sizeof(g_versions[0].label), "archive %03d", numbers[i]);
        if (haveGame && match < 0) {  // same picture = same three checksums
            unsigned char head[kHeaderSize];
            HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            DWORD got = 0;
            if (f != INVALID_HANDLE_VALUE) {
                if (ReadFile(f, head, kHeaderSize, &got, nullptr) && got == kHeaderSize &&
                    memcmp(head + 0x10, g_file + 0x10, 12) == 0)
                    match = g_versionCount - 1;
                CloseHandle(f);
            }
        }
    }
    if (haveGame && match < 0) {
        int num = n ? numbers[n - 1] + 1 : 1;
        char path[MAX_PATH], label[48];
        snprintf(path, sizeof(path), "%s%03d.cover", dir, num);
        if (WriteWholeFile(path, g_file, kFileSize, nullptr, 0)) {
            snprintf(label, sizeof(label), "archive %03d", num);
            AddVersion(path, label);
            match = g_versionCount - 1;
            Log("  Archived the current picture as %s.", path);
        } else {
            Log("  COULD NOT write %s (error %lu).", path, GetLastError());
        }
    }
    g_currentPos = g_pos = match;

    if (g_testBrowseAll) {  // test switch: every other cover picture of this profile, straight from the save folder
        char pattern[MAX_PATH], prefix[48];
        snprintf(pattern, sizeof(pattern), "%s\\%s *", save, profile);
        snprintf(prefix, sizeof(prefix), "%s ", profile);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(pattern, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                const char* rest = fd.cFileName + strlen(prefix);
                if (strncmp(fd.cFileName, prefix, strlen(prefix)) != 0 || strcmp(rest, cover) == 0) continue;
                if (strncmp(rest, "Magazine ", 9) != 0 && strncmp(rest, "DVD ", 4) != 0) continue;
                char path[MAX_PATH], label[48];
                snprintf(path, sizeof(path), "%s\\%s\\%s", save, fd.cFileName, fd.cFileName);
                snprintf(label, sizeof(label), "test: %s", rest);
                AddVersion(path, label);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
    Log("  %d version(s) to browse%s; the game shows %s.", g_versionCount, g_testBrowseAll ? " (incl. test pictures)" : "",
        g_currentPos >= 0 ? g_versions[g_currentPos].label : "an unarchived picture");
}

// Puts version `i` on screen: file -> the viewer's picture buffer -> texture (the game's own upload).
bool ShowVersion(int i) {
    const char* why;
    if (!ReadCoverFile(g_versions[i].path, &why)) {
        Log("  Can't show %s (%s): %s.", g_versions[i].label, g_versions[i].path, why);
        return false;
    }
    void* buffer = *reinterpret_cast<void* const*>(covers::kScreenCap + covers::kCapBuffer);
    uintptr_t texture = *reinterpret_cast<const uintptr_t*>(covers::kScreenCap + covers::kCapTexture);
    if (!buffer || !texture || !IsReadable(reinterpret_cast<uintptr_t>(buffer), covers::kPictureBytes)) {
        Log("  Can't show %s: the viewer's picture buffer is gone.", g_versions[i].label);
        return false;
    }
    memcpy(buffer, g_file + kHeaderSize, covers::kPictureBytes);
    using UploadFn = void(__fastcall*)(uintptr_t, void*);  // thiscall(screenCap), plain RET
    reinterpret_cast<UploadFn>(covers::kCapUpload)(covers::kScreenCap, nullptr);
    return true;
}

// ---- Input: the menu engine's pad bits ---------------------------------------------------

const int kBitUp = 0, kBitDown = 1;  // FE pad bits (direction table 0x007F7CFC), as in the Virtual Garage
uint32_t g_lastBits[8];
bool     g_padsPrimed = false;

// -1 = UP (older), +1 = DOWN (newer), 0 = nothing new this frame.
int ReadPadStep() {
    uintptr_t mgr = *reinterpret_cast<const uintptr_t*>(covers::kFEManager);
    if (!mgr || !IsReadable(mgr + 8, 4)) return 0;
    uintptr_t feng = *reinterpret_cast<const uintptr_t*>(mgr + 8);
    if (!feng || !IsReadable(feng, fe::kPadCount + 4)) return 0;
    uint32_t count = *reinterpret_cast<const uint32_t*>(feng + fe::kPadCount);
    uintptr_t pads = *reinterpret_cast<const uintptr_t*>(feng + fe::kPads);
    if (count > 8) count = 8;
    if (!pads || !count || !IsReadable(pads, count * fe::kPadEntrySize)) return 0;
    int step = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t cur = *reinterpret_cast<const uint32_t*>(pads + i * fe::kPadEntrySize + fe::kPadCur);
        uint32_t pressed = g_padsPrimed ? (cur & ~g_lastBits[i]) : 0;
        g_lastBits[i] = cur;
        if (pressed & (1u << kBitUp)) step = -1;
        if (pressed & (1u << kBitDown)) step = 1;
        if (pressed && g_detailedLog) {
            for (int b = 0; b < fe::kNumBits; b++)
                if (pressed & (1u << b)) Log("  Viewer input: pad %u bit %d (%s)", i, b, fe::BitName(b));
        }
    }
    g_padsPrimed = true;
    return step;
}

// ---- FE object list (detailed log) ------------------------------------------------------

void DumpObjects(uintptr_t first, int depth, int& budget) {
    for (uintptr_t o = first; o && budget > 0; o = *reinterpret_cast<uintptr_t*>(o + 4)) {
        if (!IsReadable(o, 0x80)) {
            Log("    (unreadable object %08X)", static_cast<unsigned>(o));
            return;
        }
        budget--;
        const unsigned char* b = reinterpret_cast<const unsigned char*>(o);
        char hex[0x80 * 3 + 1];
        for (int i = 0; i < 0x80; i++) snprintf(hex + i * 3, 4, "%02X ", b[i]);
        uint32_t type = ReadAt<uint32_t>(b, 0x18);
        Log("    %*sobj %08X hash %08X type %u flags %08X img %08X | %s", depth * 2, "", static_cast<unsigned>(o),
            static_cast<unsigned>(ReadAt<uint32_t>(b, 0x10)), static_cast<unsigned>(type),
            static_cast<unsigned>(ReadAt<uint32_t>(b, 0x1C)), static_cast<unsigned>(ReadAt<uint32_t>(b, 0x24)), hex);
        if (type == 5 && depth < 6) DumpObjects(ReadAt<uintptr_t>(b, 0x64), depth + 1, budget);
    }
}

void DumpPackageObjects(const char* name) {
    using FindFn = uintptr_t(__cdecl*)(const char*);
    uintptr_t pkg = reinterpret_cast<FindFn>(covers::kFEFindPackage)(name);
    if (!pkg || !IsReadable(pkg, 0x50)) {
        Log("  Object list of %s: package not found.", name);
        return;
    }
    Log("  Object list of %s (package %08X; address, name hash, type 1 image / 2 text / 5 group, flags, image):", name,
        static_cast<unsigned>(pkg));
    int budget = 200;
    DumpObjects(*reinterpret_cast<uintptr_t*>(pkg + 0x4C), 0, budget);
    Log("  End of object list (%d objects).", 200 - budget);
}

// ---- Earning covers (after the career) ----------------------------------------------------

// Career finished = current stage 6, or the game's own Career_IsFinished (same test as the Virtual Garage).
bool CareerFinished() {
    if (*reinterpret_cast<volatile int32_t*>(ev::kCurrentStage) >= 6) return true;
    uintptr_t stage5 = *reinterpret_cast<volatile uintptr_t*>(ev::kCareer + ev::kCareerStage5Data);
    if (!stage5 || !IsReadable(stage5 + 0x28, 4)) return false;
    using Fn = unsigned char(__fastcall*)(uintptr_t);
    return reinterpret_cast<Fn>(kCareerIsFinished)(ev::kCareer) != 0;
}

// Model hash (CARS folder name) of the career car being driven, 0 if unknown.
uint32_t CurrentCarModel() {
    uint32_t hash = *reinterpret_cast<const uint32_t*>(kCurrentSlotHash);
    for (int n = 0; n < kMaxSlots; n++) {
        const unsigned char* s = SlotPtr(n);
        if (ReadAt<uint32_t>(s, slot::kHash) == hash && s[slot::kOccupied]) return ReadAt<uint32_t>(s, slot::kCarModel);
    }
    return 0;
}

// Per-profile state, kept by showcase key (not index) so it survives a changed table.
struct State {
    char     profile[33];       // "" = nothing loaded
    int      winCount;
    uint32_t winModel[64];
    int      wins[64];
    int      coverCount;
    uint32_t coverKey[kMaxShowcases];
    uint32_t waitingFor[kMaxShowcases];  // car model, 0 = not waiting
    int      lastShot[kMaxShowcases];    // sequence number of the last post-game photo, 0 = never
    int      seq;
};
State g_state;

int StateCover(uint32_t key, bool add) {
    for (int i = 0; i < g_state.coverCount; i++)
        if (g_state.coverKey[i] == key) return i;
    if (!add || g_state.coverCount >= kMaxShowcases) return -1;
    int i = g_state.coverCount++;
    g_state.coverKey[i] = key;
    g_state.waitingFor[i] = 0;
    g_state.lastShot[i] = 0;
    return i;
}

bool StatePath(const char* profile, char out[MAX_PATH]) {
    char dir[MAX_PATH];
    GetDataDir(dir);
    snprintf(out, MAX_PATH, "%scover-archive", dir);
    CreateDirectoryA(out, nullptr);
    size_t n = strlen(out);
    snprintf(out + n, MAX_PATH - n, "\\%s", profile);
    CreateDirectoryA(out, nullptr);
    n = strlen(out);
    snprintf(out + n, MAX_PATH - n, "\\covers-state.txt");
    return true;
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
    fprintf(f, "; NFSU2 Covers state for profile \"%s\". Written by the plugin; edit only while the game is closed.\n",
            g_state.profile);
    fprintf(f, "seq %d\n", g_state.seq);
    for (int i = 0; i < g_state.winCount; i++)
        fprintf(f, "wins %08X %d ; %s\n", static_cast<unsigned>(g_state.winModel[i]), g_state.wins[i],
                CarName(g_state.winModel[i]));
    for (int i = 0; i < g_state.coverCount; i++) {
        char name[32] = "?";
        CoverNameAt(ShowcaseIndexByField(covers::showcase::kKey, g_state.coverKey[i]), name);
        if (g_state.waitingFor[i])
            fprintf(f, "waiting %08X %08X ; %s for the %s\n", static_cast<unsigned>(g_state.coverKey[i]),
                    static_cast<unsigned>(g_state.waitingFor[i]), name, CarName(g_state.waitingFor[i]));
        if (g_state.lastShot[i])
            fprintf(f, "shot %08X %d ; %s\n", static_cast<unsigned>(g_state.coverKey[i]), g_state.lastShot[i], name);
    }
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
        Log("Profile \"%s\": no covers-state.txt yet (no post-game wins counted).", profile);
        return;
    }
    while (fgets(line, sizeof(line), f)) {
        unsigned a = 0, b = 0;
        int n = 0;
        if (sscanf(line, "seq %d", &n) == 1) {
            g_state.seq = n;
        } else if (sscanf(line, "wins %x %d", &a, &n) == 2) {
            if (g_state.winCount < 64) {
                g_state.winModel[g_state.winCount] = a;
                g_state.wins[g_state.winCount++] = n;
            }
        } else if (sscanf(line, "waiting %x %x", &a, &b) == 2) {
            int i = StateCover(a, true);
            if (i >= 0) g_state.waitingFor[i] = b;
        } else if (sscanf(line, "shot %x %d", &a, &n) == 2) {
            int i = StateCover(a, true);
            if (i >= 0) g_state.lastShot[i] = n;
        }
    }
    fclose(f);
    int waiting = 0;
    for (int i = 0; i < g_state.coverCount; i++) waiting += g_state.waitingFor[i] != 0;
    Log("Profile \"%s\": loaded covers-state.txt (%d car(s) with wins, %d cover(s) waiting).", profile,
        g_state.winCount, waiting);
}

// Makes sure g_state belongs to the loaded profile. False if no usable profile is loaded.
bool EnsureState() {
    char profile[33];
    if (!ProfileName(profile)) return false;
    if (strcmp(profile, g_state.profile) != 0) LoadState(profile);
    return true;
}

bool UsesChallenge(const unsigned char* showcase);

// Earns one cover of `kind` (1 magazine, 2 DVD) for `model`. Returns its showcase index, or -1.
int Earn(int kind, uint32_t model) {
    const unsigned char* table = nullptr;
    int count = ShowcaseTable(&table);
    int waitingForCar = 0;
    for (int i = 0; i < g_state.coverCount; i++) waitingForCar += g_state.waitingFor[i] == model;
    if (waitingForCar >= g_maxWaiting) {
        Log("  The %s already has %d cover(s) waiting (MaxWaitingPerCar); no new %s.", CarName(model), waitingForCar,
            kind == 1 ? "magazine" : "DVD");
        return -1;
    }
    int best = -1, bestShot = 0;
    for (int i = 0; i < count; i++) {
        const unsigned char* s = table + i * ev::kShowcaseSize;
        if (CoverKind(s) != kind) continue;
        int k = StateCover(ReadAt<uint32_t>(s, covers::showcase::kKey), false);
        if (k >= 0 && g_state.waitingFor[k]) continue;
        int shot = k >= 0 ? g_state.lastShot[k] : 0;
        if (best < 0 || shot < bestShot) {
            best = i;
            bestShot = shot;
        }
    }
    if (best < 0) {
        Log("  Every %s cover is already waiting for some car; none earned.", kind == 1 ? "magazine" : "DVD");
        return -1;
    }
    int k = StateCover(ReadAt<uint32_t>(table + best * ev::kShowcaseSize, covers::showcase::kKey), true);
    if (k < 0) return -1;
    g_state.waitingFor[k] = model;
    char name[32] = "?";
    CoverNameAt(best, name);
    Log("  EARNED %s (%s) for the %s. Its %s shows on the map while you drive that car.", name,
        reinterpret_cast<const char*>(table + best * ev::kShowcaseSize), CarName(model),
        UsesChallenge(table + best * ev::kShowcaseSize) ? "challenge (timed run to the shoot)" : "marker");
    return best;
}

// ---- Rachel's text when a cover is earned (stock messages only) ------------------------------

// DVD n: the DVDUnlock message (type 0x0E) with parameter n = SMS_DVD_COVER_n.
uintptr_t FindDvdMessage(int n) {
    int count = *reinterpret_cast<const int32_t*>(covers::kSmsCount);
    uintptr_t t = *reinterpret_cast<const uintptr_t*>(covers::kSmsTablePtr);
    if (count <= 0 || count > 1000 || !t || !IsReadable(t, count * covers::kSmsSize)) return 0;
    for (int i = 0; i < count; i++) {
        const unsigned char* r = reinterpret_cast<const unsigned char*>(t + i * covers::kSmsSize);
        if (r[2] == 0x0E && ReadAt<int32_t>(r, 8) == n) return reinterpret_cast<uintptr_t>(r);
    }
    return 0;
}

// The magazine's world challenge (type 4, destination = the showcase key).
uintptr_t FindMagazineChallenge(uint32_t showcaseKey) {
    int count = *reinterpret_cast<const int32_t*>(covers::kWorldChallengeCount);
    uintptr_t t = *reinterpret_cast<const uintptr_t*>(covers::kWorldChallengesPtr);
    if (count <= 0 || count > 1000 || !t || !IsReadable(t, count * covers::challenge::kSize)) return 0;
    for (int i = 0; i < count; i++) {
        const unsigned char* c = reinterpret_cast<const unsigned char*>(t + i * covers::challenge::kSize);
        if (c[covers::challenge::kType] == 4 && ReadAt<uint32_t>(c, covers::challenge::kDest) == showcaseKey)
            return reinterpret_cast<uintptr_t>(c);
    }
    return 0;
}

// Sends Rachel's stock message for the cover at showcase index `index` (called on the main thread, at race end).
void SendCoverMessage(int index) {
    const unsigned char* table = nullptr;
    int count = ShowcaseTable(&table);
    if (index < 0 || index >= count) return;
    const unsigned char* show = table + index * ev::kShowcaseSize;
    char name[32] = "?";
    CoverNameAt(index, name);
    uintptr_t record = 0;
    if (CoverKind(show) == 2) {
        int n = 0;
        if (sscanf(name, "DVD %d", &n) != 1) return;
        record = FindDvdMessage(n);
        if (!record) {
            Log("  No message for %s (no DVDUnlock message with parameter %d).", name, n);
            return;
        }
        bool was = InboxRemove(record);
        using Fn = void(__fastcall*)(uintptr_t, void*, int);  // thiscall(inbox, n), RET 4
        reinterpret_cast<Fn>(covers::kSmsSendDvdCover)(covers::kInbox, nullptr, n);
        Log("  Rachel's message for %s (SMS_DVD_COVER_%d)%s: %s.", name, n, was ? ", re-sent" : "",
            InboxHas(record) ? "in the inbox" : "NOT delivered (inbox full?)");
    } else {
        uintptr_t chal = FindMagazineChallenge(ReadAt<uint32_t>(show, covers::showcase::kKey));
        if (!chal) {
            Log("  No message for %s (no world challenge leads to it).", name);
            return;
        }
        using FindFn = uintptr_t(__cdecl*)(uint32_t);
        record = reinterpret_cast<FindFn>(covers::kSmsFindMessage)(ReadAt<uint32_t>(
            reinterpret_cast<const unsigned char*>(chal), covers::challenge::kSms));
        if (!record) {
            Log("  No message for %s (its challenge's message is missing).", name);
            return;
        }
        bool was = InboxRemove(record);
        using Fn = void(__fastcall*)(uintptr_t, void*, uintptr_t);  // thiscall(inbox, challenge), RET 4
        reinterpret_cast<Fn>(covers::kChallengeQueueSms)(covers::kInbox, nullptr, chal);
        Log("  Rachel's message for %s (magazine photographer)%s: %s.", name, was ? ", re-sent" : "",
            InboxHas(record) ? "in the inbox" : "NOT delivered (inbox full?)");
    }
}

void OnRaceWon(const unsigned char* race) {
    if (!CareerFinished()) {
        if (g_detailedLog) Log("Race won before the end of the career: not counted.");
        return;
    }
    if (!EnsureState()) return;
    uint32_t model = CurrentCarModel();
    if (!model) {
        Log("Race won, but the car being driven is unknown: not counted.");
        return;
    }
    int w = 0;
    while (w < g_state.winCount && g_state.winModel[w] != model) w++;
    if (w == g_state.winCount) {
        if (w >= 64) return;
        g_state.winModel[w] = model;
        g_state.wins[w] = 0;
        g_state.winCount++;
    }
    int wins = ++g_state.wins[w];
    char raceName[48] = "?";
    if (race && IsReadable(reinterpret_cast<uintptr_t>(race) + 8, 4))
        snprintf(raceName, sizeof(raceName), "%08X", static_cast<unsigned>(ReadAt<uint32_t>(race, 8)));
    Log("Race %s won with the %s: %d post-game win(s) with this car.", raceName, CarName(model), wins);
    int dvd = (g_dvdEvery > 0 && wins % g_dvdEvery == 0) ? Earn(2, model) : -1;
    int mag = (g_magEvery > 0 && wins % g_magEvery == 0) ? Earn(1, model) : -1;
    SaveState();
    if (dvd >= 0 || mag >= 0) {
        using Fn = void(__fastcall*)(uintptr_t);
        reinterpret_cast<Fn>(covers::kRebuildMapMarkers)(ev::kCareer);
        Log("  Map markers rebuilt.");
        if (g_message) {
            if (dvd >= 0) SendCoverMessage(dvd);
            if (mag >= 0) SendCoverMessage(mag);
        }
    }
}

// Replaces the calls (and the JMP thunk) to Career_ApplyRaceResult (fastcall(g_Career), plain RET).
void __fastcall Hook_ApplyRaceResult(uintptr_t career) {
    bool armed = false;
    const unsigned char* race = nullptr;
    if (g_versionOk && career == ev::kCareer) {
        const unsigned char* c = reinterpret_cast<const unsigned char*>(career);
        armed = c[covers::kCareerRaceActive] && !c[covers::kCareerResultApplied];
        race = *reinterpret_cast<const unsigned char* const*>(career + covers::kCareerCurrentRace);
    }
    using Fn = void(__fastcall*)(uintptr_t);
    reinterpret_cast<Fn>(covers::kApplyRaceResult)(career);
    if (armed && reinterpret_cast<const unsigned char*>(career)[covers::kCareerResultApplied]) OnRaceWon(race);
}

// With MagazineChallenge on, a waiting magazine's photo shoot only opens through its stock timed challenge.
bool UsesChallenge(const unsigned char* showcase) {
    return g_magChallenge && CoverKind(showcase) == 1 &&
           FindMagazineChallenge(ReadAt<uint32_t>(showcase, covers::showcase::kKey)) != 0;
}

// Replaces the calls (and the tail JMP) to Showcase_IsOffered (thiscall(obj, showcase), RET 4).
// After the career: a cover waiting for the car being driven is offered (a magazine with MagazineChallenge only while
// its challenge runs, i.e. as the challenge's destination); everything else is the game's answer.
bool __fastcall Hook_IsOffered(void* obj, void* edx, const unsigned char* showcase) {
    if (g_versionOk && CareerFinished() && EnsureState()) {
        int i = ShowcaseIndexOf(showcase);
        if (i >= 0) {
            uint32_t key = ReadAt<uint32_t>(showcase, covers::showcase::kKey);
            uintptr_t running = uniques::RunningChallenge();
            if (running && ReadAt<uint32_t>(reinterpret_cast<const unsigned char*>(running), covers::challenge::kDest) == key)
                return true;  // the destination of the challenge being run
            int k = StateCover(key, false);
            if (k >= 0 && g_state.waitingFor[k] && g_state.waitingFor[k] == CurrentCarModel() && !UsesChallenge(showcase))
                return true;
        }
    }
    using Fn = unsigned char(__fastcall*)(void*, void*, const unsigned char*);
    return reinterpret_cast<Fn>(covers::kShowcaseIsOffered)(obj, edx, showcase) != 0;
}

// Replaces the CALL to the photo pay (thiscall(g_Career, showcase), RET 4). After the career with CashPerStar set:
// pay = CashPerStar x the car's stars (at least 1 star). The game's own code pays it and shows the total on the
// reward screen; only the showcase's cash value is changed for the call (minus the game's own star bonus).
void __fastcall Hook_PhotoCash(void* career, void* edx, unsigned char* showcase) {
    using Fn = void(__fastcall*)(void*, void*, unsigned char*);
    if (!g_versionOk || reinterpret_cast<uintptr_t>(career) != ev::kCareer || !showcase || !g_cashPerStar ||
        !CareerFinished()) {
        reinterpret_cast<Fn>(covers::kPhotoCash)(career, edx, showcase);
        return;
    }
    using RatingFn = float(__fastcall*)(uintptr_t);
    float stars = reinterpret_cast<RatingFn>(covers::kVisualRating)(ev::kCareer);
    int stage = *reinterpret_cast<const int32_t*>(ev::kCurrentStage);
    int perStar = 0;
    if (stage >= 0 && stage < ev::kNumStageRecords) {  // the game's star bonus, computed as it does
        uintptr_t data = *reinterpret_cast<const uintptr_t*>(ev::kStageRecords + stage * ev::kStageRecordSize +
                                                              covers::kStageDataPtr);
        if (data && IsReadable(data + covers::kStarCash, 2)) perStar = ReadAt<int16_t>(reinterpret_cast<const unsigned char*>(data), covers::kStarCash);
    }
    int gameBonus = static_cast<int>(perStar * stars + 0.005f);
    int total = static_cast<int>(g_cashPerStar * (stars < 1.0f ? 1.0f : stars) + 0.5f);
    int value = total - gameBonus;
    if (value > 32767) value = 32767;
    if (value < -32768) value = -32768;
    int16_t old = ReadAt<int16_t>(showcase, covers::showcase::kCash);
    int16_t v = static_cast<int16_t>(value);
    memcpy(showcase + covers::showcase::kCash, &v, 2);
    int32_t before = *reinterpret_cast<const volatile int32_t*>(kCash);
    reinterpret_cast<Fn>(covers::kPhotoCash)(career, edx, showcase);
    memcpy(showcase + covers::showcase::kCash, &old, 2);
    int32_t after = *reinterpret_cast<const volatile int32_t*>(kCash);
    Log("Photo pay with the %s: %.2f stars x $%d = $%d (paid $%d; the game would have paid $%d).",
        CarName(CurrentCarModel()), stars, g_cashPerStar, total, after - before, old + gameBonus);
}

void PrepareCover(const char* profile, const char* cover);

// Replaces the 2 calls to Showcase_MarkDone (thiscall(0x0085EF50), key at this+8, plain RET): the photo is taken.
// Before the game marks it done (which rebuilds the markers): archive the old picture, mark the cover shot.
void __fastcall Hook_MarkDone(void* obj) {
    if (g_versionOk && CareerFinished() && EnsureState() && IsReadable(reinterpret_cast<uintptr_t>(obj) + 8, 4)) {
        uint32_t key = *reinterpret_cast<const uint32_t*>(reinterpret_cast<uintptr_t>(obj) + 8);
        int index = ShowcaseIndexByField(covers::showcase::kKey, key);
        int k = StateCover(key, false);
        char name[32] = "?";
        CoverNameAt(index, name);
        if (k >= 0 && g_state.waitingFor[k]) {
            Log("Photo taken: %s with the %s. Archiving the old picture first:", name, CarName(CurrentCarModel()));
            PrepareCover(g_state.profile, name);
            g_state.waitingFor[k] = 0;
            g_state.lastShot[k] = ++g_state.seq;
            SaveState();
            Log("  %s counts as shot (#%d). The game now saves the new picture.", name, g_state.seq);
        } else {
            Log("Photo taken: %s (not one of the plugin's covers).", name);
        }
    }
    using Fn = void(__fastcall*)(void*);
    reinterpret_cast<Fn>(covers::kShowcaseMarkDone)(obj);
}

// ---- Viewer counter ------------------------------------------------------------------------

// The game's language, detected like the Virtual Garage does: the game's text 0x2699CF8E ("Browse My Other
// Cars") differs in every language. Korean/Chinese/Japanese and anything unknown get English.
struct Texts {
    const char* lang;
    const char* detect;
    const char* photo;   // "PHOTO"
    const char* upDown;  // the browse buttons (same words as the Virtual Garage)
};
const Texts kTexts[] = {
    {"EN", "Browse My Other Cars", "PHOTO", "UP/DOWN"},
    {"FR", "Parcourir mes voitures", "PHOTO", "HAUT/BAS"},
    {"DE", "Eigene Wagen ansehen", "FOTO", "AUF/AB"},
    {"IT", "Esplora il tuo parco auto", "FOTO", "SU/GI\xD9"},
    {"ES", "Explorar mis coches", "FOTO", "ARRIBA/ABAJO"},
    {"NL", "Blader door Mijn andere wagens", "FOTO", "OP/NEER"},
    {"SV", "V\xE4xla mellan mina bilar", "FOTO", "UPP/NER"},
    {"DA", "Se mine andre biler", "FOTO", "OP/NED"},
};

const Texts& T() {
    static int found = -1;
    if (found >= 0) return kTexts[found];
    using LocFn = const char*(__cdecl*)(uint32_t);
    const char* game = reinterpret_cast<LocFn>(kLocalizedString)(0x2699CF8E);
    if (!game || !IsReadable(reinterpret_cast<uintptr_t>(game), 1)) return kTexts[0];  // language not loaded yet
    found = 0;
    for (int i = 0; i < static_cast<int>(sizeof(kTexts) / sizeof(kTexts[0])); i++)
        if (strncmp(game, kTexts[i].detect, strlen(kTexts[i].detect)) == 0) found = i;
    Log("Game language: %s.", kTexts[found].lang);
    return kTexts[found];
}

// Sets an FE text object's text (the game's setter grows the buffer) and marks it changed.
void SetTextObject(uintptr_t obj, const char* text) {
    using SetFn = void(__fastcall*)(uintptr_t, void*, const char*);  // thiscall(obj, text), RET 4
    reinterpret_cast<SetFn>(kFEStringSetText)(obj, nullptr, text);
    *reinterpret_cast<uint32_t*>(obj + feobj::kFlags) |= 2;
}

// "PHOTO 2/3  UP/DOWN" in the viewer while there is more than one version (never touched otherwise).
void ShowCounter(uintptr_t screen) {
    if (!g_counter || g_versionCount <= 1 || g_pos < 0) return;
    const char* package = *reinterpret_cast<const char* const*>(screen + covers::kViewerPackageName);
    if (!package || !IsReadable(reinterpret_cast<uintptr_t>(package), 1)) return;
    using FindFn = uintptr_t(__cdecl*)(const char*, uint32_t);
    uint32_t hash = g_counter == 2 ? covers::kViewerBackObj : covers::kViewerCounterObj;
    uintptr_t obj = reinterpret_cast<FindFn>(kFEFindObject)(package, hash);
    if (!obj || !IsReadable(obj, 0x70) || ReadAt<uint32_t>(reinterpret_cast<const unsigned char*>(obj), 0x18) != 2) {
        static bool logged = false;
        if (!logged) Log("  Counter: no text object %08X in %s.", static_cast<unsigned>(hash), package);
        logged = true;
        return;
    }
    static bool described = false;
    if (!described) {  // once: what the object held before (for a round-trip check of the layout)
        described = true;
        const char* old = *reinterpret_cast<const char* const*>(obj + 0x64);
        const unsigned char* data = *reinterpret_cast<const unsigned char* const*>(obj + 0x2C);
        char hex[0x54 * 3 + 1] = "";
        if (data && IsReadable(reinterpret_cast<uintptr_t>(data), 0x54))
            for (int i = 0; i < 0x54; i++) snprintf(hex + i * 3, 4, "%02X ", data[i]);
        Log("  Counter object %08X: text \"%s\", capacity %d, flags %08X, data %s", static_cast<unsigned>(obj),
            old && IsReadable(reinterpret_cast<uintptr_t>(old), 1) ? old : "?",
            ReadAt<int32_t>(reinterpret_cast<const unsigned char*>(obj), feobj::kMaxLength),
            static_cast<unsigned>(ReadAt<uint32_t>(reinterpret_cast<const unsigned char*>(obj), feobj::kFlags)), hex);
    }
    char text[96];
    if (g_counter == 2) {
        // "Back" is centered both ways (obj+0x6C = 5): three lines (blank, Back, counter) keep it in place. The game's
        // line counter (0x0050C080) only counts a '^' after a line holding a space, so " Back " is padded.
        using LocFn = const char*(__cdecl*)(uint32_t);
        const char* back = reinterpret_cast<LocFn>(kLocalizedString)(covers::kTextBack);
        snprintf(text, sizeof(text), " ^ %s ^%s %d/%d  %s", back ? back : "", T().photo, g_pos + 1, g_versionCount,
                 T().upDown);
    } else {
        snprintf(text, sizeof(text), "%s %d/%d  %s", T().photo, g_pos + 1, g_versionCount, T().upDown);
    }
    SetTextObject(obj, text);
}

// ---- The viewer hook ---------------------------------------------------------------------

uintptr_t g_viewer = 0;      // the viewer screen the version list belongs to
uint32_t  g_viewerDesc = 0;  // and its cover
bool      g_viewerReady = false;
char      g_viewerCover[32] = "";

void OnViewerOpen(uintptr_t screen) {
    uint32_t desc = *reinterpret_cast<const uint32_t*>(covers::kCurrentDesc);
    char profile[33];
    int index = -1;
    g_viewer = screen;
    g_viewerDesc = desc;
    g_viewerReady = false;
    g_padsPrimed = false;
    g_versionCount = 0;
    if (!CoverName(desc, g_viewerCover, &index)) {
        Log("Viewer opened, but no cover has desc %08X; browsing is off.", static_cast<unsigned>(desc));
        g_viewerCover[0] = '\0';
        return;
    }
    if (!ProfileName(profile)) {
        Log("Viewer opened (%s), but no usable profile name; browsing is off.", g_viewerCover);
        return;
    }
    Log("Viewer opened: %s of profile \"%s\" (showcase %d).", g_viewerCover, profile, index);
    PrepareCover(profile, g_viewerCover);
}

void Step(uintptr_t screen, int dir) {
    if (g_versionCount <= 1) {
        Log("  %s pressed: %s has no other version yet.", dir < 0 ? "UP" : "DOWN", g_viewerCover);
        return;
    }
    int from = g_pos < 0 ? (dir < 0 ? 0 : g_versionCount - 1) : g_pos;
    for (int tries = 0; tries < g_versionCount; tries++) {  // skip versions that can't be read
        from = (from + dir + g_versionCount) % g_versionCount;
        if (ShowVersion(from)) {
            g_pos = from;
            Log("  %s: showing %d/%d = %s%s", dir < 0 ? "UP" : "DOWN", g_pos + 1, g_versionCount, g_versions[g_pos].label,
                g_pos == g_currentPos ? " (the game's current picture)" : "");
            ShowCounter(screen);
            return;
        }
    }
}

void __fastcall Hook_ViewerNotify(void* screen, void* edx, uint32_t msg, uint32_t a, uint32_t b, uint32_t c) {
    using Fn = void(__fastcall*)(void*, void*, uint32_t, uint32_t, uint32_t, uint32_t);
    reinterpret_cast<Fn>(covers::kViewerNotify)(screen, edx, msg, a, b, c);
    if (!g_versionOk) return;

    uintptr_t s = reinterpret_cast<uintptr_t>(screen);
    if (msg != covers::kMsgTick && g_detailedLog) {
        static int lines = 0;
        if (lines < 500) Log("Viewer message %08X (%08X %08X %08X)", static_cast<unsigned>(msg),
                             static_cast<unsigned>(a), static_cast<unsigned>(b), static_cast<unsigned>(c));
        if (++lines == 500) Log("(viewer message logging stops here)");
    }

    const unsigned char* sb = reinterpret_cast<const unsigned char*>(s);
    if (msg == covers::kMsgViewerOpen) {
        if (s != g_viewer || *reinterpret_cast<const uint32_t*>(covers::kCurrentDesc) != g_viewerDesc || !g_viewerReady)
            OnViewerOpen(s);
    } else if (msg == covers::kMsgLoadOk && s == g_viewer) {
        g_viewerReady = true;
        g_pos = g_currentPos;
        Log("  The game loaded and shows its picture of %s. UP/DOWN: %d version(s).", g_viewerCover, g_versionCount);
        ShowCounter(s);
        static bool dumped = false;
        if (!dumped && g_detailedLog) {
            dumped = true;
            const char* pkg = *reinterpret_cast<const char* const*>(s + covers::kViewerPackageName);
            if (pkg && IsReadable(reinterpret_cast<uintptr_t>(pkg), 1)) DumpPackageObjects(pkg);
        }
    } else if (msg == covers::kMsgLoadFailed && s == g_viewer) {
        g_viewerReady = false;
        Log("  The game could not load its picture of %s.", g_viewerCover);
    } else if (msg == kMsgPadBack && s == g_viewer) {
        Log("  Viewer closed (BACK).");
        g_viewerReady = false;
    } else if (msg == covers::kMsgTick && s == g_viewer && g_viewerReady && sb[covers::kViewerLoaded] &&
               sb[covers::kViewerShown]) {
        int step = ReadPadStep();
        if (step) Step(s, step);
    }
}

// ---- Startup -------------------------------------------------------------------------------

DWORD WINAPI VersionCheckThread(LPVOID) {
    bool ok = ExeMd5Ok() && g_hooksOk;
    InterlockedExchange(&g_versionOk, ok ? 1 : 0);
    if (!ok) Log("The hook (if any) stays installed but only passes calls through to the game.");
    else Log("Ready. After the career, wins earn covers per car; in Career -> Magazines / DVDs, UP/DOWN browses versions.");
    return 0;
}

void Init() {
    Log("==== Covers started (post-game covers earned per car; viewer UP/DOWN = older/newer) ====");
    LoadSettings();
    InitCrcTable();
    if (!CodeMatches(kCodeChecks, sizeof(kCodeChecks) / sizeof(kCodeChecks[0]))) {
        Log("Not hooking anything.");
        return;
    }
    bool ok = PatchPointer(covers::kViewerVtable + 8, covers::kViewerNotify,
                           reinterpret_cast<uintptr_t>(&Hook_ViewerNotify));
    Log("%s cover viewer Notify (vtable entry 0x%08X).", ok ? "Hooked" : "COULD NOT hook",
        static_cast<unsigned>(covers::kViewerVtable + 8));
    struct Site {
        uintptr_t   site, target;
        const void* hook;
        bool        jump;
        const char* what;
    };
    const Site sites[] = {
        {covers::kApplyResultCallSites[0], covers::kApplyRaceResult, reinterpret_cast<const void*>(&Hook_ApplyRaceResult), false, "race result"},
        {covers::kApplyResultCallSites[1], covers::kApplyRaceResult, reinterpret_cast<const void*>(&Hook_ApplyRaceResult), false, "race result"},
        {covers::kApplyResultJumpSite, covers::kApplyRaceResult, reinterpret_cast<const void*>(&Hook_ApplyRaceResult), true, "race result"},
        {covers::kIsOfferedCallSites[0], covers::kShowcaseIsOffered, reinterpret_cast<const void*>(&Hook_IsOffered), false, "cover offered"},
        {covers::kIsOfferedCallSites[1], covers::kShowcaseIsOffered, reinterpret_cast<const void*>(&Hook_IsOffered), false, "cover offered"},
        {covers::kIsOfferedCallSites[2], covers::kShowcaseIsOffered, reinterpret_cast<const void*>(&Hook_IsOffered), false, "cover offered"},
        {covers::kIsOfferedJumpSite, covers::kShowcaseIsOffered, reinterpret_cast<const void*>(&Hook_IsOffered), true, "cover offered"},
        {covers::kMarkDoneCallSites[0], covers::kShowcaseMarkDone, reinterpret_cast<const void*>(&Hook_MarkDone), false, "photo taken"},
        {covers::kMarkDoneCallSites[1], covers::kShowcaseMarkDone, reinterpret_cast<const void*>(&Hook_MarkDone), false, "photo taken"},
        {covers::kPhotoCashCall, covers::kPhotoCash, reinterpret_cast<const void*>(&Hook_PhotoCash), false, "photo pay"},
    };
    for (const Site& s : sites) {
        bool one = s.jump ? RedirectJump(s.site, s.target, s.hook) : RedirectCall(s.site, s.target, s.hook);
        Log("%s %s %s at 0x%08X.", one ? "Hooked" : "COULD NOT hook", s.what, s.jump ? "jump" : "call",
            static_cast<unsigned>(s.site));
        ok &= one;
    }
    g_hooksOk = ok;

    // CryptoAPI must not run inside DllMain, so the MD5 check runs on its own thread.
    HANDLE thread = CreateThread(nullptr, 0, VersionCheckThread, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
}

}  // namespace

// Called by Endless Career right after Career_RebuildActiveEvents (and its own races). Main thread.
// MagazineChallenge: every magazine waiting for the car being driven gets its stock challenge (start marker) back.
void bf::CoversAfterRebuild() {
    if (!g_versionOk || !g_magChallenge || uniques::RunningChallenge() || !CareerFinished() || !EnsureState()) return;
    uint32_t model = CurrentCarModel();
    if (!model) return;
    static uint32_t lastModel = 0;
    static int      lastCount = -1;
    int count = 0;
    for (int k = 0; k < g_state.coverCount; k++) {
        if (g_state.waitingFor[k] != model) continue;
        int index = ShowcaseIndexByField(covers::showcase::kKey, g_state.coverKey[k]);
        const unsigned char* table = nullptr;
        if (index < 0 || index >= ShowcaseTable(&table) || !UsesChallenge(table + index * ev::kShowcaseSize)) continue;
        uintptr_t chal = FindMagazineChallenge(g_state.coverKey[k]);
        int added = uniques::AddActiveChallenge(chal);
        if (added < 0) {
            Log("The world challenge list is full; a magazine challenge was not added.");
            break;
        }
        count++;
        if (model != lastModel || count > lastCount) {
            char name[32] = "?";
            CoverNameAt(index, name);
            const unsigned char* c = reinterpret_cast<const unsigned char*>(chal);
            Log("On the map for the %s: the challenge for %s (%d s to the photo shoot).", CarName(model), name,
                ReadAt<int32_t>(c, uniques::challenge::kTime));
        }
    }
    lastModel = model;
    lastCount = count;
}

void bf::StartCovers() { Init(); }  // patches the vtable entry before the game's own code starts running
