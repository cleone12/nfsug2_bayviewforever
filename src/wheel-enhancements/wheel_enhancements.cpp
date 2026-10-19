// Wheel: every force feedback effect and the wheel-to-steering mapping, adjustable in [Wheel.*] (ini defaults = the
// game, except FullResolution).
//
// Force feedback: all effects come from RealDriver_UpdateForceFeedback (0x00406460) through one CALL each.
// The plugin redirects those CALLs to small functions that change the arguments and call the game's own
// function (addresses: src/common/nfsu2.h, namespaces wheel and ffb):
//   steering force   0x004065DB  constant force from the front tires; scaled, then the minimum force
//   center spring    0x0040650B  the game switches the spring OFF here every frame; we can set one instead
//   full-lock spring 0x00406503, 0x004069AC
//   damper           0x004065FD  recomputed from the car's speed (strength and fade-out speed)
//   crash / bump     0x00406775 / 0x004067A6  square-wave jolts
//   road rumble      0x004068C5
//   slippery surface 0x004068F3  negative damper
//   pause in the air 0x00406631
//   FF-off spring    0x004069D6  (game's force feedback option off, or a drag race without Drag Steering)
//   menu spring      0x005CFFAA  (Input_PollDevices)
// The car (for the damper's speed) comes from the two CALLs of RealDriver_UpdateForceFeedback.
//
// Steering: the CALL to Steering_Update at 0x0041E372 passes the steering input (-1..1). The game builds it from
// an 8-bit copy of the wheel axis (127 steps per side, wider center step). The plugin reads the axis itself at
// full resolution (wheels only; only while it agrees with the game's value, so keyboard steering etc. still works), turns
// "SteeringLock degrees of WheelRotation" into full lock, and undoes the hidden "Wheel Range" option (no slider on
// PC). Two data tables, each used only by the wheel steering code, are rewritten at startup:
//   0x007E92D8  response curve (21 points), blended toward a straight line by Linearity
//   0x007E91A4  speed-sensitive steering lock (45 deg standing ... 7 deg at speed), scaled by SpeedSensitivity
// SteeringForce and SoftClip work through the Road Feedback divisor (steering+0xC), before the game's +-10000 clamp;
// SoftClip then squeezes the top of the range with a tanh knee. PedalAssist is gone on purpose: that code is the
// game's understeer model, not an assist.
// Undo: [Features] Wheel = 0. Logs force and steering statistics after each drive.

#define BF_TAG "Wheel"
#include "nfsu2.h"

#include <math.h>


using namespace nfsu2;

namespace {

const DWORD kPollInterval = 250;  // ms between player mode checks for the statistics
const int   kRamp         = 100;  // below 1% of full force the minimum force ramps in from 0
const int   kFull         = ffb::kFullForce;
const float kInputTolerance = 0.06f;  // our axis value must agree with the game's 8-bit value this closely


// ---- Settings ---------------------------------------------------------------

struct Settings {
    // [ForceFeedback]
    float steeringForce    = 100.0f;   // % of the game's steering force
    float minimumForce     = 0.0f;     // % of full force
    float centreSpring     = 0.0f;     // % of full force (spring saturation); the game has none while driving
    float centreSpringWidth = 20.0f;   // degrees of wheel turn (each side) until the center spring is at full strength
    float fullLockSpring   = 100.0f;   // % of full force
    float damper           = 70.59f;   // % of full force when standing still (game 7058.8)
    float damperFadeSpeed  = 46.8f;    // km/h where the damper has faded to 0 (game 13 m/s); 0 = never fades
    float crashJolt        = 100.0f;   // % of the game's value
    float bumpJolt         = 100.0f;
    float roadRumble       = 100.0f;
    float slipperySurface  = 100.0f;
    int   pauseInAir       = 1;
    float noFeedbackSpring = 70.58f;   // % of full force (game 0x1B92 = 7058)
    float menuSpring       = 70.0f;    // % of full force (game 7000)
    float softClip         = 0.0f;     // % headroom above full force that is squeezed in smoothly (0 = hard clip, game)
    float softClipKnee     = 70.0f;    // % of full force where the squeezing starts
    // [Steering]
    float wheelRotation    = 450.0f;   // degrees, as set in the wheel's driver (Logitech Profiler)
    float steeringLock     = 450.0f;   // degrees of wheel turn for the game's full lock
    int   fullResolution   = 1;
    float linearity        = 0.0f;     // 0 = game curve, 100 = straight line
    float speedSensitivity = 100.0f;   // 100 = game, 0 = full 45 deg lock at every speed
};
Settings g_set;

// Derived values (set once at startup)
int   g_minForce = 0;
int   g_centreSat = 0, g_centreCoef = 0;
int   g_fullLock = 10000, g_damperMax = 7059, g_noFeedbackSpring = 7058, g_menuSpring = 7000;
float g_damperFadeMs = 13.0f;
float g_lockScale = 1.0f;  // physical position (-1..1 of the wheel's rotation) -> game position

float ReadFloat(const char* path, const char* section, const char* key, float def, float lo, float hi) {
    char buf[64], defBuf[32];
    snprintf(defBuf, sizeof(defBuf), "%g", def);
    GetPrivateProfileStringA(section, key, defBuf, buf, sizeof(buf), path);
    char* end = nullptr;
    float v = strtof(buf, &end);
    if (end == buf) v = def;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}

int ReadInt(const char* path, const char* section, const char* key, int def, int lo, int hi) {
    int v = static_cast<int>(GetPrivateProfileIntA(section, key, def, path));
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}

int PercentToForce(float pct) {
    long v = lroundf(pct * 100.0f);
    if (v < 0) v = 0;
    if (v > kFull) v = kFull;
    return static_cast<int>(v);
}

int ScaleForce(int v, float pct) {
    long r = lroundf(static_cast<float>(v) * pct / 100.0f);
    if (r > kFull) r = kFull;
    if (r < -kFull) r = -kFull;
    return static_cast<int>(r);
}

void LoadSettings() {
    char path[MAX_PATH];
    bf::IniPath(path);
    const bool found = bf::IniFound();
    Settings& s = g_set;
    if (found) {
        const char* ff = "Wheel.ForceFeedback";
        s.steeringForce     = ReadFloat(path, ff, "SteeringForce", s.steeringForce, 0, 300);
        s.minimumForce      = ReadFloat(path, ff, "MinimumForce", s.minimumForce, 0, 50);
        // the old "Center..." spelling is still read
        s.centreSpring      = ReadFloat(path, ff, "CenterSpring", ReadFloat(path, ff, "CentreSpring", s.centreSpring, 0, 100), 0, 100);
        s.centreSpringWidth = ReadFloat(path, ff, "CenterSpringWidth",
                                        ReadFloat(path, ff, "CentreSpringWidth", s.centreSpringWidth, 1, 900), 1, 900);
        s.fullLockSpring    = ReadFloat(path, ff, "FullLockSpring", s.fullLockSpring, 0, 100);
        s.damper            = ReadFloat(path, ff, "Damper", s.damper, 0, 100);
        s.damperFadeSpeed   = ReadFloat(path, ff, "DamperFadeSpeed", s.damperFadeSpeed, 0, 500);
        s.crashJolt         = ReadFloat(path, ff, "CrashJolt", s.crashJolt, 0, 300);
        s.bumpJolt          = ReadFloat(path, ff, "BumpJolt", s.bumpJolt, 0, 300);
        s.roadRumble        = ReadFloat(path, ff, "RoadRumble", s.roadRumble, 0, 300);
        s.slipperySurface   = ReadFloat(path, ff, "SlipperySurface", s.slipperySurface, 0, 100);
        s.pauseInAir        = ReadInt(path, ff, "PauseInAir", s.pauseInAir, 0, 1);
        s.noFeedbackSpring  = ReadFloat(path, ff, "NoFeedbackSpring", s.noFeedbackSpring, 0, 100);
        s.menuSpring        = ReadFloat(path, ff, "MenuSpring", s.menuSpring, 0, 100);
        s.softClip          = ReadFloat(path, ff, "SoftClip", s.softClip, 0, 200);
        s.softClipKnee      = ReadFloat(path, ff, "SoftClipKnee", s.softClipKnee, 30, 95);
        const char* st = "Wheel.Steering";
        s.wheelRotation     = ReadFloat(path, st, "WheelRotation", s.wheelRotation, 40, 1080);
        s.steeringLock      = ReadFloat(path, st, "SteeringLock", s.steeringLock, 40, 1080);
        s.fullResolution    = ReadInt(path, st, "FullResolution", s.fullResolution, 0, 1);
        s.linearity         = ReadFloat(path, st, "Linearity", s.linearity, 0, 100);
        s.speedSensitivity  = ReadFloat(path, st, "SpeedSensitivity", s.speedSensitivity, 0, 100);
    }
    if (s.steeringLock > s.wheelRotation) s.steeringLock = s.wheelRotation;

    g_minForce = PercentToForce(s.minimumForce);
    g_fullLock = PercentToForce(s.fullLockSpring);
    g_damperMax = PercentToForce(s.damper);
    g_damperFadeMs = s.damperFadeSpeed / 3.6f;
    g_noFeedbackSpring = PercentToForce(s.noFeedbackSpring);
    g_menuSpring = PercentToForce(s.menuSpring);
    g_lockScale = s.wheelRotation / s.steeringLock;

    // Center spring: DirectInput spring force = coefficient * displacement, displacement 10000 = the wheel's end
    // (half its rotation). The coefficient that reaches the saturation after `width` degrees is
    // saturation * halfRotation / width; DirectInput allows at most 10000.
    g_centreSat = PercentToForce(s.centreSpring);
    float effWidth = 0;
    if (g_centreSat > 0) {
        float halfRot = s.wheelRotation * 0.5f;
        long coef = lroundf(g_centreSat * halfRot / s.centreSpringWidth);
        if (coef > kFull) coef = kFull;
        if (coef < 1) coef = 1;
        g_centreCoef = static_cast<int>(coef);
        effWidth = g_centreSat * halfRot / g_centreCoef;
    }

    Log("Settings (%s):", found ? "BayviewForever.ini" : "file not found, game defaults");
    Log("  [ForceFeedback] SteeringForce=%g%% MinimumForce=%g%% CenterSpring=%g%% CenterSpringWidth=%g deg "
        "FullLockSpring=%g%% Damper=%g%% DamperFadeSpeed=%g km/h", s.steeringForce, s.minimumForce, s.centreSpring,
        s.centreSpringWidth, s.fullLockSpring, s.damper, s.damperFadeSpeed);
    Log("  [ForceFeedback] CrashJolt=%g%% BumpJolt=%g%% RoadRumble=%g%% SlipperySurface=%g%% PauseInAir=%d "
        "NoFeedbackSpring=%g%% MenuSpring=%g%% SoftClip=%g%% SoftClipKnee=%g%%", s.crashJolt, s.bumpJolt,
        s.roadRumble, s.slipperySurface, s.pauseInAir, s.noFeedbackSpring, s.menuSpring, s.softClip, s.softClipKnee);
    Log("  [Steering] WheelRotation=%g SteeringLock=%g FullResolution=%d Linearity=%g%% SpeedSensitivity=%g%%",
        s.wheelRotation, s.steeringLock, s.fullResolution, s.linearity, s.speedSensitivity);
    if (g_centreSat > 0)
        Log("  Center spring: saturation %d, coefficient %d -> full strength after %.1f deg each side%s.",
            g_centreSat, g_centreCoef, effWidth,
            effWidth > s.centreSpringWidth + 0.05f ? " (DirectInput's maximum coefficient; wider than asked)" : "");
}

// ---- Statistics (written on the game thread, logged by the monitor thread) ----

// Steering forces by size, % of full force (after SteeringForce, before MinimumForce); the last = at or over 100 %.
const int kNumBuckets = 9;
const char* const kBucketNames[kNumBuckets] = {"0", "<2%", "2-5%", "5-10%", "10-25%", "25-50%", "50-75%", "75-99%", "100%(clipped)"};
volatile LONG g_forceCount[kNumBuckets];
volatile LONG g_softSqueezed, g_softClipped;  // SoftClip: forces above the knee / still at the game's clamp

volatile LONG g_fullResFrames, g_mismatchFrames, g_noAxisFrames, g_badRangeFrames;
volatile float g_maxMismatch;
volatile float g_rangeSeen = -1, g_perCarFactor = -1, g_speedFieldRatio = -1, g_divisorSeen = -1;
volatile int   g_skipSpeedLock = -1;
volatile LONG g_crashJolts, g_bumpJolts, g_airPauses;

int Bucket(int x) {
    if (x == 0) return 0;
    if (x < 200) return 1;
    if (x < 500) return 2;
    if (x < 1000) return 3;
    if (x < 2500) return 4;
    if (x < 5000) return 5;
    if (x < 7500) return 6;
    if (x < kFull) return 7;
    return 8;
}

// ---- Force feedback hooks -----------------------------------------------------

typedef void(__thiscall* FF1Fn)(void* ff, int port);
typedef void(__thiscall* FF2Fn)(void* ff, int port, int a);
typedef void(__thiscall* FF4Fn)(void* ff, int port, int a, int b, int c);
typedef void(__thiscall* SetConstantForceFn)(void* ff, int port, int magnitude, int angle);
typedef void(__thiscall* UpdateFFFn)(void* steering, float curveOut, int port);

void SetSpring(void* ff, int port, int saturation, int coefficient) {
    reinterpret_cast<FF4Fn>(wheel::kSetSpring)(ff, port, 0, saturation, coefficient);
}
void StopSpring(void* ff, int port) { reinterpret_cast<FF1Fn>(wheel::kStopSpring)(ff, port); }

volatile float g_speed = 0;  // m/s of the car whose force feedback is being updated
volatile bool  g_haveSpeed = false;

// Road Feedback divisor (steering+0xC): the game's value, and the one we last wrote in its place.
float g_baseDivisor = 0, g_writtenDivisor = -1;

// Both CALLs of RealDriver_UpdateForceFeedback: note the car's speed for the damper, apply SteeringForce through the
// steering force divisor (force = tire force / divisor * 10000, then clamped), then run the game's function.
void __fastcall Hook_UpdateForceFeedback(void* steering, void* /*edx*/, float curveOut, int port) {
    float* divisor = reinterpret_cast<float*>(static_cast<unsigned char*>(steering) + wheel::steering::kFFDivisor);
    if (*divisor != g_writtenDivisor) g_baseDivisor = *divisor;  // the game's own value (option changed or first time)
    g_divisorSeen = g_baseDivisor;
    const float headroom = 1.0f + g_set.softClip / 100.0f;  // SoftClip: the game clamps at headroom x full force
    if (g_set.steeringForce >= 1.0f && (g_set.steeringForce != 100.0f || headroom > 1.0f) && g_baseDivisor > 0) {
        g_writtenDivisor = g_baseDivisor * 100.0f / g_set.steeringForce * headroom;
        *divisor = g_writtenDivisor;
    }
    const unsigned char* car = *reinterpret_cast<unsigned char* const*>(steering);
    if (car) {
        const float* v = reinterpret_cast<const float*>(car + gearbox::car::kVelocity);
        g_speed = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        g_haveSpeed = true;
    } else {
        g_haveSpeed = false;
    }
    reinterpret_cast<UpdateFFFn>(wheel::kUpdateForceFeedback)(steering, curveOut, port);
}

int ShapeSteeringForce(int in) {
    int x = in < 0 ? -in : in;
    if (x > kFull) x = kFull;
    if (g_set.steeringForce < 1.0f) in = x = 0;  // SteeringForce 0 = no steering force (the divisor can't do 0)
    if (g_set.softClip > 0 && x > 0) {
        // The game's force arrives scaled down by the headroom (divisor), so x = kFull means headroom x full force.
        // Up to the knee it stays linear; above it the rest of the range is squeezed into what is left (tanh).
        const float headroom = 1.0f + g_set.softClip / 100.0f;
        const float k = g_set.softClipKnee / 100.0f;
        const float r = static_cast<float>(x) / kFull * headroom;  // in units of full force
        if (x >= kFull) InterlockedIncrement(&g_softClipped);
        float y = r;
        if (r > k) {
            y = k + (1.0f - k) * tanhf((r - k) / (1.0f - k));
            InterlockedIncrement(&g_softSqueezed);
        }
        InterlockedIncrement(&g_forceCount[Bucket(r >= 1.0f ? kFull : static_cast<int>(r * kFull))]);
        x = static_cast<int>(y * kFull + 0.5f);
        if (x > kFull) x = kFull;
        in = in < 0 ? -x : x;
    } else {
        InterlockedIncrement(&g_forceCount[Bucket(x)]);
    }
    if (g_minForce <= 0 || x == 0) return in;
    int y;
    if (x < kRamp) {
        const int atRamp = g_minForce + kRamp * (kFull - g_minForce) / kFull;
        y = x * atRamp / kRamp;
    } else {
        y = g_minForce + x * (kFull - g_minForce) / kFull;
    }
    return in < 0 ? -y : y;
}

void __fastcall Hook_SteeringForce(void* ff, void* /*edx*/, int port, int magnitude, int angle) {
    reinterpret_cast<SetConstantForceFn>(ffb::kSetConstantForce)(ff, port, ShapeSteeringForce(magnitude), angle);
}

// Normal driving, not at full lock: the game stops the spring here every frame.
void __fastcall Hook_StopSpring(void* ff, void* /*edx*/, int port) {
    if (g_centreSat > 0) SetSpring(ff, port, g_centreSat, g_centreCoef);
    else StopSpring(ff, port);
}

// Full lock (both branches): the game sets a 100% spring.
void __fastcall Hook_FullLockSpring(void* ff, void* /*edx*/, int port, int /*offset*/, int /*sat*/, int /*coef*/) {
    if (g_fullLock > 0) SetSpring(ff, port, g_fullLock, g_fullLock);
    else if (g_centreSat > 0) SetSpring(ff, port, g_centreSat, g_centreCoef);
    else StopSpring(ff, port);
}

void __fastcall Hook_NoFeedbackSpring(void* ff, void* /*edx*/, int port, int /*offset*/, int /*sat*/, int /*coef*/) {
    if (g_noFeedbackSpring > 0) SetSpring(ff, port, g_noFeedbackSpring, g_noFeedbackSpring);
    else StopSpring(ff, port);
}

void __fastcall Hook_MenuSpring(void* ff, void* /*edx*/, int port, int /*offset*/, int /*sat*/, int /*coef*/) {
    if (g_menuSpring > 0) SetSpring(ff, port, g_menuSpring, g_menuSpring);
    else StopSpring(ff, port);
}

// Game: (1 - min(1, speed / 13 m/s)) * 7058.8.
void __fastcall Hook_Damper(void* ff, void* /*edx*/, int port, int coefficient) {
    int c;
    if (g_haveSpeed) {
        float f = 1.0f;
        if (g_damperFadeMs > 0) {
            f = 1.0f - g_speed / g_damperFadeMs;
            if (f < 0) f = 0;
        }
        c = static_cast<int>(g_damperMax * f);  // truncates like the game's float-to-int
    } else {
        c = ScaleForce(coefficient, g_set.damper / 70.588235f * 100.0f);
    }
    reinterpret_cast<FF2Fn>(wheel::kSetDamper)(ff, port, c);
}

void __fastcall Hook_CrashJolt(void* ff, void* /*edx*/, int port, int magnitude) {
    InterlockedIncrement(&g_crashJolts);
    reinterpret_cast<FF2Fn>(wheel::kPlayJolt)(ff, port, ScaleForce(magnitude, g_set.crashJolt));
}

void __fastcall Hook_BumpJolt(void* ff, void* /*edx*/, int port, int magnitude) {
    InterlockedIncrement(&g_bumpJolts);
    reinterpret_cast<FF2Fn>(wheel::kPlayJolt)(ff, port, ScaleForce(magnitude, g_set.bumpJolt));
}

void __fastcall Hook_RoadRumble(void* ff, void* /*edx*/, int port, int wave, int magnitude, int periodMs) {
    reinterpret_cast<FF4Fn>(wheel::kSetRoadRumble)(ff, port, wave, ScaleForce(magnitude, g_set.roadRumble), periodMs);
}

void __fastcall Hook_NegativeDamper(void* ff, void* /*edx*/, int port, int value) {
    reinterpret_cast<FF2Fn>(wheel::kSetNegativeDamper)(ff, port, ScaleForce(value, g_set.slipperySurface));
}

void __fastcall Hook_PauseAll(void* ff, void* /*edx*/, int port) {
    InterlockedIncrement(&g_airPauses);
    if (g_set.pauseInAir) reinterpret_cast<FF1Fn>(wheel::kPauseAll)(ff, port);
}

// ---- Steering ------------------------------------------------------------------

// The port's steering axis, -1..1 in the game's direction (game: about -axis * dir / 10079). False if not found.
bool ReadSteeringAxis(int port, float* out) {
    if (port < 0 || port > 1) return false;
    const unsigned char* ff = *reinterpret_cast<unsigned char* const*>(wheel::kFFObject);
    if (!ff) return false;
    for (int i = 0; i < wheel::kNumDeviceRecords; i++) {
        const unsigned char* rec =
            reinterpret_cast<const unsigned char*>(wheel::kDeviceRecords + i * wheel::kDeviceRecordSize);
        if (ReadAt<int>(rec, wheel::device::kPort) != port) continue;
        if (ReadAt<int>(rec, wheel::device::kType) == 0x13) continue;  // keyboard
        if (!rec[wheel::device::kSteerBound]) continue;
        const int off = ReadAt<int>(rec, wheel::device::kSteerAxis);
        if (off < 0 || off > static_cast<int>(wheel::kJoyStateSize) - 4 || (off & 3)) continue;
        const int dir = static_cast<signed char>(rec[wheel::device::kSteerDir]);
        if (dir == 0) continue;
        const int axis = ReadAt<int>(ff, port * wheel::kJoyStateSize + off);
        float v = -static_cast<float>(axis) * static_cast<float>(dir) / 10000.0f;
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        *out = v;
        return true;
    }
    return false;
}

typedef uint32_t(__thiscall* SteeringUpdateFn)(void* steering, float input, float dt, int port);
typedef bool(__cdecl* IsFFWheelFn)(int port);

uint32_t __fastcall Hook_SteeringUpdate(unsigned char* steering, void* /*edx*/, float input, float dt, int port) {
    // Gamepads and keyboards take the game's own pad path: leave their input alone.
    if (!reinterpret_cast<IsFFWheelFn>(wheel::kIsFFWheel)(port))
        return reinterpret_cast<SteeringUpdateFn>(wheel::kSteeringUpdate)(steering, input, dt, port);
    const float range = ReadAt<float>(steering, wheel::steering::kRange);
    g_rangeSeen = range;
    g_skipSpeedLock = steering[wheel::steering::kSkipSpeedLock];
    const unsigned char* car = ReadAt<const unsigned char*>(steering, wheel::steering::kCar);
    if (car) {
        const unsigned char* stats = ReadAt<const unsigned char*>(car, gearbox::car::kStats);
        if (stats) g_perCarFactor = ReadAt<float>(stats, 0x270);
        if (g_haveSpeed && g_speed > 10.0f) g_speedFieldRatio = ReadAt<float>(car, 0x42C) / g_speed;
    }

    float in = input;
    if (range >= 0.05f && range <= 2.0f) {
        float x = input;  // wheel position, -1..1 of its rotation
        if (g_set.fullResolution) {
            float raw;
            if (ReadSteeringAxis(port, &raw)) {
                const float diff = fabsf(raw - input);
                if (diff <= kInputTolerance) {
                    x = raw;
                    InterlockedIncrement(&g_fullResFrames);
                } else {
                    InterlockedIncrement(&g_mismatchFrames);
                    if (diff > g_maxMismatch) g_maxMismatch = diff;
                }
            } else {
                InterlockedIncrement(&g_noAxisFrames);
            }
        }
        x *= g_lockScale;
        if (x > 1.0f) x = 1.0f;
        if (x < -1.0f) x = -1.0f;
        in = x * range;  // the game clamps to +-range and divides by it, so it sees exactly x
    } else {
        InterlockedIncrement(&g_badRangeFrames);
    }
    return reinterpret_cast<SteeringUpdateFn>(wheel::kSteeringUpdate)(steering, in, dt, port);
}

// ---- Steering tables ----------------------------------------------------------

const float kGameCurve[wheel::kCurvePoints] = {
    -1.0f, -0.8f, -0.615f, -0.483f, -0.388f, -0.288f, -0.22f, -0.161f, -0.111f, -0.057f, 0.0f,
    0.057f, 0.111f, 0.161f, 0.22f, 0.288f, 0.388f, 0.483f, 0.615f, 0.8f, 1.0f};
const float kGameLock[wheel::kLockPoints] = {45, 15, 11, 8, 7, 7, 7, 7, 7, 7};

// True if the table in memory holds the game's values (to 0.0005).
bool TableMatches(uintptr_t addr, const float* expect, int n) {
    if (!IsReadable(addr, n * 4)) return false;
    const float* t = reinterpret_cast<const float*>(addr);
    for (int i = 0; i < n; i++)
        if (fabsf(t[i] - expect[i]) > 0.0005f) return false;
    return true;
}

bool WriteTable(uintptr_t addr, const float* values, int n) {
    DWORD oldProtect;
    if (!VirtualProtect(reinterpret_cast<void*>(addr), n * 4, PAGE_READWRITE, &oldProtect)) return false;
    memcpy(reinterpret_cast<void*>(addr), values, n * 4);
    VirtualProtect(reinterpret_cast<void*>(addr), n * 4, oldProtect, &oldProtect);
    return true;
}

void PatchSteeringTables() {
    if (g_set.linearity > 0) {
        if (!TableMatches(wheel::kCurveData, kGameCurve, wheel::kCurvePoints)) {
            Log("STOP (curve only): the response curve at 0x%08X is not the game's; left alone.",
                static_cast<unsigned>(wheel::kCurveData));
        } else {
            float t[wheel::kCurvePoints];
            const float k = g_set.linearity / 100.0f;
            for (int i = 0; i < wheel::kCurvePoints; i++) {
                const float straight = -1.0f + 0.1f * i;
                t[i] = kGameCurve[i] + (straight - kGameCurve[i]) * k;
            }
            t[0] = -1.0f;  // ends stay exactly +-1 so the full-lock spring still triggers
            t[wheel::kCurvePoints - 1] = 1.0f;
            t[10] = 0.0f;
            if (WriteTable(wheel::kCurveData, t, wheel::kCurvePoints))
                Log("Response curve: 10%% wheel -> %.3f, 25%% -> %.3f, 50%% -> %.3f, 75%% -> %.3f (game 0.057/0.136/0.288/0.549).",
                    t[11], (t[12] + t[13]) / 2, t[15], (t[17] + t[18]) / 2);
            else
                Log("Could not write the response curve.");
        }
    } else {
        Log("Response curve: the game's.");
    }

    if (g_set.speedSensitivity < 100) {
        if (!TableMatches(wheel::kLockData, kGameLock, wheel::kLockPoints)) {
            Log("STOP (lock only): the steering lock table at 0x%08X is not the game's; left alone.",
                static_cast<unsigned>(wheel::kLockData));
        } else {
            // Speed sensitivity blends toward 45 deg. The game's throttle/brake factor on top of it (its understeer
            // model, see wheel::kPedalAssistFmul) is left alone.
            float t[wheel::kLockPoints];
            const float s = g_set.speedSensitivity / 100.0f;
            for (int i = 0; i < wheel::kLockPoints; i++)
                t[i] = wheel::kMaxLockDegrees - (wheel::kMaxLockDegrees - kGameLock[i]) * s;
            if (WriteTable(wheel::kLockData, t, wheel::kLockPoints))
                Log("Steering lock table by speed (0, 64, 128, 192, 256+ km/h): %.1f %.1f %.1f %.1f %.1f deg (game 45 15 11 8 7, "
                    "before the pedal and per-car factors).", t[0], t[1], t[2], t[3], t[4]);
            else
                Log("Could not write the steering lock table.");
        }
    } else {
        Log("Steering lock by speed: the game's.");
    }

}

// ---- Code checks and hooks ----------------------------------------------------

const CodeCheck kCodeChecks[] = {
    // RealDriver_UpdateForceFeedback (0x00406460)
    {0x004064FB, {0x68, 0x10, 0x27, 0x00, 0x00, 0x6A, 0x00, 0x53}, 8, "full-lock spring: PUSH 10000, PUSH 0"},
    {0x00406500, {0x6A, 0x00, 0x53, 0xE8, 0x48, 0x2C, 0x1C, 0x00}, 8, "full-lock spring: PUSH 0, PUSH port, CALL FF_SetSpring"},
    {0x0040650A, {0x53, 0xE8, 0xF0, 0x98, 0x1B, 0x00}, 6, "PUSH port, CALL FF_StopSpring"},
    {0x00406510, {0x8B, 0x37, 0x8D, 0x4E, 0x70, 0x51}, 6, "car = [EDI]; LEA ECX,[car+0x70] (velocity)"},
    {0x004065CE, {0x8B, 0x0D, 0x60, 0x07, 0x87, 0x00, 0x68, 0x0E}, 8, "steering force: ECX = FF device, PUSH 270"},
    {0x004065D9, {0x56, 0x53, 0xE8, 0x70, 0x2F, 0x1C, 0x00}, 7, "steering force: PUSH magnitude, port; CALL FF_SetConstantForceAtAngle"},
    {0x004065FB, {0x50, 0x53, 0xE8, 0x8E, 0x2F, 0x1C, 0x00}, 7, "PUSH coefficient, port; CALL FF_SetDamper"},
    {0x00406630, {0x53, 0xE8, 0xDA, 0x73, 0x1C, 0x00}, 6, "PUSH port; CALL FF_PauseAll"},
    {0x00406773, {0x50, 0x53, 0xE8, 0xE6, 0x2F, 0x1C, 0x00}, 7, "PUSH magnitude, port; CALL FF_PlayJolt (crash)"},
    {0x004067A4, {0x50, 0x53, 0xE8, 0xB5, 0x2F, 0x1C, 0x00}, 7, "PUSH magnitude, port; CALL FF_PlayJolt (bump)"},
    {0x004068C2, {0x50, 0x56, 0x53, 0xE8, 0x06, 0x37, 0x1C, 0x00}, 8, "PUSH magnitude, wave, port; CALL FF_SetRoadRumble"},
    {0x004068ED, {0x68, 0x10, 0x27, 0x00, 0x00, 0x53}, 6, "PUSH 10000, PUSH port"},
    {0x004068F2, {0x53, 0xE8, 0x98, 0x34, 0x1C, 0x00}, 6, "PUSH port; CALL FF_SetNegativeDamper"},
    {0x004069A9, {0x6A, 0x00, 0x53, 0xE8, 0x9F, 0x27, 0x1C, 0x00}, 8, "FF off: PUSH 0, port; CALL FF_SetSpring (full lock)"},
    {0x004069C9, {0x68, 0x92, 0x1B, 0x00, 0x00}, 5, "FF off: PUSH 0x1B92"},
    {0x004069D3, {0x6A, 0x00, 0x53, 0xE8, 0x75, 0x27, 0x1C, 0x00}, 8, "FF off: PUSH 0, port; CALL FF_SetSpring"},
    // FF functions
    {0x005C9550, {0x8B, 0x44, 0x24, 0x0C, 0x99, 0x56, 0xBE, 0x68}, 8, "start of FF_SetConstantForceAtAngle"},
    {0x005C9586, {0xC2, 0x0C, 0x00}, 3, "RET 0xC of FF_SetConstantForceAtAngle"},
    {0x005BFE00, {0x8B, 0x44, 0x24, 0x04, 0x8B, 0x14, 0x85, 0x80}, 8, "start of FF_StopSpring"},
    {0x005BFE17, {0xC2, 0x04, 0x00}, 3, "RET 4 of FF_StopSpring"},
    {0x005C923A, {0xC2, 0x10, 0x00}, 3, "RET 0x10 of FF_SetSpring"},
    {0x005C965D, {0xC2, 0x08, 0x00}, 3, "RET 8 of FF_SetDamper"},
    {0x005C9899, {0xC2, 0x08, 0x00}, 3, "RET 8 of FF_PlayJolt"},
    {0x005CA1AE, {0xC2, 0x10, 0x00}, 3, "RET 0x10 of FF_SetRoadRumble"},
    {0x005C9EA4, {0xC2, 0x08, 0x00}, 3, "RET 8 of FF_SetNegativeDamper"},
    {0x005CDB8B, {0xC2, 0x04, 0x00}, 3, "RET 4 of FF_PauseAll"},
    {0x00406908, {0xC2, 0x08, 0x00}, 3, "RET 8 of RealDriver_UpdateForceFeedback"},
    {0x0041E04E, {0xC2, 0x0C, 0x00}, 3, "RET 0xC of Steering_Update"},
    {0x0041DFE5, {0xE8, 0x16, 0x30, 0x1A, 0x00}, 5, "Steering_Update: CALL Input_IsFFWheel"},
    {0x005C1000, {0x8B, 0x44, 0x24, 0x04, 0x8B, 0x0C, 0x85, 0xB4}, 8, "start of Input_IsFFWheel"},
    // Menu spring (Input_PollDevices)
    {0x005CFF9D, {0x68, 0x58, 0x1B, 0x00, 0x00}, 5, "menu spring: PUSH 7000"},
    {0x005CFFA7, {0x6A, 0x00, 0x56, 0xE8, 0xA1, 0x91, 0xFF, 0xFF}, 8, "menu spring: PUSH 0, port; CALL FF_SetSpring"},
    // Callers of RealDriver_UpdateForceFeedback (ECX = steering object, [ECX] = car)
    {0x0041E01F, {0x57, 0x8B, 0xCE, 0xE8, 0x39, 0x84, 0xFE, 0xFF}, 8, "PUSH curve, MOV ECX,ESI; CALL RealDriver_UpdateForceFeedback"},
    {0x004220C8, {0x8D, 0x8E, 0x90, 0x06, 0x00, 0x00, 0xE8, 0x8D}, 8, "DragDriver: LEA ECX,[ESI+0x690]; CALL"},
    // Steering input
    {0x0041E341, {0x8A, 0x86, 0xFB, 0x02, 0x00, 0x00}, 6, "RealDriver+0x2FB (input off)"},
    {0x0041E355, {0x8B, 0x86, 0xC8, 0x02, 0x00, 0x00}, 6, "input = RealDriver+0x2C8"},
    {0x0041E36C, {0x8D, 0x8E, 0x20, 0x02, 0x00, 0x00, 0xE8, 0x49}, 8, "LEA ECX,[ESI+0x220]; CALL Steering_Update"},
    {0x004061C7, {0xD9, 0x46, 0x08, 0x8A, 0x5E, 0x04}, 6, "FLD [steering+8] (range); MOV BL,[steering+4]"},
    {0x005C1FBA, {0x8A, 0x86, 0x64, 0x03, 0x00, 0x00}, 6, "MOV AL,[rec+0x364] (steering bound)"},
    {0x005C1FC6, {0x8B, 0x8E, 0x30, 0x02, 0x00, 0x00}, 6, "MOV ECX,[rec+0x230] (port)"},
    {0x005C1FCC, {0x8B, 0xBE, 0x44, 0x03, 0x00, 0x00}, 6, "MOV EDI,[rec+0x344] (steering axis offset)"},
    {0x005C1FD8, {0x8B, 0x15, 0x60, 0x07, 0x87, 0x00}, 6, "MOV EDX,[0x00870760]"},
    {0x005C1FE3, {0xD8, 0x0D, 0x18, 0x3C, 0x7A, 0x00}, 6, "FMUL [-0.0127]"},
    {0x005C1FEE, {0xF6, 0xAE, 0x54, 0x03, 0x00, 0x00}, 6, "IMUL BYTE [rec+0x354] (direction)"},
    // Steering tables (only their own table records point at them)
    {0x007E92D8, {0x00, 0x00, 0x80, 0xBF, 0xCD, 0xCC, 0x4C, 0xBF}, 8, "response curve -1.0, -0.8"},
    {0x007E92F8, {0xF8, 0x53, 0xE3, 0xBD, 0xD5, 0x78, 0x69, 0xBD}, 8, "response curve -0.111, -0.057"},
    {0x007E9E98, {0xD8, 0x92, 0x7E, 0x00}, 4, "curve table -> data 0x007E92D8"},
    {0x007E91A4, {0x00, 0x00, 0x34, 0x42, 0x00, 0x00, 0x70, 0x41}, 8, "steering lock 45, 15"},
    {0x007E91B4, {0x00, 0x00, 0xE0, 0x40, 0x00, 0x00, 0xE0, 0x40}, 8, "steering lock 7, 7"},
    {0x007E9EFC, {0xA4, 0x91, 0x7E, 0x00}, 4, "lock table -> data 0x007E91A4"},
    // Player list (statistics)
    {0x0060A240, {0x8B, 0x0C, 0xB5, 0xB4, 0x00, 0x89, 0x00, 0xE8}, 8, "player list 0x008900B4 -> player update"},
    {0x0060A24C, {0xA1, 0xBC, 0x00, 0x89, 0x00}, 5, "player count 0x008900BC"},
    {0x00602A7D, {0x8B, 0x86, 0x2C, 0x02, 0x00, 0x00}, 6, "player update reads PLAYER_MODE [ESI+0x22C]"},
};

struct Redirect {
    uintptr_t site;
    uintptr_t target;
    const void* hook;
    const char* what;
};

bool InstallHooks() {
    const Redirect redirects[] = {
        {ffb::kSteeringForceCall, ffb::kSetConstantForce, reinterpret_cast<const void*>(&Hook_SteeringForce), "steering force"},
        {wheel::kStopSpringCall, wheel::kStopSpring, reinterpret_cast<const void*>(&Hook_StopSpring), "center spring (spring stop)"},
        {wheel::kFullLockSpringCall, wheel::kSetSpring, reinterpret_cast<const void*>(&Hook_FullLockSpring), "full-lock spring"},
        {wheel::kOffFullLockSpringCall, wheel::kSetSpring, reinterpret_cast<const void*>(&Hook_FullLockSpring), "full-lock spring (FF off)"},
        {wheel::kOffSpringCall, wheel::kSetSpring, reinterpret_cast<const void*>(&Hook_NoFeedbackSpring), "FF-off spring"},
        {wheel::kMenuSpringCall, wheel::kSetSpring, reinterpret_cast<const void*>(&Hook_MenuSpring), "menu spring"},
        {wheel::kDamperCall, wheel::kSetDamper, reinterpret_cast<const void*>(&Hook_Damper), "damper"},
        {wheel::kCrashJoltCall, wheel::kPlayJolt, reinterpret_cast<const void*>(&Hook_CrashJolt), "crash jolt"},
        {wheel::kBumpJoltCall, wheel::kPlayJolt, reinterpret_cast<const void*>(&Hook_BumpJolt), "bump jolt"},
        {wheel::kRoadRumbleCall, wheel::kSetRoadRumble, reinterpret_cast<const void*>(&Hook_RoadRumble), "road rumble"},
        {wheel::kNegativeDamperCall, wheel::kSetNegativeDamper, reinterpret_cast<const void*>(&Hook_NegativeDamper), "slippery surface"},
        {wheel::kPauseAllCall, wheel::kPauseAll, reinterpret_cast<const void*>(&Hook_PauseAll), "pause in the air"},
        {wheel::kUpdateForceFeedbackCalls[0], wheel::kUpdateForceFeedback, reinterpret_cast<const void*>(&Hook_UpdateForceFeedback), "force feedback update (normal)"},
        {wheel::kUpdateForceFeedbackCalls[1], wheel::kUpdateForceFeedback, reinterpret_cast<const void*>(&Hook_UpdateForceFeedback), "force feedback update (drag)"},
        {wheel::kSteeringUpdateCall, wheel::kSteeringUpdate, reinterpret_cast<const void*>(&Hook_SteeringUpdate), "steering input"},
    };
    // RedirectCall checks each site before writing; stop at the first failure (earlier ones stay, all are harmless alone).
    for (const Redirect& r : redirects) {
        if (!RedirectCall(r.site, r.target, r.hook)) {
            Log("STOP: could not hook the %s CALL at 0x%08X.", r.what, static_cast<unsigned>(r.site));
            return false;
        }
    }
    Log("Hooked %u CALLs (all force feedback effects, steering input).",
        static_cast<unsigned>(sizeof(redirects) / sizeof(redirects[0])));
    return true;
}

// ---- Player mode and statistics log ---------------------------------------------

// Player 1's PLAYER_MODE, or -1 when there is no player (menus, loading).
int ReadPlayerMode() {
    if (!IsReadable(player::kCount, 4)) return -1;
    int count = *reinterpret_cast<const int*>(player::kCount);
    if (count <= 0 || count > 2 || !IsReadable(player::kList, 4)) return -1;
    const unsigned char* p1 = *reinterpret_cast<unsigned char* const*>(player::kList);
    if (!p1 || !IsReadable(reinterpret_cast<uintptr_t>(p1), player::kMode + 4)) return -1;
    return ReadAt<int>(p1, player::kMode);
}

void LogDriveStats(const char* when) {
    Log("---- %s ----", when);
    LONG n[kNumBuckets];
    LONG total = 0;
    for (int i = 0; i < kNumBuckets; i++) {
        n[i] = InterlockedExchange(&g_forceCount[i], 0);
        total += n[i];
    }
    if (total > 0) {
        char line[256];
        int len = snprintf(line, sizeof(line), "Steering force (before MinimumForce), %ld updates:", total);
        for (int i = 0; i < kNumBuckets && len < static_cast<int>(sizeof(line)); i++)
            len += snprintf(line + len, sizeof(line) - len, " %s %ld%%",
                            // with SoftClip the top bucket is "at or over full force before SoftClip", squeezed
                            i == kNumBuckets - 1 && g_set.softClip > 0 ? "100%+(squeezed)" : kBucketNames[i],
                            n[i] * 100 / total);
        Log("%s", line);
    }
    const LONG squeezed = InterlockedExchange(&g_softSqueezed, 0), softClipped = InterlockedExchange(&g_softClipped, 0);
    if (g_set.softClip > 0 && total > 0)
        Log("SoftClip: %ld%% of updates above the knee (squeezed smoothly), %ld%% beyond the headroom (still clipped).",
            squeezed * 100 / total, softClipped * 100 / total);
    const LONG full = InterlockedExchange(&g_fullResFrames, 0);
    const LONG mismatch = InterlockedExchange(&g_mismatchFrames, 0);
    const LONG noAxis = InterlockedExchange(&g_noAxisFrames, 0);
    const LONG badRange = InterlockedExchange(&g_badRangeFrames, 0);
    Log("Steering input: full resolution %ld frames, game's 8-bit value %ld (didn't agree, max difference %.3f), "
        "no wheel axis found %ld, odd range %ld.", full, mismatch, g_maxMismatch, noAxis, badRange);
    g_maxMismatch = 0;
    Log("Steering object: hidden Wheel Range = %.3f, skip speed lock = %d, per-car factor = %.3f, "
        "car+0x42C / speed = %.3f, Road Feedback divisor = %.2f (2 strongest .. 20 weakest).",
        g_rangeSeen, g_skipSpeedLock, g_perCarFactor, g_speedFieldRatio, g_divisorSeen);
    Log("Effects this drive: crash jolts %ld, bump jolts %ld, in-air pause calls %ld.",
        InterlockedExchange(&g_crashJolts, 0), InterlockedExchange(&g_bumpJolts, 0), InterlockedExchange(&g_airPauses, 0));
}

void MonitorLoop() {
    const int kPollsPerMinute = 60000 / kPollInterval;
    bool wasDriving = false;
    int drivingPolls = 0;
    for (;;) {
        const int mode = ReadPlayerMode();
        const bool driving = mode == 2 || mode == 3;  // CAR_RACING (also free roam) or DRAG_RACING
        if (wasDriving && !driving) {
            LogDriveStats("end of drive");
            drivingPolls = 0;
        } else if (driving && ++drivingPolls >= kPollsPerMinute) {
            LogDriveStats("another minute of driving");
            drivingPolls = 0;
        }
        wasDriving = driving;
        Sleep(kPollInterval);
    }
}

// Every CALL this plugin redirects. If one already jumps outside speed2.exe, another plugin got there first.
const uintptr_t kHookedCallSites[] = {
    ffb::kSteeringForceCall, wheel::kStopSpringCall, wheel::kFullLockSpringCall, wheel::kOffFullLockSpringCall,
    wheel::kOffSpringCall, wheel::kMenuSpringCall, wheel::kDamperCall, wheel::kCrashJoltCall, wheel::kBumpJoltCall,
    wheel::kRoadRumbleCall, wheel::kNegativeDamperCall, wheel::kPauseAllCall, wheel::kUpdateForceFeedbackCalls[0],
    wheel::kUpdateForceFeedbackCalls[1], wheel::kSteeringUpdateCall,
};

// Logs (and returns true for) every hooked CALL that another module has already redirected.
bool ReportForeignHooks() {
    bool found = false;
    for (uintptr_t site : kHookedCallSites) {
        if (!IsReadable(site, 5)) continue;
        const unsigned char* p = reinterpret_cast<const unsigned char*>(site);
        if (p[0] != 0xE8) continue;
        int32_t rel;
        memcpy(&rel, p + 1, 4);
        const uintptr_t target = site + 5 + rel;
        if (target >= 0x00400000 && target < 0x008F5000) continue;  // still inside the exe
        char name[MAX_PATH] = "an unknown module";
        HMODULE owner = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(target), &owner) && owner)
            GetModuleFileNameA(owner, name, MAX_PATH);
        Log("STOP: the CALL at 0x%08X is already hooked by %s.", static_cast<unsigned>(site), name);
        found = true;
    }
    if (found) Log("Remove that plugin from scripts\\ (it changes the same force feedback code).");
    return found;
}

DWORD WINAPI WorkerThread(LPVOID) {
    if (!ExeMd5Ok() || ReportForeignHooks() || !CodeMatches(kCodeChecks, sizeof(kCodeChecks) / sizeof(kCodeChecks[0]))) {
        Log("Not changing anything.");
        return 0;
    }
    if (!IsReadable(wheel::kDeviceRecords, wheel::kNumDeviceRecords * wheel::kDeviceRecordSize) ||
        !IsReadable(wheel::kFFObject, 4)) {
        Log("STOP: input device records not readable. Not changing anything.");
        return 0;
    }
    LoadSettings();
    PatchSteeringTables();
    if (!InstallHooks()) return 0;
    MonitorLoop();
    return 0;
}

void Init() {
    Log("==== Wheel Enhancements started ====");
    // CryptoAPI must not run inside DllMain, so the MD5 check (and everything after it) runs on its own thread.
    HANDLE thread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
}

}  // namespace

void bf::StartWheel() { Init(); }
