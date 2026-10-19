// Virtual Garage: a career profile can own more than 5 cars. Extra cars live in
// BayviewForever\virtual-garage\<profile>\NNNNNN_<CAR>.vgcar (64-byte header + the whole 0x7F8-byte slot); the game
// never sees more than its 5 slots.
//
//  - Trade-ins (from the start of a career): before the car lot overwrites an occupied slot (CarLot_ReplaceCar), the
//    old car is stored. Stored cars stay hidden until the career is finished (stage 6 or Career_IsFinished).
//  - Browsing (after the career): in car select, UP/DOWN on the highlighted slot previews stored cars in that slot (in
//    memory, redrawn by the screen's own scroll code). ACCEPT keeps them (originals go into the store; on another slot
//    after YES to "drive X instead?"), BACK or leaving the screen restores the originals. The slot title says
//    "STORED 2/3  UP/DOWN" (words, not icons: plain FE text draws no $NAME$ icons and '$' starts an icon token).
//  - Prices (after the career): a purchase that replaces a car costs money by the stage that unlocks it (GLOBALB chunk
//    0x34A1F); the car lot's question shows the price, and ACCEPT without enough cash shows the game's own dialog.
//  - Rachel's cars: after the career her 350Z, RX-8 and Evo presets become stored cars once per profile (built the way
//    the car lot builds a new car), customization is blocked, and her end-of-game SMS gets a P.S.
//  - Texts follow the game's language (EN/FR/DE/IT/ES/NL/SV/DA, else English), see kTexts.
// Store changes are queued and written only when the game saves the profile (CALL 0x004D9C0A); a profile load discards
// them, so the store always matches the save.
// Hooks: profile load (0x005347AB, 0x00534812), save (0x004D9C0A), CarLot_ReplaceCar (0x004EDD6B, 0x004EE0B8,
// 0x004EE0EF), menu pads FEng_ProcessPadsForPackage (0x00558CC4), car lot question (0x004EDCAC), slot title
// (0x004B0664, JMP 0x004EE310), car select Notify/destructor (vtable 0x00795D00), car lot Notify (0x00795CF0), shop
// entrance and tuning menu Notify (Rachel's cars).
// Undo: [Features] VirtualGarage = 0; stored cars stay in virtual-garage\ (delete it to forget them).

#define BF_TAG "VirtualGarage"
#include "nfsu2.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace nfsu2;

namespace {

const uintptr_t kProfileLoad = 0x005344D0;  // maybe_Profile_LoadFromBuffer
const uintptr_t kProfileLoadCallSites[] = {0x005347AB, 0x00534812};
const uintptr_t kCarLotCallSites[]      = {0x004EDD6B, 0x004EE0B8, 0x004EE0EF};
const uintptr_t kPadsCallSite           = 0x00558CC4;
const uintptr_t kSaveSerialize          = 0x0052A450;  // ECX += 0x10, JMP maybe_Profile_SaveToBuffer (RET 8)
const uintptr_t kSaveSerializeCallSite  = 0x004D9C0A;  // its only caller: the save-to-disk job FUN_004D9B80
const uintptr_t kTradeDialogCallSite    = 0x004EDCAC;  // car lot Notify: CALL kShowDialogFormatted (modes 2-5)
const uintptr_t kUpdateTexts            = 0x004B0140;  // UICareerCribCarSelect_UpdateTexts: fastcall(screen), RET
const uintptr_t kUpdateTextsCallSite    = 0x004B0664;  // CALL in UICareerCribCarSelect_Scroll
const uintptr_t kUpdateTextsJumpSite    = 0x004EE310;  // tail JMP in the car select setup FUN_004EE210
const size_t    kCarSelectReady         = 0x194;       // car select screen: byte, ACCEPT is ignored while 0
const uintptr_t kIsCareerMode           = 0x00850078;  // == 1 in career (checked before using the career car, e.g. 0x0055E820)

const int kBitUp   = 0;  // FE pad bit 0 -> PAD_UP   (direction table 0x007F7CFC)
const int kBitDown = 1;  // FE pad bit 1 -> PAD_DOWN

const CodeCheck kCodeChecks[] = {
    {0x005344D0, {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x24}, 6, "prologue of maybe_Profile_LoadFromBuffer"},
    {0x005344F9, {0x8D, 0x8B, 0xB0, 0x03, 0x00, 0x00}, 6, "LEA ECX,[EBX+0x3B0] (car DB inside profile)"},
    {0x005346E7, {0xC2, 0x0C, 0x00}, 3, "RET 0xC of maybe_Profile_LoadFromBuffer"},
    {0x005347AB, {0xE8, 0x20, 0xFD, 0xFF, 0xFF}, 5, "CALL maybe_Profile_LoadFromBuffer (site 1)"},
    {0x00534812, {0xE8, 0xB9, 0xFC, 0xFF, 0xFF}, 5, "CALL maybe_Profile_LoadFromBuffer (site 2)"},
    {0x005164ED, {0x83, 0xFF, 0x05}, 3, "CMP EDI,5 in CarDB_InitCareerSlots"},
    {0x00496050, {0x8B, 0x81, 0xEC, 0x00, 0x00, 0x00}, 6, "MOV EAX,[ECX+0xEC] in CarLot_ReplaceCar"},
    {0x005349AB, {0xC2, 0x08, 0x00}, 3, "RET 8 of CarDB_ReplaceCarInCareerSlot"},
    {0x004EDD6B, {0xE8, 0xE0, 0x82, 0xFA, 0xFF}, 5, "CALL CarLot_ReplaceCar (site 1)"},
    {0x004EE0B8, {0xE8, 0x93, 0x7F, 0xFA, 0xFF}, 5, "CALL CarLot_ReplaceCar (site 2)"},
    {0x004EE0EF, {0xE8, 0x5C, 0x7F, 0xFA, 0xFF}, 5, "CALL CarLot_ReplaceCar (site 3)"},
    {0x0054D4B0, {0x8B, 0x44, 0x24, 0x04, 0x81, 0xEC, 0xA0, 0x00}, 8, "prologue of FEng_ProcessPadsForPackage"},
    {0x0054E001, {0xC2, 0x04, 0x00}, 3, "RET 4 of FEng_ProcessPadsForPackage"},
    {0x00558CC4, {0xE8, 0xE7, 0x47, 0xFF, 0xFF}, 5, "CALL FEng_ProcessPadsForPackage"},
    // Career finished test (the game's own function; reads [ECX+0x78EC]+0x28, then Career_IsEventWon)
    {0x00513880, {0x8B, 0x81, 0xEC, 0x78, 0x00, 0x00, 0x8B, 0x50}, 8, "Career_IsFinished reads [ECX+0x78EC]"},
    {0x0051388F, {0xC3}, 1, "RET of Career_IsFinished"},
    // Car select screen: vtable entries, their prologues and return sizes
    {0x00795D00, {0x30, 0xFC, 0x4D, 0x00}, 4, "vtable_UICareerCribCarSelect[0] = destructor"},
    {0x00795D08, {0x50, 0xFC, 0x4D, 0x00}, 4, "vtable_UICareerCribCarSelect[2] = Notify"},
    {0x004DFC30, {0x56, 0x8B, 0xF1, 0xE8, 0xE8, 0xC0, 0xFE, 0xFF}, 8, "prologue of the car select destructor"},
    {0x004DFC4B, {0xC2, 0x04, 0x00}, 3, "RET 4 of the car select destructor"},
    {0x004DFC50, {0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x3D, 0x9E}, 8, "prologue of UICareerCribCarSelect_Notify"},
    {0x004E00D5, {0xC2, 0x10, 0x00}, 3, "RET 0x10 of UICareerCribCarSelect_Notify"},
    {0x004B04C0, {0x8B, 0x44, 0x24, 0x04, 0x3D, 0x9E, 0x40, 0x20}, 8, "prologue of UICareerCribCarSelect_Scroll"},
    {0x004B04CC, {0x8B, 0x4E, 0x54}, 3, "Scroll reads the highlighted entry at screen+0x54"},
    {0x004B066B, {0xC2, 0x04, 0x00}, 3, "RET 4 of UICareerCribCarSelect_Scroll"},
    // List entry layout, from the entry constructor FUN_00511DB0
    {0x00511DCD, {0x89, 0x86, 0x18, 0x09, 0x00, 0x00}, 6, "entry +0x918 = car type"},
    {0x00511DD7, {0x89, 0x8E, 0x1C, 0x09, 0x00, 0x00}, 6, "entry +0x91C = value"},
    {0x00511DE1, {0x88, 0x86, 0x14, 0x09, 0x00, 0x00}, 6, "entry +0x914 = rebuild flag"},
    {0x00511DE7, {0x89, 0x96, 0x20, 0x09, 0x00, 0x00}, 6, "entry +0x920 = slot hash"},
    // Helpers the plugin calls
    {0x00610130, {0x8B, 0x54, 0x24, 0x04, 0x56, 0x8B, 0x35, 0xCC}, 8, "prologue of CarTypes_FindByNameHash"},
    {0x00537B80, {0x56, 0x8B, 0x74, 0x24, 0x08, 0x85, 0xF6, 0x57}, 8, "prologue of FEPrintf"},
    {0x004B015B, {0x50, 0x68, 0xF4, 0x0E, 0x79, 0x00, 0x68, 0xE8}, 8, "game's own FEPrintf call for SLOT_TITLE"},
    {0x004B017E, {0x83, 0xC4, 0x14}, 3, "caller pops FEPrintf's arguments (cdecl)"},
    // Car lot screen
    {0x00795CF8, {0xE0, 0xD9, 0x4E, 0x00}, 4, "vtable_UICareerCarLot[2] = car lot Notify"},
    {0x004ED9E0, {0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x3D, 0x5E}, 8, "prologue of the car lot Notify"},
    {0x004EDA90, {0xC2, 0x10, 0x00}, 3, "RET 0x10 of the car lot Notify"},
    {0x004ED8A7, {0x89, 0x46, 0x4C}, 3, "car lot mode at screen+0x4C"},
    {0x004EDD56, {0x8B, 0x46, 0x58}, 3, "car lot highlighted entry at screen+0x58"},
    {0x004EDD5C, {0x8B, 0x80, 0x20, 0x09, 0x00, 0x00}, 6, "car lot entry key at +0x920"},
    // Car lot catalogue (g_CarDB+0, 0x1C each, key at +8, count at +0x540), from CarDB_ReplaceCarInCareerSlot
    {0x0053496C, {0x8B, 0x8E, 0x40, 0x05, 0x00, 0x00}, 6, "catalogue count at g_CarDB+0x540"},
    {0x0053497C, {0x8D, 0x56, 0x08}, 3, "catalogue key at +8"},
    {0x00534985, {0x83, 0xC2, 0x1C}, 3, "catalogue stride 0x1C"},
    // Car unlock tiers (FUN_00513C50) and helpers
    {0x00513C6C, {0xA1, 0x3C, 0x84, 0x83, 0x00}, 5, "unlock table count at 0x0083843C"},
    {0x00513C88, {0xA1, 0x38, 0x84, 0x83, 0x00}, 5, "unlock table pointer at 0x00838438"},
    {0x00570A30, {0xA1, 0x24, 0x4F, 0x86, 0x00, 0xC3}, 6, "unlock alternate-race flag 0x00864F24"},
    {0x004FFC90, {0x8B, 0x15, 0xE4, 0x83, 0x83, 0x00, 0x83, 0xFA}, 8, "prologue of GRace_FindByKey"},
    {0x0061013D, {0x8D, 0x8E, 0xD0, 0x00, 0x00, 0x00}, 6, "car type name hash at +0xD0"},
    {0x004FFA80, {0x8B, 0x54, 0x24, 0x04, 0xE8, 0x47, 0xFF, 0xFF}, 8, "prologue of the localized string lookup"},
    // The parts shop's "not enough cash" dialog (0x005592B8..)
    {0x005592BB, {0x68, 0xBE, 0x60, 0xAC, 0xD1}, 5, "shop pushes the not-enough-cash message"},
    {0x005592C6, {0xE8, 0x65, 0xF1, 0xFF, 0xFF, 0x83, 0xC4, 0x0C}, 8, "shop's cdecl call of the message dialog"},
    {0x005592D0, {0xC7, 0x05, 0x70, 0x89, 0x83, 0x00, 0xEC, 0x1B}, 8, "shop sets the dialog close message"},
    // Saving the profile to disk (FUN_004D9B80 -> FUN_0052A450 -> maybe_Profile_SaveToBuffer)
    {0x004D9C0A, {0xE8, 0x41, 0x08, 0x05, 0x00}, 5, "CALL FUN_0052A450 in the save-to-disk job"},
    {0x0052A450, {0x83, 0xC1, 0x10, 0xE9, 0x28, 0xFE, 0xFF, 0xFF}, 8, "ADD ECX,0x10; JMP maybe_Profile_SaveToBuffer"},
    {0x0052A438, {0xC2, 0x08, 0x00}, 3, "RET 8 of maybe_Profile_SaveToBuffer"},
    // Car lot trade-in question (modes 3/4/5 push: current car name, new car name, text hash)
    {0x004EDCAC, {0xE8, 0xDF, 0xAA, 0x06, 0x00}, 5, "CALL kShowDialogFormatted in the car lot Notify"},
    {0x004EDCB1, {0x83, 0xC4, 0x2C}, 3, "caller pops 11 arguments (cdecl, 2 varargs)"},
    {0x004EDC1B, {0x68, 0xD1, 0xF5, 0x92, 0x31}, 5, "mode 4 pushes the trade-in-or-choose text"},
    {0x004EDC86, {0x68, 0xC6, 0x5B, 0x21, 0x85}, 5, "modes 3/5 push the trade-in text"},
    {0x004EDDEA, {0x89, 0x56, 0x64}, 3, "car lot new car key at screen+0x64 (mode 5)"},
    {0x00558790, {0x8B, 0x54, 0x24, 0x24, 0x81, 0xEC, 0x00, 0x02}, 8, "prologue of kShowDialogFormatted"},
    {0x00558811, {0x81, 0xC4, 0x38, 0x02, 0x00, 0x00, 0xC3}, 7, "it passes 11 arguments to kShowDialogText"},
    {0x00558680, {0x81, 0xEC, 0x38, 0x03, 0x00, 0x00, 0x8D, 0x4C}, 8, "prologue of kShowDialogText"},
    // Car select slot title (UICareerCribCarSelect_UpdateTexts, fastcall(screen), tail JMP to FUN_004962F0 = RET)
    {0x004B0140, {0x53, 0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x5C, 0x8B}, 8, "prologue of UICareerCribCarSelect_UpdateTexts"},
    {0x004B04B6, {0xE9, 0x35, 0x5E, 0xFE, 0xFF}, 5, "UpdateTexts tail-jumps to FUN_004962F0"},
    {0x00496386, {0x5E, 0xC3}, 2, "plain RET of FUN_004962F0"},
    {0x004B0664, {0xE8, 0xD7, 0xFA, 0xFF, 0xFF}, 5, "CALL UpdateTexts in UICareerCribCarSelect_Scroll"},
    {0x004EE310, {0xE9, 0x2B, 0x1E, 0xFC, 0xFF}, 5, "tail JMP to UpdateTexts in the car select setup"},
    {0x005379C0, {0x56, 0x8B, 0x74, 0x24, 0x08, 0x85, 0xF6, 0x57}, 8, "prologue of kFEFindObject"},
    {0x00537B50, {0x8B, 0x46, 0x68, 0x83, 0xC4, 0x0C, 0x3D, 0x00}, 8, "FEPrintf limits text to object+0x68"},
    // Car select ACCEPT on another slot asks "drive X instead?"; YES = 0xD05FC3A3 -> Career_SetCurrentCar
    {0x004E00B1, {0x3D, 0xA3, 0xC3, 0x5F, 0xD0}, 5, "car select Notify handles 0xD05FC3A3"},
    {0x004DFD96, {0x8A, 0x86, 0x94, 0x01, 0x00, 0x00}, 6, "car select ACCEPT needs screen+0x194"},
    {0x004DFE56, {0x8B, 0x91, 0x20, 0x09, 0x00, 0x00, 0x3B, 0x50}, 8, "ACCEPT compares slot hash with the current car slot"},
    // FE text: the setter that grows the buffer, and the renderer's icon test
    {0x00535FE0, {0x53, 0x56, 0x57, 0x8B, 0xF9, 0x8B, 0x47, 0x64}, 8, "prologue of kFEStringSetText (obj+0x64 text)"},
    {0x00536046, {0xC2, 0x04, 0x00}, 3, "RET 4 of kFEStringSetText"},
    {0x00537B70, {0x83, 0x4E, 0x1C, 0x02}, 4, "FEObject_SetTextV sets flag 2 after the setter"},
    {0x0051C337, {0xA9, 0x20, 0x08, 0x00, 0x00, 0x75, 0x4E}, 7, "renderer tests flags & 0x820 before icons"},
    {0x0051C33E, {0x66, 0x83, 0xFE, 0x24}, 4, "renderer: '$' starts an icon token"},
    // FE packages and objects (object dump)
    {0x0052CEF0, {0xA1, 0xD0, 0x84, 0x83, 0x00, 0x85, 0xC0, 0x56}, 8, "prologue of FE_FindPackage"},
    {0x00504884, {0x8B, 0x73, 0x4C}, 3, "a package's first object is at +0x4C"},
    {0x00504836, {0x8B, 0x70, 0x64}, 3, "a group's first child is at +0x64"},
    {0x005048D4, {0x8B, 0x50, 0x10}, 3, "object name hash at +0x10"},
    {0x004EDC36, {0x68, 0x8F, 0xE7, 0x75, 0xFB}, 5, "mode 4 pushes the Trade this car? button text"},
    {0x004FF9D0, {0x53, 0x8B, 0x1D, 0xD8, 0x83, 0x83, 0x00}, 7, "Language_FindString reads the table at 0x008383D8"},
    {0x004FF9D9, {0x8B, 0x35, 0xD0, 0x83, 0x83, 0x00}, 6, "Language_FindString reads the count at 0x008383D0"},
    // Rachel's cars: the car lot's new-car steps (maybe_CarRecord_InitFromCarType 0x00516B90) and the preset loader
    {0x0061C460, {0x53, 0x56, 0x8B, 0x35, 0xE4, 0x31, 0x8A, 0x00}, 8, "preset list head [0x008A31E4] (find preset)"},
    {0x005AC6E0, {0x53, 0x55, 0xB8, 0x00, 0x00, 0x80, 0x3F, 0x56}, 8, "prologue of the RideInfo member init"},
    {0x00610270, {0x8B, 0x44, 0x24, 0x04, 0x8B, 0xD1, 0x8A, 0x4C}, 8, "prologue of RideInfo init (RET 0x10)"},
    {0x006102E0, {0xC2, 0x10, 0x00}, 3, "RET 0x10 of RideInfo init"},
    {0x0061C280, {0x83, 0xEC, 0x40, 0x56, 0x57, 0x8B, 0x7C, 0x24}, 8, "prologue of RideInfo dummy skins (RET 4)"},
    {0x00637040, {0x83, 0xEC, 0x10, 0x53, 0x55, 0x56, 0x57, 0x8B}, 8, "prologue of RideInfo stock parts (RET 4)"},
    {0x00637270, {0x8B, 0x44, 0x24, 0x04, 0x55, 0x57, 0x50, 0x8B}, 8, "prologue of RideInfo_LoadPreset (RET 4)"},
    {0x005B6990, {0x51, 0x56, 0x8B, 0xF1, 0x80, 0x7E, 0x04, 0xFF}, 8, "prologue of the rating (RET 8)"},
    {0x00503950, {0x53, 0x55, 0x8B, 0x6C, 0x24, 0x0C, 0x8B, 0x45}, 8, "prologue of CarRecord from RideInfo (RET 4)"},
    {0x0059AFE0, {0x8B, 0x54, 0x24, 0x04, 0x83, 0xFA, 0xFF, 0x75}, 8, "prologue of the tuning defaults (RET 4)"},
    {0x00516BC3, {0x8D, 0x4C, 0x24, 0x20}, 4, "new car: member init at rideInfo+0x10 (ESP+0x20 with 2 pushes)"},
    {0x00516BF2, {0x8D, 0x4C, 0x24, 0x20}, 4, "new car: RideInfo init on rideInfo (ESP+0x20 with 6 pushes)"},
    {0x00516C3E, {0x8D, 0x4C, 0x24, 0x28}, 4, "new car: rating on rideInfo+0x10 (ESP+0x28 with 4 pushes)"},
    {0x00516C47, {0xD9, 0x5E, 0x0C}, 3, "new car: rating stored at slot+0x0C"},
    {0x00516C4F, {0x8D, 0x4E, 0x18}, 3, "new car: record at slot+0x18"},
    {0x00516C57, {0x81, 0xC6, 0x60, 0x03, 0x00, 0x00}, 6, "new car: tuning defaults from slot+0x360"},
    {0x00516C6B, {0x83, 0xC6, 0x29}, 3, "new car: 0x29 bytes per tuning set"},
    // Rachel's cars: hands off (shop entrance dialog and tuning menu)
    {0x0078F980, {0x90, 0x96, 0x49, 0x00}, 4, "vtable_UIEngageShopDialog[2] = its Notify"},
    {0x00499690, {0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x3D, 0x64}, 8, "prologue of the shop entrance Notify (RET 0x10)"},
    {0x004998D1, {0xC2, 0x10, 0x00}, 3, "RET 0x10 of the shop entrance Notify"},
    {0x004B532F, {0xC7, 0x06, 0x78, 0xF9, 0x78, 0x00}, 6, "shop entrance dialog constructor sets the vtable"},
    {0x00499944, {0x68, 0x2A, 0x00, 0x2F, 0xD7}, 5, "shop entrance: accept button object 0xD72F002A"},
    {0x0079D34C, {0x10, 0xD9, 0x55, 0x00}, 4, "vtable_UIPerformanceTuningMaster[2] = its Notify"},
    {0x0055D910, {0x55, 0x8B, 0xEC, 0x56, 0x8B, 0xF1, 0x8B, 0x86}, 8, "prologue of the tuning menu Notify (RET 0x10)"},
    {0x0055DB05, {0xC2, 0x10, 0x00}, 3, "RET 0x10 of the tuning menu Notify"},
    {0x00558490, {0x81, 0xEC, 0x38, 0x03, 0x00, 0x00, 0x8D, 0x4C}, 8, "prologue of the one-button dialog"},
    {0x005585D4, {0xE8, 0xB7, 0xFE, 0xFF, 0xFF}, 5, "the game's own call of the one-button dialog"},
    {0x0050C900, {0x56, 0x8B, 0x74, 0x24, 0x08, 0x85, 0xF6, 0x74}, 8, "prologue of FE set label hash (cdecl)"},
    {0x004996E7, {0x8B, 0x42, 0x10, 0x3D, 0xE0, 0x7D, 0x85, 0x63}, 8, "shop entrance: Cancel compares object+0x10 with 0x63857DE0"},
    {0x00499925, {0x68, 0x7C, 0x4E, 0xA0, 0x04}, 5, "shop entrance: the question object 0x04A04E7C"},
    {0x00500BD0, {0x8B, 0x44, 0x24, 0x04, 0xC6, 0x81, 0x3D, 0x87}, 8, "Career_SetCurrentCar sets g_Career+0x873D/+0x8740"},
};

const uintptr_t kCarDB = 0x0083AD90;  // g_CarDB (player 1)

// Copied range when a car goes into a slot: everything except the slot's own
// vtable, +0x004, slot hash (+0x008) and locked flag (+0x7F4).
const size_t kCopyStart = 0x00C;
const size_t kCopyEnd   = slot::kLocked;

// Store file: 64-byte header + the whole 0x7F8-byte slot.
struct StoreHeader {
    char     magic[8];  // "NFU2VGC1"
    uint32_t version;   // 1
    uint32_t carModel;  // slot +0x020
    uint32_t slotSize;  // 0x7F8
    char     profile[32];
    uint32_t reserved[3];
};
static_assert(sizeof(StoreHeader) == 64, "store header must stay 64 bytes");
const char kStoreMagic[8] = {'N', 'F', 'U', '2', 'V', 'G', 'C', '1'};

volatile bool g_versionOk = false;
bool          g_hooksOk   = false;  // all code checks passed and the hooks are in place

// ---- Small helpers -----------------------------------------------------------

const char* ProfileName() {
    const char* name = reinterpret_cast<const char*>(kProfile1Name);
    return name[0] ? name : "";
}

// <game>\BayviewForever\virtual-garage\<profile>\  (created on demand). Returns false if no profile is loaded.
bool StoreDir(char out[MAX_PATH]) {
    char safe[kProfileNameSize + 1];
    const char* name = ProfileName();
    size_t n = 0;
    for (; n < kProfileNameSize && name[n]; n++) {
        char c = name[n];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_';
        safe[n] = ok ? c : '_';
    }
    safe[n] = '\0';
    if (n == 0) return false;

    char dir[MAX_PATH];
    GetDataDir(dir);
    char root[MAX_PATH];
    snprintf(root, sizeof(root), "%svirtual-garage\\", dir);
    CreateDirectoryA(root, nullptr);
    snprintf(out, MAX_PATH, "%s%s\\", root, safe);
    CreateDirectoryA(out, nullptr);
    return true;
}

struct StoredCar {
    long        seq;
    std::string path;            // file on disk, or "" for a car still waiting in the queue
    int         pendingId = -1;  // id in g_pendingWrites when the car is not on disk yet
};

// Store changes wait here until the game saves the profile (Hook_SaveProfile), so the store files
// and the save always describe the same garage. A profile load throws them away (files written for swaps the game
// never saved once made a duplicate car vanish).
struct PendingWrite {
    int                        id;
    std::string                dir;
    std::vector<unsigned char> image;  // kSlotSize bytes
    std::string                why;
    std::string                tag;    // added to the file name, e.g. "_RACHEL"
};
std::vector<PendingWrite> g_pendingWrites;
std::vector<std::string>  g_pendingDeletes;  // store files to delete
int                       g_nextPendingId = 1;
std::string               g_pendingRachelMarker;  // marker file to write at the next save (Rachel's cars given)

std::vector<StoredCar> ListStoreFiles(const char* dir);

bool IsPendingDelete(const std::string& path) {
    for (const std::string& d : g_pendingDeletes)
        if (d == path) return true;
    return false;
}

// All cars in a profile's store, oldest first: files on disk (minus queued deletes), then queued writes.
std::vector<StoredCar> ListStore(const char* dir) {
    std::vector<StoredCar> list = ListStoreFiles(dir);
    list.erase(std::remove_if(list.begin(), list.end(), [](const StoredCar& c) { return IsPendingDelete(c.path); }),
               list.end());
    long next = 0;
    for (const StoredCar& c : list) next = std::max(next, c.seq + 1);
    for (const PendingWrite& w : g_pendingWrites)
        if (w.dir == dir) list.push_back({next++, "", w.id});
    return list;
}

// Only the files on disk, oldest (lowest number) first.
std::vector<StoredCar> ListStoreFiles(const char* dir) {
    std::vector<StoredCar> list;
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s*.vgcar", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return list;
    do {
        char* end = nullptr;
        long seq = strtol(fd.cFileName, &end, 10);
        if (end == fd.cFileName) continue;  // not one of ours
        list.push_back({seq, std::string(dir) + fd.cFileName});
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(list.begin(), list.end(), [](const StoredCar& a, const StoredCar& b) { return a.seq < b.seq; });
    return list;
}

int FindSlotByHash(uint32_t hash) {
    for (int i = 0; i < kMaxSlots; i++)
        if (ReadAt<uint32_t>(SlotPtr(i), slot::kHash) == hash) return i;
    return -1;
}

bool IsCareerSlot(const unsigned char* s) {
    return ReadAt<uint32_t>(s, slot::kInUse) != 0 && ReadAt<uint32_t>(s, slot::kKind) == 4;
}

void LogGarage(const char* title) {
    Log("%s", title);
    for (int i = 0; i < kMaxSlots; i++) LogSlot(i, SlotPtr(i));
}

// Career finished = the current stage is 6 (set when Caleb's final race, the last event of stage 5,
// is won; no race belongs to stage 6) OR the game's own test Career_IsFinished (that race is won).
// Either one is enough, so a mod that changes event statuses can't hide the stored cars by accident.
bool CareerFinished() {
    if (*reinterpret_cast<volatile int32_t*>(ev::kCurrentStage) >= 6) return true;
    uintptr_t stage5 = *reinterpret_cast<volatile uintptr_t*>(ev::kCareer + ev::kCareerStage5Data);
    if (!stage5 || !IsReadable(stage5 + 0x28, 4)) return false;
    using Fn = unsigned char(__fastcall*)(uintptr_t);
    return reinterpret_cast<Fn>(kCareerIsFinished)(ev::kCareer) != 0;
}

// Writes a slot image (0x7F8 bytes) to a new store file in `dir`, after the files already there.
// Only called when the game saves (FlushPending). Returns false on any error.
bool WriteStoreFile(const char* dir, const unsigned char* s, const char* why, const char* tag = "") {
    CreateDirectoryA(dir, nullptr);
    uint32_t model = ReadAt<uint32_t>(s, slot::kCarModel);
    std::vector<StoredCar> files = ListStoreFiles(dir);
    long next = files.empty() ? 0 : files.back().seq + 1;

    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s%06ld_%s%s.vgcar", dir, next, CarName(model), tag);
    StoreHeader hdr = {};
    memcpy(hdr.magic, kStoreMagic, sizeof(hdr.magic));
    hdr.version  = 1;
    hdr.carModel = model;
    hdr.slotSize = kSlotSize;
    strncpy(hdr.profile, ProfileName(), sizeof(hdr.profile) - 1);

    FILE* f = fopen(path, "wb");
    if (!f) {
        Log("Cannot store %s (%s): could not create %s", CarName(model), why, path);
        return false;
    }
    bool ok = fwrite(&hdr, sizeof(hdr), 1, f) == 1 && fwrite(s, kSlotSize, 1, f) == 1;
    ok = (fclose(f) == 0) && ok;
    if (!ok) {
        Log("Cannot store %s (%s): write failed for %s", CarName(model), why, path);
        DeleteFileA(path);
        return false;
    }
    Log("Stored %s (%s) -> %s", CarName(model), why, path);
    return true;
}

// Reads a store file into buf (kSlotSize bytes). Returns false if it is not a valid car.
bool LoadStoreFile(const char* path, unsigned char* buf) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    StoreHeader hdr;
    bool ok = fread(&hdr, sizeof(hdr), 1, f) == 1 && fread(buf, kSlotSize, 1, f) == 1;
    fclose(f);
    if (!ok) return false;
    if (memcmp(hdr.magic, kStoreMagic, sizeof(hdr.magic)) != 0 || hdr.version != 1 || hdr.slotSize != kSlotSize)
        return false;
    // The stored bytes must describe an occupied career slot with a real car model.
    uint32_t model = ReadAt<uint32_t>(buf, slot::kCarModel);
    return buf[slot::kOccupied] == 1 && IsCareerSlot(buf) && model != 0 && model == hdr.carModel;
}

void PutCarInSlot(int n, const unsigned char* image) {
    unsigned char* s = SlotPtr(n);
    memcpy(s + kCopyStart, image + kCopyStart, kCopyEnd - kCopyStart);
    s[slot::kOccupied] = 1;
}

// Queues a car (slot image) to go into the store at the next game save.
bool QueueStore(const unsigned char* image, const char* why, const char* tag = "") {
    char dir[MAX_PATH];
    if (!StoreDir(dir)) {
        Log("Cannot store a car (%s): no profile name.", why);
        return false;
    }
    PendingWrite w;
    w.id = g_nextPendingId++;
    w.dir = dir;
    w.image.assign(image, image + kSlotSize);
    w.why = why;
    w.tag = tag;
    g_pendingWrites.push_back(std::move(w));
    Log("Queued %s for the store (%s); written when the game saves.", CarName(ReadAt<uint32_t>(image, slot::kCarModel)), why);
    return true;
}

// Takes a car out of the store at the next game save (or drops it from the queue if it never reached the disk).
void QueueRemove(const StoredCar& c) {
    if (c.pendingId >= 0) {
        g_pendingWrites.erase(std::remove_if(g_pendingWrites.begin(), g_pendingWrites.end(),
                                             [&](const PendingWrite& w) { return w.id == c.pendingId; }),
                              g_pendingWrites.end());
    } else {
        g_pendingDeletes.push_back(c.path);
    }
}

// Reads a stored car (file or queued) into buf. Returns false if it is not a valid car.
bool LoadStoredCar(const StoredCar& c, unsigned char* buf) {
    if (c.pendingId < 0) return LoadStoreFile(c.path.c_str(), buf);
    for (const PendingWrite& w : g_pendingWrites)
        if (w.id == c.pendingId) {
            memcpy(buf, w.image.data(), kSlotSize);
            return true;
        }
    return false;
}

std::string StoredCarLabel(const StoredCar& c) {
    return c.pendingId >= 0 ? std::string("(not saved yet)") : c.path;
}

// The game is writing the profile to disk: make the store match it.
void FlushPending() {
    if (g_pendingWrites.empty() && g_pendingDeletes.empty() && g_pendingRachelMarker.empty())
        return;
    int deleted = 0, written = 0;
    for (const std::string& path : g_pendingDeletes) {
        if (DeleteFileA(path.c_str())) deleted++;
        else Log("WARNING: could not delete %s", path.c_str());
    }
    for (const PendingWrite& w : g_pendingWrites)
        if (WriteStoreFile(w.dir.c_str(), w.image.data(), w.why.c_str(), w.tag.c_str())) written++;
    if (!g_pendingRachelMarker.empty()) {
        // Written together with her cars, so the gift happens exactly once per saved profile.
        FILE* f = fopen(g_pendingRachelMarker.c_str(), "w");
        if (f) {
            fputs("Rachel's cars were given to this profile. Delete this file to get them again.\n", f);
            fclose(f);
        }
        g_pendingRachelMarker.clear();
    }
    Log("Game saved the profile: store updated (%d written, %d removed).", written, deleted);
    g_pendingWrites.clear();
    g_pendingDeletes.clear();
    g_pendingRachelMarker.clear();
}

// A profile was loaded: the game went back to its last save, so unsaved store changes go too.
void DiscardPending(const char* why) {
    if (g_pendingWrites.empty() && g_pendingDeletes.empty() && g_pendingRachelMarker.empty())
        return;
    Log("Discarded %d unsaved store change(s) (%s): the game went back to its last save.",
        static_cast<int>(g_pendingWrites.size() + g_pendingDeletes.size()), why);
    g_pendingWrites.clear();
    g_pendingDeletes.clear();
    g_pendingRachelMarker.clear();
}

// ---- Settings (scripts\BayviewForever.ini, [VirtualGarage.*] sections) --------------------

bool g_pricesEnabled = true;
int  g_tierPrice[6] = {10000, 10000, 20000, 35000, 50000, 75000};  // by unlock stage (0-1 = starter cars)
bool g_detailedLog = false;  // every button press, menu change and the car select object list
bool g_rachelCars = true;    // after the career, Rachel's 350Z and RX-8 go into the store (once per profile)
bool g_rachelEvo = true;     // ... and her Evo (its preset lacks parts: filled from a stock Evo)

void LoadSettings() {
    char path[MAX_PATH];
    bf::IniPath(path);
    const bool found = bf::IniFound();
    g_pricesEnabled = GetPrivateProfileIntA("VirtualGarage.Prices", "Enabled", 1, path) != 0;
    static const char* const kKeys[6] = {"Starter", "Starter", "Stage2", "Stage3", "Stage4", "Stage5"};
    for (int i = 0; i < 6; i++) {
        int v = static_cast<int>(GetPrivateProfileIntA("VirtualGarage.Prices", kKeys[i], g_tierPrice[i], path));
        g_tierPrice[i] = v < 0 ? 0 : (v > 9999999 ? 9999999 : v);
    }
    g_detailedLog = GetPrivateProfileIntA("VirtualGarage.Log", "Detailed", 0, path) != 0;
    g_rachelCars = GetPrivateProfileIntA("VirtualGarage.Rachel", "Cars", 1, path) != 0;
    g_rachelEvo = GetPrivateProfileIntA("VirtualGarage.Rachel", "Evo", 1, path) != 0;
    Log("Settings %s: prices %s (starter %d, stage 2 %d, stage 3 %d, stage 4 %d, stage 5 %d), Rachel's cars %s "
        "(Evo %s), detailed log %s.",
        found ? "from BayviewForever.ini" : "(no BayviewForever.ini, defaults)", g_pricesEnabled ? "on" : "off",
        g_tierPrice[1], g_tierPrice[2], g_tierPrice[3], g_tierPrice[4], g_tierPrice[5], g_rachelCars ? "on" : "off",
        g_rachelEvo ? "on" : "off", g_detailedLog ? "on" : "off");
}

// ---- Texts in the game's language ---------------------------------------------------
// Latin-1, like the game's own LANGUAGES\*.bin (its font has all these letters). Car names come from the
// game. Wording and grammar follow the game's own texts in each language (e.g. its trade-in question);
// the "choose one of your other cars" line is the game's own sentence. Korean, Chinese and Japanese use
// other encodings/fonts, so they get English. In FE text '^' is a line break and '$' starts an icon token.

struct Texts {
    const char* lang;
    const char* detect;        // the game's text 0x2699CF8E ("Browse My Other Cars") in this language
    char        separator;     // thousands separator for prices
    const char* upDown;        // the browse buttons
    const char* storedCount;   // "%d STORED"
    const char* storedPos;     // "STORED %d/%d" (while previewing)
    const char* none;          // no stored cars yet
    const char* question;      // car lot: new car, price, old car
    const char* choose;        // appended in the "garage full, choose" question (game's own sentence)
    const char* button;        // replaces the "Trade this car?" button
    const char* questionFree;  // car lot with prices off: new car, old car
    const char* rachelPS;      // added to Rachel's end-of-game message when her cars are given
    const char* handsOff;      // shop entrance / tuning with one of Rachel's cars (starts with "Rachel")
    const char* endPrefix;     // the game's end-of-game message up to its "no more events" sentence (exact)
    const char* endNew;        // replaces that sentence when Endless Career is installed
};

const Texts kTexts[] = {
    {"EN",
     "Browse My Other Cars",  // detect
     ',',  // separator
     "UP/DOWN",  // upDown
     "%d STORED",  // storedCount
     "STORED %d/%d",  // storedPos
     "NO STORED CARS",  // none
     "Do you want to buy this stock %s for %s bank?^Your %s will be stored in your garage.",  // question
     "^Or do you want to choose one of your other cars?",  // choose
     "Store this car?",  // button
     "Do you want this stock %s?^Your %s will be stored in your garage.",  // questionFree
     "P.S. You can borrow my cars whenever I'm not out driving them - they're in your garage. Press UP/DOWN in Garage Car Select to find them.",  // rachelPS
     "Rachel: Hands off!^You can borrow my car, but nobody touches my setup.",  // handsOff
     "Congratulations - you have completed the game! You have unlocked all performance and visual upgrades in Quick Race mode. ",  // endPrefix
     "There are always new events to complete in the world, and you can re-race any old events from earlier stages in your Garage.",  // endNew
    },
    {"FR",
     "Parcourir mes voitures",  // detect
     '\xA0',  // separator
     "HAUT/BAS",  // upDown
     "%d EN R\xC9SERVE",  // storedCount
     "R\xC9SERVE %d/%d",  // storedPos
     "AUCUNE VOITURE EN R\xC9SERVE",  // none
     "Veux-tu acheter cette %s de s\xE9rie pour %s\xA0?^Ta voiture actuelle (%s) sera rang\xE9" "e dans ton garage.",  // question
     "^Ou veux-tu s\xE9lectionner une autre de tes voitures\xA0?",  // choose
     "Ranger cette voiture\xA0?",  // button
     "Veux-tu cette %s de s\xE9rie\xA0?^Ta voiture actuelle (%s) sera rang\xE9" "e dans ton garage.",  // questionFree
     "P.S. Tu peux emprunter mes voitures quand je ne suis pas au volant : elles sont dans ton garage. Appuie sur HAUT/BAS dans la s\xE9lection de voiture du garage pour les trouver.",  // rachelPS
     "Rachel : Pas touche !^Tu peux emprunter ma voiture, mais personne ne touche \xE0 mes r\xE9glages.",  // handsOff
     "F\xE9licitations, tu as termin\xE9 le jeu ! Tu as d\xE9verrouill\xE9 toutes les am\xE9liorations visuelles et de performances en mode Course imm\xE9" "diate. ",  // endPrefix
     "Il y a toujours de nouvelles \xE9preuves \xE0 accomplir dans Bayview, et tu peux recourir toute \xE9preuve des \xE9tapes pr\xE9" "c\xE9" "dentes au garage.",  // endNew
    },
    {"DE",
     "Eigene Wagen ansehen",  // detect
     '.',  // separator
     "AUF/AB",  // upDown
     "%d EINGELAGERT",  // storedCount
     "LAGER %d/%d",  // storedPos
     "KEINE WAGEN EINGELAGERT",  // none
     "M\xF6" "chtest du diesen serienm\xE4\xDFigen %s f\xFCr %s kaufen?^Dein %s wird in deiner Garage eingelagert.",  // question
     "^Oder m\xF6" "chtest du einen deiner anderen Wagen ausw\xE4hlen?",  // choose
     "Wagen einlagern",  // button
     "M\xF6" "chtest du diesen serienm\xE4\xDFigen %s?^Dein %s wird in deiner Garage eingelagert.",  // questionFree
     "P.S. Du kannst dir meine Wagen ausleihen, wann immer ich sie nicht selbst fahre - sie stehen in deiner Garage. Dr\xFC" "ck AUF/AB in der Wagenauswahl der Garage, um sie zu finden.",  // rachelPS
     "Rachel: Finger weg!^Du darfst meinen Wagen fahren, aber an meinem Setup wird nichts ver\xE4ndert.",  // handsOff
     "Herzlichen Gl\xFC" "ckwunsch - du hast das Spiel durchgespielt! Alle verf\xFCgbaren Tuning- und Optikteile wurden f\xFCr den Quickrace-Modus freigeschaltet. ",  // endPrefix
     "In Bayview gibt es immer neue Events, und in der Garage kannst du alle Events aus fr\xFCheren Etappen erneut fahren.",  // endNew
    },
    {"IT",
     "Esplora il tuo parco auto",  // detect
     '.',  // separator
     "SU/GI\xD9",  // upDown
     "%d IN DEPOSITO",  // storedCount
     "DEPOSITO %d/%d",  // storedPos
     "NESSUNA AUTO IN DEPOSITO",  // none
     "Vuoi comprare questa %s di serie per %s?^La tua %s verr\xE0 messa in deposito nel tuo garage.",  // question
     "^Oppure vuoi scegliere una delle tue altre auto?",  // choose
     "Mettere in deposito quest'auto?",  // button
     "Vuoi questa %s di serie?^La tua %s verr\xE0 messa in deposito nel tuo garage.",  // questionFree
     "P.S. Puoi prendere in prestito le mie auto quando non le sto guidando: sono nel tuo garage. Premi SU/GI\xD9 nella selezione auto del garage per trovarle.",  // rachelPS
     "Rachel: Gi\xF9 le mani!^Puoi prendere in prestito la mia auto, ma nessuno tocca la mia messa a punto.",  // handsOff
     "Congratulazioni - hai finito il gioco! Hai sbloccato tutti gli elementi prestazione ed estetici nella modalit\xE0 Gara veloce. ",  // endPrefix
     "Ci sono sempre nuovi eventi da completare nel gioco, e nel garage puoi rigiocare qualsiasi evento delle fasi precedenti.",  // endNew
    },
    {"ES",
     "Explorar mis coches",  // detect
     '.',  // separator
     "ARRIBA/ABAJO",  // upDown
     "%d GUARDADOS",  // storedCount
     "GUARDADO %d/%d",  // storedPos
     "NING\xDAN COCHE GUARDADO",  // none
     "\xBFQuieres comprar este %s de serie por %s?^Tu %s se guardar\xE1 en tu garaje.",  // question
     "^\xBFO quieres escoger otro de tus coches?",  // choose
     "\xBFGuardar este coche?",  // button
     "\xBFQuieres este %s de serie?^Tu %s se guardar\xE1 en tu garaje.",  // questionFree
     "P.D.: Puedes tomar prestados mis coches cuando no los est\xE9 conduciendo. Est\xE1n en tu garaje. Pulsa ARRIBA/ABAJO en la selecci\xF3n de coche del garaje para encontrarlos.",  // rachelPS
     "Rachel: \xA1Las manos quietas!^Puedes usar mi coche, pero nadie toca mi configuraci\xF3n.",  // handsOff
     "\xA1" "Felicidades! \xA1Has terminado el juego! Has activado todas las mejoras visuales y de rendimiento en el modo de Carrera r\xE1pida. ",  // endPrefix
     "Siempre hay nuevos eventos que completar en el mundo, y en el Garaje puedes volver a correr cualquier evento de las fases anteriores.",  // endNew
    },
    {"NL",
     "Blader door Mijn andere wagens",  // detect
     '.',  // separator
     "OP/NEER",  // upDown
     "%d OPGESLAGEN",  // storedCount
     "OPGESLAGEN %d/%d",  // storedPos
     "GEEN OPGESLAGEN WAGENS",  // none
     "Wil je deze standaard %s kopen voor %s?^Je %s wordt opgeslagen in je garage.",  // question
     "^Of wil je een van je andere auto's kiezen?",  // choose
     "Deze wagen opslaan?",  // button
     "Wil je deze standaard %s?^Je %s wordt opgeslagen in je garage.",  // questionFree
     "P.S. Je mag mijn auto's lenen wanneer ik er zelf niet in rijd. Ze staan in je garage. Druk op OP/NEER bij de autokeuze in de garage om ze te vinden.",  // rachelPS
     "Rachel: Handen thuis!^Je mag mijn auto lenen, maar niemand komt aan mijn afstelling.",  // handsOff
     "Gefeliciteerd, je hebt de game uitgespeeld. Vet hoor! Je hebt alle prestatie- en visuele upgrades in Quick Race vrijgespeeld. ",  // endPrefix
     "Er zijn altijd nieuwe events in deze wereld, en in je Garage kun je events uit eerdere fases nog eens racen.",  // endNew
    },
    {"SV",
     "V\xE4xla mellan mina bilar",  // detect
     '\xA0',  // separator
     "UPP/NER",  // upDown
     "%d LAGRADE",  // storedCount
     "LAGRAD %d/%d",  // storedPos
     "INGA LAGRADE BILAR",  // none
     "Vill du k\xF6pa denna fabriksnya %s f\xF6r %s?^Din %s st\xE4lls i ditt garage.",  // question
     "^Eller vill du v\xE4lja n\xE5gon annan av dina bilar?",  // choose
     "St\xE4ll undan denna bil?",  // button
     "Vill du ha denna fabriksnya %s?^Din %s st\xE4lls i ditt garage.",  // questionFree
     "PS. Du f\xE5r l\xE5na mina bilar n\xE4r jag inte k\xF6r dem sj\xE4lv. De st\xE5r i ditt garage. Tryck UPP/NER i garagets bilval f\xF6r att hitta dem.",  // rachelPS
     "Rachel: R\xF6r inte!^Du f\xE5r l\xE5na min bil, men ingen r\xF6r mina inst\xE4llningar.",  // handsOff
     "Grattis! Du har klarat av spelet! Du har l\xE5st upp alla prestandadelar och visuella uppgraderingar i Snabbracel\xE4get. ",  // endPrefix
     "Det finns alltid nya t\xE4vlingar att klara av, och i garaget kan du k\xF6ra om t\xE4vlingar fr\xE5n tidigare etapper.",  // endNew
    },
    {"DA",
     "Se mine andre biler",  // detect
     '.',  // separator
     "OP/NED",  // upDown
     "%d OPBEVARET",  // storedCount
     "OPBEVARET %d/%d",  // storedPos
     "INGEN OPBEVAREDE BILER",  // none
     "Vil du k\xF8" "be denne fabriks-%s for %s?^Din %s bliver opbevaret i din garage.",  // question
     "^Eller \xF8nsker du at v\xE6lge en af dine andre biler?",  // choose
     "Opbevar denne bil?",  // button
     "Vil du have denne fabriks-%s?^Din %s bliver opbevaret i din garage.",  // questionFree
     "PS. Du m\xE5 l\xE5ne mine biler, n\xE5r jeg ikke selv k\xF8rer i dem. De st\xE5r i din garage. Tryk OP/NED i garagens bilvalg for at finde dem.",  // rachelPS
     "Rachel: Fingrene v\xE6k!^Du m\xE5 l\xE5ne min bil, men ingen r\xF8rer mine indstillinger.",  // handsOff
     "Tillykke - du har gennemf\xF8rt spillet! Du har l\xE5st op for alle ydelses- og visuelle opgraderinger i Hurtigt l\xF8" "bsmode. ",  // endPrefix
     "Der er altid nye begivenheder at gennemf\xF8re i spilverdenen, og i garagen kan du k\xF8re begivenheder fra tidligere etaper igen.",  // endNew
    },
};

const uint32_t kMsgBrowseOtherCars = 0x2699CF8E;  // "Browse My Other Cars": different in every language

const Texts& T() {
    static int found = -1;
    if (found >= 0) return kTexts[found];
    using LocFn = const char*(__cdecl*)(uint32_t);
    const char* game = reinterpret_cast<LocFn>(kLocalizedString)(kMsgBrowseOtherCars);
    if (!game || !IsReadable(reinterpret_cast<uintptr_t>(game), 1)) return kTexts[0];  // language not loaded yet
    found = 0;
    for (int i = 0; i < static_cast<int>(sizeof(kTexts) / sizeof(kTexts[0])); i++)
        if (strncmp(game, kTexts[i].detect, strlen(kTexts[i].detect)) == 0) found = i;
    Log("Game language: %s%s.", kTexts[found].lang, found == 0 && strncmp(game, kTexts[0].detect, 20) != 0
                                                        ? " (not one of the 8 translated languages: English texts)" : "");
    return kTexts[found];
}

// The game's texts in memory (Language_FindString 0x004FF9D0): sorted {u32 hash, char* text} records. The plugin
// changes a few of these pointers to its own texts (car lot button, Rachel's message).
const uintptr_t kLanguageTable   = 0x008383D8;  // pointer to the records
const uintptr_t kLanguageCount   = 0x008383D0;

// ---- Rachel's cars --------------------------------------------------------------------
// After the career, Rachel lends the player her cars: the game's preset rides DDAY_PLAYER_CAR (her 350Z, the car
// you drive at the start of the career), DDAY_PLAYER_CAR_OLD_RX8 (an unused RX-8) and DDAY_PLAYER_CAR_OLD (an
// Evo whose preset leaves some part positions empty, e.g. no headlights). They go into the store once per
// profile (marker file rachel-cars.txt, written at the same game save as the cars), and her end-of-game
// message gets a P.S. A car is built the way the car lot builds a new one (maybe_CarRecord_InitFromCarType
// 0x00516B90: RideInfo set up for the car type with stock parts, rating, CarRecord from the RideInfo, tuning
// defaults), except that the preset's parts are applied with the game's RideInfo_LoadPreset before the record.

const uintptr_t kFindPreset          = 0x0061C460;  // cdecl(presetNameHash) -> preset (+8 model name, +0x4C 170 parts)
const uintptr_t kRideInfoInitMember  = 0x005AC6E0;  // fastcall(rideInfo + 0x10)
const uintptr_t kRideInfoInit        = 0x00610270;  // thiscall(rideInfo, carTypeIndex, int, char, char), RET 0x10
const uintptr_t kRideInfoDummySkins  = 0x0061C280;  // thiscall(rideInfo, 1), RET 4
const uintptr_t kRideInfoStockParts  = 0x00637040;  // thiscall(rideInfo, 0), RET 4
const uintptr_t kRideInfoLoadPreset  = 0x00637270;  // thiscall(rideInfo, presetNameHash), RET 4
const uintptr_t kRideInfoRating      = 0x005B6990;  // thiscall(rideInfo + 0x10, char, float), RET 8 -> float
const uintptr_t kRecordFromRideInfo  = 0x00503950;  // thiscall(slot + 0x18, rideInfo), RET 4
const uintptr_t kTuningDefaults      = 0x0059AFE0;  // thiscall(slot + 0x360 + i * 0x29, -1), RET 4
const size_t    kSlotRating = 0x00C, kSlotInUse = 0x010, kSlotKind = 0x014, kSlotRecord = 0x018, kSlotParts = 0x024,
                kSlotTuning = 0x360, kTuningSize = 0x29, kPresetParts = 0x4C, kNumParts = 170;

alignas(16) unsigned char g_rideInfo[0x1000];  // the game uses a 0x8F4-byte stack buffer for this

// Builds a store image of a preset ride (withPreset) or of the stock car of the same model. False if not found.
bool BuildRideImage(uintptr_t preset, bool withPreset, uint32_t presetHash, unsigned char* img) {
    const char* model = reinterpret_cast<const char*>(preset + 8);
    using FindTypeFn = uintptr_t(__cdecl*)(uint32_t);
    uintptr_t type = reinterpret_cast<FindTypeFn>(ev::kCarTypesFindByName)(StringHash(model));
    if (!type) return false;
    int typeIndex = *reinterpret_cast<int32_t*>(type + 0x840);
    uintptr_t ri = reinterpret_cast<uintptr_t>(g_rideInfo);
    memset(g_rideInfo, 0, sizeof(g_rideInfo));
    using MemberFn = void(__fastcall*)(uintptr_t);
    using InitFn = void(__fastcall*)(uintptr_t, void*, int, int, int, int);
    using IntFn = void(__fastcall*)(uintptr_t, void*, int);
    using RatingFn = float(__fastcall*)(uintptr_t, void*, int, float);
    using RecordFn = void(__fastcall*)(uintptr_t, void*, uintptr_t);
    reinterpret_cast<MemberFn>(kRideInfoInitMember)(ri + 0x10);
    reinterpret_cast<InitFn>(kRideInfoInit)(ri, nullptr, -1, 0, 0, 0);
    reinterpret_cast<InitFn>(kRideInfoInit)(ri, nullptr, typeIndex, 1, 0, 0);
    reinterpret_cast<IntFn>(kRideInfoDummySkins)(ri, nullptr, 1);
    reinterpret_cast<IntFn>(kRideInfoStockParts)(ri, nullptr, 0);
    if (withPreset) reinterpret_cast<IntFn>(kRideInfoLoadPreset)(ri, nullptr, static_cast<int>(presetHash));
    float rating = reinterpret_cast<RatingFn>(kRideInfoRating)(ri + 0x10, nullptr, 0, -1.0f);

    memset(img, 0, kSlotSize);  // a fresh career slot: 0x3C0 block cleared (maybe_CarCustomization_Clear)
    uintptr_t s = reinterpret_cast<uintptr_t>(img);
    *reinterpret_cast<float*>(s + kSlotRating) = rating;
    *reinterpret_cast<int32_t*>(s + kSlotInUse) = 1;
    *reinterpret_cast<int32_t*>(s + kSlotKind) = 4;
    reinterpret_cast<RecordFn>(kRecordFromRideInfo)(s + kSlotRecord, nullptr, ri);
    for (int i = 0; i < 5; i++) reinterpret_cast<IntFn>(kTuningDefaults)(s + kSlotTuning + i * kTuningSize, nullptr, -1);
    img[slot::kOccupied] = 1;
    return true;
}

// Rachel's car from a preset. fillFromStock: part positions the preset leaves empty get the stock car's part.
bool BuildRachelCar(const char* presetName, bool fillFromStock, unsigned char* img) {
    uint32_t hash = StringHash(presetName);
    using FindPresetFn = uintptr_t(__cdecl*)(uint32_t);
    uintptr_t preset = reinterpret_cast<FindPresetFn>(kFindPreset)(hash);
    if (!preset || !IsReadable(preset, kPresetParts + kNumParts * 4)) {
        Log("Rachel's car %s: preset not found.", presetName);
        return false;
    }
    if (!BuildRideImage(preset, true, hash, img)) {
        Log("Rachel's car %s: car type %s not found.", presetName, reinterpret_cast<const char*>(preset + 8));
        return false;
    }
    int filled = 0;
    if (fillFromStock) {
        static unsigned char stock[kSlotSize];
        if (BuildRideImage(preset, false, hash, stock)) {
            for (size_t i = 0; i < kNumParts; i++) {
                uint32_t mine = ReadAt<uint32_t>(img, kSlotParts + i * 4);
                uint32_t wanted = *reinterpret_cast<const uint32_t*>(preset + kPresetParts + i * 4);
                uint32_t stockPart = ReadAt<uint32_t>(stock, kSlotParts + i * 4);
                (void)wanted;  // her preset also names parts the Evo doesn't have (lights): they end up empty too
                if (mine == 0 && stockPart != 0) {
                    *reinterpret_cast<uint32_t*>(img + kSlotParts + i * 4) = stockPart;
                    filled++;
                }
            }
        }
    }
    if (fillFromStock) {
        // Her Evo's lights (picked in the body shop): part position -> part.
        static const struct { size_t pos; uint32_t part; } kEvoLights[] = {
            {13, 0x142C4B7D}, {14, 0x39C7C0D0}, {15, 0x56D59EE9}, {27, 0xC6578696}};
        for (const auto& l : kEvoLights) *reinterpret_cast<uint32_t*>(img + kSlotParts + l.pos * 4) = l.part;
    }
    int parts = 0;
    for (size_t i = 0; i < kNumParts; i++) parts += ReadAt<uint32_t>(img, kSlotParts + i * 4) != 0;
    Log("Built Rachel's %s from %s: %d parts%s.", CarName(ReadAt<uint32_t>(img, slot::kCarModel)), presetName, parts,
        fillFromStock ? (std::string(", ") + std::to_string(filled) + " filled from the stock car").c_str() : "");
    return true;
}

// Rachel's three cars, built once per game run from the presets. Also her cars' fingerprint: a car is Rachel's
// when its model and all 170 parts equal one of these (customization is blocked, so they stay equal).
struct RachelCar {
    const char*   preset;
    bool          evo;
    bool          built;
    bool          ok;
    unsigned char img[kSlotSize];
};
RachelCar g_rachel[3] = {{"DDAY_PLAYER_CAR", false, false, false, {}},
                         {"DDAY_PLAYER_CAR_OLD_RX8", false, false, false, {}},
                         {"DDAY_PLAYER_CAR_OLD", true, false, false, {}}};

void EnsureRachelCars() {  // main thread only (calls game functions)
    for (RachelCar& r : g_rachel) {
        if (r.built) continue;
        r.built = true;
        r.ok = BuildRachelCar(r.preset, r.evo, r.img);
    }
}

// Index into g_rachel if this slot image is one of Rachel's cars, else -1. Needs EnsureRachelCars() first.
int RachelCarIndex(const unsigned char* s) {
    for (int i = 0; i < 3; i++) {
        const RachelCar& r = g_rachel[i];
        if (r.ok && ReadAt<uint32_t>(s, slot::kCarModel) == ReadAt<uint32_t>(r.img, slot::kCarModel) &&
            memcmp(s + kSlotParts, r.img + kSlotParts, kNumParts * 4) == 0)
            return i;
    }
    return -1;
}

// Once per profile, after the career: Rachel's cars go into the store (written at the next game save).
void GiveRachelCars() {
    if (!g_versionOk || !g_rachelCars || !g_pendingRachelMarker.empty()) return;
    if (*reinterpret_cast<volatile int32_t*>(kIsCareerMode) != 1 || !CareerFinished()) return;
    char dir[MAX_PATH];
    if (!StoreDir(dir)) return;
    std::string marker = std::string(dir) + "rachel-cars.txt";
    if (GetFileAttributesA(marker.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return;
    }
    EnsureRachelCars();
    int given = 0;
    for (const RachelCar& r : g_rachel) {
        if (r.evo && !g_rachelEvo) continue;
        if (r.ok && QueueStore(r.img, "Rachel's car", "_RACHEL")) given++;
    }
    if (given == 0) {
        Log("Rachel's cars could not be built (see above); trying again next time.");
        return;
    }
    g_pendingRachelMarker = marker;
    Log("Rachel lends the player %d car(s); they are written to the store at the next game save.", given);
}

// True if the player's current career car is one of Rachel's and may not be customized.
bool CurrentCarIsRachelsLocked() {
    if (!g_versionOk || !g_rachelCars || *reinterpret_cast<volatile int32_t*>(kIsCareerMode) != 1) return false;
    int n = FindSlotByHash(*reinterpret_cast<volatile uint32_t*>(kCurrentSlotHash));
    if (n < 0) return false;
    EnsureRachelCars();
    int r = RachelCarIndex(SlotPtr(n));
    if (r < 0) return false;
    return true;
}

// True if BayviewForever's Endless Career feature is switched on ([Features] EndlessCareer).
bool EndlessCareerLoaded() {
    static bool logged = false;
    const bool on = bf::FeatureOn(bf::kEndlessCareer);
    if (on && !logged) Log("Endless Career is on.");
    logged = true;
    return on;
}

// Rachel's end-of-game message (SMS_END_OF_GAME_MESSAGE, delivered by the game when the career is finished):
// with Endless Career its "no more events" sentence is replaced (if the game's text starts with the known opening),
// and with Rachel's cars it gets her P.S. Done by pointing its language table record at the plugin's text, so
// nothing is added to the save.
void SetRachelMessage() {
    static char text[2048];
    if (!g_versionOk) return;
    bool endless = EndlessCareerLoaded();
    if (!endless && !g_rachelCars) return;
    uintptr_t table = *reinterpret_cast<volatile uintptr_t*>(kLanguageTable);
    int count = *reinterpret_cast<volatile int32_t*>(kLanguageCount);
    if (!table || count <= 0 || count > 100000 || !IsReadable(table, count * 8)) return;
    const uint32_t hash = StringHash("SMS_END_OF_GAME_MESSAGE_BODY");
    for (int i = 0; i < count; i++) {
        if (*reinterpret_cast<uint32_t*>(table + i * 8) != hash) continue;
        const char** entry = reinterpret_cast<const char**>(table + i * 8 + 4);
        if (*entry == text) return;  // already done
        const char* original = *entry;
        if (!original || !IsReadable(reinterpret_cast<uintptr_t>(original), 1)) return;
        const Texts& t = T();
        int len;
        bool replaced = endless && strncmp(original, t.endPrefix, strlen(t.endPrefix)) == 0;
        if (replaced) len = snprintf(text, sizeof(text), "%s%s", t.endPrefix, t.endNew);
        else len = snprintf(text, sizeof(text), "%s", original);
        if (g_rachelCars && len > 0 && len < static_cast<int>(sizeof(text)))
            snprintf(text + len, sizeof(text) - len, "^^%s", t.rachelPS);
        *entry = text;
        Log("Rachel's end-of-game message changed (%s%s).", replaced ? "Endless Career sentence" : "game text",
            g_rachelCars ? " + her P.S." : "");
        return;
    }
}

// ---- Car select previews --------------------------------------------------------

uintptr_t g_carSelect = 0;  // the live car select screen (remembered by Hook_CarSelectNotify)

struct Preview {
    bool          active = false;
    int           storeIndex = -1;  // index into g_previewList
    unsigned char original[kSlotSize];
};
Preview                g_preview[kMaxSlots];
std::vector<StoredCar> g_previewList;  // the store as it was when the first preview started

bool AnyPreview() {
    for (const Preview& p : g_preview)
        if (p.active) return true;
    return false;
}

bool StoreIndexInUse(int index, int exceptSlot) {
    for (int i = 0; i < kMaxSlots; i++)
        if (i != exceptSlot && g_preview[i].active && g_preview[i].storeIndex == index) return true;
    return false;
}

bool IsLiveCarSelect(uintptr_t screen) {
    return screen && IsReadable(screen, 0x60) && *reinterpret_cast<uintptr_t*>(screen) == carsel::kVtable;
}

// Puts every original back (BACK, or the screen closing). Nothing on disk changes.
void RevertPreviews(const char* why) {
    if (!AnyPreview()) return;
    for (int i = 0; i < kMaxSlots; i++) {
        if (!g_preview[i].active) continue;
        PutCarInSlot(i, g_preview[i].original);
        g_preview[i].active = false;
        Log("Preview reverted (%s): slot %d is %s again.", why, i,
            CarName(ReadAt<uint32_t>(SlotPtr(i), slot::kCarModel)));
    }
    g_previewList.clear();
}

// Keeps every previewed car (ACCEPT): originals go into the store, used store files are removed.
void CommitPreviews() {
    if (!AnyPreview()) return;
    for (int i = 0; i < kMaxSlots; i++) {
        Preview& p = g_preview[i];
        if (!p.active) continue;
        // Both changes wait for the next game save, together with the slot itself.
        if (!QueueStore(p.original, "swapped out in car select")) {
            PutCarInSlot(i, p.original);
            Log("Could not store slot %d's original, so it was put back instead.", i);
        } else {
            QueueRemove(g_previewList[p.storeIndex]);
            Log("Kept: slot %d is now %s.", i, CarName(ReadAt<uint32_t>(SlotPtr(i), slot::kCarModel)));
        }
        p.active = false;
    }
    g_previewList.clear();
    char dir[MAX_PATH];
    if (StoreDir(dir)) Log("Store now holds %d car(s) (counting unsaved changes).", static_cast<int>(ListStore(dir).size()));
}

// Redraws the highlighted entry with the screen's own scroll code: point the screen at the
// neighbouring entry and scroll back onto this one. The game then rebuilds the entry's car
// (rebuild flag set), loads the model, updates the texts and plays its car-change animation.
void RedrawHighlighted(uintptr_t screen, uintptr_t entry, const unsigned char* s) {
    using FindTypeFn = uintptr_t(__cdecl*)(uint32_t);
    uintptr_t type = reinterpret_cast<FindTypeFn>(ev::kCarTypesFindByName)(ReadAt<uint32_t>(s, slot::kCarModel));
    if (!type) {
        Log("  Not redrawing: unknown car model.");
        return;
    }
    *reinterpret_cast<uintptr_t*>(entry + carsel::kCarType) = type;
    *reinterpret_cast<uint32_t*>(entry + carsel::kValue)    = ReadAt<uint32_t>(s, 0x00C);
    *reinterpret_cast<uint8_t*>(entry + carsel::kDirty)     = 1;

    uintptr_t firstLink = *reinterpret_cast<uintptr_t*>(screen + carsel::kFirst);
    uintptr_t lastLink  = *reinterpret_cast<uintptr_t*>(screen + carsel::kLast);
    uintptr_t first = firstLink ? firstLink - carsel::kLinkOffset : 0;
    uintptr_t last  = lastLink ? lastLink - carsel::kLinkOffset : 0;
    int32_t index = *reinterpret_cast<int32_t*>(screen + carsel::kIndex);

    using ScrollFn = void(__fastcall*)(uintptr_t, void*, uint32_t);  // thiscall(screen, msg), RET 4
    if (entry != first) {
        uintptr_t prev = *reinterpret_cast<uintptr_t*>(entry + carsel::kPrevLink) - carsel::kLinkOffset;
        *reinterpret_cast<uintptr_t*>(screen + carsel::kHighlighted) = prev;
        *reinterpret_cast<int32_t*>(screen + carsel::kIndex) = index - 1;
        reinterpret_cast<ScrollFn>(carsel::kScroll)(screen, nullptr, kMsgPadRight);
    } else if (entry != last) {
        uintptr_t next = *reinterpret_cast<uintptr_t*>(entry + carsel::kNextLink) - carsel::kLinkOffset;
        *reinterpret_cast<uintptr_t*>(screen + carsel::kHighlighted) = next;
        *reinterpret_cast<int32_t*>(screen + carsel::kIndex) = index + 1;
        reinterpret_cast<ScrollFn>(carsel::kScroll)(screen, nullptr, kMsgPadLeft);
    }
    if (*reinterpret_cast<uintptr_t*>(screen + carsel::kHighlighted) != entry ||
        *reinterpret_cast<int32_t*>(screen + carsel::kIndex) != index)
        Log("  WARNING: after redrawing, the screen highlights %08X index %d (expected %08X index %d).",
            static_cast<unsigned>(*reinterpret_cast<uintptr_t*>(screen + carsel::kHighlighted)),
            *reinterpret_cast<int32_t*>(screen + carsel::kIndex), static_cast<unsigned>(entry), index);
}

void SetSlotTitle(uintptr_t screen, const char* text) {
    const char* package = *reinterpret_cast<const char* const*>(screen + carsel::kPackageName);
    using PrintfFn = int(__cdecl*)(const char*, uint32_t, const char*, ...);
    reinterpret_cast<PrintfFn>(kFEPrintf)(package, StringHash("SLOT_TITLE"), "%s", text);
}

// FE objects (from FE_FindObject's visitor 0x005048D0, FEObject_Show, 0x0050C990): +0x04 next, +0x10 name hash,
// +0x18 type (1 image, 2 text, 5 group), +0x1C flags, +0x24 image texture hash; groups: +0x60 child count,
// +0x64 first child. A package's first object is at +0x4C.
const uintptr_t kFEFindPackage = 0x0052CEF0;  // cdecl(packageName) -> package or 0

// Logs every object of a menu (once per game run), to find an image object that could show an up/down arrow
// next to the slot title (FE text only draws '$NAME$' icons in the game's multi-line help widget 0x005446F0).
void DumpObjects(uintptr_t first, int depth, int& budget) {
    for (uintptr_t o = first; o && budget > 0; o = *reinterpret_cast<uintptr_t*>(o + 4)) {
        if (!IsReadable(o, 0x80)) {
            Log("  (unreadable object %08X)", static_cast<unsigned>(o));
            return;
        }
        budget--;
        const unsigned char* b = reinterpret_cast<const unsigned char*>(o);
        char hex[0x80 * 3 + 1];
        for (int i = 0; i < 0x80; i++) snprintf(hex + i * 3, 4, "%02X ", b[i]);
        uint32_t type = ReadAt<uint32_t>(b, 0x18);
        Log("  %*sobj %08X hash %08X type %u flags %08X img %08X | %s", depth * 2, "", static_cast<unsigned>(o),
            static_cast<unsigned>(ReadAt<uint32_t>(b, 0x10)), static_cast<unsigned>(type),
            static_cast<unsigned>(ReadAt<uint32_t>(b, 0x1C)), static_cast<unsigned>(ReadAt<uint32_t>(b, 0x24)), hex);
        if (type == 5 && depth < 6) DumpObjects(ReadAt<uintptr_t>(b, 0x64), depth + 1, budget);
    }
}

void DumpPackageObjects(const char* name) {
    using FindFn = uintptr_t(__cdecl*)(const char*);
    uintptr_t pkg = reinterpret_cast<FindFn>(kFEFindPackage)(name);
    if (!pkg || !IsReadable(pkg, 0x50)) {
        Log("Object list of %s: package not found.", name);
        return;
    }
    Log("Object list of %s (package %08X; per object: address, name hash, type, flags, image, first 0x80 bytes):", name,
        static_cast<unsigned>(pkg));
    int budget = 400;
    DumpObjects(*reinterpret_cast<uintptr_t*>(pkg + 0x4C), 0, budget);
    Log("End of object list (%d objects).", 400 - budget);
}

// Sets an FE text object's text without FEPrintf's cap at the object's capacity (the game's setter grows
// the buffer), then marks it changed like FEObject_SetTextV does.
void SetTextObject(uintptr_t obj, const char* text) {
    using SetFn = void(__fastcall*)(uintptr_t, void*, const char*);  // thiscall(obj, text), RET 4
    reinterpret_cast<SetFn>(kFEStringSetText)(obj, nullptr, text);
    *reinterpret_cast<uint32_t*>(obj + feobj::kFlags) |= 2;
}

// After the career the slot title shows that stored cars can be browsed: "Slot 5  UP/DOWN: 3 STORED", or
// "STORED 2/3  UP/DOWN" while previewing.
void ApplySlotTitle(uintptr_t screen) {
    if (!g_versionOk || !IsLiveCarSelect(screen)) return;
    if (*reinterpret_cast<volatile int32_t*>(kIsCareerMode) != 1 || !CareerFinished()) return;
    uintptr_t entry = *reinterpret_cast<uintptr_t*>(screen + carsel::kHighlighted);
    if (!entry || *reinterpret_cast<uint8_t*>(entry + carsel::kLockedFlag) ||
        *reinterpret_cast<uint8_t*>(entry + carsel::kEmptyFlag))
        return;
    int n = FindSlotByHash(*reinterpret_cast<uint32_t*>(entry + carsel::kSlotHash));
    if (n < 0) return;

    const char* package = *reinterpret_cast<const char* const*>(screen + carsel::kPackageName);
    using FindFn = uintptr_t(__cdecl*)(const char*, uint32_t);
    uintptr_t obj = reinterpret_cast<FindFn>(kFEFindObject)(package, StringHash("SLOT_TITLE"));
    if (!obj || *reinterpret_cast<int32_t*>(obj + feobj::kType) != 2) return;
    uint32_t flags = *reinterpret_cast<uint32_t*>(obj + feobj::kFlags);
    static bool loggedFlags = false;
    if (!loggedFlags) {
        Log("Slot title object: capacity %d characters, flags 0x%X.", *reinterpret_cast<int32_t*>(obj + feobj::kMaxLength),
            static_cast<unsigned>(flags));
        loggedFlags = true;
    }

    char text[160];
    if (g_preview[n].active) {
        int len = snprintf(text, sizeof(text), T().storedPos, g_preview[n].storeIndex + 1,
                           static_cast<int>(g_previewList.size()));
        if (len > 0 && len < static_cast<int>(sizeof(text))) snprintf(text + len, sizeof(text) - len, "  %s", T().upDown);
    } else {
        int count = static_cast<int>(g_previewList.size());
        if (!AnyPreview()) {
            char dir[MAX_PATH];
            if (!StoreDir(dir)) return;
            count = static_cast<int>(ListStore(dir).size());
        }
        using LocFn = const char*(__cdecl*)(uint32_t);
        const char* word = reinterpret_cast<LocFn>(kLocalizedString)(kMsgSlotWord);
        if (!word) word = "Slot";
        int len = snprintf(text, sizeof(text), "%s %d  %s: ", word, *reinterpret_cast<int32_t*>(screen + carsel::kIndex),
                           T().upDown);
        if (len > 0 && len < static_cast<int>(sizeof(text))) snprintf(text + len, sizeof(text) - len, T().storedCount, count);
    }
    SetTextObject(obj, text);
}

// UP (+1) / DOWN (-1) on the highlighted car select entry.
void PreviewStep(uintptr_t screen, int dir) {
    if (*reinterpret_cast<volatile int32_t*>(kIsCareerMode) != 1) return;
    if (!CareerFinished()) {
        static bool told = false;
        if (!told) Log("Career not finished in this profile: stored cars stay hidden until it is.");
        told = true;
        return;
    }
    uintptr_t entry = *reinterpret_cast<uintptr_t*>(screen + carsel::kHighlighted);
    if (!entry) return;
    if (*reinterpret_cast<uint8_t*>(entry + carsel::kLockedFlag) || *reinterpret_cast<uint8_t*>(entry + carsel::kEmptyFlag)) {
        Log("Stored cars can only be shown on a slot that has a car.");
        return;
    }
    int n = FindSlotByHash(*reinterpret_cast<uint32_t*>(entry + carsel::kSlotHash));
    if (n < 0 || !IsCareerSlot(SlotPtr(n)) || !SlotPtr(n)[slot::kOccupied]) {
        Log("Highlighted entry is not an occupied career slot.");
        return;
    }
    if (!AnyPreview()) {
        char dir[MAX_PATH];
        if (!StoreDir(dir)) return;
        g_previewList = ListStore(dir);
    }
    int count = static_cast<int>(g_previewList.size());
    if (count == 0) {
        Log("No stored cars for this profile yet (trade a car in at the car lot to store it).");
        SetSlotTitle(screen, T().none);
        return;
    }

    // Positions: -1 = the slot's own car, 0..count-1 = stored cars (skipping those previewed in other slots).
    Preview& p = g_preview[n];
    int pos = p.active ? p.storeIndex : -1;
    for (int tries = 0; tries <= count; tries++) {
        pos += dir;
        if (pos >= count) pos = -1;
        if (pos < -1) pos = count - 1;
        if (pos == -1 || !StoreIndexInUse(pos, n)) break;
    }

    if (pos == -1) {
        if (p.active) PutCarInSlot(n, p.original);
        p.active = false;
        Log("Slot %d: back to its own car %s.", n, CarName(ReadAt<uint32_t>(SlotPtr(n), slot::kCarModel)));
    } else {
        static unsigned char incoming[kSlotSize];
        if (!LoadStoredCar(g_previewList[pos], incoming)) {
            Log("%s is not a valid stored car; skipped.", StoredCarLabel(g_previewList[pos]).c_str());
            return;
        }
        if (!p.active) memcpy(p.original, SlotPtr(n), kSlotSize);
        p.active = true;
        p.storeIndex = pos;
        PutCarInSlot(n, incoming);
        Log("Slot %d: previewing stored car %d/%d %s (%s).", n, pos + 1, count,
            CarName(ReadAt<uint32_t>(incoming, slot::kCarModel)), StoredCarLabel(g_previewList[pos]).c_str());
    }

    RedrawHighlighted(screen, entry, SlotPtr(n));  // the scroll's UpdateTexts hook writes the title
    ApplySlotTitle(screen);                         // (again, in case the redraw could not scroll)
}

// ---- Car prices after the career ------------------------------------------------

// After the career the store is revealed (car lot texts say so); prices apply if enabled in the settings.
bool StoreRevealed() {
    return *reinterpret_cast<volatile int32_t*>(kIsCareerMode) == 1 && CareerFinished();
}

bool PricesActive() {
    return g_pricesEnabled && StoreRevealed();
}

// Stage of the race that unlocks this car (same table and rule as FUN_00513C50); 0 = always unlocked.
int CarTier(uint32_t nameHash) {
    uintptr_t table = *reinterpret_cast<volatile uintptr_t*>(kCarUnlockTablePtr);
    int count = *reinterpret_cast<volatile int32_t*>(kCarUnlockCount);
    if (!table || count <= 0 || count > 256 || !IsReadable(table, count * 12)) return 0;
    bool alt = *reinterpret_cast<volatile int32_t*>(kCarUnlockUseAlt) != 0;
    for (int i = 0; i < count; i++) {
        const uint32_t* e = reinterpret_cast<const uint32_t*>(table + i * 12);
        if (e[0] != nameHash) continue;
        using FindRaceFn = uintptr_t(__cdecl*)(uint32_t);
        uintptr_t race = reinterpret_cast<FindRaceFn>(kGRaceFindByKey)(alt ? e[2] : e[1]);
        return race ? *reinterpret_cast<uint8_t*>(race + ev::race::kStage) : 0;
    }
    return 0;
}

int PriceOfCarType(uintptr_t type) {
    if (!type) return -1;
    int tier = CarTier(*reinterpret_cast<uint32_t*>(type + kCarTypeNameHash));
    if (tier < 0) tier = 0;
    if (tier > 5) tier = 5;
    return g_tierPrice[tier];
}

// Car type entry of a car lot catalogue key (the key CarLot_ReplaceCar is given), or 0.
uintptr_t CarTypeOfCatalogKey(uint32_t key) {
    int count = *reinterpret_cast<volatile int32_t*>(kCarDBCatalogCount);
    uintptr_t table = *reinterpret_cast<volatile uintptr_t*>(ev::kCarTypesTablePtr);
    if (count <= 0 || count > 48 || !table) return 0;
    for (int i = 0; i < count; i++) {
        uintptr_t rec = kCarDB + i * kCatalogRecordSize;
        if (*reinterpret_cast<uint32_t*>(rec + 8) != key) continue;
        using IndexFn = int(__fastcall*)(uintptr_t);  // record vtable[1]: car type index (thiscall, no args)
        int index = (*reinterpret_cast<IndexFn*>(*reinterpret_cast<uintptr_t*>(rec) + 4))(rec);
        return index >= 0 ? table + index * ev::kCarTypeSize : 0;
    }
    return 0;
}

// "75,000" (or "75.000" etc. in the game's language). No '$': in FE text it starts an icon token.
void FormatPrice(int value, char out[24], char separator) {
    char digits[16];
    snprintf(digits, sizeof(digits), "%d", value);
    int len = static_cast<int>(strlen(digits)), o = 0;
    for (int i = 0; i < len; i++) {
        out[o++] = digits[i];
        if ((len - i - 1) % 3 == 0 && i != len - 1) out[o++] = separator;
    }
    out[o] = '\0';
}

// The parts shop's own "not enough cash" dialog, shown over the car lot.
void ShowNotEnoughCash(uintptr_t screen) {
    const char* package = *reinterpret_cast<const char* const*>(screen + carlot::kPackageName);
    using DialogFn = void(__cdecl*)(const char*, const char*, uint32_t);
    reinterpret_cast<DialogFn>(kShowMessageDialog)(package, "GenericDialog_SMALL.fng", kMsgNotEnoughCash);
    *reinterpret_cast<volatile uint32_t*>(kDialogCloseMessage) = kMsgUndim;  // as the shop does: UNDIM when it closes
}

// The car the car lot is about to buy when it replaces one: modes 3/4 = the highlighted car,
// mode 5 (you picked which of your cars goes) = the car chosen before (catalogue key at +0x64).
uintptr_t NewCarForTrade(uintptr_t screen) {
    int mode = *reinterpret_cast<int32_t*>(screen + carlot::kMode);
    if (mode == 5) return CarTypeOfCatalogKey(*reinterpret_cast<uint32_t*>(screen + carlot::kNewCarKey));
    uintptr_t entry = *reinterpret_cast<uintptr_t*>(screen + carlot::kHighlighted);
    return (mode == 3 || mode == 4) && entry ? *reinterpret_cast<uintptr_t*>(entry + carsel::kCarType) : 0;
}

uintptr_t g_carLot = 0;  // the car lot screen while its Notify runs (Hook_CarLotNotify)

// The mode-4 question's first button says "Trade this car?" (text 0xFB75E78F, used only there: the push at
// 0x004EDC36). After the career it should say "Store this car?". The game has no such text, so the entry in
// the in-memory language table ({hash, char*} records, Language_FindString 0x004FF9D0) is pointed at the
// plugin's own text, and back at the game's text during a career. The dialog reads it when it is built.
const uint32_t  kMsgTradeThisCar = 0xFB75E78F;  // "Trade this car?"
void SetTradeButtonText(bool store) {
    const char* kStoreThisCar = T().button;
    uintptr_t table = *reinterpret_cast<volatile uintptr_t*>(kLanguageTable);
    int count = *reinterpret_cast<volatile int32_t*>(kLanguageCount);
    if (!table || count <= 0 || count > 100000 || !IsReadable(table, count * 8)) return;
    static const char* original = nullptr;  // the game's text (refreshed in case the language was reloaded)
    for (int i = 0; i < count; i++) {
        if (*reinterpret_cast<uint32_t*>(table + i * 8) != kMsgTradeThisCar) continue;
        const char** text = reinterpret_cast<const char**>(table + i * 8 + 4);
        bool ours = false;
        for (const Texts& t : kTexts) ours |= *text == t.button;
        if (!ours) original = *text;
        const char* want = store ? kStoreThisCar : original;
        if (want && *text != want) {
            *text = want;
            Log("Car lot button now says \"%s\".", want);
        }
        return;
    }
}

// ---- Hooks ---------------------------------------------------------------------

void __fastcall Hook_ProfileLoad(void* profile, void* edx, void* buffer, int size, int flag) {
    using Fn = void(__fastcall*)(void*, void*, void*, int, int);
    reinterpret_cast<Fn>(kProfileLoad)(profile, edx, buffer, size, flag);
    if (reinterpret_cast<uintptr_t>(profile) != kPlayer1Profile || !g_versionOk) return;
    DiscardPending("profile loaded");
    Log("Profile \"%s\" loaded. Career finished: %s.", ProfileName(), CareerFinished() ? "yes" : "no");
    LogGarage("Garage:");
    char dir[MAX_PATH];
    if (StoreDir(dir)) Log("Store %s holds %d car(s).", dir, static_cast<int>(ListStore(dir).size()));
}

// Replaces the car lot's CALLs to CarLot_ReplaceCar (thiscall, 2 args, RET 8).
// Trade-ins are stored always, also before the career is finished (they stay hidden until then).
void __fastcall Hook_CarLotReplaceCar(void* screen, void* edx, uint32_t slotHash, uint32_t newCarKey) {
    if (g_versionOk) {
        int player = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(screen) + 0xEC);
        int n = FindSlotByHash(slotHash);
        Log("Car lot is replacing slot %d (hash %08X), player %d.", n, static_cast<unsigned>(slotHash), player);
        if (player == 0 && PricesActive()) {
            // After the career, a car beyond the 5 slots costs money (Hook_CarLotNotify already
            // refused ACCEPT if it can't be afforded; this is the last check before paying).
            uintptr_t type = CarTypeOfCatalogKey(newCarKey);
            int price = PriceOfCarType(type);
            int cash = *reinterpret_cast<volatile int32_t*>(kCash);
            const char* name = type ? CarName(*reinterpret_cast<uint32_t*>(type + kCarTypeNameHash)) : "?";
            if (price > 0 && cash < price) {
                Log("Not enough cash for %s ($%d, have $%d): purchase cancelled, nothing changed.", name, price, cash);
                ShowNotEnoughCash(reinterpret_cast<uintptr_t>(screen));
                return;
            }
            if (price > 0) {
                *reinterpret_cast<volatile int32_t*>(kCash) = cash - price;
                Log("Bought %s for $%d (tier price). Cash %d -> %d.", name, price, cash, cash - price);
            }
        }
        if (player == 0 && n >= 0 && SlotPtr(n)[slot::kOccupied])
            QueueStore(SlotPtr(n), "traded in");
    }
    using Fn = void(__fastcall*)(void*, void*, uint32_t, uint32_t);
    reinterpret_cast<Fn>(kCarLotReplaceCar)(screen, edx, slotHash, newCarKey);
    if (g_versionOk) LogGarage("Garage after the car lot:");
}

// Replaces the save-to-disk job's CALL to FUN_0052A450 (thiscall(manager, buffer, size), RET 8): the game
// is writing the profile to disk right now. Previews are never saved (originals put back for the
// serialisation), and the queued store changes are written to disk at this same moment.
void __fastcall Hook_SaveProfile(void* manager, void* edx, void* buffer, int size) {
    static unsigned char previewImage[kMaxSlots][kSlotSize];
    bool swapped[kMaxSlots] = {};
    if (g_versionOk) {
        for (int i = 0; i < kMaxSlots; i++) {
            if (!g_preview[i].active) continue;
            memcpy(previewImage[i], SlotPtr(i), kSlotSize);
            PutCarInSlot(i, g_preview[i].original);
            swapped[i] = true;
        }
    }
    using Fn = void(__fastcall*)(void*, void*, void*, int);
    reinterpret_cast<Fn>(kSaveSerialize)(manager, edx, buffer, size);
    if (!g_versionOk) return;
    for (int i = 0; i < kMaxSlots; i++)
        if (swapped[i]) PutCarInSlot(i, previewImage[i]);
    FlushPending();
}

// Replaces vtable_UICareerCarLot[2] (thiscall(screen, msg, a, b, c), RET 0x10).
void __fastcall Hook_CarLotNotify(void* screen, void* edx, uint32_t msg, uint32_t a, uint32_t b, uint32_t c) {
    uintptr_t s = reinterpret_cast<uintptr_t>(screen);
    if (g_versionOk && (msg == kMsgPadAccept || msg == carlot::kMsgClickAccept)) {
        int mode = *reinterpret_cast<int32_t*>(s + carlot::kMode);
        // Modes 3/4/5 = the garage is full: buying this car would put one into the store.
        if ((mode == 3 || mode == 4 || mode == 5) && *reinterpret_cast<uint8_t*>(s + carlot::kReady) && PricesActive()) {
            uintptr_t type = NewCarForTrade(s);
            int price = PriceOfCarType(type);
            int cash = *reinterpret_cast<volatile int32_t*>(kCash);
            const char* name = type ? CarName(*reinterpret_cast<uint32_t*>(type + kCarTypeNameHash)) : "?";
            Log("Car lot ACCEPT (mode %d) on %s: price $%d, cash $%d.", mode, name, price, cash);
            if (price > 0 && cash < price) {
                ShowNotEnoughCash(s);
                return;  // the game never sees this ACCEPT
            }
        }
    }
    g_carLot = s;  // for Hook_TradeDialog, which runs inside this call
    using Fn = void(__fastcall*)(void*, void*, uint32_t, uint32_t, uint32_t, uint32_t);
    reinterpret_cast<Fn>(carlot::kNotify)(screen, edx, msg, a, b, c);
    g_carLot = 0;
}

// Replaces the car lot's CALL to kShowDialogFormatted (cdecl; for the trade-in questions the varargs are
// the current car's name and the new car's name). After the career the question shows the price and
// that the old car is stored; otherwise (and for every other text) the call goes through unchanged.
void __cdecl Hook_TradeDialog(const char* pkg, const char* dlg, uint32_t a, uint32_t b, uint32_t yes, uint32_t no,
                              uint32_t no2, uint32_t flag, uint32_t textHash, ...) {
    va_list args;
    va_start(args, textHash);
    uint32_t arg1 = va_arg(args, uint32_t);  // this call site pushes 1 (mode 2) or 2 (modes 3-5) varargs;
    uint32_t arg2 = va_arg(args, uint32_t);  // reading 2 is harmless, the format uses only what it needs
    va_end(args);

    bool trade = textHash == kMsgTradeInOrChoose || textHash == kMsgTradeIn;
    bool revealed = g_versionOk && trade && g_carLot && StoreRevealed();
    int price = revealed && PricesActive() ? PriceOfCarType(NewCarForTrade(g_carLot)) : 0;
    if (g_versionOk && textHash == kMsgTradeInOrChoose) SetTradeButtonText(revealed);
    if (revealed && arg1 && arg2) {
        const Texts& t = T();
        const char* oldName = reinterpret_cast<const char*>(arg1);
        const char* newName = reinterpret_cast<const char*>(arg2);
        static char text[512];
        int len;
        if (price > 0) {
            char money[24];
            FormatPrice(price, money, t.separator);
            len = snprintf(text, sizeof(text), t.question, newName, money, oldName);
        } else {
            len = snprintf(text, sizeof(text), t.questionFree, newName, oldName);
        }
        if (textHash == kMsgTradeInOrChoose && len > 0 && len < static_cast<int>(sizeof(text)))
            snprintf(text + len, sizeof(text) - len, "%s", t.choose);
        Log("Car lot question: \"%s\"", text);
        const char* formatArgs[1] = {text};
        using TextFn = void(__cdecl*)(const char*, const char*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, int,
                                      uint32_t, const char*, const void*);
        reinterpret_cast<TextFn>(kShowDialogText)(pkg, dlg, a, b, yes, no, no2, 1, flag, "%s", formatArgs);
        return;
    }
    using Fn = void(__cdecl*)(const char*, const char*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                              uint32_t, ...);
    reinterpret_cast<Fn>(kShowDialogFormatted)(pkg, dlg, a, b, yes, no, no2, flag, textHash, arg1, arg2);
}

// Replaces vtable_UICareerCribCarSelect[2] (thiscall(screen, msg, a, b, c), RET 0x10).
void __fastcall Hook_CarSelectNotify(void* screen, void* edx, uint32_t msg, uint32_t a, uint32_t b, uint32_t c) {
    uintptr_t s = reinterpret_cast<uintptr_t>(screen);
    using Fn = void(__fastcall*)(void*, void*, uint32_t, uint32_t, uint32_t, uint32_t);
    if (g_versionOk && s != g_carSelect && IsLiveCarSelect(s)) {
        g_carSelect = s;
        Log("Car select screen opened (%08X).", static_cast<unsigned>(s));
        SetRachelMessage();
        GiveRachelCars();
        static bool dumped = false;
        if (!dumped && g_detailedLog) {
            dumped = true;
            DumpPackageObjects(*reinterpret_cast<const char* const*>(s + carsel::kPackageName));
        }
    }
    if (g_versionOk && AnyPreview()) {
        // Car select gets ACCEPT as 0x0C407210 (FE "object accept") when the highlighted
        // object has a responder for it, else PAD_ACCEPT (FEng_ProcessPadsForPackage, bit 4).
        if (msg == kMsgPadAccept || msg == carlot::kMsgClickAccept) {
            uintptr_t entry = *reinterpret_cast<uintptr_t*>(s + carsel::kHighlighted);
            bool usable = *reinterpret_cast<uint8_t*>(s + kCarSelectReady) && entry &&
                          !*reinterpret_cast<uint8_t*>(entry + carsel::kLockedFlag) &&
                          !*reinterpret_cast<uint8_t*>(entry + carsel::kEmptyFlag);
            if (usable && *reinterpret_cast<uint32_t*>(entry + carsel::kSlotHash) ==
                              *reinterpret_cast<volatile uint32_t*>(kCurrentSlotHash)) {
                // The game would say "You're already driving your X!" (it compares slots, not cars).
                // Keep the previews and do what YES in its "drive X instead?" question does.
                CommitPreviews();
                Log("Picked the slot you were already driving: leaving car select with the kept car.");
                reinterpret_cast<Fn>(carsel::kNotify)(screen, edx, kMsgDriveThisCar, a, b, c);
                return;
            }
            // Another slot: the game asks "drive X instead of Y?"; YES (kMsgDriveThisCar) keeps.
        } else if (msg == kMsgDriveThisCar) {
            CommitPreviews();
        } else if (msg == kMsgPadBack) {
            RevertPreviews("BACK");
        }
    }
    reinterpret_cast<Fn>(carsel::kNotify)(screen, edx, msg, a, b, c);
}

// Replaces the CALL (in Scroll) and the tail JMP (in the setup) to UICareerCribCarSelect_UpdateTexts
// (fastcall(screen), plain RET): the game writes "Slot n", then the plugin adds the stored-cars hint.
void __fastcall Hook_CarSelectUpdateTexts(void* screen) {
    reinterpret_cast<void(__fastcall*)(void*)>(kUpdateTexts)(screen);
    ApplySlotTitle(reinterpret_cast<uintptr_t>(screen));
}

// Replaces vtable_UICareerCribCarSelect[0] (thiscall(screen, flags), RET 4).
void* __fastcall Hook_CarSelectDestructor(void* screen, void* edx, uint32_t flags) {
    if (reinterpret_cast<uintptr_t>(screen) == g_carSelect) {
        RevertPreviews("car select closed");
        g_carSelect = 0;
        Log("Car select screen closed.");
    }
    using Fn = void*(__fastcall*)(void*, void*, uint32_t);
    return reinterpret_cast<Fn>(carsel::kDestructor)(screen, edx, flags);
}

// ---- Rachel's cars: hands off -------------------------------------------------------------
// Shops: the world's entrance dialog UI_EngageShopDialog (vtable 0x0078F978, Notify 0x00499690) enters the shop on
// ACCEPT of its button 0xD72F002A. The same dialog is used for the player's garage ("Enter Garage") and a timed
// challenge retry, so only real shops are blocked: [0x00863444] && [0x0086344C] (current place) with byte +0x50
// set ("Enter Shop"), and not the retry ([0x0085B948] && byte [0x0085B940]). With Rachel's car the dialog text
// (object 0x02EDBDE5) becomes her "hands off" line and ACCEPT does nothing; Cancel works as usual.
// Tuning: UI_PerformanceTuning_Master (vtable 0x0079D344, Notify 0x0055D910) opens its sub-screens on 0x0C407210;
// with Rachel's car that is refused with an in-game message (as the game's own in-game dialogs: UI_InGameDialog.fng).
const uintptr_t kEngageShopVtable   = 0x0078F978;
const uintptr_t kEngageShopNotify   = 0x00499690;
const uintptr_t kTuningMasterVtable = 0x0079D344;
const uintptr_t kTuningMasterNotify = 0x0055D910;
const uintptr_t kShowDialogOneButton = 0x00558490;  // cdecl(pkg, dlgPkg, okLabel, msg, msg2, char 1, format, va_list)
const uint32_t  kObjEngageAccept = 0xD72F002A;  // the accept button's text object ("Enter Shop"); also the clicked object
const uint32_t  kObjEngageCancel = 0x63857DE0;  // the cancel button's text object
const uint32_t  kObjEngageText   = 0x04A04E7C;  // the question ("Would you like to enter the Shop?")
const uint32_t  kLabelOK = 0x639;               // "OK"
const uintptr_t kFESetLabelHash = 0x0050C900;   // cdecl(textObject, stringHash): the object shows that game text

bool EngageIsShop() {
    if (*reinterpret_cast<volatile int32_t*>(0x0085B948) != 0 && *reinterpret_cast<volatile uint8_t*>(0x0085B940) != 0)
        return false;  // timed challenge: "retry?"
    uintptr_t place = *reinterpret_cast<volatile uintptr_t*>(0x0086344C);
    return *reinterpret_cast<volatile int32_t*>(0x00863444) != 0 && place && IsReadable(place + 0x50, 1) &&
           *reinterpret_cast<uint8_t*>(place + 0x50) != 0;
}

// With Rachel's car at a shop entrance: her line instead of the question, and "OK" (which closes the dialog like
// Cancel) instead of "Enter Shop". Done once per dialog (the dialog sets its texts again when it is created).
void ShowHandsOffInEngage(uintptr_t screen) {
    const char* package = *reinterpret_cast<const char* const*>(screen + 4);
    using FindFn = uintptr_t(__cdecl*)(const char*, uint32_t);
    using LabelFn = void(__cdecl*)(uintptr_t, uint32_t);
    uintptr_t button = reinterpret_cast<FindFn>(kFEFindObject)(package, kObjEngageAccept);
    if (!button || *reinterpret_cast<int32_t*>(button + feobj::kType) != 2) return;
    if (*reinterpret_cast<uint32_t*>(button + 0x60) == kLabelOK) return;  // already done for this dialog
    reinterpret_cast<LabelFn>(kFESetLabelHash)(button, kLabelOK);
    uintptr_t text = reinterpret_cast<FindFn>(kFEFindObject)(package, kObjEngageText);
    if (text && *reinterpret_cast<int32_t*>(text + feobj::kType) == 2) {
        reinterpret_cast<LabelFn>(kFESetLabelHash)(text, 0);
        SetTextObject(text, T().handsOff);
    }
    Log("Shop entrance with Rachel's car: hands off.");
}

// Replaces vtable_UIEngageShopDialog[2] (thiscall(screen, msg, a, b, c), RET 0x10).
void __fastcall Hook_EngageShopNotify(void* screen, void* edx, uint32_t msg, uint32_t a, uint32_t b, uint32_t c) {
    uintptr_t s = reinterpret_cast<uintptr_t>(screen);
    if (g_versionOk && EngageIsShop() && CurrentCarIsRachelsLocked()) {
        ShowHandsOffInEngage(s);
        if (msg == carlot::kMsgClickAccept && a && IsReadable(a + 0x10, 4) &&
            *reinterpret_cast<uint32_t*>(a + 0x10) == kObjEngageAccept) {
            // "OK": close the dialog the way its Cancel button does (the handler reads only object+0x10).
            static uint32_t cancelObject[8];
            cancelObject[4] = kObjEngageCancel;
            Log("Shop entrance closed: the current car is Rachel's.");
            a = reinterpret_cast<uint32_t>(cancelObject);
        }
    }
    using Fn = void(__fastcall*)(void*, void*, uint32_t, uint32_t, uint32_t, uint32_t);
    reinterpret_cast<Fn>(kEngageShopNotify)(screen, edx, msg, a, b, c);
}

// Replaces vtable_UIPerformanceTuningMaster[2] (thiscall(screen, msg, a, b, c), RET 0x10).
void __fastcall Hook_TuningMasterNotify(void* screen, void* edx, uint32_t msg, uint32_t a, uint32_t b, uint32_t c) {
    uintptr_t s = reinterpret_cast<uintptr_t>(screen);
    if (g_versionOk && msg == carlot::kMsgClickAccept && CurrentCarIsRachelsLocked()) {
        const char* package = *reinterpret_cast<const char* const*>(s + 4);
        const char* formatArgs[1] = {T().handsOff};
        using Fn = void(__cdecl*)(const char*, const char*, uint32_t, uint32_t, uint32_t, int, const char*, const void*);
        reinterpret_cast<Fn>(kShowDialogOneButton)(package, "UI_InGameDialog.fng", kLabelOK, kMsgUndim, kMsgUndim, 1,
                                                   "%s", formatArgs);
        Log("Tuning refused: the current car is Rachel's.");
        return;
    }
    using Fn = void(__fastcall*)(void*, void*, uint32_t, uint32_t, uint32_t, uint32_t);
    reinterpret_cast<Fn>(kTuningMasterNotify)(screen, edx, msg, a, b, c);
}

bool NameIs(const char* a, const char* b) { return _stricmp(a, b) == 0; }

// Logs the open menus whenever the list changes. Returns true if a dialog is open.
bool TrackOpenMenus(uintptr_t feng) {
    static char last[512] = "";
    char now[512] = "";
    bool dialog = false;
    size_t len = 0;
    int guard = 0;
    for (uintptr_t p = *reinterpret_cast<uintptr_t*>(feng + fe::kPackages); p && guard < 64;
         p = *reinterpret_cast<uintptr_t*>(p + fe::kPkgNext), guard++) {
        const char* name = *reinterpret_cast<const char* const*>(p + fe::kPkgName);
        if (!name) name = "?";
        if (strstr(name, "Dialog")) dialog = true;
        len += snprintf(now + len, len < sizeof(now) ? sizeof(now) - len : 0, "%s%s", len ? ", " : "", name);
        if (len >= sizeof(now)) break;
    }
    static int logged = 0;
    if (strcmp(now, last) != 0) {
        if (g_detailedLog && logged < 2000) Log("Open menus: %s", now[0] ? now : "(none)");
        if (++logged == 2000) Log("(menu list logging stops here)");
        snprintf(last, sizeof(last), "%s", now);
    }
    return dialog;
}

// Logs a menu's pad mask the first time the menu is seen (and when it changes).
void LogMenuMask(const char* name, uint32_t mask) {
    static char names[64][48];
    static uint32_t masks[64];
    static int count = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(names[i], name) == 0) {
            if (masks[i] != mask) {
                Log("Menu %s now listens to pads mask=0x%X", name, mask);
                masks[i] = mask;
            }
            return;
        }
    }
    if (count < 64) {
        snprintf(names[count], sizeof(names[count]), "%s", name);
        masks[count++] = mask;
    }
    Log("Menu %s listens to pads mask=0x%X", name, mask);
}

// Replaces the CALL to FEng_ProcessPadsForPackage (thiscall, 1 arg, RET 4).
void __fastcall Hook_ProcessPads(void* feng, void* edx, void* pkg) {
    int step = 0;  // +1 = UP, -1 = DOWN in car select
    if (g_versionOk) {
        uintptr_t f = reinterpret_cast<uintptr_t>(feng);
        uintptr_t p = reinterpret_cast<uintptr_t>(pkg);
        static bool dialogOpen = false;
        static bool loggedPads = false;
        static int inputLines = 0;
        if (p == *reinterpret_cast<uintptr_t*>(f + fe::kPackages)) dialogOpen = TrackOpenMenus(f);  // once per frame

        uint32_t count = *reinterpret_cast<uint32_t*>(f + fe::kPadCount);
        uintptr_t pads = *reinterpret_cast<uintptr_t*>(f + fe::kPads);
        if (!loggedPads && g_detailedLog) {
            Log("Menu engine at %08X: %u pad(s), pad table at %08X", static_cast<unsigned>(f), count,
                static_cast<unsigned>(pads));
            loggedPads = true;
        }

        const char* name = *reinterpret_cast<const char* const*>(p + fe::kPkgName);
        if (!name) name = "?";
        uint32_t mask = *reinterpret_cast<uint32_t*>(p + fe::kPkgPadMask);
        if (g_detailedLog) LogMenuMask(name, mask);

        bool inCarSelect = NameIs(name, "UI_CareerCarSelect.fng");
        for (uint32_t i = 0; mask && pads && i < count && i < 32; i++) {
            if (!(mask & (1u << i))) continue;
            uintptr_t e = pads + i * fe::kPadEntrySize;
            uint32_t prev = *reinterpret_cast<uint32_t*>(e + fe::kPadPrev);
            uint32_t cur  = *reinterpret_cast<uint32_t*>(e + fe::kPadCur);
            uint32_t pressed = cur & ~prev;
            if (!pressed) continue;
            for (int b = 0; b < fe::kNumBits; b++) {
                if (!(pressed & (1u << b))) continue;
                if (g_detailedLog && inputLines < 5000) Log("Input in %s: pad %u pressed bit %d (%s)", name, i, b, fe::BitName(b));
                if (++inputLines == 5000) Log("(input logging stops here)");
            }
            if (inCarSelect && (pressed & (1u << kBitUp))) step = 1;
            if (inCarSelect && (pressed & (1u << kBitDown))) step = -1;
        }
        // The car select screen must be the one we remember, and belong to this menu.
        if (step && (dialogOpen || !IsLiveCarSelect(g_carSelect) ||
                     *reinterpret_cast<uintptr_t*>(g_carSelect + carsel::kPackage) != p)) {
            if (dialogOpen) Log("Up/down ignored: a dialog is open.");
            else Log("Up/down ignored: car select screen not known yet (scroll left/right once).");
            step = 0;
        }
    }

    using Fn = void(__fastcall*)(void*, void*, void*);
    reinterpret_cast<Fn>(kFEngProcessPads)(feng, edx, pkg);

    if (step) PreviewStep(g_carSelect, step);  // after the game has handled this frame's input
}

// ---- Startup -------------------------------------------------------------------

DWORD WINAPI VersionCheckThread(LPVOID) {
    g_versionOk = ExeMd5Ok() && g_hooksOk;
    if (!g_versionOk) Log("The hooks (if any) stay installed but only pass calls through to the game.");
    return 0;
}

bool Redirect(uintptr_t site, uintptr_t target, const void* hook, const char* what) {
    bool ok = RedirectCall(site, target, hook);
    Log("%s %s call at 0x%08X.", ok ? "Hooked" : "COULD NOT hook", what, static_cast<unsigned>(site));
    return ok;
}

bool PatchVtable(uintptr_t addr, uintptr_t original, const void* hook, const char* what) {
    bool ok = PatchPointer(addr, original, reinterpret_cast<uintptr_t>(hook));
    Log("%s %s (vtable entry 0x%08X).", ok ? "Hooked" : "COULD NOT hook", what, static_cast<unsigned>(addr));
    return ok;
}

void Init() {
    Log("==== Virtual Garage started (car select: UP/DOWN browse stored cars after the career) ====");

    LoadSettings();
    if (!CodeMatches(kCodeChecks, sizeof(kCodeChecks) / sizeof(kCodeChecks[0]))) {
        Log("Not hooking anything.");
        return;
    }
    bool ok = true;
    for (uintptr_t site : kProfileLoadCallSites)
        ok &= Redirect(site, kProfileLoad, reinterpret_cast<const void*>(&Hook_ProfileLoad), "profile load");
    for (uintptr_t site : kCarLotCallSites)
        ok &= Redirect(site, kCarLotReplaceCar, reinterpret_cast<const void*>(&Hook_CarLotReplaceCar), "car lot replace");
    ok &= Redirect(kPadsCallSite, kFEngProcessPads, reinterpret_cast<const void*>(&Hook_ProcessPads), "menu input");
    ok &= Redirect(kSaveSerializeCallSite, kSaveSerialize, reinterpret_cast<const void*>(&Hook_SaveProfile), "profile save");
    ok &= Redirect(kTradeDialogCallSite, kShowDialogFormatted, reinterpret_cast<const void*>(&Hook_TradeDialog),
                   "car lot question");
    ok &= Redirect(kUpdateTextsCallSite, kUpdateTexts, reinterpret_cast<const void*>(&Hook_CarSelectUpdateTexts),
                   "car select title");
    {
        bool jmp = RedirectJump(kUpdateTextsJumpSite, kUpdateTexts, reinterpret_cast<const void*>(&Hook_CarSelectUpdateTexts));
        Log("%s car select title jump at 0x%08X.", jmp ? "Hooked" : "COULD NOT hook", static_cast<unsigned>(kUpdateTextsJumpSite));
        ok &= jmp;
    }
    ok &= PatchVtable(carsel::kVtable + 8, carsel::kNotify, reinterpret_cast<const void*>(&Hook_CarSelectNotify),
                      "car select Notify");
    ok &= PatchVtable(carsel::kVtable + 0, carsel::kDestructor, reinterpret_cast<const void*>(&Hook_CarSelectDestructor),
                      "car select destructor");
    ok &= PatchVtable(carlot::kVtable + 8, carlot::kNotify, reinterpret_cast<const void*>(&Hook_CarLotNotify),
                      "car lot Notify");
    ok &= PatchVtable(kEngageShopVtable + 8, kEngageShopNotify, reinterpret_cast<const void*>(&Hook_EngageShopNotify),
                      "shop entrance Notify");
    ok &= PatchVtable(kTuningMasterVtable + 8, kTuningMasterNotify, reinterpret_cast<const void*>(&Hook_TuningMasterNotify),
                      "tuning menu Notify");
    g_hooksOk = ok;
    if (!ok) Log("Some hooks failed; the plugin will only pass calls through.");

    // CryptoAPI must not run inside DllMain, so the MD5 check runs on its own thread.
    HANDLE thread = CreateThread(nullptr, 0, VersionCheckThread, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
}

}  // namespace

void bf::StartVirtualGarage() { Init(); }  // patches the CALLs and vtable entries before the game's own code runs
