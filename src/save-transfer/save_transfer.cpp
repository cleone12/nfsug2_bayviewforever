// NFSU2 Save Transfer: a small Windows program for players with several old career saves.
// One screen: pick a recipient save, tick the saves to take from, tick Cars and/or Bank, press Transfer.
//
//  - Cars (Copy cars to the Virtual Garage): copies every car (all parts, paint, vinyls) from the ticked saves into
//    the recipient profile's Virtual Garage store (<game>\BayviewForever\virtual-garage\<profile>\NNNNNN_<CAR>_FROM_<save>.vgcar).
//    New files get numbers after the ones already there, so nothing is overwritten; a car that is
//    already in the store (byte-identical) is skipped, so running it twice adds nothing.
//  - Bank (Bank transfer): adds the bank (cash) of the ticked saves to the recipient save. The other saves keep
//    their money (the money is copied).
//
// Before either one changes anything, the whole save folder and the virtual-garage folder are copied to
// "<save folder> Backups\<date time>\". "Back up all saves" makes the same backup on its own.
// It refuses to run while speed2.exe is running.
// Save and store layouts: save_core.cpp, docs/structs.md. Tested on the Mac: test.sh.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <tlhelp32.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "save_core.h"

#define TOOL_VERSION "0.2.2"
#define TOOL_TITLE L"NFSU2 Save Transfer v" TOOL_VERSION

namespace {

enum {
    IDC_SAVEDIR = 100, IDC_SAVEBROWSE, IDC_VGDIR, IDC_VGBROWSE, IDC_REFRESH, IDC_DOCARS, IDC_DOBANK,
    IDC_RECIPIENT, IDC_HINT, IDC_LIST, IDC_ALL, IDC_NONE, IDC_GO, IDC_LOG, IDC_BACKUP,
    IDC_L_SAVEDIR, IDC_L_VGDIR, IDC_L_RECIPIENT, IDC_L_LIST,
};

struct Profile {
    std::wstring    name;  // folder name = profile name
    std::wstring    path;  // <save folder>\<name>\<name>
    st::ProfileInfo info;
};

HINSTANCE            g_inst;
HWND                 g_wnd;
HFONT                g_font;
int                  g_dpi = 96;
std::vector<Profile> g_profiles;
std::wstring         g_scannedSaveDir, g_scannedVgDir;
bool                 g_internalCheck = false;  // true while the program itself ticks/unticks items

int S(int v) { return MulDiv(v, g_dpi, 96); }
HWND Ctl(int id) { return GetDlgItem(g_wnd, id); }

std::wstring Widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, &w[0], n);
    return w;
}

std::string Narrow(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring GetText(int id) {
    HWND h = Ctl(id);
    int n = GetWindowTextLengthW(h);
    std::wstring s(n, L'\0');
    if (n) GetWindowTextW(h, &s[0], n + 1);
    return s;
}

std::wstring TrimSlash(std::wstring p) {
    while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
    return p;
}

std::wstring Money(long long v) {
    wchar_t digits[32];
    swprintf(digits, 32, L"%lld", v < 0 ? -v : v);
    std::wstring s, d = digits;
    for (size_t i = 0; i < d.size(); i++) {
        if (i && (d.size() - i) % 3 == 0) s += L',';
        s += d[i];
    }
    return (v < 0 ? L"-$" : L"$") + s;
}

std::wstring ExeDir() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s = p;
    return s.substr(0, s.find_last_of(L"\\/"));
}

bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool ReadAll(const std::wstring& path, st::Bytes* out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart < 64 * 1024 * 1024;
    if (ok) {
        out->resize(static_cast<size_t>(size.QuadPart));
        DWORD got = 0;
        ok = out->empty() || (ReadFile(f, out->data(), static_cast<DWORD>(out->size()), &got, nullptr) &&
                              got == out->size());
    }
    CloseHandle(f);
    return ok;
}

// CREATE_NEW never replaces an existing file.
bool WriteAll(const std::wstring& path, const st::Bytes& d, DWORD how) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, how, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    bool ok = WriteFile(f, d.data(), static_cast<DWORD>(d.size()), &put, nullptr) && put == d.size() &&
              FlushFileBuffers(f);
    ok = CloseHandle(f) && ok;
    if (!ok) DeleteFileW(path.c_str());
    return ok;
}

// ---- Log (window + save-transfer.log next to the program) ---------------------------------------------

void Log(const std::wstring& line) {
    HWND log = Ctl(IDC_LOG);
    std::wstring text = line + L"\r\n";
    int end = GetWindowTextLengthW(log);
    SendMessageW(log, EM_SETSEL, end, end);
    SendMessageW(log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));

    SYSTEMTIME t;
    GetLocalTime(&t);
    std::wstring path = ExeDir() + L"\\save-transfer.log";
    if (FILE* f = _wfopen(path.c_str(), L"a, ccs=UTF-8")) {
        fwprintf(f, L"%04d-%02d-%02d %02d:%02d:%02d  %ls\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
                 t.wSecond, line.c_str());
        fclose(f);
    }
}

// ---- Defaults --------------------------------------------------------------------------------------

std::wstring DefaultSaveDir() {
    wchar_t p[MAX_PATH];
    if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, p) != S_OK) return L"";
    return std::wstring(p) + L"\\NFS Underground 2";  // same as the game (0x004A9C40)
}

// The program normally sits next to speed2.exe; also accept it in scripts\.
std::wstring DefaultVgDir() {
    std::wstring dir = ExeDir();
    if (!FileExists(dir + L"\\speed2.exe")) {
        std::wstring parent = dir.substr(0, dir.find_last_of(L"\\/"));
        if (FileExists(parent + L"\\speed2.exe")) dir = parent;
    }
    // Older BayviewForever versions used <game>\virtual-garage
    // (moved over the next time the game starts with the new version).
    if (!DirExists(dir + L"\\BayviewForever\\virtual-garage") && DirExists(dir + L"\\virtual-garage"))
        return dir + L"\\virtual-garage";
    return dir + L"\\BayviewForever\\virtual-garage";
}

bool GameRunning() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    bool found = false;
    for (BOOL more = Process32FirstW(snap, &pe); more && !found; more = Process32NextW(snap, &pe))
        found = _wcsicmp(pe.szExeFile, L"speed2.exe") == 0;
    CloseHandle(snap);
    return found;
}

// ---- Profiles --------------------------------------------------------------------------------------

bool IsChecked(int id) { return SendMessageW(Ctl(id), BM_GETCHECK, 0, 0) == BST_CHECKED; }
bool DoCars() { return IsChecked(IDC_DOCARS); }
bool DoBank() { return IsChecked(IDC_DOBANK); }

int RecipientIndex() {
    HWND cb = Ctl(IDC_RECIPIENT);
    int sel = static_cast<int>(SendMessageW(cb, CB_GETCURSEL, 0, 0));
    if (sel == CB_ERR) return -1;
    return static_cast<int>(SendMessageW(cb, CB_GETITEMDATA, sel, 0));
}

bool Selectable(int i) { return i >= 0 && i < static_cast<int>(g_profiles.size()) && g_profiles[i].info.ok && i != RecipientIndex(); }

std::wstring CarsText(const st::ProfileInfo& info) {
    if (!info.ok) return Widen(info.error);
    if (info.cars.empty()) return L"(no cars)";
    std::wstring s;
    for (const st::Car& c : info.cars) s += (s.empty() ? L"" : L", ") + Widen(st::CarName(c.model));
    return s;
}

std::wstring StoreDirFor(const std::wstring& profile) {
    return TrimSlash(GetText(IDC_VGDIR)) + L"\\" + Widen(st::StoreFolderName(Narrow(profile))) + L"\\";
}

struct StoreFile {
    std::wstring path;
    long         seq;
    st::Bytes    image;  // empty if the file isn't a valid car
};

std::vector<StoreFile> ListStore(const std::wstring& dir) {
    std::vector<StoreFile> list;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"*.vgcar").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return list;
    do {
        long seq = st::StoreFileSeq(Narrow(fd.cFileName));
        if (seq < 0) continue;  // the plugin ignores these too
        StoreFile f{dir + fd.cFileName, seq, {}};
        st::Bytes data;
        if (!ReadAll(f.path, &data) || !st::ReadStoreFile(data, &f.image)) f.image.clear();
        list.push_back(std::move(f));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return list;
}

void UpdateHint() {
    int r = RecipientIndex();
    std::wstring hint;
    if (r < 0) {
        hint = L"No usable save found. Check the game saves folder.";
    } else if (!DoCars() && !DoBank()) {
        hint = L"Tick what to transfer: cars, bank money, or both.";
    } else {
        const Profile& to = g_profiles[r];
        if (DoCars()) {
            size_t stored = ListStore(StoreDirFor(to.name)).size();
            hint = L"Cars: every car in the ticked saves goes into " + to.name + L"'s Virtual Garage (" +
                   std::to_wstring(stored) + L" stored now; cars already there are skipped). They show up once "
                   L"that career is finished. ";
        }
        if (DoBank())
            hint += L"Bank: the ticked saves' money is added to " + to.name + L" (now " + Money(to.info.cash) +
                    L"); they keep their own. ";
        hint += L"The ticked saves are not changed.";
    }
    SetWindowTextW(Ctl(IDC_HINT), hint.c_str());
    EnableWindow(Ctl(IDC_GO), DoCars() || DoBank());
}

void SetCheck(int item, bool on) {
    bool was = g_internalCheck;
    g_internalCheck = true;
    ListView_SetCheckState(Ctl(IDC_LIST), item, on);
    g_internalCheck = was;
}

void RecipientChanged() {
    HWND list = Ctl(IDC_LIST);
    int n = ListView_GetItemCount(list);
    for (int i = 0; i < n; i++)
        if (!Selectable(i) && ListView_GetCheckState(list, i)) SetCheck(i, false);
    InvalidateRect(list, nullptr, TRUE);
    UpdateHint();
}

void Scan() {
    std::wstring saveDir = TrimSlash(GetText(IDC_SAVEDIR));
    std::wstring oldRecipient;
    if (RecipientIndex() >= 0) oldRecipient = g_profiles[RecipientIndex()].name;
    std::vector<std::wstring> wasTicked;
    HWND list = Ctl(IDC_LIST);
    for (int i = 0; i < ListView_GetItemCount(list); i++)
        if (ListView_GetCheckState(list, i)) wasTicked.push_back(g_profiles[i].name);

    g_profiles.clear();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((saveDir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
            Profile p;
            p.name = fd.cFileName;
            p.path = saveDir + L"\\" + p.name + L"\\" + p.name;
            st::Bytes d;
            // Only profile saves have this size (DVD/magazine cover pictures live in folders like these too).
            if (!ReadAll(p.path, &d) || d.size() != st::kSaveSize) continue;
            p.info = st::ReadProfile(d);
            g_profiles.push_back(std::move(p));
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(g_profiles.begin(), g_profiles.end(),
              [](const Profile& a, const Profile& b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });

    HWND cb = Ctl(IDC_RECIPIENT);
    SendMessageW(cb, CB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < g_profiles.size(); i++) {
        if (!g_profiles[i].info.ok) continue;
        int at = static_cast<int>(SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(g_profiles[i].name.c_str())));
        SendMessageW(cb, CB_SETITEMDATA, at, static_cast<LPARAM>(i));
        if (g_profiles[i].name == oldRecipient || SendMessageW(cb, CB_GETCURSEL, 0, 0) == CB_ERR)
            SendMessageW(cb, CB_SETCURSEL, at, 0);
    }

    g_internalCheck = true;  // new items get their (empty) tick box from the list itself
    ListView_DeleteAllItems(list);
    for (size_t i = 0; i < g_profiles.size(); i++) {
        const Profile& p = g_profiles[i];
        LVITEMW it = {};
        it.mask = LVIF_TEXT;
        it.iItem = static_cast<int>(i);
        it.pszText = const_cast<wchar_t*>(p.name.c_str());
        ListView_InsertItem(list, &it);
        std::wstring cash = p.info.ok ? Money(p.info.cash) : L"";
        std::wstring career = !p.info.ok ? L"" : p.info.stage >= 6 ? L"Finished" : L"In progress";
        std::wstring cars = CarsText(p.info);
        ListView_SetItemText(list, it.iItem, 1, const_cast<wchar_t*>(cash.c_str()));
        ListView_SetItemText(list, it.iItem, 2, const_cast<wchar_t*>(career.c_str()));
        ListView_SetItemText(list, it.iItem, 3, const_cast<wchar_t*>(cars.c_str()));
        if (Selectable(it.iItem) && std::find(wasTicked.begin(), wasTicked.end(), p.name) != wasTicked.end())
            SetCheck(it.iItem, true);
    }
    g_internalCheck = false;
    g_scannedSaveDir = saveDir;
    g_scannedVgDir = TrimSlash(GetText(IDC_VGDIR));
    RecipientChanged();
}

void ScanIfChanged() {
    if (TrimSlash(GetText(IDC_SAVEDIR)) != g_scannedSaveDir || TrimSlash(GetText(IDC_VGDIR)) != g_scannedVgDir) Scan();
}

std::vector<int> Ticked() {
    std::vector<int> out;
    HWND list = Ctl(IDC_LIST);
    for (int i = 0; i < ListView_GetItemCount(list); i++)
        if (ListView_GetCheckState(list, i) && Selectable(i)) out.push_back(i);
    return out;
}

// ---- Backup ----------------------------------------------------------------------------------------

bool CopyTree(const std::wstring& from, const std::wstring& to) {
    if (!CreateDirectoryW(to.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((from + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return true;  // empty folder
    bool ok = true;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ok = CopyTree(from + L"\\" + n, to + L"\\" + n);
        else ok = CopyFileW((from + L"\\" + n).c_str(), (to + L"\\" + n).c_str(), TRUE) != 0;
    } while (ok && FindNextFileW(h, &fd));
    FindClose(h);
    return ok;
}

// Copies the whole save folder and the virtual-garage folder to "<save folder> Backups\<date time>\".
bool Backup() {
    std::wstring saveDir = TrimSlash(GetText(IDC_SAVEDIR));
    std::wstring vgDir = TrimSlash(GetText(IDC_VGDIR));
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t stamp[64];
    swprintf(stamp, 64, L"%04d-%02d-%02d %02d-%02d-%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    std::wstring root = saveDir + L" Backups";
    std::wstring dest = root + L"\\" + stamp;
    CreateDirectoryW(root.c_str(), nullptr);
    std::wstring leaf = saveDir.substr(saveDir.find_last_of(L"\\/") + 1);
    bool ok = DirExists(root) && CreateDirectoryW(dest.c_str(), nullptr) && CopyTree(saveDir, dest + L"\\" + leaf);
    if (ok && DirExists(vgDir)) ok = CopyTree(vgDir, dest + L"\\virtual-garage");
    if (!ok) {
        Log(L"Backup FAILED (" + dest + L"). Nothing was changed.");
        MessageBoxW(g_wnd, (L"The backup could not be made:\n" + dest + L"\n\nNothing was changed.").c_str(),
                    TOOL_TITLE, MB_ICONERROR);
        return false;
    }
    Log(L"Backup made: " + dest);
    return true;
}

void BackupOnly() {
    std::wstring saveDir = TrimSlash(GetText(IDC_SAVEDIR));
    if (!DirExists(saveDir)) {
        MessageBoxW(g_wnd, (L"The game saves folder doesn't exist:\n" + saveDir).c_str(), TOOL_TITLE, MB_ICONWARNING);
        return;
    }
    if (GameRunning()) {
        MessageBoxW(g_wnd, L"Need for Speed: Underground 2 is running.\n\nQuit the game first, then try again.",
                    TOOL_TITLE, MB_ICONWARNING);
        return;
    }
    if (Backup())
        MessageBoxW(g_wnd, (L"All saves (and the Virtual Garage) were backed up to:\n" + saveDir + L" Backups").c_str(),
                    TOOL_TITLE, MB_ICONINFORMATION);
}

bool ReadyToChange() {
    ScanIfChanged();
    if (GameRunning()) {
        MessageBoxW(g_wnd, L"Need for Speed: Underground 2 is running.\n\nQuit the game first, then try again.",
                    TOOL_TITLE, MB_ICONWARNING);
        return false;
    }
    if (!DoCars() && !DoBank()) {
        MessageBoxW(g_wnd, L"Tick what to transfer: cars, bank money, or both.", TOOL_TITLE, MB_ICONINFORMATION);
        return false;
    }
    if (RecipientIndex() < 0) {
        MessageBoxW(g_wnd, L"Pick a recipient save first.", TOOL_TITLE, MB_ICONINFORMATION);
        return false;
    }
    if (Ticked().empty()) {
        MessageBoxW(g_wnd, L"Tick at least one save to export from.", TOOL_TITLE, MB_ICONINFORMATION);
        return false;
    }
    return true;
}

// ---- Transfer (cars and/or bank) ------------------------------------------------------------------

// Reads a save again right before using it (it may have changed since the scan).
bool Reread(const Profile& p, st::Bytes* d, st::ProfileInfo* info) {
    if (ReadAll(p.path, d)) *info = st::ReadProfile(*d);
    if (info->ok) return true;
    MessageBoxW(g_wnd, (p.name + L" can't be read any more. Press Refresh.").c_str(), TOOL_TITLE, MB_ICONERROR);
    return false;
}

void Run() {
    if (!ReadyToChange()) return;
    const bool cars = DoCars(), bank = DoBank();
    const Profile& to = g_profiles[RecipientIndex()];
    std::vector<int> sources = Ticked();

    // What would happen: cars not yet in the store, and the money to add.
    std::wstring dir = StoreDirFor(to.name);
    std::vector<StoreFile> store;
    if (cars) store = ListStore(dir);
    struct CarPlan { std::wstring from; st::Bytes image; };
    std::vector<CarPlan> plan;
    int already = 0;
    st::Bytes save;
    st::ProfileInfo info;
    if (bank && !Reread(to, &save, &info)) return;
    long long sum = 0;
    for (int i : sources) {
        st::Bytes d;
        st::ProfileInfo from;
        if (!Reread(g_profiles[i], &d, &from)) return;
        if (bank) sum += std::max<int32_t>(from.cash, 0);
        if (!cars) continue;
        for (const st::Car& c : from.cars) {
            st::Bytes image = st::SlotImage(d, c.slot);
            bool dup = std::any_of(store.begin(), store.end(), [&](const StoreFile& f) { return st::SameCar(f.image, image); }) ||
                       std::any_of(plan.begin(), plan.end(), [&](const CarPlan& p) { return st::SameCar(p.image, image); });
            if (dup) already++;
            else plan.push_back({g_profiles[i].name, std::move(image)});
        }
    }
    long long total = std::min<long long>(static_cast<long long>(info.cash) + sum, INT_MAX);
    const bool carWork = cars && !plan.empty(), bankWork = bank && sum > 0;

    std::wstring q = L"Transfer from " + std::to_wstring(sources.size()) + L" save(s) to " + to.name + L":\n\n";
    if (cars) {
        if (plan.empty())
            q += already ? L"- Cars: nothing new, every car is already in the Virtual Garage.\n"
                         : L"- Cars: the ticked saves have no cars.\n";
        else
            q += L"- Cars: add " + std::to_wstring(plan.size()) + L" car(s) to the Virtual Garage" +
                 (already ? L" (" + std::to_wstring(already) + L" already there, skipped)" : L"") + L".\n";
    }
    if (bank) {
        if (sum == 0) q += L"- Bank: the ticked saves have no money.\n";
        else
            q += L"- Bank: add " + Money(sum) + L" (" + Money(info.cash) + L" -> " + Money(total) +
                 L"). The ticked saves keep their money; doing this again adds it again.\n";
    }
    if (!carWork && !bankWork) {
        MessageBoxW(g_wnd, (q + L"\nNothing to do.").c_str(), TOOL_TITLE, MB_ICONINFORMATION);
        return;
    }
    q += L"\nAll saves are backed up first. Go ahead?";
    if (MessageBoxW(g_wnd, q.c_str(), TOOL_TITLE, MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    if (!Backup()) return;

    std::wstring result;
    bool problem = false;
    if (carWork) {
        const std::wstring vg = TrimSlash(GetText(IDC_VGDIR));
        CreateDirectoryW(vg.substr(0, vg.find_last_of(L"\\/")).c_str(), nullptr);  // BayviewForever\ itself
        CreateDirectoryW(vg.c_str(), nullptr);
        CreateDirectoryW(dir.c_str(), nullptr);
        long next = 0;
        for (const StoreFile& f : store) next = std::max(next, f.seq + 1);
        int added = 0, failed = 0;
        for (const CarPlan& p : plan) {
            st::Bytes file = st::MakeStoreFile(p.image, Narrow(to.name));
            uint32_t model;
            memcpy(&model, &p.image[st::kModel], 4);
            bool ok = false;
            std::wstring path;
            for (int tries = 0; tries < 100 && !ok; tries++) {  // CREATE_NEW: never overwrite a store file
                path = dir + Widen(st::StoreFileName(next++, model, Narrow(p.from)));
                ok = WriteAll(path, file, CREATE_NEW);
                if (!ok && GetLastError() != ERROR_FILE_EXISTS) break;
            }
            if (ok) {
                added++;
                Log(L"Added " + Widen(st::CarName(model)) + L" from " + p.from + L" -> " + path);
            } else {
                failed++;
                Log(L"FAILED to write " + path);
            }
        }
        Log(L"Cars done: " + std::to_wstring(added) + L" added, " + std::to_wstring(already) + L" already there" +
            (failed ? L", " + std::to_wstring(failed) + L" FAILED" : L"") + L".");
        result += std::to_wstring(added) + L" car(s) added to " + to.name + L"'s Virtual Garage.\n";
        if (failed) {
            result += std::to_wstring(failed) + L" car(s) could not be written; see the log.\n";
            problem = true;
        }
    }
    if (bankWork) {
        // Write a new file next to the save, check it, then swap it in.
        st::Bytes updated = save;
        std::wstring tmp = to.path + L".transfer-tmp";
        st::Bytes back;
        bool ok = st::SetCash(updated, static_cast<int32_t>(total)) && WriteAll(tmp, updated, CREATE_ALWAYS) &&
                  ReadAll(tmp, &back) && back == updated && st::ReadProfile(back).cash == total &&
                  MoveFileExW(tmp.c_str(), to.path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        DeleteFileW(tmp.c_str());
        std::wstring names;
        for (int i : sources) names += (names.empty() ? L"" : L", ") + g_profiles[i].name;
        if (ok) {
            Log(L"Bank: " + Money(sum) + L" from " + names + L" -> " + to.name + L" (" + Money(info.cash) + L" -> " +
                Money(total) + L")");
            result += to.name + L" now has " + Money(total) + L" in the bank.\n";
        } else {
            Log(L"Bank transfer FAILED; " + to.name + L"'s save was not changed.");
            result += L"The bank transfer failed; " + to.name + L"'s save was not changed.\n";
            problem = true;
        }
    }
    MessageBoxW(g_wnd, result.c_str(), TOOL_TITLE, problem ? MB_ICONWARNING : MB_ICONINFORMATION);
    Scan();
}

// ---- Window ----------------------------------------------------------------------------------------

int CALLBACK BrowseInit(HWND h, UINT msg, LPARAM, LPARAM data) {
    if (msg == BFFM_INITIALIZED) SendMessageW(h, BFFM_SETSELECTIONW, TRUE, data);
    return 0;
}

void BrowseFolder(int editId, const wchar_t* title) {
    std::wstring start = TrimSlash(GetText(editId));
    BROWSEINFOW bi = {};
    bi.hwndOwner = g_wnd;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpfn = BrowseInit;
    bi.lParam = reinterpret_cast<LPARAM>(start.c_str());
    PIDLIST_ABSOLUTE id = SHBrowseForFolderW(&bi);
    if (!id) return;
    wchar_t path[MAX_PATH];
    if (SHGetPathFromIDListW(id, path)) {
        SetWindowTextW(Ctl(editId), path);
        Scan();
    }
    CoTaskMemFree(id);
}

HWND Make(const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD ex = 0) {
    HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, g_wnd,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_inst, nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    return h;
}

void Place(int id, int x, int y, int w, int h) { MoveWindow(Ctl(id), x, y, w, h, TRUE); }

void Layout() {
    RECT rc;
    GetClientRect(g_wnd, &rc);
    int W = rc.right, H = rc.bottom, m = S(12), lw = S(140), bw = S(90), rh = S(24);
    int ex = m + lw, ew = W - ex - m - bw - S(6);
    Place(IDC_L_SAVEDIR, m, m + S(4), lw, rh);
    Place(IDC_SAVEDIR, ex, m, ew, rh);
    Place(IDC_SAVEBROWSE, W - m - bw, m - S(1), bw, rh + S(2));
    Place(IDC_L_VGDIR, m, m + S(34), lw, rh);
    Place(IDC_VGDIR, ex, m + S(30), ew, rh);
    Place(IDC_VGBROWSE, W - m - bw, m + S(29), bw, rh + S(2));
    int y = m + S(70);
    Place(IDC_DOCARS, m, y, S(250), rh);
    Place(IDC_DOBANK, m + S(260), y, S(200), rh);
    Place(IDC_REFRESH, W - m - bw, y - S(1), bw, rh + S(2));
    y += S(34);
    Place(IDC_L_RECIPIENT, m, y + S(4), lw, rh);
    Place(IDC_RECIPIENT, ex, y, S(240), S(300));
    y += S(32);
    Place(IDC_HINT, m, y, W - 2 * m, S(52));
    y += S(58);
    Place(IDC_L_LIST, m, y, W - 2 * m, S(18));
    y += S(20);
    int logH = S(110), btnH = S(28);
    int listBottom = H - m - logH - S(8) - btnH - S(8);
    Place(IDC_LIST, m, y, W - 2 * m, std::max(S(60), listBottom - y));
    int by = std::max(S(60), listBottom - y) + y + S(8);
    Place(IDC_ALL, m, by, bw, btnH);
    Place(IDC_NONE, m + bw + S(6), by, bw, btnH);
    Place(IDC_GO, W - m - S(170), by, S(170), btnH);
    Place(IDC_BACKUP, W - m - S(170) - S(6) - S(140), by, S(140), btnH);
    Place(IDC_LOG, m, by + btnH + S(8), W - 2 * m, H - (by + btnH + S(8)) - m);

    HWND list = Ctl(IDC_LIST);
    RECT lr;
    GetClientRect(list, &lr);
    ListView_SetColumnWidth(list, 0, S(140));
    ListView_SetColumnWidth(list, 1, S(95));
    ListView_SetColumnWidth(list, 2, S(85));
    ListView_SetColumnWidth(list, 3, std::max(S(100), static_cast<int>(lr.right) - S(320)));
}

void Create() {
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g_font = CreateFontIndirectW(&ncm.lfMessageFont);

    Make(L"STATIC", L"Game saves folder:", 0, IDC_L_SAVEDIR);
    Make(L"EDIT", DefaultSaveDir().c_str(), WS_TABSTOP | ES_AUTOHSCROLL, IDC_SAVEDIR, WS_EX_CLIENTEDGE);
    Make(L"BUTTON", L"Browse...", WS_TABSTOP, IDC_SAVEBROWSE);
    Make(L"STATIC", L"Virtual Garage folder:", 0, IDC_L_VGDIR);
    Make(L"EDIT", DefaultVgDir().c_str(), WS_TABSTOP | ES_AUTOHSCROLL, IDC_VGDIR, WS_EX_CLIENTEDGE);
    Make(L"BUTTON", L"Browse...", WS_TABSTOP, IDC_VGBROWSE);
    Make(L"BUTTON", L"Copy cars to the Virtual Garage", WS_TABSTOP | BS_AUTOCHECKBOX, IDC_DOCARS);
    Make(L"BUTTON", L"Bank transfer (copy money)", WS_TABSTOP | BS_AUTOCHECKBOX, IDC_DOBANK);
    Make(L"BUTTON", L"Refresh", WS_TABSTOP | WS_GROUP, IDC_REFRESH);
    Make(L"STATIC", L"Recipient save:", 0, IDC_L_RECIPIENT);
    Make(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, IDC_RECIPIENT);
    Make(L"STATIC", L"", 0, IDC_HINT);
    Make(L"STATIC", L"Saves to export from (the recipient is greyed out):", 0, IDC_L_LIST);
    HWND list = Make(WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL, IDC_LIST,
                     WS_EX_CLIENTEDGE);
    ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    const wchar_t* cols[] = {L"Save", L"Bank", L"Career", L"Cars in the garage"};
    for (int i = 0; i < 4; i++) {
        LVCOLUMNW c = {};
        c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        c.fmt = i == 1 ? LVCFMT_RIGHT : LVCFMT_LEFT;
        c.cx = S(100);
        c.pszText = const_cast<wchar_t*>(cols[i]);
        ListView_InsertColumn(list, i, &c);
    }
    Make(L"BUTTON", L"Select all", WS_TABSTOP, IDC_ALL);
    Make(L"BUTTON", L"Select none", WS_TABSTOP, IDC_NONE);
    Make(L"BUTTON", L"Back up all saves", WS_TABSTOP, IDC_BACKUP);
    Make(L"BUTTON", L"Transfer", WS_TABSTOP | BS_DEFPUSHBUTTON, IDC_GO);
    Make(L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, IDC_LOG, WS_EX_CLIENTEDGE);
    SendMessageW(Ctl(IDC_DOCARS), BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(Ctl(IDC_DOBANK), BM_SETCHECK, BST_CHECKED, 0);

    Layout();
    Log(L"NFSU2 Save Transfer v" TOOL_VERSION);
    Scan();
    if (g_profiles.empty()) Log(L"No profile saves found in " + TrimSlash(GetText(IDC_SAVEDIR)));
    else Log(L"Found " + std::to_wstring(g_profiles.size()) + L" save(s).");
    std::wstring vg = TrimSlash(GetText(IDC_VGDIR));
    if (!FileExists(vg.substr(0, vg.find_last_of(L"\\/")) + L"\\speed2.exe"))
        Log(L"Note: speed2.exe is not next to this program, so check the Virtual Garage folder (it belongs in the game folder).");
}

LRESULT OnNotify(NMHDR* nh) {
    if (nh->idFrom != IDC_LIST) return 0;
    if (nh->code == LVN_ITEMCHANGING) {
        auto* nm = reinterpret_cast<NMLISTVIEW*>(nh);
        bool checkChange = (nm->uChanged & LVIF_STATE) && ((nm->uNewState ^ nm->uOldState) & LVIS_STATEIMAGEMASK);
        if (checkChange && !g_internalCheck && !Selectable(nm->iItem)) return TRUE;  // greyed out: no ticking
    } else if (nh->code == NM_CUSTOMDRAW) {
        auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(nh);
        if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
        if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
            if (!Selectable(static_cast<int>(cd->nmcd.dwItemSpec))) cd->clrText = GetSysColor(COLOR_GRAYTEXT);
            return CDRF_DODEFAULT;
        }
    }
    return 0;
}

LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g_wnd = h;
        Create();
        return 0;
    case WM_SIZE:
        Layout();
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
        mm->ptMinTrackSize = {S(640), S(560)};
        return 0;
    }
    case WM_CTLCOLORSTATIC:
        if (reinterpret_cast<HWND>(lp) == Ctl(IDC_LOG)) break;
        SetBkMode(reinterpret_cast<HDC>(wp), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
    case WM_NOTIFY:
        return OnNotify(reinterpret_cast<NMHDR*>(lp));
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (id == IDC_SAVEBROWSE) BrowseFolder(IDC_SAVEDIR, L"Pick the NFS Underground 2 save folder");
        else if (id == IDC_VGBROWSE) BrowseFolder(IDC_VGDIR, L"Pick the virtual-garage folder in the game folder");
        else if (id == IDC_REFRESH) Scan();
        else if ((id == IDC_SAVEDIR || id == IDC_VGDIR) && code == EN_KILLFOCUS) ScanIfChanged();
        else if ((id == IDC_DOCARS || id == IDC_DOBANK) && code == BN_CLICKED) UpdateHint();
        else if (id == IDC_RECIPIENT && code == CBN_SELCHANGE) RecipientChanged();
        else if (id == IDC_ALL || id == IDC_NONE) {
            for (int i = 0; i < ListView_GetItemCount(Ctl(IDC_LIST)); i++)
                if (Selectable(i)) SetCheck(i, id == IDC_ALL);
        } else if (id == IDC_GO) {
            Run();
        } else if (id == IDC_BACKUP) {
            BackupOnly();
        }
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    g_inst = inst;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    HDC dc = GetDC(nullptr);
    g_dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(nullptr, dc);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.lpszClassName = L"NFSU2SaveTransfer";
    RegisterClassExW(&wc);
    HWND w = CreateWindowExW(0, wc.lpszClassName, TOOL_TITLE, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             S(760), S(660), nullptr, nullptr, inst, nullptr);
    ShowWindow(w, show);
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) {
        if (IsDialogMessageW(w, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    CoUninitialize();
    return 0;
}
