// NFSU2 Save Transfer: save-file logic (see save_core.h). No Windows code here.
#include "save_core.h"

#include <cstdio>
#include <cstring>

namespace st {

namespace {

uint32_t U32(const uint8_t* p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

void PutU32(uint8_t* p, uint32_t v) { memcpy(p, &v, 4); }

const uint8_t kStoreMagic[8] = {'N', 'F', 'U', '2', 'V', 'G', 'C', '1'};

// Save dword index (in the 0x3C0 block of a saved career slot) -> memory dword index. The game
// saves the block in a slightly different order (CareerSlot_SaveToBuffer 0x00516620): see
// docs/structs.md -> "Career slot in the save".
size_t BlockDwordToMemory(size_t s) {
    if (s <= 0xA4) return s;
    if (s == 0xA5) return 0xEF;          // cash spent on parts (+0x7F0)
    if (s <= 0xA7) return s - 1;         // 0xA5-0xA6
    if (s <= 0xEB) return s + 3;         // 0xAB-0xEE: part list (+0x6E0)
    return s - 0x45;                     // 0xA7-0xAA
}

bool IsStoreNameChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' ||
           c == '_';
}

}  // namespace

uint32_t StringHash(const char* s) {
    uint32_t h = 0xFFFFFFFF;
    while (*s) h = h * 33 + static_cast<unsigned char>(*s++);
    return h;
}

// Same list as CarName in src/common/nfsu2.h (folder names in CARS\).
const char* CarName(uint32_t model) {
    static const char* const kCarNames[] = {
        "240SX", "3000GT", "350Z", "4DR_SEDAN", "4DR_SEDAN02", "A3", "AMBULANCE", "BUS", "CELICA", "CIVIC",
        "COROLLA", "COUPE", "ECLIPSE", "ESCALADE", "FIRETRUCK", "FOCUS", "G35", "GOLF", "GTO", "HATCHBACK",
        "HATCHBACK02", "HUMMER", "IMPREZA", "IMPREZAWRX", "IS300", "LANCER", "LANCEREVO8", "MIATA", "MINIVAN",
        "MUSTANGGT", "NAVIGATOR", "NEON", "PANELVAN", "PARCELVAN", "PEUGOT", "PICKUP", "RSX", "RX7", "RX8",
        "S2000", "SENTRA", "SKYLINE", "SUPRA", "SUV", "TAXI", "TAXI02", "TIBURON", "TT",
    };
    for (const char* name : kCarNames)
        if (StringHash(name) == model) return name;
    return "unknown";
}

// The game's MC02_Checksum (0x006FE173): CRC-32 table (poly 0x04C11DB7, MSB first), initial value =
// the first 4 bytes inverted. Same as Mc02Checksum in src/covers/covers.cpp.
uint32_t Mc02Checksum(const uint8_t* p, size_t n) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i << 24;
            for (int k = 0; k < 8; k++) c = (c & 0x80000000u) ? (c << 1) ^ 0x04C11DB7u : (c << 1);
            table[i] = c;
        }
        ready = true;
    }
    if (n < 4) return 0;
    uint32_t u = ~(static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 |
                   static_cast<uint32_t>(p[2]) << 8 | p[3]);
    for (size_t i = 4; i < n; i++) u = ((u << 8) | p[i]) ^ table[u >> 24];
    return ~u;
}

namespace {

bool CheckContainer(const Bytes& d, std::string* why) {
    if (d.size() != kSaveSize) {
        *why = "not a profile save (wrong size)";
        return false;
    }
    const uint8_t* p = d.data();
    if (U32(p) != kMc02Magic || U32(p + 4) != kSaveSize || U32(p + 8) != kPartB - kPartA ||
        U32(p + 0xC) != kSaveSize - kPartB) {
        *why = "not a profile save (header)";
        return false;
    }
    if (Mc02Checksum(p + kPartA, kPartB - kPartA) != U32(p + 0x10) ||
        Mc02Checksum(p + kPartB, kSaveSize - kPartB) != U32(p + 0x14) || Mc02Checksum(p, 0x18) != U32(p + 0x18)) {
        *why = "damaged (checksum mismatch)";
        return false;
    }
    return true;
}

void FixChecksums(Bytes& d) {
    uint8_t* p = d.data();
    PutU32(p + 0x10, Mc02Checksum(p + kPartA, kPartB - kPartA));
    PutU32(p + 0x14, Mc02Checksum(p + kPartB, kSaveSize - kPartB));
    PutU32(p + 0x18, Mc02Checksum(p, 0x18));
}

bool IsCar(const Bytes& image) {
    return U32(&image[kInUse]) != 0 && U32(&image[kKind]) == 4 && U32(&image[kModel]) != 0 &&
           image[kOccupied] == 1;
}

}  // namespace

Bytes SlotImage(const Bytes& save, int slot) {
    Bytes m(kSlotSize, 0);
    const uint8_t* s = &save[kCarSlots + slot * kSavedSlot];
    memcpy(&m[0x004], s, 0x430);  // car record: memory +0x004..+0x433
    m[kLocked]   = s[0x430];
    m[kOccupied] = s[0x431];
    for (size_t i = 0; i < 0x3C0 / 4; i++)  // the 0x3C0 block, memory +0x434..+0x7F3
        memcpy(&m[0x434 + BlockDwordToMemory(i) * 4], s + 0x432 + i * 4, 4);
    return m;
}

ProfileInfo ReadProfile(const Bytes& save) {
    ProfileInfo info;
    if (!CheckContainer(save, &info.error)) return info;
    info.ok = true;
    info.cash = static_cast<int32_t>(U32(&save[kCash]));
    info.stage = static_cast<int32_t>(U32(&save[kStage]));
    for (int n = 0; n < kSlots; n++) {
        Bytes m = SlotImage(save, n);
        if (!IsCar(m)) continue;
        Car c;
        c.slot = n;
        c.model = U32(&m[kModel]);
        c.parts = 0;
        for (int i = 0; i < 170; i++)
            if (U32(&m[kParts + i * 4])) c.parts++;
        c.spent = static_cast<int32_t>(U32(&m[kSpent]));
        info.cars.push_back(c);
    }
    return info;
}

Bytes MakeStoreFile(const Bytes& image, const std::string& profile) {
    Bytes f(kStoreHeader, 0);
    memcpy(&f[0], kStoreMagic, 8);
    PutU32(&f[8], 1);
    PutU32(&f[12], U32(&image[kModel]));
    PutU32(&f[16], kSlotSize);
    memcpy(&f[20], profile.data(), profile.size() < 31 ? profile.size() : 31);
    f.insert(f.end(), image.begin(), image.end());
    return f;
}

bool ReadStoreFile(const Bytes& file, Bytes* image) {
    if (file.size() < kStoreHeader + kSlotSize) return false;
    if (memcmp(&file[0], kStoreMagic, 8) != 0 || U32(&file[8]) != 1 || U32(&file[16]) != kSlotSize) return false;
    image->assign(file.begin() + kStoreHeader, file.begin() + kStoreHeader + kSlotSize);
    return IsCar(*image) && U32(&(*image)[kModel]) == U32(&file[12]);
}

bool SameCar(const Bytes& a, const Bytes& b) {
    return a.size() >= kSlotSize && b.size() >= kSlotSize &&
           memcmp(&a[kCarStart], &b[kCarStart], kCarEnd - kCarStart) == 0;
}

bool SetCash(Bytes& save, int32_t cash) {
    std::string why;
    if (!CheckContainer(save, &why)) return false;
    PutU32(&save[kCash], static_cast<uint32_t>(cash));
    FixChecksums(save);
    return true;
}

std::string StoreFolderName(const std::string& profile) {
    std::string s = profile.substr(0, 32);
    for (char& c : s)
        if (!IsStoreNameChar(c)) c = '_';
    return s;
}

std::string StoreFileName(long seq, uint32_t model, const std::string& fromProfile) {
    std::string from = StoreFolderName(fromProfile).substr(0, 20);
    char buf[96];
    snprintf(buf, sizeof(buf), "%06ld_%s_FROM_%s.vgcar", seq, CarName(model), from.c_str());
    return buf;
}

long StoreFileSeq(const std::string& fileName) {
    size_t n = 0;
    long seq = 0;
    while (n < fileName.size() && fileName[n] >= '0' && fileName[n] <= '9' && n < 9) seq = seq * 10 + (fileName[n++] - '0');
    return n == 0 ? -1 : seq;
}

}  // namespace st
