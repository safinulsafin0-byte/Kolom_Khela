// PenPhysics.hpp  -  KOLOM KHELA
// 2D rigid-body physics for ball-point pens sliding on a bench.
//
// Each pen is a rigid body (position, angle, linear + angular velocity) whose
// collision shape is a multi-segment capsule: a row of kSegs circles laid out
// along the pen axis (equivalent to an oriented capsule/OBB with round ends).
// Contacts use sequential impulses with restitution, Coulomb contact friction
// and rotational torque; the table gives anisotropic sliding friction.
// A pen whose centre of mass crosses the table edge starts to fall (gravity
// drop on a pseudo-Z axis) and no longer collides.
//
// The World is cheap to copy, which the AI uses to simulate candidate shots.
#pragma once

#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace phys {

// ---------------------------------------------------------------- constants
inline constexpr float kPi            = 3.14159265358979323846f;
inline constexpr float kStep          = 1.0f / 240.0f;   // fixed physics step
inline constexpr float kPenLength     = 120.f;
inline constexpr float kSegR          = 7.5f;            // half thickness / segment radius
inline constexpr int   kSegs          = 8;
inline constexpr float kMass          = 1.f;
inline constexpr float kInertia       = kMass * (kPenLength * kPenLength + 4.f * kSegR * kSegR) / 12.f;

inline constexpr float kRestPenPen    = 0.55f;
inline constexpr float kRestPenObs    = 0.62f;
inline constexpr float kMuContact     = 0.28f;
inline constexpr float kSlideDecel    = 520.f;           // px/s^2 along pen axis
inline constexpr float kSlideDecelLat = 610.f;           // px/s^2 sideways
inline constexpr float kSpinDecel     = 9.f;             // rad/s^2
inline constexpr float kMaxSpeed      = 1700.f;
inline constexpr float kMaxSpin       = 28.f;
inline constexpr float kMaxFlick      = 1350.f;          // px/s at 100% power
inline constexpr float kGravity       = 1700.f;          // fall acceleration
inline constexpr float kLandZ         = 210.f;           // thud when fallZ passes this
inline constexpr float kDoneZ         = 270.f;           // fall animation finished

// ------------------------------------------------------------ vector helpers
inline Vector2 vadd(Vector2 a, Vector2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vector2 vsub(Vector2 a, Vector2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vector2 vmul(Vector2 a, float s)   { return {a.x * s, a.y * s}; }
inline float   vdot(Vector2 a, Vector2 b) { return a.x * b.x + a.y * b.y; }
inline float   vcross(Vector2 a, Vector2 b) { return a.x * b.y - a.y * b.x; }
inline float   vlen2(Vector2 a)           { return a.x * a.x + a.y * a.y; }
inline float   vlen(Vector2 a)            { return std::sqrt(vlen2(a)); }
inline Vector2 vperp(Vector2 a)           { return {-a.y, a.x}; }
inline Vector2 vnorm(Vector2 a) {
    float l = vlen(a);
    return l > 1e-6f ? Vector2{a.x / l, a.y / l} : Vector2{1.f, 0.f};
}
inline float shrink(float v, float d) {
    if (v > d) return v - d;
    if (v < -d) return v + d;
    return 0.f;
}

// ---------------------------------------------------------------- bodies
struct Pen {
    Vector2 pos{0, 0};
    float   angle  = 0.f;          // axis angle (radians); local -x = cap end, +x = tip
    Vector2 vel{0, 0};
    float   angVel = 0.f;
    float   length = kPenLength;

    bool  falling     = false;     // centre of mass crossed the table edge
    bool  thudPlayed  = false;
    float fallZ       = 0.f;
    float fallVz      = 0.f;

    void Init(Vector2 p, float a) { *this = Pen(); pos = p; angle = a; }

    Vector2 Axis() const { return {std::cos(angle), std::sin(angle)}; }
    float SegX(int i) const {
        float span = length - 2.f * kSegR;
        return -span * 0.5f + span * float(i) / float(kSegs - 1);
    }
    Vector2 SegPos(int i) const {
        Vector2 a = Axis();
        float x = SegX(i);
        return {pos.x + a.x * x, pos.y + a.y * x};
    }
    bool Moving() const { return vel.x != 0.f || vel.y != 0.f || angVel != 0.f; }
    bool Gone() const   { return falling && fallZ >= kDoneZ; }
};

struct Obstacle { Vector2 c; float r; };

struct Event {
    enum Kind { PenPen, PenObstacle, FallStart, FallLand } kind;
    float   intensity;   // approach speed for impacts
    Vector2 pos;
    int     pen;
};

// A flick: direction, power 0..1, and where on the pen body the finger hits
// (0 = cap end, 0.5 = centre, 1 = tip end).
struct Shot {
    float angle   = 0.f;
    float power   = 0.f;
    float contact = 0.5f;
};

// -------------------------------------------------------------- contacts
inline Vector2 VelAt(const Pen& p, Vector2 r) {
    return {p.vel.x - p.angVel * r.y, p.vel.y + p.angVel * r.x};
}

// Impulse-based contact between pen A and pen B (or a static body if B==nullptr).
// n points from A towards B. Returns the approach speed along n (0 if separating).
inline float ResolveContact(Pen& A, Pen* B, Vector2 cp, Vector2 n, float e, float mu) {
    const Vector2 ra = vsub(cp, A.pos);
    const Vector2 rb = B ? vsub(cp, B->pos) : Vector2{0, 0};
    const float imA = 1.f / kMass, iiA = 1.f / kInertia;
    const float imB = B ? 1.f / kMass : 0.f, iiB = B ? 1.f / kInertia : 0.f;

    Vector2 vr = vsub(B ? VelAt(*B, rb) : Vector2{0, 0}, VelAt(A, ra));
    float vn = vdot(vr, n);
    if (vn >= 0.f) return 0.f;

    float raN = vcross(ra, n), rbN = vcross(rb, n);
    float denom = imA + imB + raN * raN * iiA + rbN * rbN * iiB;
    float ee = (-vn > 40.f) ? e : 0.f;                 // no bounce for tiny speeds
    float j = -(1.f + ee) * vn / denom;
    Vector2 P = vmul(n, j);

    A.vel = vsub(A.vel, vmul(P, imA));
    A.angVel -= vcross(ra, P) * iiA;
    if (B) { B->vel = vadd(B->vel, vmul(P, imB)); B->angVel += vcross(rb, P) * iiB; }

    // Coulomb friction along the contact tangent.
    vr = vsub(B ? VelAt(*B, rb) : Vector2{0, 0}, VelAt(A, ra));
    Vector2 t = vsub(vr, vmul(n, vdot(vr, n)));
    float tl = vlen(t);
    if (tl > 1e-4f) {
        t = vmul(t, 1.f / tl);
        float raT = vcross(ra, t), rbT = vcross(rb, t);
        float dT = imA + imB + raT * raT * iiA + rbT * rbT * iiB;
        float jt = -vdot(vr, t) / dT;
        jt = std::clamp(jt, -mu * j, mu * j);
        Vector2 Pt = vmul(t, jt);
        A.vel = vsub(A.vel, vmul(Pt, imA));
        A.angVel -= vcross(ra, Pt) * iiA;
        if (B) { B->vel = vadd(B->vel, vmul(Pt, imB)); B->angVel += vcross(rb, Pt) * iiB; }
    }
    return -vn;
}

inline void ApplyFriction(Pen& p, float dt) {
    // Coulomb sliding friction always opposes the velocity vector (so shots fly
    // straight); its strength blends between the axial and lateral coefficient.
    float sp = vlen(p.vel);
    if (sp > 0.f) {
        Vector2 u = vmul(p.vel, 1.f / sp);
        float ca = vdot(u, p.Axis());
        float a = kSlideDecel * ca * ca + kSlideDecelLat * (1.f - ca * ca);
        sp = std::max(0.f, sp - a * dt);
        p.vel = vmul(u, sp);
    }
    p.angVel = shrink(p.angVel, kSpinDecel * dt);
    if (vlen2(p.vel) < 16.f) p.vel = {0, 0};
    if (std::fabs(p.angVel) < 0.04f) p.angVel = 0.f;
}

// ----------------------------------------------------------------- world
struct World {
    Rectangle             table{0, 0, 0, 0};
    Pen                   pens[2];
    std::vector<Obstacle> obstacles;
    std::vector<Event>    events;
    bool  record = true;      // false while the AI simulates
    bool  penHit = false;     // the two pens touched since last reset
    float coolPP = 0.f, coolPO = 0.f;

    void Step(float dt);
    bool Settled(bool fastFall = false) const {
        for (const Pen& p : pens) {
            if (p.falling) { if (!fastFall && !p.Gone()) return false; }
            else if (p.Moving()) return false;
        }
        return true;
    }
    void Stop() { for (Pen& p : pens) { p.vel = {0, 0}; p.angVel = 0.f; } }

private:
    void Emit(Event::Kind k, float inten, Vector2 pos, int pen) {
        if (record) events.push_back({k, inten, pos, pen});
    }
    void SolvePenPen();
    void SolveObstacles();
};

inline void World::SolvePenPen() {
    Pen& A = pens[0];
    Pen& B = pens[1];
    if (A.falling || B.falling) return;
    const float reach = A.length * 0.5f + B.length * 0.5f + 2.f * kSegR;
    if (vlen2(vsub(B.pos, A.pos)) > reach * reach) return;

    const float rr = 2.f * kSegR;
    float maxImpact = 0.f;
    Vector2 where{0, 0};
    for (int it = 0; it < 3; ++it) {
        for (int i = 0; i < kSegs; ++i) {
            for (int j = 0; j < kSegs; ++j) {
                Vector2 pa = A.SegPos(i), pb = B.SegPos(j);
                Vector2 d = vsub(pb, pa);
                float d2 = vlen2(d);
                if (d2 >= rr * rr) continue;
                float dist = std::sqrt(d2);
                Vector2 n = dist > 1e-4f ? vmul(d, 1.f / dist) : vnorm(vsub(B.pos, A.pos));
                float depth = rr - dist;
                Vector2 cp = vadd(pa, vmul(n, kSegR - depth * 0.5f));
                penHit = true;
                float imp = ResolveContact(A, &B, cp, n, kRestPenPen, kMuContact);
                if (imp > maxImpact) { maxImpact = imp; where = cp; }
                float corr = std::max(depth - 0.05f, 0.f) * 0.35f;
                A.pos = vsub(A.pos, vmul(n, corr * 0.5f));
                B.pos = vadd(B.pos, vmul(n, corr * 0.5f));
            }
        }
    }
    if (maxImpact > 20.f && coolPP <= 0.f) {
        Emit(Event::PenPen, maxImpact, where, -1);
        coolPP = 0.06f;
    }
}

inline void World::SolveObstacles() {
    for (int k = 0; k < 2; ++k) {
        Pen& p = pens[k];
        if (p.falling) continue;
        for (const Obstacle& ob : obstacles) {
            if (vlen2(vsub(p.pos, ob.c)) > (p.length * 0.5f + ob.r + kSegR) * (p.length * 0.5f + ob.r + kSegR)) continue;
            for (int i = 0; i < kSegs; ++i) {
                Vector2 pc = p.SegPos(i);
                Vector2 d = vsub(ob.c, pc);
                float d2 = vlen2(d), rr = ob.r + kSegR;
                if (d2 >= rr * rr) continue;
                float dist = std::sqrt(d2);
                Vector2 n = dist > 1e-4f ? vmul(d, 1.f / dist) : Vector2{1, 0};
                float depth = rr - dist;
                Vector2 cp = vadd(pc, vmul(n, kSegR));
                float imp = ResolveContact(p, nullptr, cp, n, kRestPenObs, kMuContact);
                p.pos = vsub(p.pos, vmul(n, std::max(depth - 0.05f, 0.f) * 0.8f));
                if (imp > 20.f && coolPO <= 0.f) {
                    Emit(Event::PenObstacle, imp, cp, k);
                    coolPO = 0.06f;
                }
            }
        }
    }
}

inline void World::Step(float dt) {
    coolPP -= dt;
    coolPO -= dt;

    for (int k = 0; k < 2; ++k) {
        Pen& p = pens[k];
        if (p.falling) {                       // gravity drop: no friction, no collisions
            p.pos   = vadd(p.pos, vmul(p.vel, dt));
            p.angle += p.angVel * dt;
            p.fallVz += kGravity * dt;
            p.fallZ  += p.fallVz * dt;
            if (!p.thudPlayed && p.fallZ >= kLandZ) {
                p.thudPlayed = true;
                Emit(Event::FallLand, p.fallVz, p.pos, k);
            }
            continue;
        }
        ApplyFriction(p, dt);
        p.pos   = vadd(p.pos, vmul(p.vel, dt));
        p.angle += p.angVel * dt;
    }

    SolvePenPen();
    SolveObstacles();

    for (int k = 0; k < 2; ++k) {
        Pen& p = pens[k];
        if (p.falling) continue;
        float sp = vlen(p.vel);
        if (sp > kMaxSpeed) p.vel = vmul(p.vel, kMaxSpeed / sp);
        p.angVel = std::clamp(p.angVel, -kMaxSpin, kMaxSpin);
        if (!CheckCollisionPointRec(p.pos, table)) {
            p.falling = true;
            p.fallVz  = 0.f;
            Emit(Event::FallStart, vlen(p.vel), p.pos, k);
        }
    }
}

// ------------------------------------------------------------ flick model
// Finger hits the pen at `contact` along its body. Linear speed follows the
// flick power (centre = full knockback; ends lose a little). Torque comes from
// the lever arm; the cap end additionally "snaps" the pen into a spin.
inline void ApplyFlick(Pen& p, const Shot& s) {
    Vector2 d  = {std::cos(s.angle), std::sin(s.angle)};
    float   off = std::fabs(2.f * s.contact - 1.f);                 // 0 centre .. 1 end
    float   speed = std::clamp(s.power, 0.f, 1.f) * kMaxFlick * (1.f - 0.20f * off);

    Vector2 ax = p.Axis();
    float   xc = (s.contact - 0.5f) * p.length;
    float   lever = vcross(ax, d) * xc;
    float   w = kMass * speed * lever / kInertia * 0.30f;

    float capSpin = std::max(0.f, 1.f - 2.f * s.contact);           // 1 at cap, 0 at centre
    float sgn = vcross(ax, d) >= 0.f ? 1.f : -1.f;
    w += sgn * capSpin * speed / p.length * 1.7f;

    p.vel    = vmul(d, speed);
    p.angVel = std::clamp(w, -kMaxSpin, kMaxSpin);
}

// --------------------------------------------------------------- queries
inline float EdgeMargin(const Rectangle& t, Vector2 p) {
    return std::min(std::min(p.x - t.x, t.x + t.width - p.x),
                    std::min(p.y - t.y, t.y + t.height - p.y));
}

struct SimOutcome {
    bool  hit = false, selfOut = false, oppOut = false;
    float oppMargin = 0.f, myMargin = 0.f;
};

// Headless simulation of one shot (used by the AI). Same physics, same step.
inline SimOutcome Simulate(const World& base, int shooter, const Shot& shot, float maxT = 4.5f) {
    World w = base;
    w.record = false;
    w.events.clear();
    w.penHit = false;
    ApplyFlick(w.pens[shooter], shot);
    const int steps = int(maxT / kStep);
    for (int i = 0; i < steps; ++i) {
        w.Step(kStep);
        if (w.Settled(true)) break;
    }
    SimOutcome o;
    o.hit       = w.penHit;
    o.selfOut   = w.pens[shooter].falling;
    o.oppOut    = w.pens[1 - shooter].falling;
    o.myMargin  = EdgeMargin(w.table, w.pens[shooter].pos);
    o.oppMargin = EdgeMargin(w.table, w.pens[1 - shooter].pos);
    return o;
}

}  // namespace phys
