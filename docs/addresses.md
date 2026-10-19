# Verified addresses (speed2.exe v1.2, MD5 878678327c1aa744f2fe248cba081c86)

All found by static analysis in Ghidra on 2026-10-12. **Nothing here has been tested in-game yet.**
Names starting with `maybe_` are best guesses, not proven.

## Global objects / data

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x0083AD90` | `g_CarDB` | Car database: car records + the 5 career slots. | Used as `this` (`MOV ECX,0x83ad90`) at `0x0053292B`, `0x00603844`, `0x00541569`. |
| `0x008406E4` | `g_CarDB_CareerSlots` | Career slot 0 (= g_CarDB + 0x5954). Slot n = `0x008406E4 + n*0x7F8`. | Computed from offsets; confirmed by hardcoded `0x00840ED8` (slot 0 + 0x7F4) and `0x008406EC` (slot 0 + 0x08) in several functions. |
| `0x00840ED8` | `g_CarDB_CareerSlot0_Locked` | Slot 0's "locked" byte. Code indexes it with `+ n*0x7F8`. | Read/written in the 4 unlock loops below. |
| `0x00842EBC` | `g_CarDB_CareerSlotCount` | Number of career slots (= g_CarDB + 0x812C), normally 5. | Read directly at `0x005329C1` etc. |
| `0x0085AD40` | `g_Career` | Career state (cash, current car, stages/sponsors). Name is a guess. | Used as `this` (`MOV ECX,0x85ad40`) at `0x0060383A`. |
| `0x00861E74` | `g_Career_Cash` | **Player cash** (= g_Career + 0x7134). int, clamped at 0. | Shop code does `cash -= price; if (cash<0) cash=0`; sponsor code adds a bonus to the same field. |
| `0x00863480` | `g_Career_CurrentCarSlotHash` | Hash of the career slot currently in use (= g_Career + 0x8740), e.g. StringHash("CAREER_SLOT_2"). | Written by `maybe_Career_BuyCarIntoSlot`, read by `Career_GetCurrentCarSlot`. |
| `0x00797E74` | (string) | `"CAREER_SLOT_%d"` | String search. |

## Functions

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x0043DB50` | `StringHash` | h = -1; for each byte: h = h*33 + byte. | Decompiled. |
| `0x0050B990` | `StringHashFormatted` | sprintf into 256-byte buffer, then StringHash. Returns hash in EAX. | Decompiled. |
| `0x00516480` | `CarDB_InitCareerSlots` | Creates **5** career slots (`CMP EDI,0x5` at `0x005164ED`); all start locked and empty. | Disassembled. |
| `0x0052A8A0` | `CarDB_Reset` | Resets 20 car records (0x434 each at +0x544), then `CarDB_InitCareerSlots`. | Decompiled. |
| `0x00503680` | `CarDB_FindCareerSlotByHash` | Returns pointer to the slot whose +0x08 == hash, or 0. | Decompiled. |
| `0x00516340` | `CarDB_CareerSlotExists` | `FindCareerSlotByHash(h) != 0`. | Decompiled. |
| `0x005162D0` | `CarDB_NextRecordOfKind` | `(kindMask, prev)`: iterates the 91-pointer list at g_CarDB+0x9BD8, returns next record with +0x10 != 0 and (+0x14 & kindMask). Kind 4 = career slots. | Decompiled. |
| `0x005284D0` | `CarDB_UnlockNextCareerSlot` | Clears the first set "locked" byte (+0x7F4) among the career slots. | Decompiled. |
| `0x00503E70` | `maybe_CarCustomization_Clear` | Zeroes a 0x3C0 block (called on slot+0x434). | Decompiled; meaning is a guess. |
| `0x00516B90` | `maybe_CarRecord_InitFromCarType` | Fills a car record from a car-type entry (sets +0x08 hash, +0x0C float, +0x10 = 1, +0x14 = 2, ...). | Decompiled. |
| `0x005348E0` | `maybe_CarDB_PutCarInCareerSlot` | `(slotIndex, carKey)`: finds car-type entry, inits slot from it, sets +0x14 = 4 and +0x7F5 = 1 (occupied). | Decompiled. |
| `0x00541540` | `maybe_Career_BuyCarIntoSlot` | On g_Career: sets current car = slot n (+0x873D = 1, +0x8740 = hash), then `PutCarInCareerSlot`. Only caller: car lot screen. | Disassembled. |
| `0x00513C30` | `Career_GetCurrentCarSlot` | `FindCareerSlotByHash(g_Career+0x8740)`. | Decompiled. |
| `0x00495F70` | `CarLot_CountCareerSlots` | Counts unlocked slots (→ +0x78) and unlocked-and-empty slots (→ +0x74). | Decompiled. |
| `0x00495FD0` | `CarLot_FindFirstFreeCareerSlot` | Index of the first slot that is unlocked and empty. | Decompiled. |
| `0x00495F00` | `maybe_CarLot_ChooseBuyMode` | Free slot → 2 (buy into it); 1 unlocked & full → 3 (replace current car); ≥2 unlocked & full → 4 (choose car to replace). **The "garage full" logic.** | Decompiled; mode meanings inferred from the handler. |
| `0x004ED9E0` | `maybe_CarLotScreen_HandleMessage` | Menu message handler; mode 2 calls `maybe_Career_BuyCarIntoSlot(FindFirstFreeCareerSlot(), selectedCar)`. | Decompiled. |
| `0x00532CF0` | `maybe_Career_SetStageSponsor` | If current stage has no sponsor: `UnlockNextCareerSlot`, cash += sponsor bonus (short at sponsor+0x0C), store sponsor. **This is where career progress unlocks a garage slot.** | Decompiled. |
| `0x00541680`, `0x005417F0` | (unnamed) | Same unlock + bonus logic inlined, on g_Career. | Decompiled. |
| `0x00532830` | `maybe_Career_Reset` | New-career reset on g_Career; optionally `InitCareerSlots`, then unlocks the first slot. | Decompiled + disassembled. |
| `0x00529E80` | `maybe_CarDB_AnyCareerCarHasPart` | Checks the part list at +0x6E0 of every occupied career slot. | Decompiled. |
| `0x00527890` | (unnamed) | "Any occupied career car whose car type has flag +0x88A". | Decompiled. |
| `0x00547570` | `maybe_CustomizeShop_CommitPurchases` | Charges cash for parts, adds price to current slot's +0x7F0. | Decompiled. |
| `0x00521B00`, `0x0054FE50` | (unnamed) | Also charge cash for shop items and add to slot +0x7F0. | Decompiled. |

## Save / load (found 2026-10-12, round 3)

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x005166B0` | `CarDB_SaveToBuffer` | Writes g_CarDB into the save buffer (field by field, no vtables). | Decompiled. |
| `0x00516850` | `CarDB_LoadFromBuffer` | Reads it back. Layout in structs.md → "Save file". | Decompiled; layout checked against real saves `Chris` and `NAME`. |
| `0x00516620` | `CareerSlot_SaveToBuffer` | Writes one career slot (0x7F2 bytes). Called 5× by CarDB_SaveToBuffer. | Decompiled. |
| `0x00504040` | `CareerSlotBlock_LoadFromBuffer` | Reads the 0x3C0 block of a slot; copies values, a few dwords reordered (see structs.md). No pointer fix-ups. | Decompiled. |
| `0x00503F00` | (unnamed) | Presumably the matching block writer (called by CareerSlot_SaveToBuffer). | Not decompiled yet. |
| `0x005035C0` | `CarDB_FindRecordByHash` | Searches the 20 plain records, then the career slots. | Decompiled. |
| `0x0052A280` | `maybe_Profile_SaveToBuffer` | Whole-profile save: CarDB_SaveToBuffer, then career data (`0x00514530`), then other profile fields. | Decompiled. |
| `0x005344D0` | `maybe_Profile_LoadFromBuffer` | Whole-profile load: `maybe_Career_Reset(1)`, CarDB_LoadFromBuffer, career data (`0x00529230`), other fields. | Decompiled. |
| `0x00534770`, `0x005347C0` | `maybe_Profile_ApplyPendingLoad` (+ unnamed) | Call the profile load; a mode value of 4 (at this+0x156A8) loads *only* the car DB (size 0x993E). | Decompiled; what uses mode 4 is unknown. |

## Car records, car models and the 3D car (round 4)

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x0052A5D0` | `CarDB_Construct` | Builds a car DB: 48×0x1C @+0, 20×0x434 @+0x544, 5×0x7F8 @+0x5954, 6×0x438 @+0x8130, 12×0x1C @+0x9A84. | Decompiled. |
| `0x00531D20` | (unnamed) | Same as above (second copy, used by the profile-manager constructor `0x0053ED20`). | Decompiled. |
| `0x00516B30` | `CarRecord_Construct` | Sets vtable, zeroes +0x04..+0x14. | Decompiled. |
| `0x00525CF0` | `CareerSlot_Construct` | `CarRecord_Construct` + vtable. Also used for the 0x438 records. | Decompiled. |
| `0x0079B12C` | `vtable_CarRecord` | [0] `0x00503A40` destructor, [1] `0x00503AE0` GetCarTypeIndex, [2] `0x00503B00` GetCarNameHash (returns +0x20), [3] `0x00516CA0` BuildRideInfo. | Read memory + hand-decoded [1], [2]. |
| `0x00610130` | `CarTypes_FindByNameHash` | Searches the car-model table (46 × 0x890, pointer at `0x008A1CCC`, key at entry+0xD0); returns entry or 0. Entry+0x840 = model index. | Decompiled. |
| `0x00516CA0` | `CarRecord_vBuildRideInfo` | vtable[3]: calls `CarRecord_BuildRideInfoWithSetup` with ECX = record+0x18. | Disassembled (`LEA ECX,[ESI+0x18]`). |
| `0x00516AC0` | `CarRecord_BuildRideInfoWithSetup` | `CarRecord_BuildRideInfo`, then copies 41-byte block #param_4 (record+0x360+n*0x29) to RideInfo+0x398. | Decompiled + disassembled. |
| `0x00503880` | `CarRecord_BuildRideInfo` | Turns a record into a "RideInfo" (the car description used to draw it): model from +0x20, 170 parts from +0x24, 37 values from +0x2CC. | Decompiled (offsets +0x18). |
| `0x005309E0` | `maybe_FE_ShowCar` | `(player, recordHash, ?)`: find record → `maybe_FECarDisplay_SetRecord` → `maybe_FE_SpawnCurrentCar(1)`. | Decompiled. |
| `0x0050F800` | `maybe_FECarDisplay_SetRecord` | Stores record pointer, rebuilds RideInfo from it (vtable[3]). | Decompiled. |
| `0x00526970` | `maybe_FE_SpawnCurrentCar` | Finds current car in g_CarDB's pointer list (`0x00844968` = g_CarDB+0x9BD8), builds RideInfo, creates the menu car via `0x006373E0` named "Test Driver". | Decompiled. |
| `0x00495AF0` | `maybe_GarageScreen_ShowCurrentCar` | `maybe_FE_ShowCar(0, g_Career_CurrentCarSlotHash, 0)`. **Candidate "refresh" call for the virtual garage.** | Decompiled. |
| `0x00552CC0` | `maybe_CustomizeShop_Enter` | Backs up the whole profile in memory (for cancel?), then builds RideInfos at `0x008392C0`/`0x008389D0` from the chosen car. | Decompiled. |
| `0x00525ED0` | `maybe_Race_BuildPlayerCars` | For each player: find car record, build RideInfo, create the race car. | Decompiled. |
| `0x0083A9E0` | (data) | Per-player profile block, stride 0xAB4C; player 1's car DB = `g_CarDB` (block+0x3B0); player 2's = g_CarDB + 0xAB4C. | From `maybe_CustomizeShop_Enter` / `maybe_Race_BuildPlayerCars`. |

## Hook points used by plugins

| Address | What | Bytes checked | Used by |
|---|---|---|---|
| `0x005347AB` | `CALL maybe_Profile_LoadFromBuffer` (in `0x00534770`) | `E8 20 FD FF FF` | VirtualGarageTest: redirected to its hook |
| `0x00534812` | `CALL maybe_Profile_LoadFromBuffer` (in `maybe_Profile_ApplyPendingLoad`) | `E8 B9 FC FF FF` | VirtualGarageTest: redirected to its hook |
| `0x005344D0` | `maybe_Profile_LoadFromBuffer`: thiscall, ECX = profile block (player 1 = `0x0083A9E0`), 3 stack args, `RET 0xC` at `0x005346E7` | prologue `55 8B EC 83 EC 24` | — |
| `0x005291E4`, `0x0053331B`, `0x005417E0` | `CALL Career_RebuildActiveEvents` (all 3 sites; ECX = g_Career) | `E8` + target `0x00527D30` (checked before patching) | EndlessCareer: redirected to `Hook_Rebuild` |
| `0x0054C9A9`, `0x005416EA` | `CALL Career_OnRaceWon` (end-of-race result + debug win; ECX = g_Career, 1 stack arg) | `E8` + target `0x00541420` | EndlessCareer: redirected to `Hook_RaceWon` |

## Career events (Goal 2, found 2026-10-12, events chat)

Static analysis + decoding `GLOBAL/GLOBALB.BUN` (MD5 35c02c5e667117a3ff197468c7e12205) + Chris's real saves. **Nothing tested in-game yet.**
Race/stage/showcase definitions are **data**, loaded from the `GCareer` chunk (ID `0x80034A10`) of GLOBALB. The pointers below point into
the loaded file buffer, so an ASI can read (and edit) them in memory. Mods like "Smol Improvement" edit the same data in GLOBALB.LZC with the Binary tool.
Record layouts: structs.md → "Career events". Full race list: `docs/races.txt` (made by `tools/dump_races.py`).

### Static tables (filled by `GCareer_LoadChunk`)

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x00512C00` | `GCareer_LoadChunk` | Walks the sub-chunks of chunk `0x80034A10` and stores a pointer + count for each table. | Decompiled; sub-chunk IDs/sizes match the file and Nikki's `BinBlockID`. |
| `0x008383E0` / `0x008383E4` | `g_GRaces` / `g_GRaceCount` | Race table, 0x88 bytes each (sub-chunk 0x34A11). 224 races in the stock file. | Loader + decoded file. |
| `0x008383F0` / `0x008383F4` | (unnamed) | World shops, 0xA0 each (0x34A12). | Loader + Nikki. |
| `0x00838400` / `0x00838404` | (unnamed) | Brands, 0x44 each (0x34A14). | Loader + Nikki. |
| `0x0083840C` / `0x00838408` | `g_GShowcases` / `g_GShowcaseCount` | DVD covers / magazines, 0x40 each (0x34A16). | Loader + Nikki. |
| `0x00838414` / `0x00838410` | `g_Sponsors` / count | Sponsors, 0x10 each (0x34A19). | Loader + Nikki. |
| `0x0083841C` / `0x00838418` | (unnamed) | Stages, 0x50 each (0x34A18, via `0x00512A70`). 6 stages (0–5). | Used by `0x00514400` to resolve stage IDs. |
| `0x00838424` / `0x00838420` | `g_WorldChallenges` / count | 0x18 each (0x34A1B). | Loader + Nikki. |
| `0x00838428` / `0x0083842C` | `g_GCareerStrings` / size | String block (0x34A1D); race names etc. are 16-bit offsets into it. | Loader + decoded file. |

### Race-table helpers

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x004FFC90` | `GRace_FindByKey` | Linear search of g_GRaces for race+0x08 == key. | Decompiled. |
| `0x004FFD90` | `GRace_GetByIndex` | Bounds-checked index → record pointer. | Decompiled. |
| `0x004FFD80` | `GRace_GetCount` | Returns g_GRaceCount. | Decompiled. |
| `0x004FFCF0` | `GRace_FindNextByTrack` | Next race that uses a given track ID. | Decompiled. |
| `0x00500A40` | `GRace_GetURLCashForStage` | Cash of the first URL race (icon 2) of a stage. | Decompiled. |

### Career event state (all on `g_Career` = `0x0085AD40`, thiscall ECX = g_Career)

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x00862838` | `g_Career_CurrentStage` | = g_Career+0x7AF8. 0–5 during the story, **6 after the career is finished**. | Code + Chris's save (6) + NAME save (0). |
| `0x0086283C` | `g_Career_EventStatusCount` | = g_Career+0x7AFC. Entries used in the status list (max 0x180 = 384). | Code (reset loop of 0x180) + saves (Chris: 250). |
| `0x00862840` | (event status list) | = g_Career+0x7B00. 8-byte entries `{key, discovered, ?, status, ?}`. See structs.md. | Code + Chris's save decodes to real race keys. |
| `0x00861F08` | (active event list) | = g_Career+0x71C8. 64 GRace pointers = **the events currently on the map**. Not saved; rebuilt. | `Career_RebuildActiveEvents`, `Career_RebuildMapMarkers`. |
| `0x00500B80` | `Career_IsEventWon` | `(key)`: status entry exists and status == 1. | Decompiled. |
| `0x005003B0` | `Career_FindEventStatus` | `(key)` → entry pointer or 0. | Decompiled. |
| `0x00512F10` | `Career_AddEventStatus` | `(key)`: appends an entry (all zero) if not present, returns it. | Decompiled. |
| `0x00512F70` | `Career_MarkEventDiscovered` | `(?, race)`: creates the entry if needed, sets discovered = 1; first time for a hidden race shows a popup. | Decompiled. |
| `0x00532B10` | `maybe_Career_OnEnterMarker` | Player drove into a map marker: remembers it (+0x8700), and for a race calls `Career_MarkEventDiscovered`. | Decompiled. |
| `0x00528500` | `Career_OnRaceLost` | `(key)`: status = 4 unless already 1 (won), then `0x005242C0(race, 0)`. Called by `Career_ApplyRaceResult` when the player did NOT win. (Was misnamed `maybe_Career_OnRaceStarted` until 2026-10-13.) Writes entry+6 without a null check; the entry always exists because `Career_StartRace` creates it. | Decompiled; caller `0x0054C940`. |
| `0x00541420` | `Career_OnRaceWon` | `(key)`: status = 1; cash (+0x7134) += race CashValue (+0x30) (or a sponsor payout); then stage check `0x00532C60`, marker rebuild etc. **No null check either.** | Decompiled. |
| `0x00513880` | `Career_IsFinished` | `Career_IsEventWon(stage5.LastStageEvent)` = S5_CIRCUIT_36 (final Caleb race). Stage 5 data pointer is at g_Career+0x78EC. | Decompiled + stage data. |
| `0x005130A0` | `Career_AllStageEventsWon` | `(stage)`: every race of that stage won? | Decompiled. |
| `0x00513160` | `Career_CountEventsByCategory` | `(category, &won, &total)` for the stats screen. | Decompiled. |
| `0x00528A60` | `maybe_Career_ComputeStats` | Fills the career statistics struct (counts won races per type, showcases, …). | Decompiled. |
| `0x00532C60` | `StageRecord_CheckComplete` | Stage-complete check, run by `Career_OnRaceWon` on the record of the **won race's own stage** (ECX = g_Career+0x72D8+race.BelongsToStage*0x104). If that stage isn't complete and its LastStageEvent is won (+ required showcases): +0xFC = 1 and `Career_AdvanceStageIfCurrent`. | Decompiled + disassembled (ECX set at `0x005414F8`, untouched by `0x00513390`). |
| `0x00528320` | `Career_AdvanceStageIfCurrent` | `(stageId)`: stage += 1 **only if stageId == current stage**. So replaying an old race after the career never advances the stage. | Decompiled. |
| `0x00513390` | `StageRecord_AddWonRace` | Adds the won race's key to its stage record's won lists (+0x00 regular ×35, +0x8C sponsor ×4, +0x9C URL ×10). Keeps ECX. | Disassembled. |

### Which events are on the map

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x00527D30` | `Career_RebuildActiveEvents` | Rebuilds the active list (g_Career+0x71C8). **Returns with an EMPTY list if `Career_IsFinished`.** Otherwise: world challenges; keeps unwon events from the old list; fills each category up to the stage's MaxXShownOnMap with random candidates; adds hidden races (GRace+0x7F) and URL races of the current stage. | Decompiled. Callers: `0x005291E0`, `0x00541680`, `Career_RebuildMapMarkers`. |
| `0x00527BB0` | `EventList_PickRandomCandidate` | `(category)`: races with BelongsToStage == current stage, not URL, not hidden, not won, not S5_CIRCUIT_36, matching the category (and SUV races only with an SUV); shuffles (`0x0043C1C0` = random) and returns one. | Decompiled. |
| `0x00527AA0` | `Career_GetMaxActiveForCategory` | Category 0–4 → stage data +0x30/+0x31/+0x33/+0x34/+0x32 (Max Circuit/Drag/Drift/Sprint/StreetX shown); 5 = SUV; 6–10 = sponsor races. | Decompiled. |
| `0x00513770` | `EventList_CountActiveInCategory` | Counts active-list entries in a category (`0x00513600` = race-matches-category). ECX = g_Career+0x7148. | Decompiled. |
| `0x005007A0` | `EventList_AddActive` | Appends a race to the first free active-list slot. | Decompiled. |
| `0x00533310` | `Career_RebuildMapMarkers` | Calls `Career_RebuildActiveEvents`, then builds the world-map marker array (g_Career+0, 64 × 0x18): shops, showcases, then one marker per active race at its EventTrigger (race+0x28, looked up with `0x004FFC50`). | Decompiled. |
| `0x00529230` | `Career_LoadFromBuffer` | Loads career data: 8 stage records (0x101 bytes each in the save, `0x00514400`), cash, stage, the event status list (0xC00 bytes), current car, … | Decompiled; offsets checked against Chris's save. |

## Car lot trade-in, menu engine (FEng) and profile name (garage chat, 2026-10-13)

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x0083A9E0` | `g_Profile1_Name` | Profile name, 32-byte C string (start of player 1's profile block). | `0x00510AF0` strlen/copies it to build a save name; "Chris"/"NAME" found at the matching save offset 0xD225. |
| `0x00496050` | `CarLot_ReplaceCar` | thiscall(carLotScreen, slotHash, newCarKey); picks player's car DB (screen+0xEC) and tail-jumps to `CarDB_ReplaceCarInCareerSlot`. **Only** path that overwrites an occupied slot. Called from `0x004EDD6B`, `0x004EE0B8`, `0x004EE0EF`. | Disassembled; all 3 call sites read (args slotHash, newCar). |
| `0x00534950` | `CarDB_ReplaceCarInCareerSlot` | Clears the slot's 0x3C0 block, re-inits the record from the new car type, kind=4, occupied=1. `RET 8`. | Disassembled. |
| `0x00851148` | `g_CustomizeShop_ProfileBackup` | Heap copy of the profile made on entering the customize shop; freed + zeroed on exit (`maybe_CustomizeShop_Exit` 0x0054FD40) and at `0x00552C61`. **Non-zero = inside the shop.** | Decompiled all 3 writers. |
| `0x00850078` | (data) | == 1 in career mode (code uses the career car only when it is 1). | `0x0055E820`, `maybe_Race_BuildPlayerCars`. Name is a guess. |
| `0x008384C4` | `g_FEManager` | Menu manager; `*(g_FEManager)+8` = FEng object. | `FE_FindPackage`. |
| `0x00558C00` | `FEng_Update` | Per-frame menu engine update: reads pads, then `FEng_ProcessPadsForPackage` for each open package. | Decompiled. |
| `0x0054D4B0` | `FEng_ProcessPadsForPackage` | thiscall(FEng, package), `RET 4`. Turns pad bit edges into menu messages (`g_FEPadPressMessages`). Single caller `0x00558CC4`. | Decompiled + disassembled. |
| `0x007F7D20` | `g_FEPadPressMessages` | Message hash per pad bit: [4] PAD_ACCEPT [5] PAD_BACK [6] PAD_START [7] PAD_LTRIGGER [8] PAD_RTRIGGER [9..18] PAD_BUTTON0..9. | Read memory; hashes = StringHash of the names (`tools/match_hashes.py`). |
| `0x007F7D78` | `g_FEPadReleaseMessages` | Same, for button releases. | From `FEng_ProcessPadsForPackage`. |
| `0x0055B5C0` | `MenuScreen_BaseNotify` | Shared handler every menu screen calls: PAD_BUTTON4 = help, PAD_BUTTON1 = messenger; then the screen's own handler (vtable+8). | Decompiled (debug strings). |
| `0x0052CF60` | `FE_IsPackageOpen` | (name) → is that .fng open. | Decompiled. |
| `0x0052CEF0` / `0x00519E50` | `FE_FindPackage` / `FEPackageList_FindByName` | Find an open package by name (package+0x0C name, +0x10 name hash). | Decompiled. |
| `0x0052D170` | `FE_HelpBarClickToPadMessage` | PC help-bar click bitmask → pad message. | Decompiled. |
| `0x005309E0` | `maybe_FE_ShowCar` | **cdecl**(player, recordHash, 0); the garage calls it as `(0, g_Career_CurrentCarSlotHash, 0)` at `0x00495B06` and pops the args itself. | Disassembled `0x00495AF0`. |

Menu message hashes (StringHash, upper case names): PAD_ACCEPT 406415E3, PAD_BACK 911AB364, PAD_LEFT 9120409E, PAD_RIGHT B5971BF1,
PAD_UP 72619778, PAD_DOWN 911C0A4B, PAD_START B5AF2461, PAD_LTRIGGER 5073EF13, PAD_RTRIGGER D9FEEC59, PAD_BUTTON0..4 C519BFBF..C519BFC3.
Code uses per message: BUTTON0 3, BUTTON1 29, BUTTON2 1, **BUTTON3 0**, BUTTON4 10 (`tools/find_hash_uses.py`).

## Race start/finish and payout (events chat, 2026-10-13)

Found while designing NFSU2EndlessCareer v0.1.0. Static analysis; the flow matches what the event logger saw in test round 2.

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x00541160` | `Career_StartRace` | `(race)`: +0x8730 = race, `Career_AddEventStatus(key)` (so the status entry always exists), status = 2 unless already 1. **Does not refuse already-won races.** Callers `0x00499B10`, `0x004E0F60`, debug `0x00541680`. | Decompiled; status 2 seen in-game. |
| `0x0054C9D0` | `Career_OnRaceFinished` | Applies the result, picks the post-race movie (flag: race stage != current stage), clears +0x8730. | Decompiled. |
| `0x0054C940` | `Career_ApplyRaceResult` | Won → `Career_OnRaceWon(key)`, else `Career_OnRaceLost(key)`. | Decompiled; CALL `Career_OnRaceWon` at `0x0054C9A9` (`E8 72 4A FF FF`). |
| `0x00532E50` | `Career_GetRacePayout` | Payout used by `Career_OnRaceWon` while a race is current (+0x8730): sponsor race (icon 1) AND the current stage record has a sponsor (+0x73C0) → sponsor's per-win cash (short at sponsor+2); otherwise **race CashValue (+0x30)**. At stage 6 there is no sponsor → CashValue. | Decompiled. |
| `0x00513A50` | `Career_PickPostRaceMovie` | Plays the race's outro movie (+0x04, read at `0x00513A71`) if it's set and the race is won; Scene07 for other-stage races in stage 2; Scene15/16 for the final race. | Decompiled + bytes. |
| `0x005002E0` | `GRace_GetMapCategory` | (EAX = race) → 7 SUV, 5 URL, else by type: 0 circuit, 1 drag, 2 street X, 3 drift, 4 sprint. Index into the per-category counts (+0x72C8). | Decompiled. |
| `0x005007A0` | `EventList_AddActive` | __thiscall(ECX = g_Career+0x7148, race), `RET 4`: count[category]++ and put the race in the first free slot of the 64-slot active list. | Disassembled. |
| `0x00500820` | `EventList_CountActive` | Number of non-empty active-list slots. | Decompiled. |
| `0x00541680` | `maybe_Debug_WinRandomActiveRace` | Picks a random active race, starts it, wins it, rebuilds. No normal caller found; looks like a debug/cheat function. | Decompiled. |
| `0x0054CAF0` | `Career_OnEnterShop` (was `maybe_Career_OnShowcaseDone`, renamed 2026-10-16) | Runs when the player drives into a shop: marks the shop key with status 5, handles the DDAY prologue chain, starts the unique-part pick when a world challenge ends at this shop. | Decompiled. |
| `0x00503AE0` | (CarRecord vtable[1], GetCarTypeIndex) | `CarTypes_FindByNameHash(record+0x20)` → entry+0x840 (index) or -1. SUV flag = `*0x008A1CCC + index*0x890 + 0x88A`. | Bytes. |
| `0x00527D93` | (in `Career_RebuildActiveEvents`) | g_Career+0xC08 != 0 → the game shows no events at all (mode unknown). The plugin respects it. | Disassembled. |

Data facts:
- 14 races have an intro and/or outro movie (story scenes, the drag/street X/drift tutorials, the area-unlock movies after S2_URL_3, S3_URL_8, S4_URL_7).
- 224 races share only **117 map spots** (EventTrigger, race+0x28); 66 spots are used by more than one race.
- Only DDAY_EVENT_A forces a car (race+0x2C).

### Race session (found in test round 4, 2026-10-13)

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x00532EA0` | `RaceSession_Init` | __thiscall(ECX = g_Career+0xD24, race), `RET 4` (at `0x0053309C`, `0x005330C2`). Sets up legs/opponents, then flags: +0xD25 = 1 (result counts), +0xD26 = 1 (pays). **If the race is already won: +0xD26 = 0 and +0xD28 = 1 ("replay").** Callers: `0x00541190` (Career_StartRace), `0x00541101` (DDAY prologue). | Decompiled + bytes; explains round 4 (wins of replayed races did nothing). |
| `0x0054C943`, `0x0054C94D` | (in `Career_ApplyRaceResult`) | Applies the result only if +0xD25 != 0 **and** +0xD28 == 0. | Bytes. |
| `0x00541425` | (in `Career_OnRaceWon`) | Pays only if +0xD26 != 0 and +0xD27 == 0. | Bytes. |

Hook points: `0x00541190`, `0x00541101` = `CALL RaceSession_Init` (`E8` + target `0x00532EA0`), redirected by EndlessCareer v0.1.2 to `Hook_SessionInit`.

### Race AI opponents (found in test round 5, 2026-10-13)

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x005260B0` | `Race_SetupOpponents` | `(race)`: creates the AI opponents (race+0x40, 12 bytes each). Mode byte opp+2: **0** -> car level = opp+3; **3** -> `StageRecord_GetAiCarLevel` of the **current** stage record (`MOV ECX,[0x00862838]; IMUL ECX,0x104; ADD ECX,0x00862018` at `0x0052616F`). Preset opp+4 == StringHash("RANDOM") -> random car of that level (`0x004FEAF0`). Skill = opp+8+difficulty (`0x005261E6`), catch-up = opp+0xB. Level is clamped to 10.0 (`0x00784560`) and x0.1 (`0x00784358`) = fraction of the car's performance range. | Decompiled + disassembled + constants. |
| `0x005132E0` | `StageRecord_GetAiCarLevel` | thiscall(stage record): lerp from stage data +0x44 (min) to +0x48 (max) by the stage's progress. Stage data min/max: 0:0/0, 1:0/1, 2:0/3, 3:3/4, 4:4/8, 5:8/10. **At stage 6 the record points at stage 0's data -> level 0 -> weakest random cars (Corollas).** | Decompiled + stage data. |
| `0x00839C40` | `g_FECarDisplay` | Menu car display object. +0x000 = pointer to the car record shown (set by `maybe_FECarDisplay_SetRecord`), +0x004 RideInfo, +0xA0C player. The 3D car itself is spawned from `g_Career_CurrentCarSlotHash` (`maybe_FE_SpawnCurrentCar`). | `maybe_FE_ShowCar` passes `MOV ECX,0x839C40` to it (disassembled 2026-10-13). |

### Corrections and car select (garage chat, 2026-10-13, after test round 4)

- **`0x00552CC0` renamed `maybe_FECarSession_Begin`** (was `maybe_CustomizeShop_Enter`), **`0x00851148` renamed `g_FECarSession_ProfileBackup`**
  (was `g_CustomizeShop_ProfileBackup`). The game starts this session in the garage menus too (backup non-zero in `UI_CareerCarSelect`
  and `UI_CareerCrib`, round 4), so it does **not** mean "in the customize shop". The backup is never restored: exactly 7 references in
  the exe (raw byte scan): check, alloc, SaveToBuffer, 2× free.

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x004FC110` | `Create_UICareerCribCarSelect` | Factory: `malloc(0x1B0)` + constructor. | Found via its unwind handler (`PUSH 0x00776EB4`), which deletes an object named "UICareerCribCarSelect". |
| `0x004F9AD0` | `UICareerCribCarSelect_Construct` | Career car select screen constructor; vtable `0x00795D00`. Opens GarageMain.fng if needed. | Decompiled. |
| `0x00795D00` | `vtable_UICareerCribCarSelect` | [0] `0x004DFC30`, [1] `0x0040D080`, **[2] `0x004DFC50` Notify**, [3] `0x00570A70`, [4] `0x004E0C10`. | Read memory. |
| `0x004DFC50` | `UICareerCribCarSelect_Notify` | Message handler. Screen+0x54 = highlighted list entry; entry+0x920 = its slot hash. ACCEPT on a non-current car → confirm → `FUN_00500BD0(hash)` makes it current. | Decompiled. |
| `0x004B04C0` | `UICareerCribCarSelect_Scroll` | PAD_LEFT/RIGHT: moves screen+0x54 along the entry list, then redraws (FUN_00557EC0, FUN_0055B1A0, FUN_004A5EE0 model load, FUN_004B0140). | Decompiled. |
| `0x004FC1F0` | `maybe_UIQRCarSelect_Notify` | Car select for non-career modes (quick race / online; modes 4, 8, 0x20, 0x100, 0x200). Highlighted car = `*(screen+0x58)+0x920`. | Decompiled. |

### Corrections after test round 5 (garage chat, 2026-10-13)

- **`0x005309E0` is NOT a menu redraw.** Renamed `maybe_FE_LaunchCar`; `0x00526970` → `maybe_FE_LaunchCurrentCarIntoWorld` (spawns the
  current car as "Test Driver" and calls `GameFlow_SetLoadingRegion` 0x00581020 = start loading the world); `0x00495AF0` →
  `maybe_GarageScreen_LaunchDyno`. Evidence: calling it after a swap loaded `UI_PerformanceTuning_Setting` (dyno) both times in round 5;
  the same Button 3 press without the call (round 4) stayed in the menu.
- `0x00500BD0` = `Career_SetCurrentCar(hash)`: thiscall on g_Career, sets +0x873D = 1, +0x8740 = hash. Nothing else.
- `0x00511E00` (fastcall, ECX = car list entry): returns entry+0x20 RideInfo, rebuilding it first from the record whose hash is at
  entry+0x920 if entry+0x914 (dirty) is set; entry+0x910 = player. Car lot: `MOV ECX,[ESI+0x58]` (0x004EE04B); car select ctor:
  `MOV ECX,[ESI+0x54]` then `FUN_0055B1A0` on `0x008637D0` (0x004F9BD4..). Candidate for a real refresh in car select (v0.7).
- `0x004D8EC0(rideInfo, 1, 0)`: cdecl; car lot uses it to show the bought car.

### Garage backdrop car redraw (garage chat, 2026-10-13, for v0.7)

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x0040C910` | (RideInfo constructor) | thiscall(ride), no args, plain `RET`. RideInfo is ~0x8F4 bytes. | Disassembled. |
| `0x004022C0` | (empty) | The game's "RideInfo destructor" call is an empty function (`RET`). | Decompiled. |
| `0x004D8EC0` | `FE_ShowGarageCar` (name mine) | cdecl(ride, mode, which). If GarageMain is open, copies the RideInfo into the garage display and loads that 3D car + sets its name text (`FUN_004D8C70` → `FUN_004A5F30`). Customize screen uses mode 2 (0x0055E9B9), car lot mode 1 (0x004EE058). | Decompiled + disassembled call sites. |
| `0x004A78B0` | (data) | Opens GarageMain.fng if it isn't. | Decompiled. |

### Car select browsing (garage chat, 2026-10-14, for v0.8)

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x004E0160` | (car select list builder) | One 0x940-byte entry per career slot (vtable `0x00790EAC`): locked → +0x930=1,+0x931=1; empty → +0x931=1; occupied → `FUN_00511DB0(type, slot+0xC, slot hash, player, 0)` + RideInfo built from the slot. Highlights the first entry and shows its car. | Decompiled. |
| `0x00511DB0` | (list entry constructor) | thiscall(entry, type, value, hash, player, dirty), `RET 0x14`: +0x918 type, +0x91C value, +0x920 hash, +0x910 player, +0x914 dirty. | Disassembled. |
| `0x004B0140` | (car select texts) | Sets SLOT_TITLE via `FEPrintf(pkgName, StringHash("SLOT_TITLE"), "%s %d", locString(0x93475966), index)`, badges, arrows. | Disassembled. |
| `0x00537B80` | `FEPrintf` (name mine) | cdecl(packageName, objectHash, format, ...): printf into an FE text object (only if the object is text, type 2). | Decompiled + the game's call at `0x004B015B` (`ADD ESP,0x14`). |
| `0x004DFC30` | (car select destructor) | vtable[0], thiscall(screen, flags), `RET 4`. | Disassembled. |
| `0x00513880` | `Career_IsFinished` | fastcall(g_Career) → AL. Reads `[ECX+0x78EC]+0x28` (crashes if 0), then `Career_IsEventWon` (read-only). | Disassembled (events chat name). |
| `0x007F7CFC` | (direction table) | Pad bits 0/1/2/3 → PAD_UP / PAD_DOWN / PAD_LEFT / PAD_RIGHT (+ diagonals). | Read memory. |
| Car select Notify tick | `0xC98356BA` | ~1 s after a scroll (screen+0xB8 timestamp) shows the highlighted entry's RideInfo in the garage (`FE_ShowGarageCar`). | Decompiled. |

### Car lot purchases and car unlock tiers (garage chat, 2026-10-14, for v0.9)

**The game never charges for cars and never pays for trade-ins**: every instruction that writes cash was checked (26 uses of
`+0x7134`, 33 of `0x00861E74`); none is in car lot code. "Trade_Group" is a car select text group the game hides; the only price
text is `PartPrice_%d` (parts).

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x00795CF0` | `vtable_UICareerCarLot` | [0] `0x004DF9A0`, [2] `0x004ED9E0` = `maybe_CarLotScreen_HandleMessage` (thiscall(screen, msg, a, b, c), **RET 0x10**). | Read memory + every RET in the function. |
| `0x004FC0B0` / `0x004F9930` | (car lot factory / constructor) | `malloc(0x1C0)`; mode via `maybe_CarLot_ChooseBuyMode` → `FUN_004ED8A0(mode)` (sets screen+0x4C; mode 4 prints FILTERTYPE = loc(0x8D0DAFEC)). | Found via its unwind handler; decompiled. |
| `0x004DF9C0` | (car lot list builder) | `(kind)`: kind 1 = catalogue (cars for sale), kind 4 = your slots (mode 5). Filter: car type +0xDA bit 1, `FUN_005022C0` (2 special-car blacklists), unlocked list from `FUN_00513C50`. Entries = `CarSelectEntry_Construct` (+0x918 type, +0x920 catalogue key). | Decompiled. |
| `0x00513C50` | (unlocked car types) | All 46 types unlocked, then each car in the unlock table whose race is not won is locked. | Decompiled + disassembled. |
| `0x00838438` / `0x0083843C` | (unlock table / count) | 12-byte records {car name hash, race key, alternate race key} = GLOBALB chunk **0x34A1F** (24 cars). Alternate used when `[0x00864F24]` != 0 (`FUN_00570A30`). | Disassembly at `0x00513C6C`, `0x00513C88`; decoded by `tools/dump_car_unlocks.py`. |
| `0x00558430` | (message dialog) | cdecl(callerPackageName, "GenericDialog_SMALL.fng", messageHash). Shop: message `0xD1AC60BE` = not enough cash, then `[0x00838970] = 0x34DC1BEC` (UNDIM on close). | Disassembled at `0x005592B8`. |
| `0x004FFA80` | (localized string) | cdecl(hash) → const char*. | Decompiled + call sites. |
| `0x00559240` | (parts shop buy) | `if (cash < price) → not-enough-cash dialog`. | Decompiled. |

Car tiers (unlock race stage): starter (not in table) 240SX, Civic, Corolla, Focus, Impreza, Lancer, Neon, Peugeot, S2000;
stage 2 Celica, Escalade, Hummer, Navigator, Sentra, Tiburon (Miata alt); stage 3 A3, Eclipse, Golf, IS300, RSX, Supra, TT;
stage 4 3000GT, 350Z, G35, GTO, Lancer Evo 8, Mustang GT, RX-7, RX-8; stage 5 Impreza WRX, Skyline.
Race payouts (median / max): stage 2 $750 / $1,100, stage 3 $600 / $1,500, stage 4 $1,200 / $8,000, stage 5 $2,000 / $10,000.

### Profile save to disk, scrollbars, menu layout files (garage chat, 2026-10-14, for v0.10)

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x004D9B80` | (save-to-disk job) | thiscall(memcard manager, name): allocates a buffer, `FUN_0052A450` serialises the profile into it (call at **`0x004D9C0A`**, its only caller), then `FUN_006FBF13` writes it (the `20CM` save code). | Decompiled + disassembled. |
| `0x0052A450` | (serialise for save) | `ADD ECX,0x10; JMP maybe_Profile_SaveToBuffer` → thiscall(manager=`0x0083A9D0`, buffer, size), `RET 8` (`0x0052A438`). | Disassembled. |
| `0x0052A460` / `0x0052A4C0` | (save snapshot / "changed?") | Snapshot of the profile at manager+0x16778; later compared to a fresh serialisation to decide whether there is anything to save. | Decompiled. |
| `0x00542AC0` | (scrollbar setup) | thiscall(scrollbar, package, name, ...): finds FE objects `<name>_Backing`, `_Handle`, `_Arrow_1`, `_Arrow_2`, `_Backing_End_1`, `_Backing_End_2`. Car select uses "Scroll Bar"; customize uses "ScrollBar" and "SideScrollBar". | Decompiled. |
| `0x00538080` | (scrollbar update) | thiscall(scrollbar, ?, count, position): shows/hides parts, positions the handle (`FUN_0052E200` / `FUN_0052E320` by orientation). | Decompiled. |

**Menu layouts (.fng):** the 131 front-end packages are inside `FRONTEND\FrontB.lzc` (JDLZ, decompress with `tools/jdlz.py`;
verified byte-identical on GlobalB.lzc vs GLOBALB.BUN). Each package (header `FEn\xE7` … `PkHd`) is compressed **again** with a
second, unknown LZ-style scheme (names appear with back-references, e.g. "UI_Care\x04er\x05\x00Select"). Not decoded yet.

## Dialog texts, FE text objects, car select ACCEPT (garage chat, round 9, 2026-10-14)

| Address | Name | Purpose / how verified |
|---|---|---|
| 0x00558790 | FE_ShowDialogFormatted | cdecl(pkg, dlgPkg, a, b, yesMsg, noMsg, noMsg2, flag, textHash, ...): localized text = printf format. Disassembly: passes 11 args to 0x00558680 (`ADD ESP,0x238`). |
| 0x00558680 | FE_ShowDialogText | cdecl(pkg, dlgPkg, a, b, yes, no, no2, char 1, flag, format, va_list): vsprintf into 768 bytes, then FUN_00558020 shows it. |
| 0x004EDCAC | car lot CALL FE_ShowDialogFormatted | modes 2-5 questions; modes 3/4/5 push (current car name, new car name). Hooked by virtual garage v0.11. |
| 0x005379C0 | FE_FindObject | cdecl(packageName, objectHash) -> object (same lookup as FEPrintf). Object +0x18 type (2 = text), text +0x68 max length (FEPrintf keeps +0x68 − 1 chars, 0x00537B50). |
| 0x00537B30 | FEObject_SetTextV | vsprintf + set text; returns the count (FEPrintf returns 0 if the object is missing). |
| 0x0050CA50 / 0x0050CA00 | FEObject_Show / maybe_FEObject_Hide | clear / set the hidden bit (recursive for groups). |
| 0x0050BC10 | FEFont_GetCharAdvance | '^' and '\n' are line breaks unless text flag 0x20. |
| 0x004B0140 | UICareerCribCarSelect_UpdateTexts | fastcall(screen), plain RET (tail JMP to FUN_004962F0). Writes SLOT_TITLE "%s %d" ("Slot", screen+0x5C), CARBADGE, arrows 0x26F1F64B/C, scrollbar. Called from Scroll (CALL 0x004B0664) and the setup (JMP 0x004EE310). |
| 0x004EE210 | UICareerCribCarSelect_Setup | builds the list, hides FILTERTYPE/CUSTOMIZE/Trade_Group, then UpdateTexts. Called from the constructor 0x004F9AD0. |
| 0x004DFD96 / 0x004DFE56 | car select ACCEPT checks | needs screen+0x194; compares entry+0x920 with the current car slot's hash → "already driving" 0x7EE84E4E, else "drive X instead of Y?" 0x1231C38F with YES = 0xD05FC3A3. |
| 0x00500BD0 | Career_SetCurrentCar | thiscall(g_Career 0x0085AD40, slotHash): +0x873D = 1, +0x8740 = hash (= kCurrentSlotHash 0x00863480). |
| 0x0085AD40 | g_Career | (0x00513C30 on it returns the current car slot). |
| GLOBALA.BUN +0x21500 | font chunk 0x30201 | `conduitmditc_tt21i`, "FNTF" at +0x200, 211 glyphs (16-byte records, u16 char code first). No arrow glyphs. |

Texts (LANGUAGES/English.bin, `tools/dump_strings.py`): 0x3192F5D1 / 0x85215BC6 trade-in questions, 0x7EE84E4E "You're already
driving your %s!", 0x1231C38F "Do you want to drive your %s instead of your %s?", 0xDA8F3E96 "Do you want this stock %s?",
0x93475966 "Slot", 0x8D0DAFEC "Career Cars".

## FE text icons and setters (garage chat, round 10, 2026-10-14)

| Address | Name | Purpose / how verified |
|---|---|---|
| 0x00535FE0 | FEString_SetText | thiscall(textObject, const char*), RET 4: sets obj+0x64 text, grows the buffer (FUN_00532670 → FUN_0051DE90 malloc) when longer than obj+0x68. |
| 0x0050BB10 | FEText_GetIconImage | "$NAME$" → image: ICON_SPACER = 0xBCA80379, else FUN_00572190(name, input device) (button image for that JOY_EVENT). |
| 0x0051C110 | maybe_FEText_Draw | '$' token handling unless obj flags & 0x820 (0x0051C337); '^' line break unless flags & 0x20. |
| 0x00520070 | maybe_FEText_WrapLines | same token rules when measuring lines. |
| 0x004FF9D0 | Language_FindString | fastcall(EDX = hash); table [0x008383D8] {hash, char*} sorted, count [0x008383D0]. |
| 0x0052D330 / 0x0054E6E0 | PCHelpBar_GetButtonLabel / maybe_PCHelpBar_SetButtons | PC help bar = clickable buttons chosen by a bitmask (screen+0x20); labels like "Back", "Customize". |

## Button icons, textures, language table (garage chat, round 11, 2026-10-14)

| Address | Name | Purpose / how verified |
|---|---|---|
| 0x00572190 | JoyIcon_GetImageForEvent | JOY_EVENT name → icon texture hash via table 0x007FDD10 (0x20-byte records, +8 event id, +0x18 image; count [0x007FE770]). PC: PAD_* entries have image 0. Entry 15 = PAD_DOWN at 0x007FDEF0. |
| 0x004901D0 | Texture_FindByHash | cdecl(hash, useDefault, 0): loaded texture or 0 (useDefault 1 → placeholder texture, the "-" seen in round 10). |
| 0x008383D8 / 0x008383D0 | language table / count | sorted {u32 hash, char* text}; Language_FindString 0x004FF9D0. |
| 0x004EDC36 | car lot mode 4 | pushes button text 0xFB75E78F "Trade this car?" (its only use); second button 0x2699CF8E "Browse My Other Cars". Yes/No/OK are 0xF770/0x61C/0x639. |
| 0x00558020 | FE dialog show | copies the request to 0x00838640 and asks the FE manager to build the dialog (labels are read then, not during the call). |
| texture | U2_GRAB_ARROW 0x52A3428B | FrontB: round icon with up and down triangles. |

## Drag race steering (drag steering chat, 2026-10-15)

How drag "slot car" steering works: in a drag race the player's car gets a different **controller** (the object that drives
a car). Its driver part, DragDriver, is a normal driver with its steering input switched off. It listens to lane-change
buttons, and an AI steers the car into the chosen lane. All static analysis (Ghidra + objdump); **not yet tested in-game.**

| Address | Name | Purpose / how verified |
|---|---|---|
| 0x005EBA30 | Car_SetControllerType | thiscall(car, type): car+0x24 = type, car+0x2C = controller, car+0x30 = driver (= controller+8). Types: 0 idle (0x005EB930), 1 HumanController (0x004266D0, normal player driving), 2 DragController (0x004299C0), 3 AI racer base (0x004260B0), 4 0x00426580, 5 0x00426650. Decompiled. |
| 0x00602650 | (player per-frame update) | At 0x00602A79: if car+0x24 == 2 and player+0x22C (PLAYER_MODE) != 3 → Player_SwitchToNormalController; if car+0x24 != 2 and mode == 3 → Player_SwitchToDragController. Disassembled. |
| 0x00602A92 | drag switch JNE | bytes `3B C7 75 07 8B CE E8 73 05 FF FF` (cmp eax,edi; jne +7; mov ecx,esi; call 0x005F3010). **Patch idea:** 0x00602A94 `75` → `EB` = never switch the player to the drag controller. |
| 0x005F3010 / 0x005F3080 | Player_SwitchToDragController / Player_SwitchToNormalController | Car_SetControllerType(2) / (1), then the same driver set-up calls (vtable +0x10, +0x1C(0), +0x34(x), +0x14). Only caller of type 2 in the exe. |
| 0x005F6D50 | Car_InitControllerForDriver | driverInfo+4 == 1 (human) → type 1; AI → 3/4; +0x900 set → 5. |
| PLAYER_MODE | player+0x22C | names at 0x008020A0: 0 NONE, 1 PAUSEMENU, 2 CAR_RACING, **3 DRAG_RACING**, 4 AUTO_PILOT, 5 TURRET_SELECTOR, 6 FROZEN_ZONE, 7 AI_DEMO, 8 PRERACE, 9 FINISH_RACE. |
| 0x00427F20 | DragDriver_ctor | 0x7A0 bytes, vtable 0x00785118, wraps a RealDriver (ctor 0x00425FC0) at +0x470 and writes 0 to its steering flag (+0x470+0x2F8 = +0x768, at 0x00427F64). |
| RealDriver+0x2F8 | steering input enabled | set to 1 by the ctor 0x00425FC0; 0x0041E060 (input registration) only registers JOY_EVENT_STEER 0x8F / STEER_ANALOG 0x90 (handler 0x00407750) when it's set; 0x0041E337 (update) only applies steering when it's set. |
| 0x00416B10 | DragDriver register input | registers 0x004102B0 for JOY_EVENT_DRAG_RACE_CHANGE_LANE_LEFT/RIGHT (0xB8/0xB9) and their _ANALOG forms (0xBA/0xBB). |
| 0x004102B0 | DragDriver lane-change handler | writes the DragController's lane request (+0x7B4: 0 left, 1 right, 2 none), clears target lanes +0x7B8/+0x7BC to 0xFF. |
| 0x004299C0 | DragController_ctor | 0x7D4 bytes, vtable 0x00785518; lane fields +0x7B4..+0x7CC. Update 0x0042DF10 (vtable +0xC) acts on the lane request, then 0x0042C0C0 drives. |
| 0x0042C060 | DragController pick goal | (vtable 0x007855A0) drag race: human → goal 3 "Steer player in drag race" (0x0040CAB0, vtable 0x00784830), AI → goal 2 "Win drag race"; else goal 1. |
| 0x0042AA20 | AIController_SetGoal | thiscall(controller, goalType) → controller+0x6FC. |
| 0x0089E7D8 | is drag race (byte) | = race options 0x00864FC0 != 0 (RaceOptions_ApplyToGlobals 0x0053FEB0). 140 readers. 0x0089E7D9 = 0x00864FC4 (drift?). |
| 0x007FAF90.. | JOY_EVENT id table | {u32 id, char* name}: STEER 0x8F, STEER_ANALOG 0x90, AUTO_PILOT 0xB5, lane left/right 0xB8/0xB9, analog 0xBA/0xBB. |

Safety check for the patch: every other code that touches DragController fields (0x004246DE, 0x004249F1) first checks
car+0x24 == 2, and nothing reads the player's lane fields from outside, so drag opponents don't depend on the player's
controller.

### Force feedback (drag steering chat, 2026-10-15, after test round 1)

| Address | Name | Purpose / how verified |
|---|---|---|
| 0x00406460 | RealDriver_UpdateForceFeedback | thiscall(driver part, steer, port), called from 0x0041E022 / 0x004220CE. Exits unless car+0x28 == 1, FF device (0x00870760) present, game state [0x008900D8] == 4. If player option 0x005EB420 (byte 0x0083AA68 + player*0x28) is 0 **or RaceOptions_IsDragRace** → 0x0040692A: stop effects, centering spring only. Disassembled. |
| 0x004064D6 | drag check | `84 C0 0F 85 4C 04 00 00` (TEST AL,AL; JNE 0x0040692A). NFSU2DragSteering v0.2.0 NOPs the JNE at 0x004064D8 (6-byte NOP `66 0F 1F 44 00 00`). |
| 0x004018F0 | RaceOptions_IsDragRace | thiscall(ECX = 0x0089E7A0): byte +0x38 (= 0x0089E7D8) or [0x00890108]. 14 callers. |
| 0x005C9550 | FF_SetConstantForceAtAngle | (port, magnitude ±10000, angle deg) → 0x005C9380 with a direction table at FF+0x25F0. Steering force uses angle 270. 3 callers (0x004065DB, 0x005CD771, 0x005CDC34). |
| 0x005C9590 / 0x005C9760 / 0x005C9FD0 / 0x005C9D90 | FF effects | **damper** (corrected 2026-10-16: not a spring, see "Force feedback effects and steering input"; strength (1 − min(1, speed × 0.0769)) × k), impact jolts, terrain rumble by surface (car+0x394/+0x398), and a 10000 effect for surface 0xD above 23 m/s. Error strings at 0x007A3906.. name the DirectInput calls. |

Steering force math (0x00406510..0x004065DB): sum of two front-tire lateral values / [driver+0xC] × 10000, clamped
±10000, forced to 0 below 1 m/s. **No deadband in the game's math**, so the dead feel near the centre is likely the wheel's
own friction (small forces don't move it); a "minimum force" boost would hook the CALL at 0x004065DB. 0x00406A40
(steering assist) also reads the drag flag, but both branches are identical.

Note (2026-10-15): in `src/common/nfsu2.h` the player list/mode offsets are in namespace `player`, the steering-force
CALL in namespace `ffb` (used by NFSU2WheelEnhancements), the drag patches in `drag`.

## Tracks, start lines and race markers (events chat, 2026-10-15, for Endless Career v2)

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x00883DA0` / `0x00883DA4` | `g_TrackInfos` / `g_TrackInfoCount` | Track info table (GLOBALB chunk 0x34201), 0x128-byte records, 131 stock. Layout in structs.md → "Track info". | Loader at `0x005D3EB0` (`cmp [eax],0x34201`, count = size / 0x128 via the magic divide); decoded file. |
| `0x005D3E40` | `TrackInfo_FindByID` | cdecl(trackID) → record with +0x8A == ID, or 0. | Decompiled. |
| `0x005D3E80` | `TrackInfo_GetByIndex` | cdecl(index) → record, bounds-checked. | Decompiled. |
| `0x004FFC50` | `WorldTrigger_FindByHash` | cdecl(hash) → world trigger record (types 13–15, mask 0xE000) whose +0x30 == hash. The map marker builder places a race marker with `WorldTrigger_FindByHash(race+0x28)`, so a race's marker spot = its EventTrigger hash. Record: +0x04 x, +0x08 y (world metres). | Decompiled `0x004FFC50`, `0x005D9C70`, `0x005D9BF0`; same records as `PathsFreeRoam.bin` chunk 0x3414A. |
| `0x004ABF70` | `GRace_GetMapIconType` | fastcall(race) → world-map icon: behavior (+0x0F) 0/3 → 1 circuit, 1 → 4 sprint, 4 → 2 drag, 5 → 3 drift, else 5 street X; EventIcon (+0x34) == 2 → 7 URL; IsSUV (+0x0D) → 8 SUV. **The icon follows the race type byte, never the track.** | Decompiled; indicator table `0x007F6828` ({FE name, id}: CIRCUIT 0x0C … SUV 0x12). |
| `0x00533310` | `Career_RebuildMapMarkers` | (already known) For each active race: `WorldTrigger_FindByHash(race+0x28)` → marker slot {trigger, race, …, type 1}. | Decompiled. |

Start lines are **not** in the exe: they are `ForwardStartLine` / `BackwardStartLine` records (plus one
`Forward/BackwardStartingPosition` per grid slot) in `TRACKS\ROUTESL4R?\TrackPosMarkersAll.bin`, chunk 0x34146,
keyed by track ID. Every circuit/sprint/drag/drift/street X/speedway variant has one. Full list with distances to the
nearest race markers: `docs/tracks.txt` (tool `tools/dump_tracks.py`).

## Gearbox, clutch and H-shifter (H-shifter chat, 2026-10-15)

The game has a **hidden "manual with clutch" shift mode (3)** left over from development. The menus only ever pick
0 (automatic) or 1 (manual), but the driver input, drivetrain and gear-grind code for mode 3 are all present.
Everything below was read statically (Ghidra + objdump); **not yet confirmed in-game**.

| Address | Name | Notes |
|---|---|---|
| 0x00407280 | RealDriver_SetShiftMode | RealDriver vtable 0x00784FC8 slot +0x34 (not a Ghidra function). thiscall(mode): 0 → auto (+0x2C0=0, wanted gear -1), 1 → manual, **3 → manual with clutch**, other → 2 (drag, wanted gear -1/-2 sentinel). Modes 1/3 copy the car's gear (car+0x434) into wanted gear. Copies the mode into drivetrain+0x60 (drivetrain = car+0x34 → +0x4C). |
| 0x005EB430 | Player_GetTransmissionOption | fastcall: returns the profile's transmission byte (+0x20; 0 auto / 1 manual), forced to 1 when [0x0089E7D8]==1 && [0x00864FAC]==0. Called only at **0x004B5C2F** (race start, FUN_004B5AD0 → `push eax; call [ebx+0x34]`) and **0x00427FF7** (DragDriver_ctor). CALL-redirect targets to force mode 3. |
| (inlined copies) | transmission option → SetShiftMode | The same "drag flag ? 1 : profile+0x20" read is **inlined** before `call [vtable+0x34]` at 0x004A7B78 (option changed: also writes the new byte), 0x005F3068, 0x005F30D6, 0x005F3E4D, 0x005F6E08, 0x005FAF70. H-shifter round 1: hooking only 0x004B5C2F never fired; the race used an inlined copy. **Hook the setter instead:** its only pointer is RealDriver vtable slot **0x00784FFC** (value 0x00407280; the exe contains that dword once). |
| RealDriver+0x1E4 | wanted gear | 0=R, 1=N, 2=1st … (top gear index = car+0x20 → +0x1C8, +1). Both third-party mods wrote this from a thread. Getter/setter vtable +0x20/+0x28 (0x00405B20/0x00405B40). |
| RealDriver+0x1E8 | drag target gear | used with wanted gear -2 in mode 2. |
| RealDriver+0x2C0 | shift mode | 0 auto, 1 manual, 2 drag, 3 clutch, 4 = clutch mode temporarily on automatic while a shift into/out of N or R completes (0x0041E3C4–0x0041E405). |
| RealDriver+0x2F4 | clutch engagement | float, 1.0 = pedal up (ctor 0x00426089), 0.0 = pedal down. In mode 3 passed every frame to Drivetrain_SetClutch (CALL at **0x0041E41C**, ECX = drivetrain (EDI), ESI = driver). |
| 0x00407FC0 / 0x004080B0 | RealDriver_OnShiftUp / _OnShiftDown | JOY_EVENT 0x9B/0x9D and 0x9C/0x9E. Mode 1: wanted ±1 (down stops at N, up at top). Mode 3: wanted = car gear (car+0x434) ±1. Mode 2: drag target. Mode 0: 0x0050D520. |
| 0x00408180 | RealDriver_OnClutchEvent | JOY_EVENT_DISENGAGE_CLUTCH 0x9F (down → +0x2F4=0, up → 1) and _ANALOG 0xA0 (1 − scaled value). Registered by 0x0041E060 but **only acts in mode 3**. No controller-menu entry binds these events. |
| 0x007FAFE0 | JOY_EVENT id/name table | {u32 id, char* name} pairs: 0x8F STEER … 0x9B SHIFTUP, 0x9C SHIFTDOWN, 0x9D/0x9E alternates, 0x9F DISENGAGE_CLUTCH, 0xA0 DISENGAGE_CLUTCH_ANALOG, 0xA1 RESET_CAR. |
| 0x007A2508 | Drivetrain vtable | tag string "Drvtrn" just before it. Slot 6 0x005949B0 = shift delay (0 in mode 3), slot 7 = 0x005A1400. |
| 0x00594910 | Drivetrain_SetClutch | thiscall(float c). Sets +0x60=3, +0x14=c, state +0x44: 3 engaged (c≥~1), 1 disengaged (c≈0), 2 engaging (rising), 0 disengaging. |
| 0x005A1400 | Drivetrain_UpdateClutchState | Per tick. Mode 3 (+0x60): torque factor engine(+0x20)+0x80 = 1.0; **current gear +0x3C := target +0x38 only when clutch state is 1 (fully disengaged) and a change is pending (+0x48)**. Other modes: auto-shift state machine (0x005A161B `mov [esi+0x44],2` = the shift-delay byte the NFSU2.Clutch mod patched). Bottom: while +0x5C (grind start time) is set, torque factor = 0 for 0.45 s (0x007A18C8). |
| 0x005AA960 | Drivetrain_RequestGear | thiscall(gear, rpm) → result. -1 = automatic picks, -2 = drag logic. Mode 3, gear ≠ current: clutch not fully down → **refused** (returns current gear) and grind starts (+0x5C = now, +0x48 = 0); clutch down → +0x38 = gear, +0x48 = 1. One caller: 0x005ABDB0. |
| 0x005ABDB0 | (per-tick gear handoff) | `r = Drivetrain_RequestGear(driver->wanted, rpm); driver->wanted = r`. |
| 0x0086518C | game clock | int ticks; × 0.00025 (0x00784268) = seconds. |
| Drivetrain+0x14 / +0x38 / +0x3C / +0x44 / +0x48 / +0x5C / +0x60 | clutch / target gear / current gear / clutch state / change pending / grind start / shift mode | |
| Drivetrain+0x20 → engine+0x80 | engine power factor | float: 1.0 normal, 0 while the grind timer (+0x5C) runs, 0.7 while the automatic engages (state 2), >1 briefly after an upshift (+0x64/+0x68). Written only inside Drivetrain_UpdateClutchState. Confirmed in-game: 0 = no power (rounds 2-3). |
| 0x007A2524 | Drivetrain vtable slot 7 | only pointer to Drivetrain_UpdateClutchState 0x005A1400 (thiscall(drivetrain), no stack args, plain RET at 0x005A1668/0x005A16E8); runs once per physics step for every car. H-shifter v0.3.0 hooks it to scale the power factor. |

Third-party references (in `H-Shifter Mods/`, not used as code): "NFSU2 Hshift.CT" (Cheat Engine, writes
[0x00865998]+0x1E4 on numpad hotkeys) and NFSU2.Clutch (ASI: two background threads, keyboard via GetAsyncKeyState,
fakes the clutch by forcing N and writing [0x008659BC]+0x1E4; optional patches 0x005A161E (shift delay 2→3) and
0x004C6A6E (dashboard reads its own gear)). Both pointers are runtime copies of the player's RealDriver.

**G27 in DirectInput (H-shifter round 1, Chris's wheel, "Report Combined Pedals" off):** 23 buttons, 5 axes, 1 POV.
Shifter positions = buttons 9-14 (gears 1-6), 15 = reverse (1-based); paddles = buttons 5 (up) / 6 (down).
With c_dfDIJoystick2: wheel = X; pedals report 10000 up → 0 pressed: accelerator Y, brake RZ, clutch SLIDER1;
Z stays 0, SLIDER0 stays 5000. The **first GetDeviceState after opening returns 5000 on every axis**, before the first
real report. EnumObjects' dwOfs did not match the data format (labels came out shifted); GetObjectInfo(DIPH_BYOFFSET)
is the reliable way to name the slots.

**Start-line hold (H-shifter round 4-6):** RealDriver+0x2B4 (byte) = car held at the start; while set, the driver update
forces wanted gear = N every frame (0x0041E37E/0x0041E38F), so the engine can be revved. Cleared at GO by 0x00410430(0),
which calls vtable+0x30 = 0x00407310 (wanted = 1st in modes 1/3, -1 otherwise). Writing another gear during the hold
makes the shift mode flip 3 <-> 4 every frame. **car+0x70** = velocity vector (m/s), its length is the speed
(0x00440F70 = vector length, used at 0x00406516); it has no sign, so it can't tell forward from reverse on its own.

## Audio output mode / cut surround sound (surround chat, 2026-10-16)

Static analysis only (Ghidra + objdump), nothing tested in-game yet. The PC sound library still contains the full
quad / 5.1 output path; only the game-side menu and two call sites limit it to mono/stereo.

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x0083AA50` | `g_AudioMode` (FE option) | 0 Mono, 1 Stereo. Toggle at 0x0049AB80 wraps 0..1 only (`inc edi; cmp edi,1; jle` / `dec edi; jns`). | objdump. |
| `0x0049ABD0` | `FEAudioOptions_DrawAudioMode` | Label "Audio Mode" (0x2881AB87) + value: 0 → "Mono" 0xC50FA35F, 1 → "Stereo" 0x55DA8BF8, else blank. | Decompiled. |
| `0x00486C63` | squash site 1 | Passes `g_AudioMode != 0` to `Snd_SetOutputMode` (`sub eax,0; je; push 1; dec eax` = switch with cases 2/3 compiled out). Then calls `Snd_ApplyOutputMode`. | objdump. |
| `0x0046B2C0` | `AudioSettings_SetAudioMode` | thiscall(mode): stores obj+0x14, calls `Snd_SetOutputMode(mode != 0)` (same squash, sites 0x0046B2E5 / 0x0046B2F6). | Decompiled + objdump. |
| `0x00721681` | `Snd_SetOutputMode` | cdecl(mode 0-3): if supported (cap table) stores to `0x00818084` (or pending `0x008B4028`/`0x008B402C` when running); else returns -5. | Decompiled. |
| `0x0072163D` | `Snd_IsOutputModeSupported` | Checks byte cap table `0x008B3FFC[mode] == 1` (9 bytes copied from `0x008B4710`). | Decompiled. |
| `0x0072183A` | `Snd_ApplyOutputMode` | Re-inits the mixer/output with the stored mode (fails -5 while voices are active). | Decompiled. |
| `0x00724CD0` | `Snd_OutputModeToChannels` | mode 0 → 1 ch, 1 → 2 ch, 2 → 4 ch, 3 → 5 ch + LFE flag (cfg+0x3F channels, +0x40 LFE). | Decompiled. |
| `0x00725DB5` | `SndDS_ProbeChannelCaps` | `DirectSoundCreate`, then tries primary formats with 1, 2, 4, 6 channels; sets cap bytes [0..3] and max channels (0x008B44A5). | Decompiled. |
| `0x00725A85` | `SndDS_SetPrimaryFormat` | (channels, rate): `WAVE_FORMAT_EXTENSIBLE` (0xFFFE), mask 0x33 quad / 0x3F 5.1, PCM for ≤2; primary `SetFormat` (vtbl+0x38) + 0.5 s secondary buffer. | Decompiled. |
| `0x00725F52` | `SndDS_OpenOutput` | Opens the real output with channels + LFE (`0x008B44DF` + `0x008B44E0`). | Decompiled. |
| `0x0072559B` | `Snd_BuildSpeakerPanTable` | 256-step azimuth → per-speaker gain table for N speakers (pairwise panning between speaker angles at `0x008B46D4`). | Decompiled. |
| `0x008B44DF` / `0x008B44E0` | mixer channels / LFE flag | Clamped to the probed min/max in 0x00724AB9. | Decompiled. |

Unused texts in English.bin (no reference in the exe): "Dolby Surround" 0x666128E0, "Dolby Pro Logic II" 0xF6FAFF24,
"Audio Mode:" 0x76A191B3 (console leftovers). The `dsound.dll` (34 KB, 2013) in the Mac game folder is an old ASI loader
that forwards to the system DirectSound (exports DirectSound*, loads `scripts\*.asi`).
Unknown: whether the game-level 3D sound code (SFXCTL_3D*Pos, MixMaps\MAPOUTPUT*.mxb) gives full 360° azimuths or
only front/stereo positions. Only an in-game test can tell.

**More (surround chat, building NFSU2Surround v0.1.0, 2026-10-16):**
- Boot order: audio init `0x0047AAD0` (region loader 0x004853C0) calls `AudioSettings_SetAudioMode(1)` at 0x0047ABA4
  before the sound system runs (so `Snd_SetOutputMode` stores straight to 0x00818084), then `0x007217A7` opens the
  output with that mode. Every world load (region loader `0x0057DC60` step 3 → 0x00486EA0 → 0x00486C10) sets
  `g_AudioMode != 0` again and calls `Snd_ApplyOutputMode` (re-init; returns -5 while voices play).
- Speaker angles (0x007258E6, by channels+LFE `0x008B46E8`): 2 ch 90°/270°; 4 ch 45/135/225/315°; 6 ch 0° (centre)
  + 45/135/225/315°. Angles run clockwise from the front (45 = front right, 315 = front left).
- Output channel remap in the interleaver `0x00729574` (used when output channels > 1, flag `0x008B47FA`):
  2 ch [1,0]; 4 ch [3,0,2,1]; 6 ch [4,1,0,5,3,2] (output slot ← mixer speaker). Gives Windows order L R / FL FR RL RR /
  FL FR C LFE RL RR from the angle order above. Buffer writer `0x007260F7` → `0x00725C8E` → `0x00729574`.
- `0x008B47F9` = output channels incl. LFE (from SndDS_OpenOutput's config), `0x008B44C6` = sample rate.

**Voices and pan (surround chat, NFSU2Surround v0.2.0, 2026-10-16):** voice array `[0x008B466C]` + n*0x90 (one voice
per source channel): +0x1C u16 direction (0 ahead, clockwise, 0x2000 = front right; mono only: azimuth + voice+0x50 +
+0x4C), +0x1E spread (-0x4000..0x3FFF, stored only), +0x48 float volume, +0x4E LFE-only flag, +0x5F volume byte 0..127.
| Address | Name | Purpose |
|---|---|---|
| `0x00724FC5` | `Voice_UpdatePan` | LFE-only voice → gains 0 + LFE 1.0; else `Snd_AngleToGains(voice+0x1C, gains)` at **0x00725012** (only CALL). Then Voice_UpdateGains. |
| `0x00725878` | `Snd_AngleToGains` | cdecl(u16 angle, float* gains): gains[i] = panTable[i][angle>>8] / 255; LFE gain 0 in 5.1. |
| `0x00724F5D` | `Voice_UpdateGains` | volume = (char)v[0x5F] * v[0x48] / 127; `Snd_SetVoiceChannelGain(voice, ch, volume*gains[ch])` at **0x00724FAC** (only CALL), for ch < `0x008B46E8`. Called on every volume/pan change (9 callers). |
| `0x0072905D` | `Snd_SetVoiceChannelGain` | cdecl(voice, ch, float): stores into the mixer's per-voice channel gains (`[0x008B4A00]`). |
| `0x00724093` | `Snd_SetAzimuth` | (handle, azimuth, spread): mono voices only; angle = azimuth + offsets; forced to 0 in mono output. Game callers 0x00473E60, 0x00474030 (angle from vtable+0x3C). |
| `0x007221A7` | `Snd_StartVoices` | Sets each channel's angle at start: from the sound's pan, or a default table `0x007C692A` [out layout][source channels][ch] (5.1/quad: mono → 0° centre, stereo → 315°/45° front L/R, 4-ch → FL FR RL RR, 6-ch → FL C FR RL RR C; stereo output: 270°/90°). |
Gear on the player's car: `[[0x008900B4]+4]+0x434` (same car as driver+0x2A8, confirmed by H-shifter round 6 log).

## Force feedback effects and steering input (wheel chat, 2026-10-16)

Static analysis only (Ghidra + objdump). Nothing tested in-game yet. Function names below were set in Ghidra.

### Every force feedback effect the game uses (DirectInput, "EZ Wheel Wrapper v4.26")

| Address | Name | Effect / when |
|---|---|---|
| 0x005C9380 | FF_SetConstantForce | Constant force (GUID 13541C20). Via FF_SetConstantForceAtAngle 0x005C9550 (angle 270). **Steering force**: front tire lateral forces / Steering+0xC ("Road Feedback" option, 2..20) × 10000, clamped ±10000, 0 below 1 m/s. Only sent to non-gamepad devices; resent only when the value changes. |
| 0x005C9150 | FF_SetSpring | thiscall(port, offset, saturation, coefficient), Spring (GUID 13541C27), condition slot 0, deadband 0. **Only** used: 100% when the steering curve output is exactly ±1.0 = full lock (0x00406503, works like a soft lock), 70.6% in drags / FF off (0x004069D6), 70% in menus (Input_PollDevices 0x005CFD60). In normal driving the game **stops the spring every frame** (FF_StopSpring 0x005BFE00, CALL at **0x0040650B** `53 E8 F0 98 1B 00`). |
| 0x005C9590 | FF_SetDamper | Damper (GUID 13541C28), slot 1. Coefficient (1 − min(1, speed / 13 m/s)) × 7058.8: heavy when parked, gone above 47 km/h. |
| 0x005C9D90 | FF_SetNegativeDamper | Damper with coefficient −10000 (loose wheel), slot 2, on surface 0xD above 23 m/s. |
| 0x005C9760 | FF_PlayJolt | Square wave (GUID 13541C22), 150 ms, period 75 ms, periodic slot 0, max once per 150 ms. Speed drop > 4 m/s in one frame → min(1, (Δ−4)/15) × 10000; suspension hit (car+0x370..+0x37C sum > 0.05) → min(1, sum/4) × 6667. |
| 0x005C9FD0 | FF_SetRoadRumble | Periodic slot 3: wave per surface (sine/square/triangle/saw up/saw down), magnitude = surface value/2 × speed × 39.2, period ≤ 150 ms. Surfaces car+0x394/+0x398 (types 8–11, 0xF–0x11). |
| 0x005CDA10 / 0x005CDBB0 | FF_PauseAll / resume | All forces paused while car+0x390 and car+0x391 are both 0 (probably airborne). |
| 0x005C9120 | FF gain | DIPROP_FFGAIN from "Overall Force Strength" (options+0x24, 0..1) × 255. |
| 0x005CD3D7 | autocenter off | DIPROP_AUTOCENTER (9) is switched off when a wheel (DI8DEVTYPE_DRIVING) is opened. |

Effect GUID table: 0x007BFC80 Damper, 0x007BFC90 Spring, 0x007BFCA0 SawtoothDown, 0x007BFCB0 SawtoothUp,
0x007BFCC0 Triangle, 0x007BFCD0 Sine, 0x007BFCE0 Square, 0x007BFCF0 ConstantForce. FF object = [0x00870760];
per-port DirectInput device [0x0086FFB4 + port*4]; device type flags 0x0086E5BC (wheel), 0x0086F8A8 (gamepad),
0x0086FF88 (joystick); FF axes count [0x0086FF80 + port*4].

### Steering input path (wheel)

1. **DirectInput**: EnumObjects callback 0x005C8660 sets DIPROP_RANGE −10000..10000 on every axis (no deadzone set).
   State polled into [0x00870760] + port*0x110 (DIJOYSTATE2).
2. **8-bit quantisation**: Input_ReadJoystickToBytes 0x005C1840, at 0x005C1FE0: byte = 0x80 − trunc(axis × −0.0127)
   × dir (device record 0x00874C40 + i*0x370: +0x344 axis offset, +0x354 direction, +0x364 steering bound,
   +0x230 port). Only ±127 steps; |axis| < 79 all give 0 (centre step twice as wide). Replays record these bytes.
3. JOY_EVENT 0x8F/0x90 handler 0x00407750 (not a Ghidra function): RealDriver+0x2C8 = (byte − 128) × −1/128
   (255 → −1.0 special case).
4. RealDriver update 0x0041E355: input = +0x2C8 (0 if +0x2FB), CALL **0x0041E372** → Steering_Update 0x0041DFC0
   (thiscall on Steering = RealDriver+0x220, args input, dt, port) → result word → RealDriver+0x1E0.
5. Steering_Update: [0x0081CB8C] (never written → dead dev path Steering_ComputeDevUnused 0x00406A40);
   Input_IsFFWheel(port) 0x005C1000 → **Steering_ComputeWheel 0x004061C0**; else Steering_ComputePad 0x00416BB0.
   Input_IsFFWheel = cdecl bool(port): device [0x0086FFB4+port*4] non-null and wheel flag byte [0x0086E5BC+port]
   (disassembled 2026-10-18). Wheel's steering hook calls it too (0.2.8) and leaves pad/keyboard input alone.
   Then RealDriver_UpdateForceFeedback(curve output, port).
6. Steering_ComputeWheel:
   - x = clamp(input, ±Steering+8) / Steering+8. Steering+8 = hidden **"Wheel Range"** option (0.1..1.0, step 0.1;
     the slider code exists but Chris confirmed the PC menu does **not** show it; runtime value **1.0** on Chris's
     profile, Wheel round 2). Per-car factor [car+0x20]+0x270 = 1.1 on Chris's car.
   - **Response curve** Table_Lookup(0x007E9E88), 21 points over −1..1, data 0x007E92D8 (only user):
     0.1→0.057, 0.2→0.111, 0.3→0.161, 0.4→0.220, 0.5→0.288, 0.6→0.388, 0.7→0.483, 0.8→0.615, 0.9→0.800, 1→1.
   - **Speed-sensitive lock** (skipped when Steering+4 = options+0x19 byte is set): maxAngle = table 0x007E9EEC
     over car+0x42c (speed in **m/s**, confirmed Wheel round 2: car+0x42C / |car+0x70| = 1.00): 45° at 0, 15° @17.8, 11° @35.6, 8° @53.3,
     7° from 71. × (1 + table 0x007E9F14(speed) × (1 − 0.5 × (drv+0x20C + 1 − 0.5 × (drv+0x210 + drv+0x214)))),
     drv = [car+0x30]: **+0x20C throttle, +0x210 handbrake, +0x214 brake** (identified 2026-10-17: setters
     0x005EB910 / 0x005EB900 / 0x005EB8F0, filled in 0x00407A30 from RealDriver +0x2D4 throttle / +0x2EC e-brake /
     +0x2DC brake; the car physics 0x005A2A60 copies them next to the steering angle). So this is a **pedal assist**:
     ×1 at full throttle, ×(1 + 0.5·G) coasting, up to ×(1 + G) braking + handbrake, G = table 0x007E9F14 (1 at rest →
     0.32 at speed). The 1.0 multiplier is read by FMUL [0x00783AE0] at **0x00406294** (shared constant; WE 0.2.2
     repoints the operand at 0x00406296). Then × table 0x007E9F00(|Steering+0x20|) (1 at 0).
   - **Countersteer assist**: slide flag Steering+0x10 (on: |slip| > 15° and speed ≥ 13.4 m/s; off: < 5°).
     Countersteering while sliding (or |slip| > maxAngle) → maxAngle = |slip|, max 45°.
   - angle = maxAngle × [car+0x20]+0x270 (per-car factor) × curve(x), clamped ±45°, → Steering+0x7C (65536 = 360°).
   - Returns curve(x); exactly ±1.0 (at or past the Wheel Range end) → 100% spring.
7. Wheel options copied at input registration (0x0041E23E): player options 0x0083AA64 + player*0x28:
   +0x19 byte → Steering+4, +0x1C Wheel Range → Steering+8, +0x20 Road Feedback → Steering+0xC, +0x24 Overall
   Force Strength → gain. Option sliders: 0x0049B3C0 (range, 0.1..1), 0x0049B4D0 (road feedback, shown as
   1 − (v−2)/18), 0x0049B5C0 (overall). Labels: hashes 0x56FEB0D2 / 0x1CC4B396 / 0x28F42AE8.

Steering object (RealDriver+0x220): +0 car, +4 skip-speed-lock flag, +8 range, +0xC FF divisor (ctor 0x00405C60:
range 1.0, divisor 7.0, +0x80 = 45.0), +0x10 slide flag, +0x14 two filters, +0x20 unknown float (appears to stay 0
for the player), +0x7C angle word, +0x84 last curve output.

## DVD and magazine covers ("showcases") (covers research, 2026-10-16)

Static analysis + GLOBALB data + Chris's 30 saved cover files. **Nothing tested in-game yet.** Full table with texts:
`docs/showcases.txt` (`tools/dump_showcases.py`). Record layouts: structs.md → "Showcases". All names below set in Ghidra.

### Unlocking

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x005331C0` | `Showcase_CheckDVDUnlocks` | ECX = g_Career. DVDs (type 2/4) of the **current stage**: if `Career_GetCurrentCarVisualRating` ≥ showcase+0x3C → status entry discovered = 2 + Rachel SMS (`SMS_QueueShowcaseOffer`). | Decompiled. |
| `0x005412D0` | `Career_OnWorldLoad` | Only caller of the DVD check (also DDAY_EVENT_B bookkeeping). Called from the world loader `0x0060A680` (`CALL` at `0x0060A78C`) when in career mode. | Decompiled + call site in disassembly. |
| `0x00528410` | `Career_GetCurrentCarVisualRating` | Visual rating (stars, float) of the car in g_Career+0x8740 (via a RideInfo, `0x005234C0`). | Decompiled. |
| `0x00527970` | `WorldChallenge_CheckUnlock` | (stageRecord, challenge) → adds the challenge to the 32-entry active world-challenge list at g_Career+0x7148 (ECX; corrected 2026-10-16, see "Unique performance parts") when its count is met. **Magazines are world challenges of type 4.** Only called by `Career_RebuildActiveEvents` for the current stage, after the career-finished early return. | Decompiled. |
| `0x00533BE0` | `WorldChallenge_Start` | Starts a timed challenge: status entry status = 2 for the challenge, the destination showcase (challenge+0xC) gets discovered = 2, markers rebuilt. Callers `0x00541120` (from the showcase dialog) and `0x00533CF0`. | Decompiled. |
| `0x00513490` | `Stage_AreRequiredShowcasesDone` | Stage complete needs every showcase of the stage with RequiredToComplete (+0x35) done: **all 10 DVDs are required, magazines are not.** | Decompiled. |
| `0x005253F0` | `SMS_QueueShowcaseOffer` | Queues Rachel's message for a showcase (`0x005111F0`, type 0xE magazine / 0xD DVD). | Decompiled. |
| `0x005337D0` / `0x00533750` | (unnamed) | Unlock paths for showcase+0x34 = 2 (after a race key) and 0/1. **Dead in stock data** (+0x34 is 3 or 4). | Decompiled. |

### Taking the photo

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x00498F00` | `ShowcaseDialog_Notify` | The dialog when you drive into a showcase marker: "take photo" (msg 0xB8A7C6CE → `ShowcaseDialog_TakePhoto`) or start the magazine's timed challenge (0xD72F002A → `0x00541120`). | Decompiled. |
| `0x00498E70` | `ShowcaseDialog_TakePhoto` | Auto-photo showcases (type 3/4, `Showcase_IsAutoPhoto 0x005011A0`): `Showcase_MarkDone`, cash, `UI_MagazineReward.fng`. Player-photo ones (1/2): opens `UI_ShowCase_Preview.fng` (you pose the camera). | Decompiled. |
| `0x004CEDC0` | `ShowcasePhoto_Notify` | Photo screen (vtable slot set at `0x004CF58B`). Overlays `MagazineMast` / `MagazineExtra` textures (0x00836508 / 0x00836510). PAD_ACCEPT (0x406415E3, = take the photo) → `Showcase_MarkDone` + "save picture?" dialog; yes → after 5 frames `ScreenCap_Create`. | Decompiled. |
| `0x00533680` | `Showcase_MarkDone` | Showcase key == this+8 → status entry discovered = 2, **status = 1 (done)**; moves the player to the destination trigger; rebuilds markers. | Decompiled. |
| `0x005CE510` | `ScreenCap_Create` | 512×512 32-bit capture texture "ScreenCap" (0x100000 bytes) + optional 64×48 thumbnail. | Decompiled. |

### Saved pictures and the gallery

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x00510AF0` | `Showcase_MakePictureKey` | 8-char key: 7 chars of the profile name (padded `_`) + **one base-36 char = showcase index** → stock scheme handles at most 36 showcases (30 used). | Decompiled. |
| `0x00523CB0` | `Showcase_PictureKeyToName` | Key char 7 → index → `Showcase_FormatPictureName`. Any other char → index 0. | Disassembled. |
| `0x00510B80` | `Showcase_FormatPictureName` | "Magazine %d" / "DVD %d" (1-based within its kind). Save file = `<profile> Magazine N` in the save folder. | Decompiled + Chris's files. |
| `0x004D9C60` / `0x004A9750` | `MemCard_SaveShowcasePicture` / `maybe_MemCard_LoadShowcasePicture` | Memory-card manager ops 0xB0 / 0xC0 (dispatcher `0x004F3570`, mode in 0x00836514). Save = data type 0x10D. | Decompiled. |
| `0x006FD2F6` | `MC02_LoadFileStep` | Opens `<dir>\<name>\<name>`, checks magic, size and all three checksums. | Decompiled. |
| `0x006FE173` | `MC02_Checksum` | Save-file checksum (CRC32-style, MSB first, table `0x008145E0`, init = ~first 4 bytes). **Filename not included.** | Reimplemented in Python; matches profile + cover files. |
| `0x004AFB70` | `CareerMenu_AddCoverEntries` | Career menu entries "Magazines" / "DVDs" (CRWShowMagazines / CRWShowDVDs), greyed out while 0 done (`Showcase_CountDoneOfType 0x00510D50`, 1 = magazines, 2 = DVDs). | Decompiled. |
| `0x00523F60` | `CoverGallery_BuildList` | One gallery item per done showcase of the chosen kind (0x00838578: 1 = magazines); item+0x48 = showcase index; title = `<desc>_TITLE`. | Decompiled. |
| `0x0054B150` | `CoverGallery_SetTexts` | Gallery title/body from the showcase's desc hash + "_TITLE" / "_BODY". | Decompiled. |
| `0x00499EC0` / `0x00510C90` | (unnamed) | Load `FRONTEND\MAGAZINES_SHOWCASE.BIN` / `MAGAZINES_FRONTEND.BIN`; mastheads in `MAGAZINES_STREAMING_LOW/HIGH.bin`. | Decompiled. |
| `0x004FFF80` / `0x004FFFC0` | `Showcase_FindByKey` / `Showcase_FindByTrigger` | Lookups in g_GShowcases. | Decompiled. |
| `0x00501150` | `Showcase_IsOffered` | discovered == 2 and status != 1. `Career_RebuildMapMarkers` (loop at `0x005333CE`) shows the showcase marker when true, **with no career-finished check** → an ASI can re-offer covers post-game. | Decompiled. |

### Cover gallery screens (covers chat, 2026-10-16)

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x0054B210` | (gallery list Notify, vtable `0x0079CE04` [2]) | UI_MagazineSelect: LEFT/RIGHT scroll, BACK leaves; each frame the preview image `0x0079C724` = stock cover art (texture hash = showcase+0x28). Not a Ghidra function. | Disassembled. |
| `0x0053DE60` | (gallery item Notify, vtable `0x0079B840`) | Item ACCEPT (0x0C407210): `0x00837FD8` = showcase+0x28, opens UI_MagazineView.fng. | Disassembled. |
| `0x00565B50` | (cover viewer Notify, vtable `0x0079ACE8` [2] at `0x0079ACF0`) | thiscall(screen, msg, a, b, c), RET 0x10. 0x35F8620B: unless screen+0x50, makes the 512×512 ScreenCap (`0x004889F0`) and starts loading the picture (memcard op 2 via `0x004A95B0`, key from `0x00523C70`); 0x42317A19 = loaded → `ScreenCap_UploadToTexture`, screen+0x51 = 1; 0x2B46FE08 = failed; 0xC98356BA every frame → FE image "MagImage" shows the texture. **Hooked by NFSU2Covers.** | Disassembled; code checks in `src/covers/covers.cpp`. |
| `0x00832DAC` | (global ScreenCap of the viewer) | +0x08 picture buffer (0x100000 = part B of the file), +0x0C texture. | Disassembled. |
| `0x00488B10` | `ScreenCap_UploadToTexture` | thiscall(screenCap), plain RET: locks the texture and copies the buffer into it. | Decompiled + disassembled. |
| `0x00837FD8` | (current cover desc hash) | Set by the gallery item and by `0x00541930` (photo shoot). | All 4 writers/readers found. |
| `0x008384C4` | (FE manager) | `[+8]` = the FEng object (pads at +0x8, count at +0xD4, 0x88 bytes each, +4 = bits this frame). | FEng_Update callers `0x00558EE0`. |
| `0x004A9C40` | (save folder setup) | Save folder = SHGetFolderPathA(CSIDL_LOCAL_APPDATA) + `\` + `*(char**)0x007F66A0` ("NFS Underground 2"). | Decompiled. |

## Unique performance parts (unique parts research, 2026-10-16)

Static analysis + GLOBALB data + Chris's saves (`tools/dump_uniques.py`). **Nothing tested in-game yet.** Names set in
Ghidra. Record layouts: structs.md → "Unique performance parts".

**Summary.** Unique performance parts exist only as rewards of 3 timed world challenges (type 2), one per stage 3-5.
Each offers 3 parts; the player keeps **one** (or declines it forever), and it goes on the **car driven at that moment**
only (flag in the car record). So a stock career gives at most **3 of the 9** unique parts, spread over whichever cars
were driven. They are never sold in a shop, never unlock in Quick Race, and nothing re-offers them after the career
(or even after the stage ends). Confirmed in Chris's finished save: 240SX Engine, Eclipse Turbo, Lancer Evo Nitrous.

| Challenge | Stage | Unlock | Time | Offers (part id → category) |
|---|---|---|---|---|
| `STAGE_3_PERF_1` | 3 | 3 outruns won in stage 3 | 150 s | 1 Engine, 5 Transmission, 7 Tires |
| `STAGE_4_PERF_2` | 4 | 9 outruns won in stage 4 | 90 s | 2 ECU, 9 Turbo, 8 Brakes |
| `STAGE_5_PERF_3` | 5 | 6 outruns won in stage 5 | 110 s | 4 Nitrous, 6 Weight Reduction, 3 Suspension |

All three start at `TRIGGER_START_STAGE_n_PERF_n` and end at **CC_PERF_SHOP** (shop key `0x4E79CD0A`, City Core,
available from stage 1). The 5 visual ones (type 1) work the same way (destination a body/graphics shop, the part is
installed and charged).

### Flow

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x00533A90` | `WorldChallenge_CheckOutrunSMS` | After an outrun win (arg != 0): challenges with method 2 of the **current stage** whose count == stage record +0xDC (outruns won) → `WorldChallenge_QueueUnlockSMS`. | Decompiled. |
| `0x00533B40` | `WorldChallenge_CheckRegRaceSMS` | Same for method 0 (regular races won, `FUN_005004D0`); showcases also get Rachel's offer. | Decompiled. |
| `0x005338C0` | `WorldChallenge_QueueUnlockSMS` | Challenge SMS (+8 hash); type 2 text = `SMS_PERF_GENERIC_%d_*` (3 variants), type 1 `SMS_VISUAL_GENERIC_%d_*`. | Decompiled. |
| `0x00527970` | `WorldChallenge_CheckUnlock` | Puts the challenge on the map: **ECX = g_Career+0x7148** (32-entry active world-challenge list), arg 1 = current stage record, arg 2 = challenge. Skips status 1. Called only from `Career_RebuildActiveEvents`, current stage only, after the career-finished early return. | Disassembly at `0x00527E79` (`LEA ECX,[EBX+0x7148]`). |
| `0x00533310` | `Career_RebuildMapMarkers` | Draws a marker for every entry of g_Career+0x7148 whose start trigger exists (loop at `0x005335B0`), **no stage check**. | Decompiled. |
| `0x00527800` | `WorldTrigger_Classify` | Trigger → race / shop (`Shop_FindByTrigger`) / showcase / world challenge (`WorldChallenge_FindByTrigger 0x00512A10`, by trigger-name hash). | Decompiled. |
| `0x00533BE0` | `WorldChallenge_Start` | ECX = challenge state **g_Career+0xC00** (`0x0085B940`): +8 challenge, +4 timer = challenge+0x10 seconds, +0x11C state = 1, status entry status = 2. | Decompiled (covers chat). |
| `0x0054CAF0` | `Career_OnEnterShop` | (renamed from `maybe_Career_OnShowcaseDone`.) Driving into a shop: shop status 5, g_Career+0x8734 = shop; if a challenge runs and its destination == shop key (+0x38): state +0x11C = 4 → `WorldChallenge_BuildUniquePartList`. **No stage check.** | Decompiled. |
| `0x00513910` | `Career_GetCurrentShop` | State 4 → the hidden shop `THE_UNIQUES_SHOP` (type 6, stage 10 = never on the map), else +0x8734. `0x00528560` picks the screen: type 6 → `UI_ChooseUniquePart.fng` (`0x0050E2E0`). | Decompiled. |
| `0x00533D20` | `WorldChallenge_BuildUniquePartList` | List (state+0xC, count +0x110): type 2 → state+0x10C = 1, each part byte → `UniquePerf_IdToCategory`, **offered only if the current car's slot+0x2F4+cat×4 == 0**. | Decompiled. |
| `0x00502240` | `UniquePerf_IdToCategory` | id 1-10 → category 3, 7, 5, 2, 1, 0, 8, 6, 4, 9. | Decompiled. |
| `0x0050E790` | `Perf_GetCategoryNameHash` | Category → name text: 0 Weight Reduction, 1 Transmission, 2 Nitrous Oxide, 3 Engine, 4 Turbo, 5 Suspension, 6 Brakes, 7 ECU, 8 Tires. | Decompiled + English.bin. |
| `0x00554B20` / `0x00553200` | `UniquePartScreen_Create` / `_Construct` | 0x154-byte screen, vtable `0x0079CAB0` ("UniquesBrowser"). | Decompiled. |
| `0x00561720` | `UniquePartScreen_Notify` | ACCEPT `0xD05FC3A3`: perf → `FECarSession_GrantUniquePerfPart(entry+0x48)` + Rachel SMS type 9 (`0x00525610`); visual → install + charge (`0x0055C230`, `0x005289F0`). Then `WorldChallenge_Complete`. BACK `0x911AB364` → "If you don't choose a unique you won't be able to choose it ever again" (`0x20D0D219`); yes → also `WorldChallenge_Complete` (no part). | Decompiled + disassembled (ECX at the calls). |
| `0x00521430` | `FECarSession_GrantUniquePerfPart` | ECX = g_FECarSession `0x00838980`: RideInfo (+0x940) +0x318+cat×4 = 1, refresh, `CarRecord_FromRideInfo` → career slot +0x2F4+cat×4 = 1. **The only writer of these flags** (grep of all `+0x318`/`+0xC58` stores). | Decompiled + full-text grep. |
| `0x005419C0` | `WorldChallenge_Complete` | Challenge status = 1, state cleared, markers rebuilt. Status 1 = never offered again. | Decompiled. |
| `0x005997E0` | (perf → physics) | Adds the unique tuning values to the car's physics when a category flag (RideInfo+0x318 block) is set. | Decompiled (not mapped in detail). |
| `0x004FFE20` / `0x004FFE60` | `Shop_FindByKey` / `Shop_FindByTrigger` | Lookups in the GCareer shop table (0x34A12, 0xA0 bytes, `0x008383F0`, 24). | Decompiled. |

### Not unique (for contrast)

- Normal perf packages (Street/Pro/Extreme = levels 1-3, `UI_ChoosePerformancePackage`, builder `0x00556E60`) unlock by
  career stage / shop (`0x0052A110` → `0x00515630`, `0x00579D90`); there is no "Unique" level in any shop, career or
  Quick Race. Parts bought on a car: bit array at slot +0x590 (`PerfShop_IsPartOwned 0x00530350`).

### What an Endless Career version could do (ideas, nothing built)

- The challenge chain itself has no stage checks after the unlock step: putting a stock perf challenge back into
  g_Career+0x7148 (and its status entry back to 0/2) should give the marker, timer, CC_PERF_SHOP and the pick screen
  post-game. Offered parts are filtered per car, so the same 3 challenges can be reused for every car.
- Or skip the drive: grant directly (slot +0x2F4+cat×4 = 1 on a car the player owns), e.g. as a race reward. Needs the
  car's RideInfo/physics refreshed if it is the current car (the game does `FE_ShowGarageCar` + `CarRecord_FromRideInfo`).
- Open questions: does re-installing stock parts / buying a package in that category clear the unique flag? Does
  `Career_RebuildActiveEvents`' `+0xC08 != 0` early return (challenge running) interfere with Endless Career's own
  event list? Do post-game outruns still count in stage record 6 +0xDC?

### Unique parts after the career (BayviewForever 0.2.0 UniqueParts, 2026-10-18, static only, not tested yet)

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x00541320` | `Career_OnOutrunFinished` | (renamed from FUN_00541320) thiscall(g_Career, char won), RET 4 (`0x0054141D`). Only if `0x00850078` == 1 (career mode): rep ± stage record +0x100→+2, **outruns won +1 (stage record +0xDC)**, `WorldChallenge_CheckOutrunSMS`, `Career_RebuildMapMarkers`. Only CALL `0x005F84D7` (`MOV ECX,g_Career; SETE AL` = WORD [..+0x10] == 6). **Hooked by UniqueParts** (counts post-game outrun wins per car model). | Decompiled + disassembled. |
| `0x00861E88` | g_Career+0x7148 | 32 active world challenges (pointers into g_WorldChallenges). Zeroed by every `Career_RebuildActiveEvents`; counted by `FUN_00500910` (non-null entries); markers drawn from it by `Career_RebuildMapMarkers` (loop `0x005335B0`, `CMP EBP,0x20`). No status or stage check anywhere after this list, so **a challenge pointer written here post-game gets its start marker**, and the rest of the chain (WorldTrigger_Classify by trigger hash → ShowcaseDialog_Notify `0x00498FAC` → `0x00541120` → WorldChallenge_Start) runs as in the career. | Decompiled. |
| `0x0085B940` | g_Career+0xC00 | Running challenge: +8 = challenge (= g_Career+0xC08: non-zero → Career_RebuildActiveEvents returns early, so races and the list are empty during the run; Endless Career's AfterRebuild returns too), +0x11C step. | Decompiled (`0x00533BE0`, `0x005419C0`). |
| `0x00533CF0` | (challenge step) | +0x11C == 7 (failed/cancelled): +8 = 0, markers rebuilt (status stays 2 → can be re-offered); == 8 restart. | Decompiled. |
| `0x0056178E` / `0x00561870` | CALLs to `WorldChallenge_Complete` | In UniquePartScreen_Notify: declined (after the "never again" dialog) / part picked. ECX = `0x0085B940` (`MOV` at `0x00561782` / `0x0056186B`). Complete itself rebuilds the markers, so the hook uses up the earned part **before** calling it. **Hooked by UniqueParts.** | Disassembled. |
| `0x00561826` | CALL to `FECarSession_GrantUniquePerfPart` | ECX = `0x00838980` (`0x00561821`), arg = category. **Hooked by UniqueParts** (log only). | Disassembled. |
| `0x005340DD` | (part list check) | `MOV ECX,[ESI+EAX*4+0x2F4]`, ESI = current car's slot (CarDB_FindCareerSlotByHash): parts already on the car are not offered. | Disassembled. |

How UniqueParts uses them: post-game outrun wins per model (state `<game>\endless-career\<profile>\unique-parts.txt`);
every `OutrunsPerPart` wins (ini, default 10) one part is earned (if the car still lacks more parts than are waiting);
`bf::UniquePartsAfterRebuild()` (called by Endless Career's `Hook_Rebuild`) writes the perf challenge with the most parts
the current car lacks into the first free entry of g_Career+0x7148 while one is waiting for the car being driven and no
challenge runs. Rachel's text = `WorldChallenge_QueueUnlockSMS` (stock SMS_PERF_GENERIC_n). The 3 challenges together
offer all 9 categories (0-8), so a car can collect every unique part.

### Challenge finish paths, RaceToShop and magazine challenges (BayviewForever 0.2.0, 2026-10-18, static only)

| Address | Name | Purpose | How verified |
|---|---|---|---|
| g_Career+0xD1C | challenge step | = challenge state (+0xC00) +0x11C: 1 running, **3 arrived at a showcase**, 4 arrived at the destination shop, 5 completing, 7 failed, 8 restart. | Disassembly (`0x00532BF8`, `0x0054CC80`). |
| `0x00541960` | `WorldChallenge_CompleteIfArrived` | (renamed) step 3 → challenge status 1, cleared, markers rebuilt. Called when the cover photo is taken, right before Showcase_MarkDone (auto `0x00498EB5`, posed `0x004CE994`). This is how magazine challenges finish. | Decompiled + xrefs. |
| `0x005139C0` / `0x00513B30` | (leave free roam) | Mid-challenge exit: +0xC08 = 0, step 0, the destination's discovered status (+4) = 0. | Decompiled. |
| `0x00515060` | (running challenge destination) | While +0xC08 != 0, Career_RebuildMapMarkers draws only the destination (shop by key, else showcase by key at its +0x2C trigger), **without** Showcase_IsOffered. | Decompiled. |
| `0x0054CAF0` | `Career_OnEnterShop` | thiscall(g_Career, shop), RET 4 (`0x0054CC93`); only CALL `0x00499497`. Calls `FUN_00533750` (re-discovers type-0 showcases + rebuilds markers) and `FUN_00529860` (shop SMS) before the challenge check at its end. **Hooked by UniqueParts** (RaceToShop = 0). | Decompiled + disassembled. |

UniqueParts `[UniqueParts] RaceToShop = 0`: no start marker; on entering the shop the challenge picked for the car
(PickChallenge) is written into the state (+0 = 0, +4 = its time, +8 = it, +0x11C = 1) only if the shop key (+0x38) ==
its destination (+0xC) — the end of Career_OnEnterShop then sets step 4 and builds the list, so the pick screen opens.
No Rachel text (her SMS_PERF_GENERIC texts say "go to the star").

Covers `[Covers.Earn] MagazineChallenge = 1`: `bf::CoversAfterRebuild()` (from Endless Career's Hook_Rebuild) adds the
stock type-4 challenge of each magazine waiting for the car being driven to g_Career+0x7148; `Hook_IsOffered` then
returns false for those magazines (no direct shoot marker), and true for the running challenge's destination. Finish =
the stock `WorldChallenge_CompleteIfArrived` + Showcase_MarkDone (Covers' hook archives and marks it shot). Timer out →
step 7 → cleared → the start marker comes back on the next rebuild. DVDs have no challenge (straight to the shoot).

### Immersive Travel: pause menu garage jump and the free-roam start spot (BayviewForever 0.2.0, 2026-10-18, static only)

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x004C2900` | (UI_Pause item builder) | Builds the pause menu's 0x48-byte option objects (vtable [1] = action). Career only: "Go To Garage" (label 0xB8038AC0, help 0x453890E1 "Jump to Garage?", vtable `0x00795518`). | Decompiled. |
| `0x004C2730` | (Go To Garage action) | On 0x0C407210: `FE_ShowDialogFormatted(pkg, "UI_InGameDialog.fng", 0x639 OK, 0x541389E5, Yes 0x43DA9FD0, No 0xB4623F67, No, 1, 0xC9BA9E3E "Jump to Garage?")` at **CALL `0x004C2765`** (cdecl, `ADD ESP,0x2C` after the next call), then `FUN_0055DDA0(0x96DDA9E6, pkg)`. **Hooked by ImmersiveTravel.** | Disassembled. |
| `0x004C2170` | (UI_Pause Notify) | Yes 0x43DA9FD0 → `FUN_004A7C70` (go to the garage menus); No 0xB4623F67 → undim + help bar. | Decompiled. |
| `0x004AF3A0` | (garage menu Notify) | Back to free roam: `0x00863484` = 1 → `Career_StartFreeRoam`. So the car always restarts where it was saved. | Decompiled. |
| `0x005404A0` | `Career_StartFreeRoam` | (renamed) spawn struct; if g_Career+0x8744 (`0x00863484`) the car starts at +0x8750 x, +0x8754 y, +0x8758 z (floats, +0x875C = 0) facing +0x8760 (u16 = car+0x40C). | Decompiled. |
| `0x00500ABC`, `0x0051395E`, `0x005139C3`, `0x00513B4C`, `0x005411D6`, `0x0054CA0C`, `0x0054CBFC` | (spot writers, instruction addresses) | Copy the player car's +0x60/+0x64/+0x68 and +0x40C into the spot, flag 1 (leaving free roam, entering a shop = Career_OnEnterShop, ...). `maybe_Career_Reset` clears the flag. | Grep of `+0x8744` writers. |
| `0x00529230` | `Career_LoadFromBuffer` | thiscall(g_Career, buffer, size) → next buffer, RET 8; reads the spot at `0x00529312..0x00529359`. Only CALL `0x0053450F`. Save layout: b873C b873D d8740 **b8744 f8750 f8754 f8758 f875C u16 8760** d8764 (checked against Chris's saves: e.g. 914.01 −1213.14 13.65, heading 65216). **Hooked by ImmersiveTravel.** | Disassembled + saves. |
| `0x00499497` | CALL Career_OnEnterShop | Shop = the engaged marker's shop (`0x0086344C`); garage = CRIB_1 (type 0, key 0x55446B8F, trigger 0xF98D0C92 at 654.17 −101.49, z 15.45, direction −0.756 0.654). **Chained** by UniqueParts and ImmersiveTravel (`nfsu2::ChainCall`). | Disassembled + PathsFreeRoam.bin. |

World trigger records (PathsFreeRoam.bin 0x3414A) also hold +0x0C/+0x10 a direction vector and **+0x14 the height** (7-61 m across
markers). ImmersiveTravel doesn't convert those into a spawn (the u16 heading encoding isn't mapped); it keeps the spot the
game saves on entering the garage (`<game>\BayviewForever-garage.txt`) and writes it into g_Career+0x8744.. after every
profile load (only with a career car). Built-in spot: to be filled in from a test round.

### Sunday round 2 findings (BayviewForever 0.2.1, 2026-10-18)

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x005014F0` | (SMS inbox save) | Writes all **256** inbox entries (message name hash + 4 bytes + 3 dwords) + count/unread: a fixed-size block, so the save can't grow (all saves 54,966 bytes). SMSInbox_Add refuses at 0x100 entries. | Decompiled + save sizes. |
| `0x00502F40` | `Profile_SetName` | (renamed) thiscall(0x0083A9E0, name, int), RET 8; +0x20 (`0x0083AA00`) = named. Callers only in the memory card screen: `0x004E8F02`, `0x004E8F46` (before save jobs `0x004D9AB0`/`0x004D9B80`, name = screen+0x6C) and `0x004FBAFD` (msg 0xDA5B8712 = name-entry keyboard accepted, text at `0x0083A8B0`). **Hooked by Covers** (ProfileNameGuard). | Decompiled + disassembled. |
| `0x0083A8B0` | (keyboard text) | Name-entry keyboard buffer (256 bytes; `0x0083A7B0` a second one). In the round it held ".tga" when the save screen asked for a name after a magazine photo. Writer not found yet. | Disassembled (`0x0054C7F0`, `0x0054E5F5`). |
| `0x004D9C60` | `MemCard_SaveShowcasePicture` | Picture file name = profile name + " " + `Showcase_PictureKeyToName(key)`; strncpy(`0x00835BB8`, name, 16). | Disassembled. |
| `0x00541090` | (stage showcases) | Every photo adds the showcase key to the CURRENT stage record's 6-entry list (+0xC4) and runs `StageRecord_CheckComplete` (record 6 is already complete post-game, so nothing happens). | Decompiled + saves. |
| `0x004A7CF0` | (pause quit) | Quit to Main Menu (Yes 0x30ED2368, CALLs `0x004C21F7` / `0x004C2444`), saves the spot via `FUN_005139C0`; returning to the career doesn't reload the profile. **Hooked by ImmersiveTravel.** | Disassembled. |

### Cover photo save and the archive-only option (BayviewForever 0.2.1, 2026-10-18)

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x0055B260` | (UI_MagazineReward Notify) | `FUN_0055B220` asks "Do you want to save this magazine?" / "...DVD?" (`0x03312F33` / `0xE9641A65`; Yes `0x11223344` → capture → `FUN_00523EF0`; No `0x11223345` → `0x0054B090`). Answers `0x172176E8` (done) / `0x0036FAD7` (failed) both free the capture and go back to free roam (`0x0054B090` → Career_StartFreeRoam). | Decompiled. |
| `0x00523EF0` | (picture save) | `FUN_004A95B0(2, key, [0x00832DB4], 0x100000)`, then **CALL `0x00523F49`** `MemCard_Start(pkg, pkg, 0xCB1, 0, 0, 0x172176E8, 0x0036FAD7)`. **Hooked by Covers** (PhotosToArchiveOnly). | Disassembled. |
| `0x004B59D0` | (posed photo save) | Same as `0x00523EF0` for posed (player) photos: CALL **`0x004B5A5D`** MemCard_Start(pkg, pkg, 0xCB1, 0, 0, 0x172176E8, 0x0036FAD7); ShowcasePhoto_Notify (`0x004CEDC0`) handles the answers the same way. **Hooked by Covers.** | Disassembled + decompiled. |
| `0x004A3100` | `MemCard_Start` | (renamed) cdecl(callerPkg, pkg, operation, ?, ?, doneMsg, failMsg): operation → `0x00836514` (& 0xF0: 0xB0 save showcase picture, 0xC0 load it), opens MC_Main.fng. | Decompiled. |
| `0x0055DDA0` | `FE_SendToPackage` | (renamed) cdecl(message, packageName): calls that package's screen Notify directly. | Disassembled. |

Cover files: header `20CM`, size 0x100024, 8, 0x100000, CRC(part A), CRC(picture), CRC(header 0x18); part A is always
`{0x10D, 0x100000}` (CRC `0x6051D0BB`); picture = the capture buffer as is. Covers writes this format itself.

### Race barriers (drag barrier question, 2026-10-18)

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x00578060` | (race scenery set-up) | thiscall(`0x008654A8`): track id = [`0x00883DAC`]+0x8A; enables the scenery groups `BARRIERS_<id>` (→ +0x18) and `PLAYER_BARRIERS_<id>` (→ +0x1C) via `0x005D7400` (name prefix table `0x00800E00`), `0x005D7470` (load list `0x00883E70+0x641C`, 8 entries), `0x005DB340` (find loaded group) and `0x005710A0` (show: clears instance flag 0x10); then `BARRIERS_CAREER<stage>` / `FREE_ROAM*`. **No race-type check**: drag races get their track's barriers too. Callers `0x005782C0`, `0x0057A689`. | Decompiled + disassembled. |
| `0x005710E0` / `0x005711D0` | (hide group / reset) | Hide = sets instance flag 0x10; reset hides the previous race's groups. Nothing else hides barriers (no drag-specific path). | Grep of all CALLs. |
| `0x005F54A0` | (player barriers) | Also enables PLAYER_BARRIERS_ / BARRIERS_ for the player's track and BARRIERS_CAREER<stage>. | Decompiled. |

Scenery group records in TRACKS/L4R*.BUN: name[0x20], hash, 0, count, count × u16 instance index. Drag barrier sets:
4201/4202 none, 4211-4213 51/71/72 (L4RB), 4221-4223 1019/1015/1052 (L4RB), **4401 357, 4402/4403/4404 only 4 each**
(L4RD). So the highway "Organic" drags 2-4 have practically no barriers in the game data; nothing to switch on.

### GPS route and the blue arrow (drag question, 2026-10-18, static only)

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x005F3CB0` | `Player_DrawGPSArrow` | (renamed) The free-roam "blue arrow" (in-world effect `MARKER_GPS_AID`, player+0x110, made in `0x005F3E10`), drawn at a point ahead on the route. Conditions: `[0x0089E7B0]` == 0; car controller type (car+0x24) == 1 (human: true in drags with DragSteering); player camera (player+0x238) +0xC (camera mode, `0x005F1FD0` switch) == 1; controller navigation (+0x3E4 = 1/2 and route +0x3E8, `0x005F1640`). Called every frame from the render loop `0x006316D0`, any race type. | Decompiled. |
| `0x00413120` | `Route_RequestToTarget` | (renamed) thiscall(route request `0x0081CBC0`, car, float target[3]), RET 8: starts the road route search (`0x0040C440`). Updated each frame by `0x004268A0` from the sim update `0x00609D22`; when done the controller gets navigation (vtable `0x00785230` +0x4C = `0x0041EA30`: +0x3E4 mode, +0x3F0 target). | Disassembled + decompiled. |
| `0x0042A360` | (controller navigation update) | Turns navigation off near the target (+0x400); otherwise keeps / rebuilds the route (`0x0041AF90`), re-requesting it via `0x004188C0` when lost, so leaving the route re-routes from where the car is. | Decompiled. |
| `0x00529480` | `GPS_Activate` | (renamed) World map "Activate GPS" (Yes `0x51123592` in the map screen `0x004E08E0`): stores the target (marker's trigger ints +0x34/+0x38/+0x3C → floats) and calls Route_RequestToTarget(`0x0081CBC0`, player car `0x005F0E30`, target). | Decompiled. |
| `0x004131F0` | `Route_Clear` | (renamed) Clears the route request and the controller's navigation (vtable+0x4C(0,0)). Called at race start (`0x00541126` challenge, `0x005411A0` race), on shop entry (`0x0054CB04`) and at `0x0060A9B9`. | Disassembled. |

HUD: `HUD_Drag.fng` (InGameDrag.bun) has **no minimap at all**; the minimap + GPS selector live in InGameRace.bun /
InGameFreeRoam.bun. The arrow is a 3D world effect from InGameCommon.bun (loaded in every mode). Drag finish lines:
TRACKS/ROUTESL4R*/TrackPosMarkersAll.bin start lines with x, y, z (record +16); a drag's forward finish = its reverse
start (e.g. 4402F start 609.4 75.9 17.2, finish = 4402R start −1190.3 −270.5 24.7).
Not settled statically: the drag camera mode (must be 1), whether the road route follows the drag course at forks.

### FE text: line breaks and centring (covers counter, 2026-10-18)

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x0050C080` | `FEText_CountLines` | (renamed) (text, flags, maxWidth) → lines. **Quirk:** `^`/`\n` only counts when the current line already had a space (reuses the word-wrap "last space" pointer, `0x0050C0EC`); the drawing always breaks. Used for vertical centring. | Disassembled. |
| `0x0051C110` | `maybe_FEText_Draw` | Justification = text object +0x6C: bit 1 h-centre, 2 right, 4 v-centre, 8 bottom; each line is centred on its own width (`0x0050BCE0`, stops at `^`). UI_MagazineView "Back" label: +0x6C = 5 (centred both ways), position data+0x1C/+0x20 = (227, 199), scale 0.45. | Decompiled + round-3 object dump. |

### Earning covers after the career (NFSU2Covers v0.2, covers chat 2026-10-16)

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x0054C940` | `Career_ApplyRaceResult` | fastcall(g_Career), plain RET. Runs only if +0xD25 and !+0xD28; sets **+0xD28 = 1 when the player won**, then `Career_OnRaceWon(race+8)` or `Career_OnRaceLost`. Race = +0x8730. Callers: CALL `0x0054C9DF` (Career_OnRaceFinished), CALL `0x0054CA43` (FUN_0054CA40), JMP thunk `0x0054CA30` (from `0x004F3A30` / `0x004F3AB7`). **Hooked by NFSU2Covers** (Endless Career hooks the CALL to Career_OnRaceWon inside it, so they don't clash). | Decompiled + disassembled. |
| `0x00501150` | `Showcase_IsOffered` | thiscall(g_Career+0x4210, showcase), RET 4. Callers: CALL `0x005334A1` (map markers), `0x0053319B` (FUN_00533110 marker check), `0x0052793C` (FUN_005278F0 "current marker still available"), tail JMP `0x00532AED` (FUN_00532A50). **Hooked by NFSU2Covers**: after the career, true for a cover waiting for the car being driven. | Disassembled. |
| `0x00533680` | `Showcase_MarkDone` | thiscall(`0x0085EF50`), key at this+8, ends with JMP `0x004F92A0` (plain RET). Callers `0x00498EBF` (auto photo), `0x004CEEB2` (posed photo, PAD_ACCEPT). Calls `Career_RebuildMapMarkers` itself. **Hooked by NFSU2Covers** (archives the old picture, marks the cover shot, before the original). | Disassembled. |
| `0x00532B10` | `maybe_Career_OnEnterMarker` | For a showcase marker: g_Career+0x8728 = showcase; if a world challenge is running (+0xC08) and its destination is this showcase → +0xD1C = 3 (challenge success). | Decompiled. |
| `0x00498D00` | (engage dialog opener) | Engaged marker copy at g_Career+0x8704 (= 0x00863444: +0 trigger, +4 race, +8 shop, +0xC showcase, +0x10 challenge). Showcase or challenge → `UI_EngageEventDialog.fng` (→ `ShowcaseDialog_Notify`). | Decompiled. |

### Particles, rain and frame timing (particles chat 2026-10-16, static only)

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x005BEA20` | `VideoSettings_LoadFromRegistry` | Reads HKLM `Software\EA Games\Need for Speed Underground 2` values. `g_ParticleSystemEnable` → `0x00870CEC`, `g_RainEnable` → `0x00870D10`, `g_VSyncOn` → `0x00870D24`. | Decompiled. |
| `0x00488730` | `Particles_InitPool` | "AcidParticleSlotPool" `0x0082DA38`: 0x20-byte particles, 1000 slots (300 in one mode). Emitter-group pool `0x0082DA3C`. | Decompiled. |
| `0x008357E0` | (particle list, used) | Every real particle effect is added here (13 adders, e.g. `0x005A8D69`, `0x00602257`). Updated by `Particles_Update` with the **real** dt: in a race from `0x00609CE0` (sim dt), in menus from `0x004D82D0` (`0x00865198`). | Byte search for the address. |
| `0x00835860` | (particle list, never filled) | Only ctor/dtor/clear/render touch it, so its fixed 1/60 s update in `Render_Frame` (`0x005D2C0D`) is harmless. | Byte search: 7 hits, none add. |
| `0x00492E10` | `Particles_Update` | thiscall(list, float dt). Lifetime in 1/1024 s (`_ftol_trunc(dt*1024)` per frame). Per particle: velocity = u16 speed (+0xC, /128) × signed-byte direction (+0x1C..+0x1E, /127), quadratic drag + gravity, position += v·dt, then **speed and direction are re-truncated every frame**. | Decompiled + disassembled `0x00493050-0x004932D9`. |
| `0x00491E50` | `ParticleEmitter_Spawn` | Spawns by time accumulator (frame-rate independent). Emitter def +0x20 != 0 → particle goes to the "oriented" list (index 1), else the sprite list (index 0). Skipped when `g_ParticleSystemEnable` = 0 and def +0x21 = 0. | Decompiled. |
| `0x0048BDF0` | `Particles_Render` | List 0: camera-facing sprites through the sprite batch. List 1: **one `Particle_DrawOrientedQuadUP` per particle**. Called from `Render_World` `0x005CAEB8`/`0x005CB531`. | Decompiled. |
| `0x005BB220` | `SpriteBatch_Init` | Batch object `0x0086B7A4`: 1000 quads, VB `(4000+10)*24` bytes, usage 0x208 (DYNAMIC\|WRITEONLY), D3DPOOL_DEFAULT; static index buffer. Init calls `0x005D1DDE`, `0x005D27EC` (device reset). | Decompiled. |
| `0x005BB320` | `SpriteBatch_Begin` | Lock(0, n·24, flags 0x2800 = DISCARD\|NOSYSLOCK) **at offset 0 for every emitter batch**. | Decompiled. |
| `0x005BB430` | `SpriteBatch_AddQuad` | Writes 4 verts (24 bytes each); alpha ≤ 0x14 → degenerate quad. | Decompiled. |
| `0x005C57E0` | `SpriteBatch_Flush` | SetStreamSource, SetIndices, effect pass, one DrawIndexedPrimitive per batch. | Decompiled. |
| `0x005C5580` | `Particle_DrawOrientedQuadUP` | Per particle: material/state setup, effect CommitChanges + BeginPass(0), **DrawPrimitiveUP(TRIANGLEFAN, 2, stride 0x20)**, EndPass. Gated by byte `0x007FF05C`. | Disassembled. |
| `0x0061F120` | `Rain_UpdateAndDraw` | Rain around the camera + drops on the lens (`0x006140F0`, `0x0061E700`). Drawn from `Render_World` only when particles are on. **Returns without updating or drawing when \|dt\| ≤ 0.009 s** (`0x007A8734`, check at `0x0061F134`). | Decompiled. |
| `0x005811C0` | `Game_MainLoop` | Measures the frame, `Frame_LimitAndMeasure`, sim dt `0x00865154` (in race: `0x005EA360` → `0x00601A60`), world update, `Render_Frame` `0x005D2850`. | Decompiled. |
| `0x0057EAD0` | `Frame_LimitAndMeasure` | Busy-waits (QPC spin) until the frame lasts ≥ `[0x00865558]` s. Frame dt `0x00865198` = ticks × 1/1800 s, clamped to [limit, 0.05 s]. | Disassembled. |
| `0x007FB710` | (default frame limit) | float 1/120 → the game caps itself at **120 fps**. `FrameLimiter_SetFps` `0x00576CB0` (1/fps), `FrameLimiter_ResetDefault` `0x00576CD0`. 30 fps only in online races (`0x005FF688`, condition `0x0089CF69`/`0x008669F4`/`0x00866A14`) and one other mode (`0x00435FA8`). | Disassembled + constants read from the exe. |
| `0x0043BD90` / `0x0043BDF0` / `0x0043BE20` | `Timer_Init` / `Timer_ReadTicks` / `Timer_TicksToMs` | QueryPerformanceCounter, shifted so 2^31 ticks ≥ 60 s; ms = ticks·2^shift·1000/freq. Works with Windows 10's fixed 10 MHz counter (shift 0). | Decompiled. |
| `0x0075BED4` | `_ftol_trunc` | MSVC float→int: **truncates toward zero**. | Disassembled. |

## Gear counts per car (surround chat, 2026-10-16)

Car type table (GLOBALB chunk 0x34600, 46 × 0x890, `*0x008A1CCC`): performance block at entry+0x110, copied into
the car's stats by `0x005AC4F0` (0xAC dwords). Stock ratios at block+0x1D0 (8 floats R, N, 1st..6th), forward gear
count stats+0x1C8. `0x005A6BD0`: if the transmission upgrade level (FUN_005A6580(1,3), 1 Street / 2 Pro / 3 Extreme)
is set, copies block+0x310+L*0x40 (ratios at +0x20) and recounts the non-zero forward ratios. Result: 6 gears stock on
Tiburon, Sentra, Celica, Supra, A3, RSX, TT, RX-8, 350Z, G35, 3000GT, GTO, Skyline, Impreza; every other player car has 5
(SUVs 4) and gets a 6th gear (SUVs a 5th) with the **Extreme** transmission. Traffic: 5. Tool: `tools/dump_car_gears.py`.

### Car physics side of the steering (2026-10-17)

| Address | Name | Notes |
|---|---|---|
| 0x005ABBF0 | (vehicle input update) | Reads driver+0x1E0 (u16 angle) and calls the steering component [obj+0x24] vtable+4 (0x005ABD2C). |
| 0x005A2A60 | (vehicle input copy) | Copies driver+0x1E0 → component+0x130, throttle +0x20C → +0x128, brake +0x214 → +0x12C. |
| 0x007A2084 | TwoWheelAckermanSteering vtable | Pool 0x0086B2E4 (0xA0-byte objects), created at 0x005B5E0D. Slot 0x0059DEA0 = debug dump. |
| 0x0059DEF0 | Ackerman_SetSteering | (not a Ghidra function) u16 angle × 2π/65536 → θ; inner wheel θ, outer aθ/(a + bθ). No rate limit, no speed scaling: the game-side angle is what the tires get. |

Full wheel → tire chain is now known; `tools/steering_calc.py [Linearity] [SpeedSensitivity] [SteeringLock] [percar]
[PedalAssist]` computes it from the exe's own tables (front-wheel angle at full lock and the centre steering ratio by speed
and pedal state).

Race-lost hook (events chat, 2026-10-17): `Career_OnRaceLost` 0x00528500 has exactly one CALL, 0x0054C9BC
(`E8 3F BB FD FF`, in Career_ApplyRaceResult, ECX = g_Career, push race key), RET 4 at 0x00528551. Hooked by Endless
Career v0.2.2 (log only). A third CALL to Career_OnRaceWon, 0x0054CB8B, is in the world-challenge/showcase finish path.

## Race AI: catch-up (rubber band) and blocking / "PIT" (AI research, 2026-10-17, static only)

All addresses verified in Ghidra/objdump against the v1.2 exe. Nothing patched yet.

### AI tuning data
| Address | Name | Purpose / how verified |
|---|---|---|
| `0x00408200` | AITuning chunk loader | Chunk **0x34B00** → `[0x0081CB60]` = chunk data. The chunk is in `GLOBAL/GLOBALB.BUN` (file offset 0x750980, 0xAE0 bytes). 44 code references. |
| AI+0x000..0x2BF | catch-up curves | 8 × 11-point curves {distance m, speed fraction}, 0x58 bytes each (e.g. 320 m behind → +0.22, 640 m ahead → −0.25). Picked by laps (`0x007E95C8` thresholds vs `0x0089E7BC`) and race kind (`0x0089E7A8` 2/3); boost curve at index i, "slow down" curve at i+4. Online/Street X: +0x2C0 / +0x318. |
| AI+0x420..0x423 | override flags | All 0 in stock data (force the gated parts on in Street X). |
| AI+0x504 | progress curve | 4 points {race fraction, scale}, all 1.0 (no effect). |
| AI+0x524 + skill×0x68 | per-skill block (skill 0..9) | e.g. +0x5C (0x580 for skill 0) = seconds before catch-up may start (3, 4, 5, 6, 8, 10, 13, 15, 17, 19 s); +0x34..+0x40 drag AI (drag only, `0x005B6204`). |
| AI+0x934 + i×8 | block personalities | {int priority, bool} = {200,1}, {200,1}, {400,1}. Index = racer+0x902. |

### Catch-up (rubber band)
| Address | Name | Purpose / how verified |
|---|---|---|
| `0x0089E7F8` | Catch Up option | int in the race options struct (0x0089E7A0 + 0x58). Copied from the Quick Race settings (byte +9 of the 0x14-byte record per mode at `0x0083AAC0`; mode index `0x0083AAB4`, 8 = career) at `0x0053F215`, `0x0053F435`, `0x0053F5F3`, `0x0053F71F`, `0x0053F946`, `0x0053FB0E`; online `0x00601E24`. Game text "Catch Up" exists (English.bin). |
| `0x0053FC20` | RaceOptions_SetupByMode | Switch on `0x0083AAB4`: 0/1/7 → 0x0053F150, 2 → 0x0053F690, 3 → 0x0053F830, 4 → 0x0053FA40, 5/10 → 0x0053F370, **8 → RaceOptions_SetupCareer**, 9 → 0x0053F580, 0xB → 0x00526720. |
| `0x00526830` | RaceOptions_SetupCareer | Sets `0x0089E7F8 = 0` (`0x00526892`). |
| `0x005322E0` | Career_SetupRaceOptions | Career race start (race record in param 1): difficulty = `0x0083AC30`, race-type flags (+0xF behaviour: Street X → `0x0089E7E3`, drag → `0x0089E7D8`, drift → `0x0089E7D9`, circuit → `0x0089E7E0`); never writes 0x0089E7F8. ~~→ career runs with catch-up off~~ **WRONG, see RaceOptions_Reset below: career races run with Catch Up = 1** (AI round 1 log). |
| `0x005ED5F0` | RaceOptions_Reset | fastcall(ECX = 0x0089E7A0): zeroes the struct and sets defaults, incl. **+0x58 Catch Up = 1** (`0x005ED681`: `MOV [EDX+0x58],1`, a register write the first address search missed). Called by RaceOptions_SetupByMode (then overwritten per mode) and from 0x0053FD3A, 0x005791DE, 0x0057FF4B, 0x0077EDA5. In-game (AI round 1, Endless Career races S4_SPRINT_12 / S4_URL_4): game mode 9, Catch Up = 1, AI boosts up to ×1.15. |
| `0x004172D0` | AI_UpdateCatchUpSpeedScale | AI vtable method (not a Ghidra function). Per frame, result → ai+0x770 and car+0x468 (= max(result, 1.0)). Off for drags/drifts. Part 1 = AI_CatchUpRubberBand; part 2 (reads `0x0089E7F8` at `0x0041757B`: `A1 F8 E7 89 00`) = per-skill pack factor; part 3 = progress curve (flat). |
| `0x004084A0` | AI_CatchUpRubberBand | thiscall(ai, me, leader, out boost, out pack). Returns 0/0 unless `0x0089E7F8` != 0 (read at `0x004084B9`: `A1 F8 E7 89 00`). |

**Disabling catch-up**: replace both reads (`0x004084B9`, `0x0041757B`, each `A1 F8 E7 89 00` = MOV EAX,[0x0089E7F8]) with `31 C0 90 90 90` (XOR EAX,EAX + 3 NOPs). The menu option keeps its value; the AI just never sees it. Undo: write the original 5 bytes back. Changes Quick Race with Catch Up ON **and career races** (which run with Catch Up = 1).

### Blocking ("PIT manoeuvre")
AI behaviours are classes with name strings (`0x00784C98..`): JustDrive (vtable 0x00785684), Draft (0x007856B4), Pass (0x007856D8), NudgeAvoidable (0x00785704), **Block (0x00785728)**, PullOver, GetUnstuck, IJustGotHit, AccidentRecovery, AvoidAvoidable (0x00785858) …
| Address | Name | Purpose / how verified |
|---|---|---|
| `0x0042D0F0` / `0x0042D6F0` | AI_ChooseAction / AI_ChooseAction2 | Two copies of the AI decision function (two AI controller kinds). Candidates: get unstuck/got hit, avoid, **Block** (if not Street X, state ≠ 4, current priority ≤ block priority and AI_FindCarToBlock finds a car), Pass, JustDrive. |
| `0x00414130` | AI_FindCarToBlock | cdecl(ai, out worldObj*) → priority or −1. Only when the AI drives at > 80 % of its speed target. Scans cars behind it in race order: target ≥ 10 m/s, ≤ 50 m behind. **AI target: only if it would reach us within 2 s** (0x00783694); **human target: within 30 s** (0x00783690), or anywhere within 35 m (0x00783698). So the AI deliberately moves into the player's line → at speed this is the "PIT". |
| `0x0041CC55`, `0x0042D36A`, `0x0042D96A` | CALLs to AI_FindCarToBlock | `E8` rel32, each followed by `ADD ESP,8`. 0x0041CC55 is in the Block action's "still valid?" method (0x0041CC40, vtable 0x00785728 slot +0x18), the other two start a block. |
| `0x004238C0` | AIActionBlock_Create | Creates the Block action (priority, -1.0f, target). Callers 0x0042D39D, 0x0042D99D, 0x00427273 (factory). |
| `0x00423780` | AIActionNudgeAvoidable_Create | Only called by the action factory 0x004270E0 (rebuilds actions from a stream by type id: 0 Pass, 1 Draft, 2 Nudge, 3 Avoid, 4 Block …). Normal racing never picks Nudge. |
| `0x00614880` | WorldObj_IsHumanRacer | fastcall(worldObj) → AL: `[[obj+0x1C]+0x14]+4 == 1`. Racer info: type at +4 (1 human, 2 AI, confirmed in 0x005B5CC0); skill +0x901 (−2 for the player), block personality +0x902. |
| `0x0089E970` | world object table | 128 pointers (0x0063B590 registers). AI_FindCarToBlock returns one of these. |
| racer+0x902 | block personality | = GCareer opponent byte +0xB (Nikki's "CatchUp"), via `Race_SetupOpponents` → 0x006373E0. **It is not catch-up**: it picks the block priority (200/200/400). |

| `0x0042B5B0` | AI_FindCarToPass | cdecl(ai, out worldObj*, out route) → **100** or −1. Cars ahead in race order; plans a passing route (0x00429890). In AI_ChooseAction the Block test comes before Pass, and a new action may replace the current one when the current priority (param 3: thresholds 500/400/300/100 in the function) ≤ the new one: Pass 100 < Block 200/400. |

**Pass → PIT (Chris: "a lot when they go to pass")**: while the AI is overtaking, the player is ahead, so no block. The moment the AI noses ahead, the player is "behind within 35 m", which for a human target needs no closing speed. AI_FindCarToBlock picks the player at once, Block (200) beats the running Pass (100), and the AI moves across the player's nose while still overlapping → contact on the player's front corner. Same fix as below: refusing human targets also stops this. Untested whether the plain pass route (merge back to the racing line, no Block) still clips the player.

**Career rubber band re-checked (2026-10-17) — conclusion WRONG, disproved in-game the same day (RaceOptions_Reset sets Catch Up = 1; career races boost AIs up to ×1.15)**: no indirect writes of `0x0089E7F8` (no struct copies, no `[reg+0x58]` writes via 0x0089E7A0); other readers are online sync (0x005F95E0, 0x00601D20) and a two-human split-screen catch-up (0x005F6740, same flag). Every other reader of the AI per-skill table (0x00408280, 0x00408430, 0x00413E33, 0x0041C5E0, 0x0041C990, 0x0042C0C0) uses only skill values and frame time, never race position or the distance to the player. car+0x468 / ai+0x770 are reset to 1.0 (0x00408C80, 0x00591ED0, 0x00591F76). Not rubber band, but felt in career: AI at the stage's car level, and (probably) AI nitrous more often on the last lap (0x00416FD3 → 0x0040D850, per-skill timers +0x44 / +0x48). Static only; a logger of `0x0089E7F8` and each AI's car+0x468 during a career race would prove it in-game.

**Gap and timing details (for a distance check or delay, 2026-10-17)**:
- Gap = `0x0060BFF0`(entry) (distance along the route, target − AI, negative = behind) + entry+0xC + own entry+0xC (car half-lengths, added because all 3 block personalities have the flag at AI+0x938). So gap < 0 means the target's nose is behind the AI's tail: the stock AI may block as soon as it is fully ahead, even by centimetres.
- `0x004142DE`: `D8 1D 3C 7D 7D 00` = FCOMP [0x007D7D3C] (shared 0.0): "gap ≥ 0 → no block". Pointing the operand (0x004142E0) at our own float −N gives **"no block unless the target is more than N m behind the AI's tail"**. Applies to AI and human targets, and also ends a running Block (the "still valid" check 0x0041CC55 calls the same search) once the target closes within N m.
- `0x0041435B`: `D9 05 90 36 78 00` = FLD [0x00783690] (30 s, human time window); `0x00414363`: `D9 05 94 36 78 00` (2 s, AI window); `0x0041430C`: `D9 05 A0 36 78 00` (50 m, how far behind it looks). The 35 m human rule: `0x004141B4`: `D9 05 98 36 78 00` = FLD [0x00783698], negated and kept at [esp+0x28], compared at 0x00414399.
- Game clock `0x0086518C`: int, 4000 ticks per second (starts at 0xFA0 = 1 s at 0x00576D01; code converts with ×0.00025 = [0x00784268]). Good for per-AI delay timers in a hook.
- AI_ChooseAction2 (0x0042D6F0) is called through a vtable (pointer at 0x00784B90); AI_ChooseAction 0x0042D0F0 has no direct caller or pointer found. How often they run per second is unknown (a test log can count calls).

**Fix options** (not built): (a) redirect the 3 CALLs to a hook that calls AI_FindCarToBlock and returns −1 when the target is human (AIs still block each other); (b) treat the player like an AI (2 s window, no 35 m rule): needs a code patch, since the constants are shared; (c) turn Block off completely; (d) distance check: operand patch at 0x004142E0 (minimum gap N m, everyone); (e) delay: hook on the 3 CALLs with a per-AI timer (game clock) so a human target is only accepted after it has been the candidate for T s.

### NFSU2RaceAI plugin (v0.1.0, 2026-10-17)
`src/race-ai/race_ai.cpp`, ini `NFSU2RaceAI.ini`, log `race-ai.log`. Uses: the two Catch Up reads (XOR EAX,EAX), the gap
compare operand at 0x004142E0 (→ plugin float −BlockMinGap), and RedirectCall on `kFindCarToBlockCallSites`:
0x0041CC55 → HookFindStillValid (only keeps the delay stretch alive), 0x0042D36A / 0x0042D96A → HookFindToStart
(delay per AI on the game clock; a stretch restarts after 1 s without the player as target; BlockPlayer=0 refuses).
Compiled hooks checked: cdecl, args at [esp+4]/[esp+8], original called with 2 stack args, plain RET.
Log chains (read-only, IsReadable each step): ai → [+0] vehicle → [+0x2C] controller → [+4] car → [+0x14] racer;
car+0x468 applied catch-up boost, controller+0x770 raw result, racer+0x901 skill, +0x902 block style, +0x90C name (unconfirmed).
A new race in the log = game clock went back or 5 s of real time without an AI search (so a long pause also splits it).

### Pass (overtake) → PIT (AI round 1, 2026-10-17)
Round 1 (`Test Results /Saturday All - Round 1/race-ai.log`, v0.1.0 with BlockMinGap 15, BlockDelay 1.5): Chris: "still
pitting when going to pass. They get right past the rear bumper and steer into the player's quarter panel." The log has
only 3 blocks on the player (all from 15 m+), so the PIT is not the Block action.
| Address | Name | Purpose / how verified |
|---|---|---|
| `0x0042B940` | AIActionPass slot 2 (per frame) | Not a Ghidra function. target = [this+0x218] (world object); entry = AIBrain_FindEntry (CALL `0x0042B95C`); gap = AIBrain_GapTo (target − own distance along the route, centre to centre). `0x0042B97D` `D8 1D 3C 7D 7D 00` FCOMP [0.0]: **gap ≤ 0 → AIRoute_SetRacingLine (CALL `0x0042B9F8`)**, i.e. back to the racing line the moment the two centres are level, while the cars still overlap. Else AIRoute_PlanAround; if it fails → racing line too (CALL `0x0042B9C9`). |
| `0x0042B7F0` | AIActionPass slot 6 | Re-targets via AI_FindCarToPass (CALL `0x0042B858`); after 3 s the pass is finished once the AI's tail is 10 m (0x00783684) ahead of the target's nose. |
| `0x0060C120` | AIBrain_FindEntry | thiscall(brain, worldObj) → entry or 0, RET 4. Entries brain+8 + i×0x30 (count brain+4), handle +0x2C (int16 into 0x0089E970). |
| `0x0060BFF0` | AIBrain_GapTo | thiscall(brain, entry) → ST0 = entry+0x10 − own entry+0x10 (own index int8 brain+0x308), RET 4. |
| `0x00429890` | AIRoute_PlanAround | thiscall(route, ?, targetEntry, brain, 3.0): lateral target = target's lateral ± (half-widths + 3.0 m, 0x00783818); side from 0x0041D2D0; builds the route (0x00428DF0, result in AL). No check of the gap sign. |
| `0x00420D40` | AIRoute_SetRacingLine | thiscall(route, action+0x20, brain), RET 8. |
Entry fields: +0x4 closing speed, +0xC half-length, +0x10 distance along the route, +0x2C world handle.

**NFSU2RaceAI v0.2.0**: `PassClearGap` (m): CALL 0x0042B95C → HookPassFindEntry sets a float to −(both half-lengths +
PassClearGap) and the compare's operand (0x0042B97F) points at it, so the AI keeps its passing line until its tail is
PassClearGap m ahead of the target's nose (max 10, where the game ends the pass anyway). Log hooks on the two
racing-line CALLs (once per pass and reason, with the tail-to-nose gap) and on the 3 CALLs to AI_FindCarToPass.
Compiled hooks checked: fastcall @12 RET 4 for the entry hook; racing-line hooks tail-jump to 0x00420D40 with the
stack untouched (RET 8 by the game).

### Cover messages and the viewer counter (NFSU2Covers v0.3, covers chat 2026-10-17)

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x00860A60` | SMS inbox (g_Career+0x5D20) | 0x100 entries × 0x14: +0 message record*, +4 read (1), +5 auto-opened, +8/+0xC/+0x10 text hashes. +0x1400 count, +0x1404 unread count (Add +1, reading −1 at `0x00514DC0`), +0x140C new-mail flag. Saved with the profile (record pointers → stock records only). | Disassembly of `SMSInbox_Add` `0x005295F0`, callers' ECX (`0x0053326D`, `0x00533AF5`). |
| `0x005299B0` | `SMS_SendDvdCoverMessage` | thiscall(inbox, n), RET 4: adds the DVDUnlock (type 0x0E) message with parameter n = SMS_DVD_COVER_n unless already in the inbox. Stock: n = star rating. | Decompiled. |
| `0x00529670` | `SMSInbox_AddWithTexts` | thiscall(inbox, record, body, subject, arg), RET 0x10. | Disassembled. |
| `0x005338C0` | `WorldChallenge_QueueUnlockSMS` | (also) thiscall(inbox, challenge), RET 4: for a showcase challenge: SMS_FindMessage(challenge+8) with SMS_MAG_GENERIC_BODY_1/2 + SUBJ_n (alternating, counter `0x00864210`) and the showcase title as %s ("Nice race pal. Word on the street is there's a photographer from %s magazine…"). | Decompiled + disassembled. |
| `0x00501310` | `SMS_FindMessage` | cdecl(name hash) → record (plain RET). | Disassembled. |
| UI_MagazineView.fng objects | | Text `0xB8A7C6D0` = "Back" label (text hash `0x001335F0`, capacity 17); text `0x47FF4E7C` = no stock text (label `0x9588BD38` not in English.bin, capacity 16), both not hidden (hidden = flags bit 0, `0x0050CA00`). Object layout: +0x2C data block, +0x60 label hash, +0x64 text buffer, +0x68 capacity. | covers.log (round "Saturday All 1"). |

**NFSU2RaceAI v0.3.0** (after AI round 2): PassClearGap only when the pass target is human (HookPassFindEntry checks
WorldObj type); `BlockStartDelay` refuses every block start for N s after the first AI search of a race; AI-vs-AI pass
and block events within 25 m (route distance) of the player are logged. The player's entry in a brain is found by
scanning its entries for a human world object. Note: AI goals are a separate class (vtable e.g. 0x00784B58, the
"Rolling start" goal: slot +0x38 = 0x0042D6F0 AI_ChooseAction2, +0x3C = 0x004269E0 JustDrive); the field holding the
current action is not found yet.

## Money pickups and the map's X icon (events chat, 2026-10-17, for Endless Career v0.3.0)

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x00838430` / `0x00838434` | `g_MoneyPickups` / `g_MoneyPickupCount` | GCareer sub-chunk 0x34A1E, 12-byte records: u16 cash, u8 condition (0 always, 1 = only while current stage > value, 2 = only once race `value` is won), u8 collected-flag index, i32 value, u32 type-19 world trigger hash. 59 stock (indices 0-58, conditions 1/0..4 and two 0/0). | `GCareer_LoadChunk` (`MOV [0x838430],EDX` at 0x00512D85); decoded file. |
| `0x005141B0` | `Career_BuildLiveMoneyPickups` | fastcall(g_Career), plain RET (0x00514275). Rebuilds the live list at g_Career+0x800 (0x100 trigger pointers) from the type-19 triggers whose record passes the condition and whose collected flag is 0. Called by `Career_RebuildMapMarkers` and `MoneyPickup_Collect`. | Decompiled + disassembled. |
| `0x00513890` | `MoneyPickup_IsAvailable` | thiscall(career, record): condition + collected flag. | Decompiled. |
| `0x00528370` | `MoneyPickup_Collect` | thiscall(career, record), RET 4: cash += u16 record+0 (0x0052837B), flag `[career+index+0x8768]` = 1 (0x0052838A), rebuild list. | Disassembled. |
| `0x005283C0` | `MoneyPickup_OnTriggerEntered` | cdecl(trigger): finds the record by trigger hash, collects it if available. | Decompiled. |
| `0x008634A8` | collected flags | = g_Career+0x8768, one byte per pickup index, part of the saved career data. **After the career every uncollected pickup is live** (stage 6 > every condition value). | Code. |
| `0x004B0670` / `0x004AC460` / `0x00496A40` | world map / map indicator / race info pop-up | Race marker image: race +0x34 == 1 (sponsor) → texture 0xB90F50BB (the **X**), else 0x25559384 (circle). The pop-up shows a sponsor badge for +0x34 == 1. | Decompiled; cmp bytes at 0x004B06D0 and 0x004AC49F. |
| `0x00500C30` | race payout shown | +0x34 == 1 and the current stage has a sponsor → sponsor payout, else race +0x30 (after the career: always +0x30). | Decompiled. |
| `0x00610510` | racer skill getter | Racer info +0x901 (forced to 9 when [0x008650B0] is set). The skill indexes the AI's per-skill table (10 entries, AI+0x524 + skill×0x68), so **valid skills are 0-9**; stock races use 0-9. | Disassembled + addresses.md "Race AI". |

#### Sprite batch object and main-loop CALLs (particles chat 2026-10-17)

Sprite batch at `0x0086B7A4` (`SpriteBatch_Init 0x005BB220`): +0x00 VB (dynamic, write-only, default pool), +0x04 IB
(static, 6 indices per quad), +0x08 quad capacity (1000), +0x0C vertex capacity (4000), **+0x10 cursor**, +0x14
vertex count of the open batch, **+0x18 start vertex**, +0x1C stride 0x18, +0x20 byte locked, +0x21 byte, +0x24 write
pointer (= `0x0086B7C8`, used by `SpriteBatch_AddQuad`), +0x28 end pointer (`0x0086B7CC`), +0x2C data, +0x30 byte.
`SpriteBatch_Begin` resets the cursor to 0 and locks with DISCARD every time; `SpriteBatch_Flush` (`0x005C57E0`) draws
`DrawIndexedPrimitive(TRIANGLELIST, 0, start, count, (start/4)*6, count/2)`, so appending at the cursor works
without other changes. Unlock + flush also at `0x005CC250` (JMP to flush), `0x005CF9B1` and `0x005D1C73` (device
lost). Second begin wrapper at `0x005BB3B0`-`0x005BB42B` (CALL `0x005BB424`). Verified: disassembly.

Game_MainLoop CALLs in run order (each frame): `0x005811EE` (0x005BCFA0, loop top), `0x005812ED`
Frame_LimitAndMeasure, `0x00581301` (0x00456940), in the world `0x005813CA` (0x005EA360 sim dt, with its own
catch-up busy-wait), `0x00581407` (0x00609CE0 world update), `0x00581416` (0x00578E10), then `0x00581484`
(0x0060AEE0), `0x005814CC` (0x005CE850), `0x005814D1` (0x005CE8A0: creates and fills up to 10 managed VBs for newly
loaded meshes), `0x005814D6` Render_Frame, `0x005814DB` empty stub. Verified: disassembly.

## Car sound objects and 3D direction (surround chat, 2026-10-17)

| Address | Name | Purpose |
|---|---|---|
| `0x007ED138`… | sound class registry | Records {id, name, parent, factory}: e.g. CARSFX_PlayerEngine id 0x10000 factory 0x0047D6E0, CARSFX_AIEngine 0x20000 → 0x0047D680, SFXCTL_3DCarPos 0x10080 → 0x0047C7A0, SFXCTL_Engine, SFXCTL_3DLeft/RightWheelPos, CARSFX_RoadNoise, CARSFX_Turbo … |
| `0x0045DE60` | `Snd3D_ComputeAngle` | angle (u16) = 0x4000 − acos(dot(normalize(src − listener), listenerFwd)), mirrored by the cross product; src = obj+0x78/+0x7C (2D). Unstable when src ≈ listener. |
| `0x0045DF00` | `Snd3D_Update` | Picks listener position/forward per mode `0x0082BC14` (tables 0x0082BF10/0x0082BECC, 0x0082BEDC/0x0082C6C4, 0x0082BF20/0x0082BE70, 0x0082C670), writes angles obj+0x10/+0x0C and distances (0x00401150) obj+0x08/+0x04. |
| `0x00473D30` | PlayerEngine ctor | Class table `0x0078AF30`; 2 layer slots (+0xCC, +0xE8, 0x1C each, handle at +0x14 → +0xE0/+0xFC). Layers = accel GIN + decel (`_DCL`) GIN; no separate exhaust sound. AI engine: ctor 0x00473CE0, table 0x0078AEE0. |
| `0x00474310` | PlayerEngine update (slot 18) | → 0x00473E60 (1 layer) / 0x00474030 (2 layers), each sets volume (0x0072412A), direction `Snd_SetAzimuth(handle, this->vtbl[0x3C](0,0))` at **0x00473F5A / 0x0047412F / 0x00474285**, pitch etc. |
| `0x0045E3B0` | engine GetAngle(slot) | u16 at (obj+0x58[slot])+2: output of the attached 3D position controller. |
| `0x00725480` | `Snd_HandleToVoice` | voice index = handle & 0xFF, valid if voice+0x6D and voice+0 == handle. `0x00727FBD` walks voices with the same group byte (+0x26). |

## Sunday round 4 findings (polish chat, 2026-10-18, BayviewForever 0.2.3)

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x00407A30` | `RealDriver_MapPedals` | (renamed) thiscall(driver, float dt), RET 4. Pedal inputs (throttle driver+0x2D4, brake +0x2DC) → outputs (throttle +0x20C via `0x005EB910`, brake +0x214 via `0x005EB8F0`). The game's arcade reverse lives here, in **every** shift mode: brake > 0.6 (`0x00784564`) with low throttle sets flag +0x2F0; below 0.5 m/s (`0x00783B28`, speed = car+0x42C) the brake becomes the throttle and wanted gear = R (`0x00407CBE`); throttle while wanted gear is R calls vtable+0x30 (`0x00407E5C`: wanted = 1st in modes 1/3). End: race-finished autopilot (`0x005F00F0` && state 6) sets throttle 0, brake 0.5. One CALL: **0x0041E332** in the driver update, before the gear logic. With the H-shifter in R this flipped mode 3↔4 every frame (Sunday round 4: throttle bogged, brake drove backwards). H-shifter BrakeOnlyBrakes redirects that CALL. | Decompiled + objdump; symptom matches the round 4 log. |
| `0x005EB8F0` / `0x005EB900` / `0x005EB910` | `RealDriver_SetBrakeOut` / (+0x210) / `RealDriver_SetThrottleOut` | thiscall setters, RET 4: brake → +0x214, (+0x210), throttle → +0x20C. | objdump. |
| `0x0063176A` | CALL `Player_DrawGPSArrow` | Only call (render loop `0x006316D0`). The arrow function's only free-roam test is `[0x0089E7B0]` at `0x005F3CB9`; its callees (`0x00419B20`, `0x00419D20`, `0x0040A2A0`, `0x005EAC90`, `0x005C5930`, `0x005F1640`) never read it. The marker effect (player+0x110) is made for every player (`0x005F3E10`). Drag Steering 0.2.3 fakes game kind 0 around this one call during GPS drags. | Decompiled + grep of all 171 `0x89e7b0` uses. |
| `0x004268A0` | `Route_UpdateRequest` | (renamed) The player's route request (`0x0081CBC0`) worker, every world update (`0x00609D27`), any game kind. State 2 waits for +0x5B4 == 1. Round 4: one drag's request stayed busy all race (cause unknown), the next was ready at once. | Decompiled. |
| `0x007208D8` | (sound lib, no Ghidra function) | Sets a voice's angle (+0x1C) directly and re-pans it; reached only through the sound library's table at `0x00817F3C`. Other car sounds than the engine (turbo, road noise ...) get their direction outside `Snd_SetAzimuth`. | objdump. |

**Own-car sounds in surround (round 4 drag dump):** the loudest sound of the player's car (voice 62, 22050 Hz, vol 0.35,
angle 0x7F6A = straight behind in hood view) was not the engine (voice 13, vol 0.27, angle 0x7F72, placed by the
plugin). Registry (`0x007ED138`, records {id, name, parent, factory}) lists `CARSFX_Turbo` (0x10020, factory
`0x0047CA70`), `CARSFX_Nitrous`, `CARSFX_RoadNoise`, `CARSFX_WindNoise`, `CARSFX_Skids`, `CARSFX_Shift` etc.; the ones
on `SFXCTL_3DCarPos` share the engine's direction. Surround 0.2.3 matches voices to the engine's latest direction
(±0x100) while it is not ahead.

## Turbo blow-off (polish chat, 2026-10-18, BayviewForever 0.2.4)

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x00472480` | `CarSfxTurbo_ctor` | (renamed) CARSFX_Turbo (registry id 0x10020, factory `0x0047CA70`), vtable `0x0078AC10`. | Decompiled. |
| `0x0047CAD0` | `CarSfxTurbo_Update` | (renamed) vtable slot 9. Boost from controller +0xB8 (vtable +0x38); state +0x180: 0 → 1 above `[0x007ED3A4]`, 1 → 2 when it drops = blow-off via `SndBase_PlayOneShot` at **0x0047CB9D** (its only caller); 2 → 0 when the sound ends (`0x0045EFF0`). Spool: `0x00472770`, volume +0xE4. | Decompiled + objdump. |
| `0x0045EE70` | `SndBase_PlayOneShot` | (renamed) thiscall(obj, 0, 0, 0, 0, id), RET 0x14. Plays only if obj+0xC0 == 0. Variant +0xEC = 1, or 2/3 at random when boost +0xF8 > `[0x00785510]`. Volume **+0xE8** = turbo tuning (`[+0xE0]+8`, or `+0xC` for 2/3) × boost; sound object → +0xC0 (`0x00458260`, bank FX_TURBO_01). | objdump. |
| `0x0045ECE0` | (vtable slot 10, no Ghidra function) | Per frame: blow-off volume = min(+0xE8 × master (vtable +0x38) >> 15 × fade +0xD8, 0x7FFF) → sound+0x8 (`0x00735580`); spool the same from +0xE4. So a louder blow-off works by scaling +0xE8 until the 0x7FFF cap. | objdump. |

Surround 0.2.4 `[Surround.Sounds] BlowOffVolume` redirects the CALL 0x0047CB9D and scales +0xE8 once a blow-off has
started (it skips the call when one was already playing).

### Turbo spool (0.2.5)

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x00472770` | `CarSfxTurbo_UpdateSpool` | (renamed, 0.2.5) thiscall(obj, dt), RET 4; called twice per turbo update (0x0047CB2C, 0x0047CBAF). No spool sound (+0xBC == 0): starts it (0x0045F0A0) and returns. Otherwise writes a fresh spool volume to **+0xE4** each call (0x00472968–0x004729B4); slot 10 sends min(+0xE4 × master >> 15, 0x7FFF). Surround `[Surround.Sounds] SpoolVolume` scales +0xE4 after the call. | objdump. |

No separate exhaust sound exists: the player engine plays an accel and a decel recording (`PlayerEngine` 2 layers);
the other own-car sounds are CARSFX_Turbo (spool + blow-off), _Nitrous, _Shift, _Skids, _RoadNoise, _WindNoise etc.

## Visual uniques, cover photo pay, traffic density (polish chat, 2026-10-18, BayviewForever 0.2.7, static only)

| Address | Name | Purpose | How verified |
|---|---|---|---|
| `0x00533D20` | `WorldChallenge_BuildUniquePartList` | Type 1 (visual): part byte 0x3E (62) → every part of slot 0x40 with byte5 group 0x18 + level bits 0x80 (vinyls); 0x0D (13) → rims (slot 0x1D) filtered by the car's rim sizes; other ids → `UniqueVisual_IdToPartSlot` → all parts of that slot at **level 4** for the car type (`PartDb_FindFirst/Next`). **Never filtered by what the car has**, so a visual challenge can be offered again and again. | Decompiled + disassembled. |
| `0x005020C0` | `UniqueVisual_IdToPartSlot` | (renamed) 11 → 9 (hood), 12 → 0xC (spoiler), 22 → 6 (body kit = widebody), 0xD → 0x1D, ... | Decompiled. |
| `0x0061BA30` / `0x0061BA50` | `PartDb_FindFirst` / `PartDb_FindNext` | (renamed) thiscall(ECX = `0x008A2B68`, carType, partSlot, 0, level) → part or 0, RET 0x10 (Next takes the previous part first). Pure lookups. | Disassembled. |
| `0x00503AE0` | CarRecord vtable[1] GetCarTypeIndex | thiscall(slot): CarTypes_FindByNameHash(slot+0x20) → entry+0x840, or -1. | objdump. |
| `0x0055C230` | `FECarSession_InstallPart` | (renamed) thiscall(g_FECarSession, partSlot, part): installs on the RideInfo (+0x940), redraws the car, `CarRecord_FromRideInfo`. Visual pick on UI_ChooseUniquePart. | Decompiled. |
| `0x005289F0` | `Career_ChargeForPart` | (renamed) thiscall(g_Career, partSlot, part): cash (+0x7134) −= part price (min 0), slot +0x7F0 (spent) += price. So visual uniques **cost money** (as in the career). | Decompiled. |
| `0x00527780` | `Career_FindActiveRaceByTrigger` | (renamed) WorldTrigger_Classify (`0x00527800`, CALL `0x0052781C`) checks active races first: an active race (g_Career+0x71C8) whose +0x28 == the trigger wins over a shop, showcase or world challenge on the same trigger. STAGE_2_VISUAL_1 (hoods) starts at **EVENT_LOCATOR_01**, a race marker Endless Career may use; UniqueParts then skips the hood that time. | Decompiled. |
| `0x00541210` | `Showcase_PayPhoto` | (renamed) thiscall(g_Career, showcase), RET 4: cash += showcase+0x26 (int16, 500 for all) + trunc(stars × short [stage record(current stage)+0x100 → +4] + 0.005f); `HUD_QueueShowcaseCashMessage` (`0x004AC170`, the reward text shows base + bonus as one total via `0x004A9FF0`); `0x00541090` (stage record's cover list). Per-star pay (GCareerStage +4): stages 0-1 0, 2-5 **100**. Only CALL **`0x00541941`** in `Showcase_PayAndSetCurrent` (`0x00541930`, called by the auto photo `0x00498EAA` and the posed photo `0x004CE9F4`). **Hooked by Covers** (CashPerStar). | Decompiled + disassembled + GLOBALB. |
| `0x00528410` | `Career_GetCurrentCarVisualRating` | thiscall(g_Career) → ST0 float stars, plain RET (`0x005284AD`). | Disassembled. |
| `0x005322E0` | `Career_SetupRaceOptions` | Race options +0x08 (`0x0089E7A8`) = **GRace +0x36 TrafficDensity** (`0x0053235C`). | Decompiled. |
| `0x004FE9F0` | `Race_SpawnTraffic` | (renamed) density 0 → no traffic; else cars = char [TrackInfo(track) + 0xEC + reverse + density×2] (per-track counts per level and direction), max 16. Stock races: density 0/1/2 only (circuits 32×0, 47×1, 1×2; sprints/drags/Street X almost all 1; drifts 9×0, 27×2). | Decompiled + GLOBALB tally. |
| `0x0049ABD0` | `FEAudioOptions_DrawAudioMode` | Value text at CALL `0x0049ABFC` (`FUN_0050C900(textObj, 0x55DA8BF8 "Stereo")`, cdecl, `ADD ESP,8`). **Hooked by Surround** (0.2.7, `[Surround.Audio] MenuLabel`): after the game's setter, FEString_SetText writes "5.1 Surround" / "Quad" when the last mode the sound library accepted is 5.1 / quad. | Disassembled. |

Shops of the challenges: CC_PERF_SHOP `0x4E79CD0A` (perf), CC_BODYSHOP_1 `0x2033AE7C` (hoods, rims, spoilers,
widebody), CC_GRAPHICS `0xCB1B31D5` (vinyls); all City Core, stage 1.

## Accepted exes (2026-10-19)
- `878678327c1aa744f2fe248cba081c86`: the exe all analysis was done on.
- `6445adbf4e8d64be51e96a69bba168a4`: HOODLUM's v1.2 No-CD. Same size; only the PE header (< 0x1000) differs, all
  sections byte-identical (verified with a byte compare on the Mac), so every address here applies unchanged.
