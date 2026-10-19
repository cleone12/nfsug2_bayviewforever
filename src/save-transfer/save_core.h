// NFSU2 Save Transfer: save-file logic, kept free of Windows code so it can be tested on the Mac
// (test_core.cpp). Layouts: docs/structs.md -> "Save file" and "Virtual garage store file".
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace st {

using Bytes = std::vector<uint8_t>;

// ---- Profile save (`<save folder>\<profile>\<profile>`, MC02 container) --------------------------
const size_t   kSaveSize     = 54966;   // every v1.2 profile save has this size
const uint32_t kMc02Magic    = 0x4D433032;  // 'MC02' (file bytes "20CM")
const size_t   kMc02Header   = 0x1C;    // magic, size, size A, size B, checksum A, checksum B, header checksum
const size_t   kPartA        = 0x1C;    // 8 bytes
const size_t   kPartB        = 0x24;    // the profile itself
const size_t   kCarSlots     = 0x24 + 0x484 + 20 * 0x430;  // 5 career slots in the car DB section
const size_t   kSavedSlot    = 0x7F2;   // career slot size in the save
const size_t   kCash         = 0xA16A;  // career cash (int), first field after the 8 stage records
const size_t   kStage        = 0xA175;  // current stage (int); 6 = career finished
const int      kSlots        = 5;

// ---- Career slot in memory / in a store file -----------------------------------------------------
const size_t kSlotSize = 0x7F8;
const size_t kInUse    = 0x010;
const size_t kKind     = 0x014;  // 4 = career slot
const size_t kModel    = 0x020;  // StringHash of the car's CARS\ folder name
const size_t kParts    = 0x024;  // 170 part keys
const size_t kSpent    = 0x7F0;
const size_t kLocked   = 0x7F4;
const size_t kOccupied = 0x7F5;
// The Virtual Garage copies +0x00C..+0x7F3 when it puts a car into a slot, so that range is "the car".
const size_t kCarStart = 0x00C;
const size_t kCarEnd   = kLocked;

// ---- Virtual garage store file (`virtual-garage\<profile>\NNNNNN_<CAR>.vgcar`) ---------------------
const size_t kStoreHeader = 64;  // "NFU2VGC1", u32 version 1, u32 model, u32 slot size, char[32] profile, 12 reserved

uint32_t StringHash(const char* s);
const char* CarName(uint32_t model);  // "unknown" if not a known CARS\ folder
uint32_t Mc02Checksum(const uint8_t* p, size_t n);

struct Car {
    int      slot;   // 0..4
    uint32_t model;
    int      parts;  // number of filled part positions
    int32_t  spent;  // cash spent on its parts
};

struct ProfileInfo {
    bool             ok = false;
    std::string      error;  // why it can't be used (when !ok)
    int32_t          cash  = 0;
    int32_t          stage = 0;
    std::vector<Car> cars;   // occupied career slots
};

// Checks the container (magic, sizes, all three checksums) and reads cash, stage and cars.
ProfileInfo ReadProfile(const Bytes& save);

// The car in career slot `slot` of a save, rebuilt in memory layout (0x7F8 bytes), as the
// Virtual Garage stores it. Bytes the save doesn't hold (vtable, padding) are 0.
Bytes SlotImage(const Bytes& save, int slot);

// A complete .vgcar file for a slot image; `profile` = the profile whose store it goes into.
Bytes MakeStoreFile(const Bytes& image, const std::string& profile);

// Same checks as the plugin's LoadStoreFile. On success *image = the 0x7F8-byte slot.
bool ReadStoreFile(const Bytes& file, Bytes* image);

// True if two slot images describe the same car (the range the Virtual Garage copies).
bool SameCar(const Bytes& a, const Bytes& b);

// Sets the cash in a save and recomputes its checksums. Returns false if the save isn't valid.
bool SetCash(Bytes& save, int32_t cash);

// Folder name the plugin uses for a profile's store (other characters become '_').
std::string StoreFolderName(const std::string& profile);

// File name for a new store file: NNNNNN_<CAR>_FROM_<profile>.vgcar (the plugin only reads the number).
std::string StoreFileName(long seq, uint32_t model, const std::string& fromProfile);

// Sequence number at the start of a store file name, or -1 if it isn't one.
long StoreFileSeq(const std::string& fileName);

}  // namespace st
