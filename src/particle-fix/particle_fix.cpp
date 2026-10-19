// Particle Fix: one fix plus a frame-timing log.
//
// Fix ([ParticleFix.Fix] BatchAppend): SpriteBatch_Begin (0x005BB320) is replaced by a JMP. The game locks its dynamic
// particle vertex buffer with DISCARD for every sprite batch (hundreds per frame with a full pool), which can stall
// the driver; the replacement appends batches (NOOVERWRITE) and discards only when the buffer is full.
// Timing log ([Debug] ParticleFix sections): the particle system's CALLs (update, render, oriented draw, batch
// lock/flush, rain) are timed, and stack-neutral clock stubs on the main loop's CALLs split each frame into sections.
// Frames over SpikeMs are logged with their breakdown, labeled "waiting for the graphics card" when the time sits in
// draw/Present. The game thread only fills memory; the log thread writes. The frame rate limit is never touched.
// Never hook Game_MainLoop's CALLs 0x005811EE / 0x005812ED: WidescreenFix hooks them (tools/check_wsf_overlap.py).
// Undo: [Features] ParticleFix = 0.

#define BF_TAG "ParticleFix"
#include "nfsu2.h"
#include <d3d9.h>

using namespace nfsu2;

namespace {

double g_spikeMs = 25.0;      // [ParticleFix.Log] SpikeMs
int g_summarySeconds = 10;    // [Log] SummarySeconds
bool g_timingHooks = true;    // [Test] TimingHooks
bool g_batchAppend = true;    // [Fix] BatchAppend
bool g_mainLoopClock = true;  // [Test] HookMainLoop
// Per-hook switches, in the order of the hooks[] table in WorkerThread ([Test] keys).
const char* const kHookKeys[] = {"HookRenderFrame", "HookUpdate", "HookRenderMain", "HookRenderView3",
                                 "HookOrientedDraw", "HookBatchBegin", "HookBatchFlush", "HookRain"};
const int kNumHooks = sizeof(kHookKeys) / sizeof(kHookKeys[0]);
bool g_hookOn[kNumHooks];
volatile LONG g_hookRuns[kNumHooks];  // calls per hook (game thread increments, log thread reads and resets)

const CodeCheck kCodeChecks[] = {
    {0x005814D6, {0xE8, 0x75, 0x13, 0x05, 0x00}, 5, "Game_MainLoop: CALL Render_Frame"},
    {0x005D2CA0, {0xE9, 0x9B, 0xDC, 0xFE, 0xFF}, 5, "Render_Frame: tail JMP 0x005C0940 (cdecl, no args)"},
    {0x005814DB, {0xE8, 0xE0, 0x0D, 0xE8, 0xFF, 0xE8, 0xDB, 0x0D}, 8, "Game_MainLoop: CALL empty stub; CALL empty stub"},
    {0x004022C0, {0xC3}, 1, "empty stub: plain RET"},
    {0x0060A17C, {0xB9, 0xE0, 0x57, 0x83, 0x00, 0xE8, 0x8A, 0x8C}, 8, "world update: mov ecx,list; CALL Particles_Update"},
    {0x00493381, {0xC2, 0x04, 0x00}, 3, "Particles_Update: RET 4"},
    {0x005CAEBE, {0xB9, 0xE0, 0x57, 0x83, 0x00, 0xE8, 0x28, 0x0F}, 8, "Render_World: mov ecx,list; CALL Particles_Render (view 3)"},
    {0x005CB537, {0xB9, 0xE0, 0x57, 0x83, 0x00, 0xE8, 0xAF, 0x08}, 8, "Render_World: mov ecx,list; CALL Particles_Render (main)"},
    {0x0048CD2D, {0xC2, 0x04, 0x00}, 3, "Particles_Render: RET 4"},
    {0x0048CC54, {0xE8, 0x27, 0xB2, 0x13, 0x00}, 5, "Particles_Render: CALL oriented particle draw"},
    {0x005C7E94, {0xE8, 0xE7, 0xD6, 0xFF, 0xFF, 0x83, 0xC4, 0x10}, 8, "oriented draw thunk: CALL Particle_DrawOrientedQuadUP"},
    {0x005C7E9C, {0xC2, 0x14, 0x00}, 3, "oriented draw thunk: RET 0x14"},
    {0x005C0E53, {0xB9, 0xA4, 0xB7, 0x86, 0x00, 0x89, 0x7C, 0x24}, 8, "sprite batch: mov ecx,0x0086B7A4"},
    {0x005C0E5C, {0xE8, 0xBF, 0xA4, 0xFF, 0xFF}, 5, "sprite batch: CALL SpriteBatch_Begin"},
    {0x005BB3A3, {0xC2, 0x04, 0x00}, 3, "SpriteBatch_Begin: RET 4"},
    {0x005CE14F, {0xB9, 0xA4, 0xB7, 0x86, 0x00, 0xE8, 0x87, 0x76}, 8, "particle batch end: mov ecx,batch; CALL SpriteBatch_Flush"},
    {0x005C589A, {0xC3}, 1, "SpriteBatch_Flush: plain RET"},
    {0x005CB465, {0x8B, 0x48, 0x64, 0x83, 0xC4, 0x04, 0xE8, 0xB0}, 8, "Render_World: mov ecx,[eax+0x64]; CALL Rain_UpdateAndDraw"},
    {0x0061F1E0, {0xC3}, 1, "Rain_UpdateAndDraw: plain RET"},
    // SpriteBatch_Begin, replaced by a JMP at its start (BatchAppend). The fields the replacement writes:
    {0x005BB320, {0x8B, 0x44, 0x24, 0x04, 0x53, 0x56, 0x8B, 0xF1}, 8, "SpriteBatch_Begin: mov eax,[esp+4]; push ebx; push esi; mov esi,ecx"},
    {0x005BB32B, {0x88, 0x5E, 0x21, 0x8B, 0x38, 0x8B, 0x46, 0x0C}, 8, "SpriteBatch_Begin: +0x21 = 1; n = *arg; cap = +0x0C"},
    {0x005BB34C, {0x88, 0x5E, 0x30, 0x89, 0x7E, 0x14, 0x89, 0x56}, 8, "SpriteBatch_Begin: +0x30 = 1; +0x14 = n; +0x18 = 0"},
    {0x005BB358, {0x68, 0x00, 0x28, 0x00, 0x00, 0x8D, 0x6C, 0x24}, 8, "SpriteBatch_Begin: Lock flags 0x2800 (DISCARD|NOSYSLOCK)"},
    {0x005BB362, {0x8B, 0x6E, 0x1C, 0x0F, 0xAF, 0xEF, 0x55, 0x52}, 8, "SpriteBatch_Begin: size = stride(+0x1C) * n; offset 0"},
    {0x005BB36A, {0x88, 0x56, 0x21, 0x89, 0x56, 0x10, 0x89, 0x54}, 8, "SpriteBatch_Begin: +0x21 = 0; cursor +0x10 = 0"},
    {0x005BB374, {0x8B, 0x01, 0x51, 0xFF, 0x50, 0x2C, 0x85, 0xC0}, 8, "SpriteBatch_Begin: vb->Lock (vtable +0x2C)"},
    {0x005BB37F, {0x8B, 0x46, 0x10, 0x89, 0x46, 0x18, 0x03, 0xC7}, 8, "SpriteBatch_Begin: start = cursor; cursor += n"},
    {0x005BB38E, {0x8D, 0x0C, 0x7F, 0x8D, 0x14, 0xC8, 0x5F, 0x88}, 8, "SpriteBatch_Begin: end = data + n*24"},
    {0x005BB395, {0x88, 0x5E, 0x20, 0x89, 0x46, 0x24, 0x89, 0x46}, 8, "SpriteBatch_Begin: locked = 1; +0x24 = +0x2C = data"},
    {0x005BB39D, {0x2C, 0x89, 0x56, 0x28, 0x5E, 0x5B, 0xC2, 0x04}, 8, "SpriteBatch_Begin: +0x28 = end; RET 4"},
    {0x005C582D, {0x8B, 0x47, 0x18, 0x8B, 0x5F, 0x14, 0xC1, 0xE8}, 8, "SpriteBatch_Flush: start +0x18, count +0x14; start >> 2"},
    {0x005C5866, {0x8B, 0x47, 0x14, 0x8B, 0x4F, 0x18, 0x8B, 0x16}, 8, "SpriteBatch_Flush: DrawIndexedPrimitive(MinIndex start, NumVertices count)"},
    // Main-loop clock stubs (HookMainLoop): the CALLs in Game_MainLoop, in the order they run. Not 0x005811EE /
    // 0x005812ED: the 2025 WidescreenFix hooks those (its frame limiter and delta time).
    {0x00581301, {0xE8, 0x3A, 0x56, 0xED, 0xFF}, 5, "Game_MainLoop: CALL 0x00456940"},
    {0x005813CA, {0xE8, 0x91, 0x8F, 0x06, 0x00}, 5, "Game_MainLoop: CALL 0x005EA360 (sim dt)"},
    {0x00581407, {0xE8, 0xD4, 0x88, 0x08, 0x00}, 5, "Game_MainLoop: CALL 0x00609CE0 (world update)"},
    {0x00581416, {0xE8, 0xF5, 0x79, 0xFF, 0xFF}, 5, "Game_MainLoop: CALL 0x00578E10 (after world)"},
    {0x00581484, {0xE8, 0x57, 0x9A, 0x08, 0x00}, 5, "Game_MainLoop: CALL 0x0060AEE0"},
    {0x005814CC, {0xE8, 0x7F, 0xD3, 0x04, 0x00}, 5, "Game_MainLoop: CALL 0x005CE850"},
    {0x005814D1, {0xE8, 0xCA, 0xD3, 0x04, 0x00}, 5, "Game_MainLoop: CALL 0x005CE8A0 (new mesh vertex buffers)"},
};

// ---- Timing ------------------------------------------------------------------

double g_msPerTick = 0.0;

inline LONGLONG Now() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}
inline double Ms(LONGLONG ticks) { return static_cast<double>(ticks) * g_msPerTick; }

// One frame's particle costs (game thread only).
// Main-loop sections, one per clock stub (each runs until the next stamped CALL; the last one, "draw", until the
// end of Render_Frame). Section 9 is Render_Frame itself.
// "tail" runs from the end of the previous Render_Frame to the first stamp: the rest of the loop, its top and the
// frame limiter (the game's or another plugin's).
const int kNumStamps = 8;
const int kDrawStamp = kNumStamps - 1;
const char* const kSectionNames[kNumStamps] = {"presim", "simdt", "world", "postworld", "misc", "stream", "meshvb",
                                               "draw"};
struct FrameStats {
    LONGLONG render, update, partMain, partMirror, oriented, lock, lockMax, flush, rain;
    LONGLONG tail, section[kNumStamps];  // tail: end of the previous Render_Frame to the first stamp
    int orientedCount, batches, quads, aliveSprites, aliveOriented, appends, discards;
    bool inWorld;  // the world particle update ran this frame
};
FrameStats g_cur;

// Running summary (game thread fills, copied out under the lock).
struct Summary {
    int frames, spikes, gpuSpikes;
    double frameMs, frameMax, renderMs, updateMs, partMs, orientedMs, lockMs, lockMax, flushMs, rainMs;
    double tailMs, sectionMs[kNumStamps];
    int orientedMax, batchesMax, aliveMax, appends, discards;
};
Summary g_sum;

// Hand-over to the log thread.
CRITICAL_SECTION g_lock;
const int kMaxPending = 64;
const int kLineLen = 420;
char g_pending[kMaxPending][kLineLen];
int g_pendingCount = 0;
int g_dropped = 0;

void Queue(const char* fmt, ...) {
    char line[kLineLen];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    EnterCriticalSection(&g_lock);
    if (g_pendingCount < kMaxPending) memcpy(g_pending[g_pendingCount++], line, sizeof(line));
    else g_dropped++;
    LeaveCriticalSection(&g_lock);
}

void CountAlive(FrameStats& s) {
    const uintptr_t list = particles::kList;
    uintptr_t g = *reinterpret_cast<const uintptr_t*>(list);
    for (int n = 0; g && g != list && n < 10000; n++) {
        s.aliveSprites += *reinterpret_cast<const int*>(g + particles::kGroupSprites);
        s.aliveOriented += *reinterpret_cast<const int*>(g + particles::kGroupOriented);
        g = *reinterpret_cast<const uintptr_t*>(g);
    }
}

// Called at the start of each Render_Frame with the time since the previous one.
void EndFrame(LONGLONG interval) {
    const FrameStats& s = g_cur;
    if (s.inWorld) {
        const double frame = Ms(interval);
        const double part = Ms(s.partMain + s.partMirror);
        g_sum.frames++;
        g_sum.frameMs += frame;
        if (frame > g_sum.frameMax) g_sum.frameMax = frame;
        g_sum.renderMs += Ms(s.render);
        g_sum.updateMs += Ms(s.update);
        g_sum.partMs += part;
        g_sum.orientedMs += Ms(s.oriented);
        g_sum.lockMs += Ms(s.lock);
        if (Ms(s.lockMax) > g_sum.lockMax) g_sum.lockMax = Ms(s.lockMax);
        g_sum.flushMs += Ms(s.flush);
        g_sum.rainMs += Ms(s.rain);
        g_sum.tailMs += Ms(s.tail);
        for (int k = 0; k < kNumStamps; k++) g_sum.sectionMs[k] += Ms(s.section[k]);
        g_sum.appends += s.appends;
        g_sum.discards += s.discards;
        if (s.orientedCount > g_sum.orientedMax) g_sum.orientedMax = s.orientedCount;
        if (s.batches > g_sum.batchesMax) g_sum.batchesMax = s.batches;
        if (s.aliveSprites + s.aliveOriented > g_sum.aliveMax) g_sum.aliveMax = s.aliveSprites + s.aliveOriented;
        if (frame > g_spikeMs) {
            g_sum.spikes++;
            char sections[200];
            int len = snprintf(sections, sizeof(sections), "tail %.1f", Ms(s.tail));
            for (int k = 0; k < kNumStamps; k++)
                len += snprintf(sections + len, sizeof(sections) - len, " %s %.1f", kSectionNames[k], Ms(s.section[k]));
            // Time in draw (D3D calls) + tail (Present / limiter) with little game work = waiting for the GPU.
            const double cpu = part + Ms(s.rain) + Ms(s.update) + Ms(s.section[0]) + Ms(s.section[1]) +
                               Ms(s.section[2]) + Ms(s.section[3]) + Ms(s.section[4]) + Ms(s.section[5]) + Ms(s.section[6]);
            const bool gpu = Ms(s.section[kNumStamps - 1]) + Ms(s.tail) >= frame * 0.75 && cpu < 5.0;
            if (gpu) g_sum.gpuSpikes++;
            Queue("SPIKE %.1f ms (%s): %s | particles main %.2f + view3 %.2f (oriented %d = %.2f, batches %d/%d quads, "
                  "lock %.2f max %.2f, %d append %d discard, flush %.2f), rain %.2f, particle update %.2f | alive %d + %d",
                  frame, gpu ? "waiting for the graphics card" : "game work", sections, Ms(s.partMain), Ms(s.partMirror), s.orientedCount, Ms(s.oriented), s.batches,
                  s.quads, Ms(s.lock), Ms(s.lockMax), s.appends, s.discards, Ms(s.flush), Ms(s.rain), Ms(s.update),
                  s.aliveSprites, s.aliveOriented);
        }
    }
    memset(&g_cur, 0, sizeof(g_cur));
}

void MaybeQueueSummary(LONGLONG now) {
    static LONGLONG last = 0;
    if (!last) last = now;
    if (Ms(now - last) < g_summarySeconds * 1000.0) return;
    last = now;
    const Summary& s = g_sum;
    if (s.frames > 0) {
        const double n = s.frames;
        char sections[200];
        int len = snprintf(sections, sizeof(sections), "tail %.1f", s.tailMs / n);
        for (int k = 0; k < kNumStamps; k++)
            len += snprintf(sections + len, sizeof(sections) - len, " %s %.1f", kSectionNames[k], s.sectionMs[k] / n);
        Queue("Summary %d frames: avg %.1f fps, worst frame %.1f ms, %d spikes > %.0f ms (%d waiting for the graphics "
              "card) | avg ms: %s | particles %.2f "
              "(oriented %.2f, lock %.2f [max %.2f], flush %.2f), rain %.2f, update %.2f | batches %d append %d discard "
              "| max: %d oriented drawn, %d batches, %d alive",
              s.frames, 1000.0 * n / s.frameMs, s.frameMax, s.spikes, g_spikeMs, s.gpuSpikes, sections, s.partMs / n,
              s.orientedMs / n, s.lockMs / n, s.lockMax, s.flushMs / n, s.rainMs / n, s.updateMs / n, s.appends,
              s.discards, s.orientedMax, s.batchesMax, s.aliveMax);
    }
    memset(&g_sum, 0, sizeof(g_sum));
}

// ---- Hooks (all run on the game's main thread) --------------------------------

}  // namespace

// Stack-neutral clock stubs for CALLs in Game_MainLoop (stamp 7 = Render_Frame, 0x005814D6): save registers, store the
// clock in g_stamp[k], restore, JMP to the game's function. Must stay stack-neutral: a C++ wrapper around
// Render_Frame broke the main menu layout.
extern "C" void* g_qpc;
extern "C" LARGE_INTEGER g_stamp[8];
extern "C" uintptr_t g_stampTarget[8];
void* g_qpc = reinterpret_cast<void*>(&QueryPerformanceCounter);
LARGE_INTEGER g_stamp[8];
uintptr_t g_stampTarget[8];  // filled before each CALL is redirected
#define STAMP_STUB(k)                                    \
    ".globl _Stamp" #k "\n"                              \
    "_Stamp" #k ":\n"                                    \
    "\tpushal\n"                                         \
    "\tpushfl\n"                                         \
    "\tpushl $(_g_stamp+" #k "*8)\n"                     \
    "\tcall *_g_qpc\n" /* stdcall: pops its argument */ \
    "\tpopfl\n"                                          \
    "\tpopal\n"                                          \
    "\tjmp *(_g_stampTarget+" #k "*4)\n"
asm(".text\n" STAMP_STUB(0) STAMP_STUB(1) STAMP_STUB(2) STAMP_STUB(3) STAMP_STUB(4) STAMP_STUB(5) STAMP_STUB(6)
        STAMP_STUB(7));
extern "C" void Stamp0();
extern "C" void Stamp1();
extern "C" void Stamp2();
extern "C" void Stamp3();
extern "C" void Stamp4();
extern "C" void Stamp5();
extern "C" void Stamp6();
extern "C" void Stamp7();

namespace {

typedef void(__thiscall* ListViewFn)(void* list, int view);
typedef void(__thiscall* UpdateFn)(void* list, float dt);
typedef void(__stdcall* OrientedFn)(void* verts, void* texture, void* matrix, int a, int b);
typedef int(__thiscall* BatchBeginFn)(void* batch, int* quads);
typedef void(__thiscall* ThisOnlyFn)(void* self);

// Replaces the empty CALL 0x004022C0 right after Render_Frame (0x005814DB): closes the frame. Runs in the main loop,
// outside any game function, so nothing of the game's sits on top of this code's stack.
void __cdecl Hook_AfterRender() {
    g_hookRuns[0]++;
    static LONGLONG lastEnd = 0;
    const LONGLONG end = Now();
    g_cur.render = end - g_stamp[kDrawStamp].QuadPart;
    // Sections from the stamps taken since the last frame end (stubs on branches not taken this frame are skipped).
    LONGLONG prev = lastEnd;
    int prevK = -1;
    for (int k = 0; k < kNumStamps; k++) {
        const LONGLONG t = g_stamp[k].QuadPart;
        if (!lastEnd || t <= lastEnd || t > end) continue;
        if (prevK < 0) g_cur.tail = t - prev;
        else g_cur.section[prevK] = t - prev;
        prev = t;
        prevK = k;
    }
    if (prevK >= 0) g_cur.section[prevK] = end - prev;
    if (lastEnd) EndFrame(end - lastEnd);
    else memset(&g_cur, 0, sizeof(g_cur));
    lastEnd = end;
    MaybeQueueSummary(end);
}

void __fastcall Hook_Update(void* list, void*, float dt) {
    g_hookRuns[1]++;
    const LONGLONG t = Now();
    reinterpret_cast<UpdateFn>(particles::kUpdate)(list, dt);
    g_cur.update += Now() - t;
    g_cur.inWorld = true;
    g_cur.aliveSprites = g_cur.aliveOriented = 0;
    CountAlive(g_cur);
}

void __fastcall Hook_RenderMain(void* list, void*, int view) {
    g_hookRuns[2]++;
    const LONGLONG t = Now();
    reinterpret_cast<ListViewFn>(particles::kRender)(list, view);
    g_cur.partMain += Now() - t;
}

void __fastcall Hook_RenderMirror(void* list, void*, int view) {
    g_hookRuns[3]++;
    const LONGLONG t = Now();
    reinterpret_cast<ListViewFn>(particles::kRender)(list, view);
    g_cur.partMirror += Now() - t;
}

void __stdcall Hook_OrientedDraw(void* verts, void* texture, void* matrix, int a, int b) {
    g_hookRuns[4]++;
    const LONGLONG t = Now();
    reinterpret_cast<OrientedFn>(particles::kOrientedDraw)(verts, texture, matrix, a, b);
    g_cur.oriented += Now() - t;
    g_cur.orientedCount++;
}

int __fastcall Hook_BatchBegin(void* batch, void*, int* quads) {
    g_hookRuns[5]++;
    const LONGLONG t = Now();
    const int r = reinterpret_cast<BatchBeginFn>(particles::kBatchBegin)(batch, quads);
    const LONGLONG d = Now() - t;
    g_cur.lock += d;
    if (d > g_cur.lockMax) g_cur.lockMax = d;
    g_cur.batches++;
    if (quads) g_cur.quads += *quads;
    return r;
}

// Replacement for SpriteBatch_Begin (thiscall(batch, int* quads), RET 4), reached by a JMP at its start.
// Same field writes as the game's code (see the CodeChecks), but instead of Lock(0, n, DISCARD) for every batch
// it appends: Lock(cursor, n, NOOVERWRITE) while the batch fits after the previous ones in this buffer, DISCARD
// (and back to 0) only when it doesn't. SpriteBatch_Flush already draws from +0x18 (start), so nothing else changes.
typedef HRESULT(__stdcall* VBLockFn)(void* vb, UINT offset, UINT size, void** data, DWORD flags);
inline unsigned& U32(unsigned char* b, size_t off) { return *reinterpret_cast<unsigned*>(b + off); }
inline unsigned char*& Ptr(unsigned char* b, size_t off) { return *reinterpret_cast<unsigned char**>(b + off); }

int __fastcall New_BatchBegin(unsigned char* b, void*, int* quads) {
    b[0x21] = 1;
    unsigned n = static_cast<unsigned>(*quads) << 2;  // vertices
    const unsigned cap = U32(b, 0x0C);
    if (n > cap) n = cap;
    void* vb = Ptr(b, 0x00);
    b[0x30] = 1;
    U32(b, 0x14) = n;
    U32(b, 0x18) = 0;
    if (!vb) return 0;
    const unsigned stride = U32(b, 0x1C);
    unsigned offset = U32(b, 0x10);
    DWORD flags;
    if (g_batchAppend && n > 0 && offset > 0 && offset + n <= cap) {  // n = 0: the game locks the whole buffer
        flags = 0x1000 | 0x800;  // D3DLOCK_NOOVERWRITE | D3DLOCK_NOSYSLOCK
        g_cur.appends++;
    } else {
        offset = 0;
        flags = 0x2000 | 0x800;  // D3DLOCK_DISCARD | D3DLOCK_NOSYSLOCK (the game's flags)
        g_cur.discards++;
    }
    b[0x21] = 0;
    U32(b, 0x10) = 0;
    void* data = nullptr;
    const VBLockFn lock = (*reinterpret_cast<VBLockFn* const*>(vb))[11];  // IDirect3DVertexBuffer9::Lock
    if (lock(vb, offset * stride, n * stride, &data, flags) != 0) return static_cast<int>(reinterpret_cast<uintptr_t>(data));
    U32(b, 0x18) = offset;
    U32(b, 0x10) = offset + n;
    b[0x20] = 1;
    Ptr(b, 0x24) = static_cast<unsigned char*>(data);
    Ptr(b, 0x2C) = static_cast<unsigned char*>(data);
    Ptr(b, 0x28) = static_cast<unsigned char*>(data) + n * 24;
    return static_cast<int>(reinterpret_cast<uintptr_t>(data));
}

bool WriteJump(uintptr_t at, const void* to) {
    unsigned char code[5] = {0xE9};
    const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - (at + 5));
    memcpy(code + 1, &rel, 4);
    DWORD old;
    if (!VirtualProtect(reinterpret_cast<void*>(at), 5, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(reinterpret_cast<void*>(at), code, 5);
    VirtualProtect(reinterpret_cast<void*>(at), 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(at), 5);
    return true;
}

void __fastcall Hook_BatchFlush(void* batch) {
    g_hookRuns[6]++;
    const LONGLONG t = Now();
    reinterpret_cast<ThisOnlyFn>(particles::kBatchFlush)(batch);
    g_cur.flush += Now() - t;
}

void __fastcall Hook_Rain(void* rain) {
    g_hookRuns[7]++;
    const LONGLONG t = Now();
    reinterpret_cast<ThisOnlyFn>(particles::kRain)(rain);
    g_cur.rain += Now() - t;
}

// ---- Settings ---------------------------------------------------------------

void LoadSettings() {
    char path[MAX_PATH];
    bf::IniPath(path);
    const bool found = bf::IniFound();
    int spike = static_cast<int>(GetPrivateProfileIntA("ParticleFix.Log", "SpikeMs", 25, path));
    g_spikeMs = spike >= 5 ? spike : 25;
    g_summarySeconds = static_cast<int>(GetPrivateProfileIntA("ParticleFix.Log", "SummarySeconds", 10, path));
    if (g_summarySeconds < 1) g_summarySeconds = 10;
    g_timingHooks = GetPrivateProfileIntA("ParticleFix.Test", "TimingHooks", 1, path) != 0;
    g_batchAppend = GetPrivateProfileIntA("ParticleFix.Fix", "BatchAppend", 1, path) != 0;
    g_mainLoopClock = GetPrivateProfileIntA("ParticleFix.Test", "HookMainLoop", 1, path) != 0;
    char hooks[160];
    int len = 0;
    for (int i = 0; i < kNumHooks; i++) {
        g_hookOn[i] = GetPrivateProfileIntA("ParticleFix.Test", kHookKeys[i], 1, path) != 0;
        len += snprintf(hooks + len, sizeof(hooks) - len, "%s%d", i ? " " : "", g_hookOn[i] ? 1 : 0);
    }
    Log("Settings (%s): BatchAppend=%d, SpikeMs=%.0f, SummarySeconds=%d, TimingHooks=%d, HookMainLoop=%d, "
        "hooks on [%s].", found ? "BayviewForever.ini" : "file not found, defaults", g_batchAppend ? 1 : 0, g_spikeMs,
        g_summarySeconds, g_timingHooks ? 1 : 0, g_mainLoopClock ? 1 : 0, hooks);
}

// ---- Threads ----------------------------------------------------------------

struct Hook {
    uintptr_t site, target;
    const void* fn;
    const char* what;
};

void LogStateChanges() {
    static int lastParticles = -1, lastRain = -1;
    const int p = *reinterpret_cast<const int*>(particles::kParticlesEnabled);
    const int r = *reinterpret_cast<const int*>(particles::kRainEnabled);
    if (p != lastParticles || r != lastRain) {
        Log("Video options: particle system %s, rain %s.", p ? "ON" : "off", r ? "ON" : "off");
        lastParticles = p;
        lastRain = r;
    }
}

// Frame timing values the cap touches (plain reads from this thread), every 5 s.
void LogTimingValues() {
    static int tick = 0;
    if (++tick % 5) return;
    Log("Frame time: %.5f s (game's own measure; the frame rate limit is not changed by this plugin).",
        *reinterpret_cast<const float*>(particles::kFrameDt));
    char runs[200];
    int len = 0;
    for (int i = 0; i < kNumHooks; i++)
        len += snprintf(runs + len, sizeof(runs) - len, " %s %ld", kHookKeys[i] + 4, InterlockedExchange(&g_hookRuns[i], 0));
    Log("Hook calls in the last 5 s:%s", runs);
}

void LogLoop() {
    static char lines[kMaxPending][kLineLen];  // same row size as g_pending (was 300: rows came out shifted/garbled)
    for (;;) {
        Sleep(1000);
        EnterCriticalSection(&g_lock);
        const int n = g_pendingCount;
        memcpy(lines, g_pending, sizeof(lines[0]) * n);
        g_pendingCount = 0;
        const int dropped = g_dropped;
        g_dropped = 0;
        LeaveCriticalSection(&g_lock);
        for (int i = 0; i < n; i++) Log("%s", lines[i]);
        if (dropped) Log("(%d more spike lines this second not logged)", dropped);
        LogStateChanges();
        LogTimingValues();
    }
}

// Logs the graphics card and the desktop mode (read-only, through a separate IDirect3D9 object from the d3d9.dll the
// game already loaded; the game's own device is never touched).
void LogGraphicsCard() {
    HMODULE d3d = GetModuleHandleA("d3d9.dll");
    typedef IDirect3D9*(WINAPI* CreateFn)(UINT);
    CreateFn create = d3d ? reinterpret_cast<CreateFn>(reinterpret_cast<void*>(GetProcAddress(d3d, "Direct3DCreate9"))) : nullptr;
    IDirect3D9* d = create ? create(D3D_SDK_VERSION) : nullptr;
    if (!d) {
        Log("Graphics card: unknown (d3d9.dll not loaded yet).");
        return;
    }
    D3DADAPTER_IDENTIFIER9 id = {};
    D3DDISPLAYMODE mode = {};
    if (SUCCEEDED(d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id))) {
        Log("Graphics card: %s (driver %s, version %u.%u.%u.%u).", id.Description, id.Driver,
            HIWORD(id.DriverVersion.HighPart), LOWORD(id.DriverVersion.HighPart), HIWORD(id.DriverVersion.LowPart),
            LOWORD(id.DriverVersion.LowPart));
    }
    if (SUCCEEDED(d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &mode)))
        Log("Desktop: %ux%u at %u Hz.", mode.Width, mode.Height, mode.RefreshRate);
    d->Release();
}

DWORD WINAPI WorkerThread(LPVOID) {
    if (!ExeMd5Ok() || !CodeMatches(kCodeChecks, sizeof(kCodeChecks) / sizeof(kCodeChecks[0]))) {
        Log("Not changing anything.");
        return 0;
    }
    LoadSettings();
    LogGraphicsCard();
    if (g_batchAppend) {
        if (WriteJump(particles::kBatchBegin, reinterpret_cast<const void*>(&New_BatchBegin)))
            Log("Sprite batch fix ON: SpriteBatch_Begin (0x%08X) appends batches (NOOVERWRITE), DISCARD only when full.",
                static_cast<unsigned>(particles::kBatchBegin));
        else
            Log("Could not patch SpriteBatch_Begin: sprite batch fix off.");
    } else {
        Log("BatchAppend=0: the game's own sprite batch (DISCARD every batch).");
    }

    if (!g_timingHooks) {
        Log("TimingHooks=0: no CALLs redirected, sprite batch fix only.");
        LogLoop();
        return 0;
    }
    const Hook hooks[] = {
        {particles::kRenderFrameCall, particles::kRenderFrame, reinterpret_cast<const void*>(&Stamp7), "Render_Frame (clock stub)"},
        {particles::kUpdateCallRace, particles::kUpdate, reinterpret_cast<const void*>(&Hook_Update), "Particles_Update"},
        {particles::kRenderCallMain, particles::kRender, reinterpret_cast<const void*>(&Hook_RenderMain), "Particles_Render main"},
        {particles::kRenderCallMirror, particles::kRender, reinterpret_cast<const void*>(&Hook_RenderMirror), "Particles_Render view 3"},
        {particles::kOrientedDrawCall, particles::kOrientedDraw, reinterpret_cast<const void*>(&Hook_OrientedDraw), "oriented draw"},
        {particles::kBatchBeginCall, particles::kBatchBegin, reinterpret_cast<const void*>(&Hook_BatchBegin), "SpriteBatch_Begin"},
        {particles::kBatchFlushCall, particles::kBatchFlush, reinterpret_cast<const void*>(&Hook_BatchFlush), "SpriteBatch_Flush"},
        {particles::kRainCall, particles::kRain, reinterpret_cast<const void*>(&Hook_Rain), "Rain_UpdateAndDraw"},
    };
    // Render_Frame goes last: it is the frame boundary, so the others must be in place before frames are counted.
    static_assert(sizeof(hooks) / sizeof(hooks[0]) == kNumHooks, "hooks[] and kHookKeys[] must match");
    for (int i = kNumHooks - 1; i >= 0; i--) {
        if (!g_hookOn[i]) {
            Log("Not hooking %s (%s=0).", hooks[i].what, kHookKeys[i]);
            continue;
        }
        if (i == 0) {  // frame end first, so a frame never starts without its end being counted
            if (!RedirectCall(particles::kAfterRenderCall, particles::kEmptyStub, reinterpret_cast<const void*>(&Hook_AfterRender))) {
                Log("Could not hook the empty CALL after Render_Frame (0x%08X): no frame timing.",
                    static_cast<unsigned>(particles::kAfterRenderCall));
                continue;
            }
            g_stampTarget[kDrawStamp] = particles::kRenderFrame;
            if (g_mainLoopClock) {
                static_assert(sizeof(particles::kMainLoopClock) / sizeof(particles::kMainLoopClock[0]) == kDrawStamp,
                              "one clock site per section before draw");
                void (*const stubs[kDrawStamp])() = {Stamp0, Stamp1, Stamp2, Stamp3, Stamp4, Stamp5, Stamp6};
                int ok = 0;
                for (int k = 0; k < kDrawStamp; k++) {
                    g_stampTarget[k] = particles::kMainLoopClock[k].target;
                    if (RedirectCall(particles::kMainLoopClock[k].site, particles::kMainLoopClock[k].target,
                                     reinterpret_cast<const void*>(stubs[k])))
                        ok++;
                    else
                        Log("Could not put a clock on CALL 0x%08X (%s).", static_cast<unsigned>(particles::kMainLoopClock[k].site),
                            kSectionNames[k]);
                }
                Log("Main-loop clocks: %d of %d CALLs (sections: tail presim simdt world postworld misc stream meshvb draw).",
                    ok, kDrawStamp);
            }
        }
        if (!RedirectCall(hooks[i].site, hooks[i].target, hooks[i].fn)) {
            Log("Could not hook %s (CALL 0x%08X): no timing for it.", hooks[i].what, static_cast<unsigned>(hooks[i].site));
            continue;
        }
        Log("Timing %s (CALL 0x%08X).", hooks[i].what, static_cast<unsigned>(hooks[i].site));
    }
    Log("Spike lines: frames over %.0f ms in a race or free roam. Summary every %d s.", g_spikeMs, g_summarySeconds);
    LogLoop();
    return 0;
}

void Init() {
    Log("==== Particle Fix started ====");
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_msPerTick = 1000.0 / static_cast<double>(f.QuadPart);
    InitializeCriticalSection(&g_lock);
    // CryptoAPI must not run inside DllMain, so the MD5 check (and everything after it) runs on its own thread.
    HANDLE thread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
}

}  // namespace

void bf::StartParticleFix() { Init(); }
