# Data structures

Offsets in hex. "?" = not known yet. Static analysis only (speed2.exe v1.2); nothing confirmed in-game yet.

## g_CarDB (global at 0x0083AD90)

| Offset | Size | What | Confidence |
|---|---|---|---|
| +0x0000 | up to 48 × 0x1C | Car-type entries (entry+0x08 = key compared in `PutCarInCareerSlot`) | Medium |
| +0x0540 | 4 | Count of the above | Medium |
| +0x0544 | 20 × 0x434 | Car records (kind 2) | Medium |
| +0x5954 | 5 × 0x7F8 | **Career slots** `CAREER_SLOT_0..4` (kind 4) | Strong |
| +0x812C | 4 | Career slot count (= 5) | Strong |
| +0x9BD8 | 91 × 4 | Pointers to all car records (walked by `CarDB_NextRecordOfKind`) | Strong |

## Car record / career slot

A career slot (0x7F8 bytes) = a 0x434-byte car record + a 0x3C0-byte block + flags. Plain records are 0x434 bytes and have kind 2.

**The slot is plain data apart from the vtable.** The save code stores memory +0x004..+0x7F5 and never fixes up pointers on load, so
copying those bytes captures a whole car. Do not copy +0x000 (vtable, a code address); leave the game's value in place.

| Offset | Size | What | Confidence |
|---|---|---|---|
| +0x000 | 4 | vtable pointer (C++ object). Virtual method at vtable+4 returns the car-type index (into a table of 0x890-byte entries at `*0x008A1CCC`). | Strong |
| +0x004 | 4 | ? (init 0) | — |
| +0x008 | 4 | Name hash, e.g. StringHash("CAREER_SLOT_n") | Strong |
| +0x00C | 4 | float, computed when a car is put in. Maybe value/rating. | Guess |
| +0x010 | 4 | In use (non-zero = valid) | Strong (iterator skips 0) |
| +0x014 | 4 | Kind bitmask: 2 = plain record, 4 = career slot | Strong |
| +0x018 | | Start of the part `CarRecord_BuildRideInfo` reads (it gets ECX = record+0x18) | Strong |
| +0x020 | 4 | **Car model** = StringHash of its `CARS\` folder name (e.g. "240SX" = 0150FB80) | **Confirmed**: Chris's save decodes to 240SX, ESCALADE, ECLIPSE, LANCEREVO8, empty |
| +0x024 | 170 × 4 | **Parts**: one part key per part position 0–169 (0 = none). Each is resolved for the car model and installed on the 3D car. Paint/vinyls probably included. | Strong (code); real cars have 89–102 filled |
| +0x2CC | 37 × 4 | Copied verbatim into RideInfo+0x2F0. Real values are floats like 1.95, 2.0, 3.0. **Guess: tuning settings.** | Medium |
| +0x360 | 5 × 41 | Five byte-arrays, default value 100 (0x64). One is chosen by an index with 5 cases and copied to RideInfo+0x398. **Guess: tuning slider set per race type.** | Medium |
| +0x415..+0x433 | | ? | — |
| +0x434 | 0x3C0 | Start of the extra block (cleared by `maybe_CarCustomization_Clear`) | |
| +0x590 | 16 | 128-bit bitfield indexed by part number (checked by `0x005155A0`) | Medium |
| +0x6E0 | 68 × 4 | List of part IDs, 0-terminated (searched by `maybe_CarDB_AnyCareerCarHasPart`) | Medium |
| +0x7F0 | 4 | int: total cash spent on this car's parts | Strong |
| +0x7F4 | 1 | **Locked** (1 = locked) | **Confirmed in real save data** |
| +0x7F5 | 1 | **Occupied** (1 = has a car) | **Confirmed in real save data** |
| +0x7F6 | 2 | padding? | — |

## g_Career (global at 0x0085AD40), partial

| Offset | Address | What | Confidence |
|---|---|---|---|
| +0x7134 | 0x00861E74 | Cash (int) | Strong |
| +0x72D8 | | 8 stage records × 0x104 | Medium |
| +0x72D8 + n*0x104 + 0xE8 (= +0x73C0) | | Sponsor chosen for stage n (pointer; sponsor+0x0C = signing bonus, short) | Medium |
| +0x7AF8 | | Current stage index | Medium |
| +0x873D | 0x0086347D | byte, set to 1 when a car is bought into a career slot | — |
| +0x8740 | 0x00863480 | Hash of current career car slot | Strong |

## Save file (`AppData\Local\NFS Underground 2\<profile>\<profile>`, 54,966 bytes)

| File offset | Size | What |
|---|---|---|
| 0x00 | 4 | Magic `20CM` |
| 0x04 | 4 | File size (0xD6B6) |
| 0x08 | 4 | 8 (version?) |
| 0x0C | 4 | 0xD692 = size − 0x24 |
| 0x10 | 16 | `bb c0 87 f2` + 12 bytes that differ per profile. **Possibly a checksum/hash.** Unknown. Matters only if we ever edit save files directly. |
| 0x20 | 4 | 0xD692 again |
| 0x24 | 0x995E | **g_CarDB section** (CarDB_SaveToBuffer) |
| 0x9982 | … | Rest (career state etc.). The current-car hash (g_Career+0x8740) appears at 0xAD7F in profile `Chris`, preceded by bytes `01 01`. |

g_CarDB section layout (offsets from 0x24):

| Section offset | Count × size | Memory source |
|---|---|---|
| +0x0000 | 48 × 0x18 | type entries, memory entry +0x04..+0x1B |
| +0x0480 | 4 | g_CarDB+0x540 count |
| +0x0484 | 20 × 0x430 | plain records, memory +0x004..+0x433 |
| +0x5844 | 5 × 0x7F2 | career slots (below) |
| +0x801E | 4 | g_CarDB+0x812C slot count (5) |
| +0x8022 | 6 × 0x434 | records at g_CarDB+0x8134 (stride 0x438), memory +0x004..+0x437 |
| +0x995A | 4 | g_CarDB+0x9A80 |

Career slot in the save (0x7F2 bytes):

| Save offset | Memory offset | What |
|---|---|---|
| +0x000..+0x42F | +0x004..+0x433 | car record, straight copy |
| +0x430 | +0x7F4 | locked |
| +0x431 | +0x7F5 | occupied |
| +0x432..+0x7F1 | +0x434..+0x7F3 | 0x3C0 block. Same order as memory, **except**: save dword 0xA5 = mem dword 0xEF (+0x7F0 spent); save 0xA6–0xA7 = mem 0xA5–0xA6; save 0xA8–0xEB = mem 0xAB–0xEE (part list +0x6E0); save 0xEC–0xEF = mem 0xA7–0xAA. (Dword indexes from the block start.) |

Checked against two real saves: `Chris` (finished career: slots 0–3 occupied, slot 4 empty, all unlocked) and `NAME` (new career: slot 0 unlocked + empty, slots 1–4 locked).

## Career events (2026-10-12, events chat)

Field names come from Nikki (the library behind the Binary tool, `SpeedReflect/Nikki`), matching the names in Smol Improvement's scripts.
Offsets checked against the exe code (which offsets it reads) and the decoded GLOBALB file.

### GRace (race definition, 0x88 bytes, table at `*0x008383E0`)

| Offset | Size | Field | Notes |
|---|---|---|---|
| +0x00 | 2 | Name | offset into g_GCareerStrings, e.g. `S4_H_CIRCUIT_14` |
| +0x02 / +0x04 / +0x06 | 2 each | IntroMovie / OutroMovie / EventTrigger name | string offsets |
| +0x08 | 4 | **Key** = hash of the name. Used everywhere (status list, lookups). | Strong |
| +0x0C | 1 | UnlockMethod: 0 SpecificRaceWon, 1 AtStageStart, 2 ?, 3 counts, 4 ? | |
| +0x0D | 1 | IsSUVRace | code: SUV races only offered with an SUV |
| +0x0F | 1 | EventBehavior: 0 Circuit, 1 Sprint, 2 StreetX, 3 OpenWorld, 4 Drag, 5 Drift | code switch matches |
| +0x10 | 4 | Requirement (key of required race, or counts) | depends on UnlockMethod; final race: +0x10 = required race key |
| +0x14 | 4 | EarnableRespect | |
| +0x18 | 4 × 4 | Up to 4 legs: {u16 TrackID, u8 reverse, u8 laps} | NumLaps_Stage1..4 in the mod |
| +0x28 | 4 | **EventTrigger hash** = where on the map the marker sits | used by the marker builder |
| +0x2C | 4 | PlayerCarType (forced car, rare) | |
| +0x30 | 4 | **CashValue** (payout) | paid by `Career_OnRaceWon` |
| +0x34 | 1 | EventIcon: 0 none, 1 sponsor, 2 URL, 3 regular | |
| +0x35 | 1 | IsDriveToGPS | |
| +0x36 | 1 | TrafficDensity | |
| +0x37 | 1 | **BelongsToStage** (0–5) | |
| +0x38 | 1 | NumMapItems | |
| +0x3C | 4 | GPSDestination hash | |
| +0x40 | 5 × 12 | Opponents: {u16 name, u8 **mode** (0 = fixed car level, 3 = level from the current stage), u8 **level** (0..10, used in mode 0), u32 PresetRide hash ("RANDOM" = random car), u8 SkillEasy, SkillMedium, SkillHard, CatchUp}. Nikki calls mode+level one u16 "StatsMultiplier". Stock: mode 3 almost everywhere (mode 1 in stage 0). Skill E/M/H: stage 0 0/2/5, stage 2 0/4/7, stage 5 up to 2/6/9. | Confirmed by code (`Race_SetupOpponents`) |
| +0x7C | 1 | NumOpponents | |
| +0x7E | 1 | NumStages (legs) | |
| +0x7F | 1 | **IsHiddenEvent** | Smol Improvement sets this to 0 for 33 races |

Stock file: 224 races; by stage 0:5, 1:5, 2:35, 3:48, 4:62, 5:69; 33 hidden.

### GCareerStage (stage definition, 0x50 bytes, 6 records)

| Offset | Field | Stock values (stages 0–5) |
|---|---|---|
| +0x00 | Stage ID (byte) | 0..5 |
| +0x01 | NumberOfSponsorsToChoose | 0,0,5,5,5,5 |
| +0x02 | OutrunCashValue (short) | 100,100,100,100,250,400 |
| +0x08..+0x1B | 5 sponsor hashes | |
| +0x28 | **LastStageEvent** key | DDAY_EVENT_B, none, S2_URL_3, S3_URL_8, S4_URL_7, **S5_CIRCUIT_36** |
| +0x30..+0x34 | MaxCircuits/Drags/StreetX/Drifts/Sprints ShownOnMap | 3,1,2,3,4,4 (each) |
| +0x40 | MaxOutrunEvents | |

### g_Career event fields (g_Career = 0x0085AD40)

| Offset | Address | What |
|---|---|---|
| +0x0000 | 0x0085AD40 | World-map marker array, 64 × 0x18 (built by `Career_RebuildMapMarkers`) |
| +0x0C08 | | non-zero → no events rebuilt (some special mode; unknown) |
| +0x7148 | | Event-list sub-object (helpers use ECX = this) |
| +0x71C8 | 0x00861F08 | **Active events on the map**: 64 GRace pointers |
| +0x72C8 | | 8 per-category counts of active events (bytes) |
| +0x72D8 + n×0x104 | | Stage record n (8 records, only 0–5 real). +0xE8 sponsor, +0xFC **stage complete** (byte), +0x100 pointer to GCareerStage data |
| +0x78EC | | = stage 5 record +0x100: used by `Career_IsFinished` |
| +0x7AF8 | 0x00862838 | Current stage (0–5; **6 after the career is beaten**) |
| +0x7AFC | 0x0086283C | Event status count |
| +0x7B00 | 0x00862840 | Event status list, 384 × 8 bytes, saved verbatim |
| +0x8700 | | Last map marker the player entered |

Event status entry (8 bytes):

| Offset | Size | What |
|---|---|---|
| +0 | 4 | Key (race key, or a showcase/challenge key) |
| +4 | 1 | Discovered: 0/1/2 (1 = driven into the marker; 2 used for showcases) |
| +6 | 1 | **Status** (confirmed in-game 2026-10-13): 0 none; **2 = race in progress** (set ~2 s after driving into the marker, when the race starts; DDAY_EVENT_A stays 2 until the prologue ends); **1 = won** (2 → 1 at the finish, same moment cash += CashValue); 5 = world shop found (keys of the 0xA0 shop records, see `0x00500460`; NOT showcases: a done DVD/magazine is 1); 4 = set by `0x00528500` (meaning unknown, not seen at race start); 3 seen once |

Save file: the career section's stage records are 0x101 bytes each (0xFD bytes + 4-byte stage ID, which is resolved to the +0x100 pointer on load;
the unused records 6 and 7 have ID 0, so they point at **stage 0's data**). In profile `Chris` the career tail starts at file offset 0xA16A
(cash 253,170; stage 6; 250 status entries; last entry S5_CIRCUIT_36 = won). Stage records start at 0xA16A − 8×0x101 = 0x9962
(these decode cleanly: IDs 0–5, complete flags, sponsors). **Note for the garage section:** 0x9962 = 0x24 + 0x993E, the car-DB size used by
load mode 4, so the car-DB section is probably 0x993E bytes, not 0x995E as in the "Save file" table above. Needs a re-check by the garage chat.

## Menu engine (FEng) input, for plugins (garage chat, 2026-10-13)

FEng object (ECX of `FEng_ProcessPadsForPackage`): +0x08 pad entry array pointer, +0xD4 pad count, +0xE4 first open package.
Pad entry (0x88 bytes): +0x00 button bits last frame, +0x04 button bits this frame (pressed = cur & ~prev).
Bits: 0–3 directions, 4 ACCEPT, 5 BACK, 6 START, 7 LTRIGGER, 8 RTRIGGER, 9–18 BUTTON0–9.
FEPackage: +0x04 next, +0x0C name (char*, e.g. "GarageMain.fng"), +0x10 name hash, +0x24 mask of pads it listens to.
A press only becomes a menu message if the package's .fng has a responder for it (`FEng_ProcessPadsForPackage`), so plugins
read the raw bits instead. The .fng files are inside `FRONTEND\FrontB.lzc` (JDLZ-compressed; not decoded yet).

## Virtual garage store file (`virtual-garage\<profile>\NNNNNN_<CAR>.vgcar`)

64-byte header: magic "NFU2VGC1", u32 version (1), u32 car model hash, u32 slot size (0x7F8), char[32] profile name, 12 reserved bytes.
Then the whole 0x7F8-byte career slot as it was in memory. When restored, only +0x00C..+0x7F3 is copied (the target slot keeps its
vtable, +0x004, slot hash and locked flag) and occupied is set to 1. Lowest number = oldest = next to come out.

## Career car select screen (UICareerCribCarSelect, 0x1B0 bytes)

+0x00 vtable `0x00795D00`, +0x04 package name (char*), +0x18 FEPackage*, +0x4C first list entry link, +0x50 last, **+0x54 highlighted
entry**, +0x5C highlighted index. Entry links (+0x10 next, +0x14 prev) point at entry+0x10. Entry: +0x918 car type entry,
**+0x920 slot hash**, +0x930 / +0x931 flags. The entries are built when the screen opens, so after changing a slot the list is stale
until the screen is re-opened. Browsing does not change `g_Career_CurrentCarSlotHash`; ACCEPT + confirm does.

## Track info (GLOBALB chunk 0x34201, 0x128 bytes, `g_TrackInfos`) (events chat, 2026-10-15)

Field order from Nikki (`Support.Underground2/Class/Track.cs`), offsets checked against the stock file.

| Offset | Size | Field | Notes |
|---|---|---|---|
| +0x00 | 0x20 | RaceDescription | e.g. `(4011) Short Circuit #1` |
| +0x20 | 0x20 | TrackDirectory | `Location4\Track_4011` |
| +0x40 | 8 | RegionName | L4RA city, L4RB drag highways, L4RC drift lots/stadium, L4RD "organic" drags, L4RF street X, L4RG speedways |
| +0x48 | 0x20 | RegionDirectory | |
| +0x68 | 4 | LocationIndex | 4 = Bayview |
| +0x6C | 0x10 | LocationDirectory | |
| +0x7C | 4 | LocationType = **district code** | 0 Jackson Heights (hills, "SH"), 1 City Core, 2 Beacon Hill ("UC"), 3 Coal Harbor (industrial), 4 airport side, 5 special. Matches where the forward start line is, except street X (labels look swapped). |
| +0x80 | 4 | DriftType | 1 = downhill drift |
| +0x84 | 1 | IsValid | |
| +0x85 | 1 | IsLoopingRace, **inverted**: 0 = looping (circuit) | |
| +0x86 | 1 | ReverseVersionExists | 0 for all drifts and the XBL outrun tracks |
| +0x88 | 1 | IsPerformanceTuning | |
| +0x8A / +0x8C | 2 / 2 | TrackID (twice) | |
| +0x90 | 4 | SunInfo hash | |
| +0x94 | 4 | Race modes allowed (mask) | 0x71 circuit, 0x34 sprint, 0x02 drag, 0x08 drift, 0x800 street X, 0x2010 speedway |
| +0x98 | 4 | **RaceLength** | metres per lap (sprint/drag/downhill drift: whole race). 4011 = 1573, 4088 = 7344. |
| +0x9C.. | | time/score to beat, map calibration, difficulty, traffic, menu map zoom | see Nikki |

Race leg in the GRace record (+0x18, up to 4): `{u16 TrackID, u8 reverse, u8 laps}`; leg count at +0x7E.

## World trigger / race marker (PathsFreeRoam.bin chunk 0x3414A, in memory via `WorldTrigger_FindByHash`)

Variable-size records (+0x42 = record size, usually 0x64): +0x00 type (13 = race/shop/showcase marker), +0x04 x, +0x08 y,
+0x0C z?, +0x20 bounding box (4 floats), +0x30 **name hash** (StringHash of e.g. `EV_07`), +0x40 point count, +0x44 points.
196 type-13 markers in the stock city; 116 are used as race EventTriggers, the rest are shops/showcases etc.

## Track position markers (TrackPosMarkersAll.bin chunk 0x34146)

0x30-byte records after 8 bytes of 0x11 padding: +0x08 name hash, +0x10 x, +0x14 y, +0x18 z, +0x24 u16 TrackID.
Names: `ForwardStartLine`, `BackwardStartLine`, `ForwardStartingPosition`, `BackwardStartingPosition` (one per grid slot).

## Showcases: DVD and magazine covers (covers research, 2026-10-16)

### GShowcase (GCareer sub-chunk 0x34A16, 0x40 bytes, g_GShowcases `0x0083840C`, count `0x00838408` = 30)

Field names from Nikki `GShowcase.cs`; meanings checked in code. Full list: `docs/showcases.txt`.

| Offset | Size | Field | Notes |
|---|---|---|---|
| +0x00 | 0x20 | Name | `STAGE_2_DVD_1`, `STAGE_3_SHOW_2_PICTURE` (SHOW = magazine) |
| +0x20 | 4 | Key | StringHash(name); used in the event status list |
| +0x24 | 1 | TakePhotoType | 1 magazine / 2 DVD with the **player posing the camera**; 3 magazine / 4 DVD taken automatically |
| +0x25 | 1 | BelongsToStage | 2-5 (no covers in stages 0-1) |
| +0x26 | 2 | CashValue | 500 for all |
| +0x28 | 4 | Desc hash | title/body text = this hash continued over "_TITLE"/"_BODY"; also picks the cover art |
| +0x2C | 4 | DestinationPoint | world trigger hash (photo location marker) |
| +0x34 | 1 | unlock method | stock: 3 magazines, 4 DVDs. Code paths exist for 0/1/2 (unused). |
| +0x35 | 1 | RequiredToComplete | 1 for all DVDs → stage can't finish without them |
| +0x38 | 4 | DescAttrib / unlock key | 0x001A076F on all magazines, 0 on DVDs |
| +0x3C | 4 | RequiredVisualRating (float) | DVDs 1.0 … 10.0 stars; magazines 0 |

Stock set: 10 DVDs (stage 2: 1, 3: 2, 4: 3, 5: 4; star 1→10) and 20 magazines (2+4+6+8).

### Magazine unlock = WorldChallenge (sub-chunk 0x34A1B, 0x18 bytes, g_WorldChallenges `0x00838424`, 28)

`{u16 name, u16 trigger name (string offsets), u16 stage, u8 unlock method, u8 count, u32 SMS hash, u32 destination
(showcase key for type 4), i32 time limit (s), u8 type (1 visual part, 2 performance part, 4 showcase), u8 part ×3}`.
Unlock methods: 0 regular races won in the stage, 1 URL, 2 outruns, 3 sponsor races. Magazines: "win N regular races
in this stage" (2…34), then drive from TRIGGER_START_… to the photo location within 45-200 s.

### Status entries

A showcase uses the normal event status list (g_Career+0x7B00, 384 max): discovered = 2 when offered, status = 1 when
done. The gallery shows every showcase with status 1. A world challenge uses its own entry (name hash).

### Saved cover picture (save folder `<profile> Magazine N\<profile> Magazine N`, 1,048,612 bytes)

MC02 container (same as the profile save): +0x00 `'MC02'`, +0x04 file size, +0x08 size A (8), +0x0C size B (0x100000),
+0x10 checksum(A), +0x14 checksum(B), +0x18 checksum(header bytes 0..0x17) (`MC02_Checksum`); +0x1C A = {u32 0x10D, u32
0x100000}; +0x24 B = **512×512 pixels, 4 bytes each, B G R A, top row first**. The picture already has the magazine/DVD
art on it (it is a capture of the finished cover screen). Converter used for checking: decode BGRA → PNG.

## Unique performance parts (unique parts research, 2026-10-16)

### Per car (career slot, memory offsets; save = memory − 4)

The car record's 37-dword performance block (slot +0x2CC..+0x35F) is copied to RideInfo +0x2F0..+0x383
(`CarRecord_BuildRideInfo`) and back (`CarRecord_FromRideInfo`).

| Slot offset | RideInfo | Size | What |
|---|---|---|---|
| +0x2CC | +0x2F0 | 10 × f32 | per-category values (not mapped; floats like 1.95, 2.0, 3.0) |
| +0x2F4 + cat×4 | +0x318 + cat×4 | 10 × u32 | **unique part installed** (1) per category. Only writer `FECarSession_GrantUniquePerfPart`. |
| +0x31C | +0x340 | 17 × u32 | bytes, "part installed" flags (`0x00599610` reads RideInfo+0x340+part) |
| +0x590 | — | bit array | perf parts owned (bought) on this car (`PerfShop_IsPartOwned`) |

Categories (`Perf_GetCategoryNameHash`): 0 Weight Reduction, 1 Transmission, 2 Nitrous, 3 Engine, 4 Turbo,
5 Suspension, 6 Brakes, 7 ECU, 8 Tires, 9 unused.
Note: empty slots can still hold an old car's data (NAME save: empty slot 0 has an Engine flag), so check occupied first.

### Per profile

- Challenge status: event status entry keyed by StringHash(`STAGE_3_PERF_1` etc.): absent = never unlocked, 2 = started
  (failed timer or running), **1 = done (part picked or declined; can't tell which from the status)**.
- Active world challenges: g_Career+0x7148, 32 pointers into g_WorldChallenges (start of the event-list sub-object).
- Running challenge: g_Career+0xC00 (`0x0085B940`): +0 flag, +4 timer (s), +8 challenge, +0xC list of offered items
  (0x40 max), +0x10C 1 = performance, +0x110 item count, +0x114 slot type (visual), +0x118 ?, +0x11C state
  (1 running, 2 ?, 4 at the destination shop, 5 completing, 6/7/8 abort/cancel/restart paths).

### WorldChallenge record (0x18 bytes), unique-part fields

+0x14 type (1 visual, 2 performance, 4 showcase), +0x15..+0x17 part ids (255 = none; perf ids 1-10, see
`UniquePerf_IdToCategory`; visual = part-slot ids 11, 12, 13, 22, 62, not mapped), +0x0C destination = **shop key** for types 1/2.

### Shop record (GCareer 0x34A12, 0xA0 bytes, `0x008383F0`, 24)

+0x00 name, +0x20 scene name, +0x38 key, +0x3C trigger hash, +0x50 type (0 crib, 1 graphics, 2 body, 3 perf,
4 car lot, 5 car specialty, 6 uniques), +0x51 must be found (status 5) before the marker shows, +0x9D stage it appears.
