// KOLOM KHELA  -  Bangladeshi school-bench pen fight
// C++17 + raylib 5.0. Zero external assets: vector graphics + synthesised audio.
//
//   main.cpp       state machine, UI, game loop, AI, foul / miss rules
//   PenPhysics.hpp multi-segment rigid-body physics
//   SoundSynth.hpp procedural PCM audio
#include "raylib.h"

#include "PenPhysics.hpp"
#include "SoundSynth.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

using namespace phys;

namespace {

// ------------------------------------------------------------------ palette
constexpr float VW = 1280.f, VH = 720.f;                 // virtual resolution

const Color MINT   = {188, 232, 208, 255};
const Color INK    = {21, 52, 43, 255};                   // dark forest ink
const Color P1COL   = {35, 99, 235, 255};                  // player 1
const Color P2COL = {244, 122, 30, 255};                 // player 2 / AI
const Color WOOD_A = {204, 148, 88, 255};
const Color WOOD_B = {189, 130, 74, 255};
const Color CREAM  = {250, 247, 235, 255};
const Color PANELC = {232, 247, 237, 255};
const Color DANGER    = {214, 56, 56, 255};
const Color OKGREEN  = {46, 160, 90, 255};
const Color GREY   = {160, 176, 168, 255};

// ------------------------------------------------------------------ helpers
float Rf(float a, float b) { return a + (b - a) * float(GetRandomValue(0, 10000)) / 10000.f; }
float Lerpf(float a, float b, float t) { return a + (b - a) * t; }
float EaseOut(float t) { t = std::clamp(t, 0.f, 1.f); return 1.f - (1.f - t) * (1.f - t) * (1.f - t); }
Color PCol(int i) { return i == 0 ? P1COL : P2COL; }
Color Darker(Color c, float k) { return {(unsigned char)(c.r * k), (unsigned char)(c.g * k), (unsigned char)(c.b * k), c.a}; }
bool Inside(Vector2 p, Rectangle r) { return CheckCollisionPointRec(p, r); }

Font gFont;

// y is the vertical centre of the text. align: 0 left, 1 centre, 2 right
void Txt(const std::string& s, float x, float y, float size, Color c, int align = 1) {
    float sp = size / 10.f;
    Vector2 m = MeasureTextEx(gFont, s.c_str(), size, sp);
    float px = align == 1 ? x - m.x * 0.5f : (align == 2 ? x - m.x : x);
    DrawTextEx(gFont, s.c_str(), {px, y - m.y * 0.5f}, size, sp, c);
}

void RR(Rectangle r, float radius, Color c) {
    float m = std::min(r.width, r.height);
    DrawRectangleRounded(r, std::min(1.f, 2.f * radius / m), 14, c);
}

// Flat panel with a dark-ink border and a hard drop shadow.
void Panel(Rectangle r, float radius, Color fill, float bw = 4.f, bool shadow = true) {
    if (shadow) RR({r.x, r.y + 6, r.width, r.height}, radius, Fade(INK, 0.35f));
    RR({r.x - bw, r.y - bw, r.width + 2 * bw, r.height + 2 * bw}, radius + bw, INK);
    RR(r, radius, fill);
}

// Triangle regardless of winding (raylib culls clockwise triangles).
void FillTri(Vector2 a, Vector2 b, Vector2 c, Color col) {
    float cr = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cr > 0) DrawTriangle(a, c, b, col); else DrawTriangle(a, b, c, col);
}

void DashedLine(Vector2 a, Vector2 b, float dash, float gap, float th, Color c) {
    Vector2 d = vsub(b, a);
    float L = vlen(d);
    if (L < 1.f) return;
    d = vmul(d, 1.f / L);
    for (float s = 0; s < L; s += dash + gap)
        DrawLineEx(vadd(a, vmul(d, s)), vadd(a, vmul(d, std::min(s + dash, L))), th, c);
}

// ------------------------------------------------------------------ pointer
struct Pointer {
    Vector2 pos{0, 0}, pressPos{0, 0}, rawLast{0, 0};
    bool down = false, pressed = false, released = false, prev = false;

    void Update(const Camera2D& cam) {
        bool d;
#if defined(__ANDROID__)
        d = GetTouchPointCount() > 0;
        if (d) rawLast = GetTouchPosition(0);
#else
        d = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        rawLast = GetMousePosition();
#endif
        pos = {(rawLast.x - cam.offset.x) / cam.zoom, (rawLast.y - cam.offset.y) / cam.zoom};
        pressed = d && !prev;
        released = !d && prev;
        down = d;
        prev = d;
        if (pressed) pressPos = pos;
    }
};

// -------------------------------------------------------------------- types
enum class St { Menu, HowTo, Banner, Aim, AiThink, AiAim, Sim, Penalty, RoundEnd };
enum class Mode { Campaign, Friend };
enum class EndKind { RoundWon, MatchOver, LevelClear, GameOver };
enum class Why { Knockoff, Foul, MissPush };
enum class Act { None, Launch, ToMenu, ToggleSound, StartCampaign, StartFriend, HowTo, Primary };

struct FloatText { std::string text; Vector2 pos; float life, maxLife, size; Color col; };
struct Particle  { Vector2 pos, vel; float life, maxLife, size; Color col; };

// ----------------------------------------------------------------- the game
struct Game {
    Camera2D  cam{};
    Pointer   ptr;
    SoundSynth snd;
    bool      quit = false;

    St   st = St::Menu;
    Mode mode = Mode::Campaign;
    float stTime = 0.f;

    int  level = 1, bestLevel = 1;
    int  wins[2] = {0, 0};
    int  roundNo = 0;
    int  turn = 0;                 // whose strike it is
    int  shooter = 0;              // who launched the current shot
    int  misses[2] = {0, 0};

    World     world;
    Rectangle table{0, 0, 0, 0};
    float     acc = 0.f, simTime = 0.f;

    // aiming
    bool  dragging = false, aimSet = false, sliderDrag = false;
    Vector2 dragStart{0, 0};
    float aimAngle = 0.f, aimPower = 0.f, contact = 0.5f;

    // AI
    Shot aiShot;

    // miss penalty tween
    Vector2 penFrom{0, 0}, penTo{0, 0};
    bool    penaltyOut = false;

    // round end
    int     roundWinner = 0;
    Why     why = Why::Knockoff;
    EndKind endKind = EndKind::RoundWon;
    float   endSoundDelay = -1.f;
    SoundSynth::Id endSound = SoundSynth::Victory;

    // fx
    std::vector<FloatText> floats;
    std::vector<Particle>  parts;
    float shake = 0.f;

    Act act = Act::None;

    // ---------------------------------------------------------------- names
    std::string Name(int i) const {
        if (mode == Mode::Campaign) return i == 0 ? "YOU" : "COMPUTER";
        return i == 0 ? "PLAYER 1" : "PLAYER 2";
    }
    std::string Subj(int i) const {
        if (mode == Mode::Campaign) return i == 0 ? "You" : "The computer";
        return i == 0 ? "Player 1" : "Player 2";
    }
    std::string Poss(int i) const {
        if (mode == Mode::Campaign) return i == 0 ? "Your" : "The computer's";
        return i == 0 ? "Player 1's" : "Player 2's";
    }
    std::string PossLower(int i) const {
        if (mode == Mode::Campaign) return i == 0 ? "your" : "the computer's";
        return i == 0 ? "Player 1's" : "Player 2's";
    }
    bool HumanTurn() const { return mode == Mode::Friend || turn == 0; }

    // ------------------------------------------------------------ lifecycle
    void Run() {
        SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
        InitWindow(1280, 720, "KOLOM KHELA");
        SetExitKey(KEY_NULL);
        SetTargetFPS(60);
        SetRandomSeed((unsigned int)std::time(nullptr));
        InitAudioDevice();
        snd.Init();
        gFont = GetFontDefault();
        SetTextureFilter(gFont.texture, TEXTURE_FILTER_BILINEAR);

        while (!WindowShouldClose() && !quit) Frame();

        snd.Unload();
        CloseAudioDevice();
        CloseWindow();
    }

    void Frame() {
        float dt = std::min(GetFrameTime(), 0.05f);

        float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
        float zoom = std::min(sw / VW, sh / VH);
        shake = std::max(0.f, shake - dt * 22.f);
        cam.zoom = zoom;
        cam.target = {0, 0};
        cam.rotation = 0.f;
        cam.offset = {(sw - VW * zoom) * 0.5f + Rf(-1, 1) * shake * zoom,
                      (sh - VH * zoom) * 0.5f + Rf(-1, 1) * shake * zoom};
        ptr.Update(cam);

        act = Act::None;
        Update(dt);

        BeginDrawing();
        ClearBackground(MINT);
        BeginMode2D(cam);
        Draw();
        EndMode2D();
        EndDrawing();

        ApplyAct();
    }

    // --------------------------------------------------------------- setup
    void StartCampaign() { mode = Mode::Campaign; level = 1; wins[0] = wins[1] = 0; roundNo = 0; NewRound(); }
    void StartFriend()   { mode = Mode::Friend;   level = 1; wins[0] = wins[1] = 0; roundNo = 0; NewRound(); }

    void NewRound() {
        SetupRound();
        int first = mode == Mode::Campaign ? 0 : (roundNo % 2);
        ++roundNo;
        misses[0] = misses[1] = 0;
        BeginTurn(first);
    }

    void SetupRound() {
        int k = mode == Mode::Campaign ? std::min(level - 1, 12) : 0;     // tighter bench each level
        float w = 1100.f - 36.f * k, h = 440.f - 16.f * k;
        table = {640.f - w * 0.5f, 322.f - h * 0.5f, w, h};

        world = World();
        world.table = table;
        float cy = table.y + h * 0.5f;
        float inset = w * 0.22f;                               // start well back from the edge
        world.pens[0].Init({table.x + inset, cy + Rf(-50, 50)}, Rf(-1.1f, 1.1f));
        world.pens[1].Init({table.x + w - inset, cy + Rf(-50, 50)}, kPi + Rf(-1.1f, 1.1f));

        int nObs = (mode == Mode::Campaign && level >= 2) ? std::min(level / 2 + 1, 5) : 0;
        for (int tries = 0; tries < 300 && (int)world.obstacles.size() < nObs; ++tries) {
            Obstacle o{{640.f + Rf(-w * 0.17f, w * 0.17f), Rf(table.y + 60.f, table.y + h - 60.f)}, Rf(14.f, 22.f)};
            bool ok = true;
            for (const Obstacle& q : world.obstacles)
                if (vlen(vsub(o.c, q.c)) < o.r + q.r + 70.f) ok = false;
            if (ok) world.obstacles.push_back(o);
        }

        floats.clear();
        parts.clear();
        aimSet = false;
        contact = 0.5f;
        acc = 0.f;
    }

    void BeginTurn(int who) {
        turn = who;
        st = St::Banner;
        stTime = 0.f;
        aimSet = false;
        dragging = false;
        sliderDrag = false;
        aimPower = 0.f;
        contact = 0.5f;
        world.penHit = false;
    }

    // ------------------------------------------------------------ gameplay
    void Launch(int who, const Shot& s) {
        ApplyFlick(world.pens[who], s);
        snd.Play(SoundSynth::Flick, 0.45f + 0.55f * s.power, Rf(0.92f, 1.08f));
        shooter = who;
        simTime = 0.f;
        acc = 0.f;
        world.penHit = false;
        penaltyOut = false;
        dragging = false;
        st = St::Sim;
        stTime = 0.f;
    }

    void AddFloat(const std::string& t, Vector2 p, Color c, float size = 30.f, float life = 1.1f) {
        floats.push_back({t, p, life, life, size, c});
    }

    void Sparks(Vector2 p, int n, Color c, float speed) {
        for (int i = 0; i < n; ++i) {
            float a = Rf(0.f, 2.f * kPi), s = Rf(0.3f, 1.f) * speed;
            parts.push_back({p, {std::cos(a) * s, std::sin(a) * s}, Rf(0.25f, 0.5f), 0.5f, Rf(2.f, 4.5f), c});
        }
    }

    void ProcessEvents() {
        for (const Event& e : world.events) {
            switch (e.kind) {
                case Event::PenPen: {
                    float v = std::clamp(e.intensity / 650.f, 0.25f, 1.f);
                    snd.Play(SoundSynth::PenClick, v, Rf(0.9f, 1.15f));
                    shake = std::max(shake, std::min(9.f, e.intensity / 110.f));
                    Sparks(e.pos, 9, CREAM, 220.f);
                    if (e.intensity > 150.f) {
                        const char* w[3] = {"CLACK!", "THWACK!", "BAM!"};
                        AddFloat(w[e.intensity > 600.f ? 2 : (e.intensity > 300.f ? 1 : 0)],
                                 {e.pos.x, e.pos.y - 34.f}, INK, 30.f, 0.9f);
                    }
                } break;
                case Event::PenObstacle:
                    snd.Play(SoundSynth::WoodTap, std::clamp(e.intensity / 600.f, 0.25f, 1.f), Rf(0.8f, 1.2f));
                    Sparks(e.pos, 5, WOOD_A, 140.f);
                    break;
                case Event::FallStart:
                    AddFloat("OFF!", {e.pos.x, e.pos.y - 20.f}, DANGER, 34.f, 1.0f);
                    snd.Play(SoundSynth::WoodTap, 0.35f, 0.7f);
                    break;
                case Event::FallLand:
                    snd.Play(SoundSynth::FallThud, 0.9f, Rf(0.9f, 1.1f));
                    shake = std::max(shake, 6.f);
                    break;
            }
        }
        world.events.clear();
    }

    // Called once every pen has stopped (or finished falling).
    void OnSettled() {
        Pen& me = world.pens[shooter];
        Pen& op = world.pens[1 - shooter];
        if (me.falling) {                                   // self-knockout => FOUL, immediate loss
            EndRound(1 - shooter, penaltyOut ? Why::MissPush : Why::Foul);
        } else if (op.falling) {
            EndRound(shooter, Why::Knockoff);
        } else if (!world.penHit) {                         // no contact => missed turn
            ++misses[shooter];
            int m = misses[shooter];
            AddFloat(m >= 2 ? "MISS x" + std::to_string(m) + "!" : "MISS!",
                     {me.pos.x, me.pos.y - 40.f}, DANGER, 34.f, 1.3f);
            penFrom = me.pos;
            float dist = m >= 2 ? 44.f * float(m - 1) : 0.f;  // consecutive misses shove the pen to the edge
            Vector2 dir = {shooter == 0 ? -1.f : 1.f, 0.f};
            penTo = vadd(penFrom, vmul(dir, dist));
            st = St::Penalty;
            stTime = 0.f;
        } else {
            misses[shooter] = 0;
            BeginTurn(1 - shooter);
        }
    }

    void EndRound(int winner, Why w) {
        roundWinner = winner;
        why = w;
        st = St::RoundEnd;
        stTime = 0.f;
        if (mode == Mode::Friend) {
            ++wins[winner];
            endKind = wins[winner] >= 2 ? EndKind::MatchOver : EndKind::RoundWon;
            endSound = SoundSynth::Victory;
        } else if (winner == 0) {
            endKind = EndKind::LevelClear;
            bestLevel = std::max(bestLevel, level);
            endSound = SoundSynth::Victory;
        } else {
            endKind = EndKind::GameOver;
            endSound = SoundSynth::Defeat;
        }
        if (w == Why::Knockoff) { endSoundDelay = 0.35f; }
        else { snd.Play(SoundSynth::Foul, 0.8f); endSoundDelay = 0.6f; }
    }

    // -------------------------------------------------------------------- AI
    float ScoreOutcome(const SimOutcome& o) const {
        float s = 0.f;
        if (o.selfOut) s -= 6000.f;
        if (o.oppOut)  s += 5000.f;
        s += o.hit ? 400.f : -600.f;
        s += (260.f - std::clamp(o.oppMargin, 0.f, 260.f)) * 1.5f;   // push the enemy towards the edge
        s -= (200.f - std::clamp(o.myMargin, 0.f, 200.f)) * 1.2f;    // keep my own pen safe
        return s;
    }

    // Calculates angle, force and strike point by simulating candidate shots on a
    // copy of the world; skill (candidate count + execution noise) scales with level.
    Shot AiChoose(int who) {
        const Pen& me = world.pens[who];
        const Pen& op = world.pens[1 - who];
        int cand = std::min(30 + level * 14, 220);
        float dist = vlen(vsub(op.pos, me.pos));
        float needP = std::sqrt(2.f * kSlideDecel * dist) / kMaxFlick;
        Vector2 opAx = op.Axis();

        Shot best;
        float bestScore = -1e9f;
        for (int k = 0; k < cand; ++k) {
            Vector2 target = op.pos;
            switch (k % 4) {
                case 1: target = vadd(op.pos, vmul(opAx, Rf(-0.5f, 0.5f) * op.length)); break;
                case 2: target = vadd(op.pos, vadd(vmul(opAx, Rf(-0.5f, 0.5f) * op.length), vmul(vperp(opAx), Rf(-12.f, 12.f)))); break;
                case 3: target = vadd(op.pos, {Rf(-60.f, 60.f), Rf(-60.f, 60.f)}); break;
                default: break;
            }
            Shot s;
            s.angle = std::atan2(target.y - me.pos.y, target.x - me.pos.x);
            s.power = std::clamp(needP * Rf(0.8f, 1.7f), 0.15f, 1.f);
            int cm = k % 5;
            s.contact = cm == 0 ? Rf(0.f, 0.3f) : (cm == 1 ? 0.5f : Rf(0.25f, 1.f));
            float sc = ScoreOutcome(Simulate(world, who, s));
            if (sc > bestScore) { bestScore = sc; best = s; }
        }

        // Human-like execution error; shrinks as levels go up.
        float noiseA = std::max(0.012f, 0.11f * std::exp(-0.30f * float(level - 1)));
        float noiseP = std::max(0.010f, 0.08f * std::exp(-0.28f * float(level - 1)));
        float blunder = std::max(0.f, 0.10f - 0.025f * float(level - 1));
        if (Rf(0.f, 1.f) < blunder) {
            best.angle += Rf(-0.35f, 0.35f);
            best.power = Rf(0.3f, 1.f);
        }
        best.angle += (Rf(-1.f, 1.f) + Rf(-1.f, 1.f)) * 0.5f * noiseA;
        best.power = std::clamp(best.power * (1.f + Rf(-1.f, 1.f) * noiseP), 0.1f, 1.f);
        return best;
    }

    // ---------------------------------------------------------------- update
    void Update(float dt) {
        stTime += dt;

        if (IsKeyPressed(KEY_ESCAPE)) {
            if (st == St::Menu) quit = true; else { st = St::Menu; }
        }

        // floating text / particles
        for (auto& f : floats) { f.life -= dt; f.pos.y -= 34.f * dt; }
        floats.erase(std::remove_if(floats.begin(), floats.end(), [](const FloatText& f) { return f.life <= 0.f; }), floats.end());
        for (auto& p : parts) { p.life -= dt; p.pos = vadd(p.pos, vmul(p.vel, dt)); p.vel = vmul(p.vel, 0.92f); }
        parts.erase(std::remove_if(parts.begin(), parts.end(), [](const Particle& p) { return p.life <= 0.f; }), parts.end());

        switch (st) {
            case St::Menu:
            case St::HowTo:
                break;

            case St::Banner: {
                float dur = mode == Mode::Friend ? 2.2f : 1.2f;
                if (stTime > dur || (ptr.pressed && stTime > 0.4f)) {
                    st = HumanTurn() ? St::Aim : St::AiThink;
                    stTime = 0.f;
                }
            } break;

            case St::Aim: UpdateAim(dt); break;

            case St::AiThink:
                if (stTime > 0.85f) {
                    aiShot = AiChoose(turn);
                    st = St::AiAim;
                    stTime = 0.f;
                }
                break;

            case St::AiAim: {
                float p = EaseOut(stTime / 0.8f);
                aimAngle = aiShot.angle;
                aimPower = aiShot.power * p;
                contact = Lerpf(0.5f, aiShot.contact, p);
                if (stTime > 1.15f) { aimPower = aiShot.power; Launch(turn, aiShot); }
            } break;

            case St::Sim: {
                acc += dt;
                for (int n = 0; acc >= kStep && n < 24; ++n) { world.Step(kStep); acc -= kStep; }
                if (acc > kStep) acc = 0.f;
                ProcessEvents();
                simTime += dt;
                if (simTime > 9.f) world.Stop();
                if (simTime > 0.2f && world.Settled()) OnSettled();
            } break;

            case St::Penalty: {
                Pen& p = world.pens[shooter];
                float t = std::clamp(stTime / 0.7f, 0.f, 1.f);
                p.pos = {Lerpf(penFrom.x, penTo.x, EaseOut(t)), Lerpf(penFrom.y, penTo.y, EaseOut(t))};
                if (stTime > 0.4f && !penaltyOut && vlen(vsub(penTo, penFrom)) > 0.f && stTime < 0.45f)
                    snd.Play(SoundSynth::WoodTap, 0.4f, 0.8f);
                if (stTime > 1.0f) {
                    if (!CheckCollisionPointRec(p.pos, table)) {      // shoved right off the bench
                        p.falling = true;
                        p.fallVz = 0.f;
                        p.vel = {shooter == 0 ? -140.f : 140.f, 0.f};
                        penaltyOut = true;
                        simTime = 0.f;
                        acc = 0.f;
                        st = St::Sim;
                        stTime = 0.f;
                        AddFloat("PUSHED OFF!", {p.pos.x, p.pos.y - 30.f}, DANGER, 34.f, 1.2f);
                    } else {
                        BeginTurn(1 - shooter);
                    }
                }
            } break;

            case St::RoundEnd:
                if (endSoundDelay >= 0.f) {
                    endSoundDelay -= dt;
                    if (endSoundDelay < 0.f) snd.Play(endSound, 0.9f);
                }
                // keep fall animations playing under the overlay
                acc += dt;
                for (int n = 0; acc >= kStep && n < 24; ++n) { world.Step(kStep); acc -= kStep; }
                if (acc > kStep) acc = 0.f;
                ProcessEvents();
                break;
        }
    }

    // Layout of the bottom control panel
    static Rectangle SliderRect() { return {330.f, 646.f, 540.f, 30.f}; }
    static Rectangle LaunchRect() { return {930.f, 616.f, 270.f, 76.f}; }
    static Rectangle MenuRect()   { return {430.f, 16.f, 100.f, 40.f}; }
    static Rectangle SndRect()    { return {750.f, 16.f, 100.f, 40.f}; }

    void UpdateAim(float dt) {
        const float maxDrag = 190.f;
        Pen& me = world.pens[turn];

        // strike-point slider
        Rectangle sr = SliderRect();
        Rectangle grab = {sr.x - 20.f, sr.y - 24.f, sr.width + 40.f, sr.height + 48.f};
        if (ptr.pressed && Inside(ptr.pressPos, grab)) sliderDrag = true;
        if (sliderDrag && ptr.down) contact = std::clamp((ptr.pos.x - sr.x) / sr.width, 0.f, 1.f);
        if (!ptr.down) sliderDrag = false;
        if (IsKeyDown(KEY_LEFT))  contact = std::max(0.f, contact - dt * 0.7f);
        if (IsKeyDown(KEY_RIGHT)) contact = std::min(1.f, contact + dt * 0.7f);

        // drag-to-aim (slingshot: pull back, the pen flies the other way)
        auto inAimArea = [](Vector2 p) { return p.y > 70.f && p.y < 580.f; };
        if (ptr.pressed && inAimArea(ptr.pressPos) && !sliderDrag) { dragging = true; dragStart = ptr.pos; }
        if (dragging && ptr.down) {
            Vector2 dv = vsub(dragStart, ptr.pos);
            float l = vlen(dv);
            if (l > 14.f) {
                aimAngle = std::atan2(dv.y, dv.x);
                aimPower = std::min(1.f, l / maxDrag);
                aimSet = true;
            }
        }
        if (ptr.released) dragging = false;
        (void)me;

        if (aimSet && (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER))) act = Act::Launch;
    }

    void ApplyAct() {
        switch (act) {
            case Act::None: break;
            case Act::Launch:
                if (st == St::Aim && aimSet) {
                    Shot s{aimAngle, aimPower, contact};
                    Launch(turn, s);
                }
                break;
            case Act::ToMenu:       snd.Play(SoundSynth::UiClick, 0.6f); st = St::Menu; break;
            case Act::ToggleSound:  snd.SetMuted(!snd.Muted()); snd.Play(SoundSynth::UiClick, 0.6f); break;
            case Act::StartCampaign: snd.Play(SoundSynth::UiClick, 0.6f); StartCampaign(); break;
            case Act::StartFriend:   snd.Play(SoundSynth::UiClick, 0.6f); StartFriend(); break;
            case Act::HowTo:         snd.Play(SoundSynth::UiClick, 0.6f); st = St::HowTo; break;
            case Act::Primary:
                snd.Play(SoundSynth::UiClick, 0.6f);
                switch (endKind) {
                    case EndKind::LevelClear: ++level; NewRound(); break;
                    case EndKind::GameOver:   NewRound(); break;
                    case EndKind::RoundWon:   NewRound(); break;
                    case EndKind::MatchOver:  wins[0] = wins[1] = 0; roundNo = 0; NewRound(); break;
                }
                break;
        }
    }

    // ------------------------------------------------------------- UI widgets
    bool Button(Rectangle r, const std::string& label, Color fill, Color textCol = INK,
                bool enabled = true, float fs = 26.f) {
        bool over = Inside(ptr.pos, r);
        bool held = enabled && ptr.down && Inside(ptr.pressPos, r) && over;
        float off = held ? 4.f : 0.f;
        Color f = enabled ? fill : Color{196, 208, 200, 255};
        float rad = std::min(r.height * 0.5f, 22.f);
        RR({r.x - 3, r.y + 3, r.width + 6, r.height + 6}, rad + 3, Fade(INK, 0.4f));
        Rectangle b = {r.x, r.y + off, r.width, r.height};
        RR({b.x - 3, b.y - 3, b.width + 6, b.height + 6}, rad + 3, INK);
        RR(b, rad, f);
        RR({b.x + 4, b.y + 3, b.width - 8, b.height * 0.35f}, rad * 0.6f, Fade(WHITE, enabled ? 0.18f : 0.1f));
        Txt(label, b.x + b.width * 0.5f, b.y + b.height * 0.5f + 1.f, fs, enabled ? textCol : GREY);
        return enabled && ptr.released && Inside(ptr.pressPos, r) && over;
    }

    // ---------------------------------------------------------------- drawing
    void Draw() {
        // faint background dots
        for (int y = 0; y < 14; ++y)
            for (int x = 0; x < 26; ++x)
                DrawCircleV({x * 52.f + (y % 2) * 26.f, y * 52.f}, 2.f, Fade(INK, 0.06f));

        switch (st) {
            case St::Menu:  DrawMenu(); break;
            case St::HowTo: DrawHowTo(); break;
            default:        DrawGame(); break;
        }
    }

    // ---------- table
    void DrawTable() {
        const Rectangle t = table;
        const float b = 12.f;
        RR({t.x - b + 4, t.y - b + 12, t.width + 2 * b, t.height + 2 * b}, 22.f, Fade(INK, 0.22f));
        // legs
        RR({t.x + 50, t.y + t.height - 10, 46, 46}, 6.f, INK);
        RR({t.x + t.width - 96, t.y + t.height - 10, 46, 46}, 6.f, INK);
        // frame + surface
        RR({t.x - b, t.y - b, t.width + 2 * b, t.height + 2 * b}, 22.f, INK);
        RR(t, 10.f, WOOD_A);
        int n = std::max(3, int(t.width / 52.f));
        if (n % 2 == 0) ++n;                                   // odd => both end stripes keep the rounded base
        float sw = t.width / float(n);
        for (int i = 1; i < n - 1; i += 2) DrawRectangleRec({t.x + sw * i, t.y, sw, t.height}, WOOD_B);
        for (int i = 0; i < 46; ++i) {                          // wood grain
            float gx = t.x + float((i * 97) % 1000) / 1000.f * t.width;
            float gy = t.y + float((i * 53 + 17) % 1000) / 1000.f * (t.height - 60.f);
            float gl = 24.f + float((i * 37) % 50);
            DrawLineEx({gx, gy}, {gx + (i % 3 - 1) * 2.f, gy + gl}, 1.6f, Fade(INK, 0.07f));
        }
        DrawLineEx({t.x + 10, t.y + 3}, {t.x + t.width - 10, t.y + 3}, 3.f, Fade(WHITE, 0.28f));
        DashedLine({640.f, t.y + 14}, {640.f, t.y + t.height - 14}, 10.f, 14.f, 2.f, Fade(INK, 0.10f));
        DrawRing({640.f, t.y + t.height * 0.5f}, 34.f, 36.f, 0, 360, 36, Fade(INK, 0.10f));
        // "danger" tint near the edge
        DrawRectangleLinesEx({t.x + 3, t.y + 3, t.width - 6, t.height - 6}, 4.f, Fade(DANGER, 0.10f));
    }

    void DrawObstacles() {
        for (const Obstacle& o : world.obstacles) {
            DrawCircleV({o.c.x + 4, o.c.y + 6}, o.r, Fade(INK, 0.3f));
            DrawCircleV(o.c, o.r + 3, INK);
            DrawCircleV(o.c, o.r, {52, 74, 150, 255});
            DrawCircleV({o.c.x - o.r * 0.28f, o.c.y - o.r * 0.30f}, o.r * 0.30f, Fade(WHITE, 0.5f));
            DrawCircleV(o.c, o.r * 0.45f, {30, 44, 100, 255});
            DrawCircleV(o.c, o.r * 0.22f, INK);
        }
    }

    // ---------- pens
    void DrawPenShape(const Pen& p, Color cap, bool shadow) {
        const float hl = p.length * 0.5f, h = kSegR;
        float s = 1.f, alpha = 1.f;
        Vector2 c = p.pos;
        if (p.falling) {
            s = std::max(0.45f, 1.f - p.fallZ / 520.f);
            c.y += p.fallZ * 0.55f;
            alpha = std::clamp(1.f - (p.fallZ - 120.f) / 160.f, 0.f, 1.f);
            if (alpha <= 0.f) return;
        }
        if (shadow) {
            if (p.falling) return;
            c = {c.x + 4.f, c.y + 6.f};
        }
        const Vector2 ax = p.Axis(), pr = vperp(ax);
        const float deg = p.angle * RAD2DEG;
        auto L = [&](float x, float y) { return Vector2{c.x + (ax.x * x + pr.x * y) * s, c.y + (ax.y * x + pr.y * y) * s}; };
        auto Seg = [&](float x0, float x1, float hh, Color col) {
            Vector2 m = L((x0 + x1) * 0.5f, 0.f);
            DrawRectanglePro({m.x, m.y, (x1 - x0) * s, 2.f * hh * s}, {(x1 - x0) * s * 0.5f, hh * s}, deg, col);
        };

        if (shadow) {
            Color sh = Fade(INK, 0.22f * alpha);
            DrawCircleV(L(-hl + h, 0), h * s, sh);
            Seg(-hl + h, hl - 4.f, h, sh);
            return;
        }
        const Color ink = Fade(INK, alpha), capC = Fade(cap, alpha), bandC = Fade(Darker(cap, 0.68f), alpha);
        const Color body = Fade(CREAM, alpha), metal = Fade({206, 212, 216, 255}, alpha);
        const float ho = h + 2.4f;
        const float tipX = hl - 22.f;
        // outline
        DrawCircleV(L(-hl + h, 0), ho * s, ink);
        Seg(-hl + h, tipX, ho, ink);
        FillTri(L(tipX, -ho), L(tipX, ho), L(hl + 2.6f, 0), ink);
        // cap, band, body
        DrawCircleV(L(-hl + h, 0), h * s, capC);
        Seg(-hl + h, -hl + h + 26.f, h, capC);
        Seg(-hl + h + 26.f, -hl + h + 31.f, h, bandC);
        Seg(-hl + h + 31.f, tipX, h, body);
        Seg(-8.f, tipX - 6.f, 1.8f, capC);                      // coloured barrel stripe
        Seg(-hl + h + 6.f, -hl + h + 22.f, 2.2f, bandC);        // pocket clip hint
        // nib cone
        FillTri(L(tipX, -h), L(tipX, h), L(hl, 0), metal);
        DrawCircleV(L(hl - 0.5f, 0), 2.4f * s, ink);
    }

    void DrawPens() {
        for (int i = 0; i < 2; ++i) DrawPenShape(world.pens[i], PCol(i), true);
        for (int i = 0; i < 2; ++i) DrawPenShape(world.pens[i], PCol(i), false);
    }

    // Strike-point marker (the ring seen on the pen in the reference art)
    void DrawStrikeRing(const Pen& p, float t, Color col) {
        Vector2 ax = p.Axis();
        float x = (t - 0.5f) * p.length;
        Vector2 c = {p.pos.x + ax.x * x, p.pos.y + ax.y * x};
        float pulse = 1.f + 0.12f * std::sin(float(GetTime()) * 7.f);
        DrawRing(c, 9.f * pulse, 12.5f * pulse, 0, 360, 28, Fade(WHITE, 0.95f));
        DrawRing(c, 12.5f * pulse, 14.5f * pulse, 0, 360, 28, Fade(col, 0.95f));
    }

    void DrawAimGuide(const Pen& p, float angle, float power, float t, Color col) {
        Vector2 d = {std::cos(angle), std::sin(angle)};
        float prec = t > 0.75f ? 1.f + (t - 0.75f) * 3.2f : 1.f;      // tip end = longer, sharper guide
        float len = (70.f + power * 300.f) * prec;
        Vector2 a = vadd(p.pos, vmul(d, 24.f)), b = vadd(a, vmul(d, len));
        DashedLine({a.x, a.y + 2.5f}, {b.x, b.y + 2.5f}, 12.f, 9.f, 5.f, Fade(INK, 0.35f));
        DashedLine(a, b, 12.f, 9.f, 4.f, Fade(WHITE, 0.97f));
        Vector2 q = vperp(d);
        FillTri(vadd(b, vmul(d, 16.f)), vadd(b, vmul(q, 10.f)), vsub(b, vmul(q, 10.f)), WHITE);
        if (t > 0.75f) {
            DrawRing(vadd(b, vmul(d, 26.f)), 7.f, 9.f, 0, 360, 24, Fade(WHITE, 0.95f));
            DrawCircleV(vadd(b, vmul(d, 26.f)), 2.5f, WHITE);
        }
        float pb = 28.f + power * 62.f;                                // slingshot pull-back handle
        Vector2 hnd = vsub(p.pos, vmul(d, pb));
        DrawLineEx(p.pos, hnd, 4.f, Fade(col, 0.85f));
        DrawCircleV(hnd, 11.f, INK);
        DrawCircleV(hnd, 8.f, col);
    }

    // ---------- HUD
    void DrawBadge(int i, bool left) {
        float cx = left ? 58.f : VW - 58.f;
        Color col = PCol(i);
        bool active = (st == St::Aim || st == St::AiThink || st == St::AiAim || st == St::Banner) && turn == i;
        if (active) {
            float pulse = 1.f + 0.08f * std::sin(float(GetTime()) * 6.f);
            DrawRing({cx, 54.f}, 33.f * pulse, 38.f * pulse, 0, 360, 36, Fade(col, 0.55f));
        }
        DrawCircleV({cx, 60.f}, 30.f, Fade(INK, 0.35f));
        DrawCircleV({cx, 54.f}, 32.f, INK);
        DrawCircleV({cx, 54.f}, 27.f, col);
        Txt(i == 0 ? "1" : (mode == Mode::Campaign ? "AI" : "2"), cx, 55.f, 28.f, WHITE);

        float tx = left ? cx + 44.f : cx - 44.f;
        int al = left ? 0 : 2;
        Txt(Name(i), tx, 40.f, 22.f, INK, al);
        if (mode == Mode::Friend) {
            for (int k = 0; k < 2; ++k) {
                float px = left ? tx + 10.f + k * 26.f : tx - 10.f - k * 26.f;
                DrawCircleV({px, 66.f}, 9.f, INK);
                DrawCircleV({px, 66.f}, 6.5f, wins[i] > k ? col : CREAM);
            }
        } else if (i == 1) {
            Txt("LEVEL " + std::to_string(level), tx, 66.f, 16.f, Fade(INK, 0.7f), al);
        } else {
            Txt("BEST LV " + std::to_string(bestLevel), tx, 66.f, 16.f, Fade(INK, 0.7f), al);
        }
        if (misses[i] > 0 && st != St::Menu) {
            std::string m = "MISSES: " + std::to_string(misses[i]);
            Txt(m, tx, 90.f, 14.f, misses[i] >= 2 ? DANGER : INK, al);
        }
    }

    void DrawHud() {
        DrawBadge(0, true);
        DrawBadge(1, false);
        Panel({540.f, 12.f, 200.f, 44.f}, 22.f, CREAM, 3.f);
        Txt(mode == Mode::Campaign ? "VS COMPUTER" : "VS FRIEND", 640.f, 35.f, 22.f, INK);
        std::string sub = mode == Mode::Campaign ? "LEVEL " + std::to_string(level)
                                                 : "ROUND " + std::to_string(std::max(1, roundNo)) + " · FIRST TO 2";
        Txt(sub, 640.f, 76.f, 17.f, Fade(INK, 0.75f));
        if (Button(MenuRect(), "MENU", PANELC, INK, true, 18.f)) act = Act::ToMenu;
        if (Button(SndRect(), snd.Muted() ? "SND OFF" : "SND ON", PANELC, INK, true, 18.f)) act = Act::ToggleSound;
    }

    void DrawSliderPen(bool enabled) {
        Rectangle tr = SliderRect();
        Color cap = PCol(turn);
        float bodyW = tr.width - 56.f;
        // outline
        RR({tr.x - 4, tr.y - 4, bodyW + 8, tr.height + 8}, 19.f, INK);
        DrawRectangleRec({tr.x + bodyW - 20.f, tr.y - 4, 20.f, tr.height + 8}, INK);
        FillTri({tr.x + bodyW - 1, tr.y - 4}, {tr.x + bodyW - 1, tr.y + tr.height + 4}, {tr.x + tr.width + 5, tr.y + tr.height * 0.5f}, INK);
        // fill
        RR({tr.x, tr.y, bodyW, tr.height}, 15.f, CREAM);
        DrawRectangleRec({tr.x + bodyW - 20.f, tr.y, 20.f, tr.height}, CREAM);
        RR({tr.x, tr.y, 110.f, tr.height}, 15.f, cap);
        DrawRectangleRec({tr.x + 55.f, tr.y, 55.f, tr.height}, cap);
        DrawRectangleRec({tr.x + 110.f, tr.y, 7.f, tr.height}, Darker(cap, 0.68f));
        FillTri({tr.x + bodyW, tr.y}, {tr.x + bodyW, tr.y + tr.height}, {tr.x + tr.width, tr.y + tr.height * 0.5f}, {206, 212, 216, 255});
        // zones
        Txt("CAP · SPIN", tr.x, tr.y - 20.f, 15.f, INK, 0);
        Txt("CENTRE · KNOCKBACK", tr.x + tr.width * 0.5f, tr.y - 20.f, 15.f, INK, 1);
        Txt("TIP · PRECISION", tr.x + tr.width, tr.y - 20.f, 15.f, INK, 2);
        for (int i = 0; i <= 2; ++i)
            DrawLineEx({tr.x + tr.width * 0.5f * i, tr.y + tr.height + 6.f}, {tr.x + tr.width * 0.5f * i, tr.y + tr.height + 14.f}, 2.f, Fade(INK, 0.6f));
        // knob
        float kx = tr.x + contact * tr.width, ky = tr.y + tr.height * 0.5f;
        DrawCircleV({kx, ky + 3.f}, 19.f, Fade(INK, 0.35f));
        DrawCircleV({kx, ky}, 19.f, INK);
        DrawCircleV({kx, ky}, 14.f, enabled ? WHITE : Color{220, 228, 222, 255});
        DrawCircleV({kx, ky}, 6.f, cap);
    }

    void DrawBottomPanel() {
        Panel({30.f, 596.f, 1220.f, 110.f}, 26.f, PANELC, 4.f);
        // power meter
        Txt("POWER", 56.f, 624.f, 16.f, INK, 0);
        int pct = int(aimPower * 100.f + 0.5f);
        Txt(std::to_string(pct) + "%", 266.f, 624.f, 16.f, INK, 2);
        RR({52.f, 640.f, 218.f, 26.f}, 13.f, INK);
        RR({56.f, 644.f, 210.f, 18.f}, 9.f, CREAM);
        if (aimPower > 0.01f) {
            Color pc = aimPower < 0.5f ? OKGREEN : (aimPower < 0.8f ? Color{232, 170, 40, 255} : DANGER);
            RR({56.f, 644.f, std::max(18.f, 210.f * aimPower), 18.f}, 9.f, pc);
        }
        Txt(st == St::Aim ? (aimSet ? "READY" : "DRAG ON THE BENCH") : " ", 56.f, 684.f, 14.f, Fade(INK, 0.7f), 0);

        bool enabled = st == St::Aim;
        DrawSliderPen(enabled);

        bool canLaunch = st == St::Aim && aimSet;
        if (Button(LaunchRect(), "LAUNCH", canLaunch ? PCol(turn) : Color{196, 208, 200, 255},
                   canLaunch ? WHITE : GREY, canLaunch, 34.f))
            act = Act::Launch;
    }

    std::string StatusText() const {
        switch (st) {
            case St::Banner:  return "GET READY";
            case St::Aim:     return "DRAG TO AIM  ·  SET THE STRIKE POINT  ·  PRESS LAUNCH";
            case St::AiThink: {
                int d = int(stTime * 3.f) % 4;
                return "COMPUTER IS THINKING" + std::string(d, '.');
            }
            case St::AiAim:   return "COMPUTER LINES UP A SHOT";
            case St::Sim:     return "PENS IN MOTION...";
            case St::Penalty: return "MISSED TURN";
            default:          return "";
        }
    }

    void DrawBanner() {
        float t = EaseOut(stTime / 0.3f);
        float a = stTime > (mode == Mode::Friend ? 1.9f : 0.9f)
                      ? std::clamp(1.f - (stTime - (mode == Mode::Friend ? 1.9f : 0.9f)) / 0.3f, 0.f, 1.f) : 1.f;
        std::string title = mode == Mode::Campaign ? (turn == 0 ? "YOUR TURN" : "COMPUTER'S TURN")
                                                   : "PLAYER " + std::to_string(turn + 1) + "'S TURN";
        float w = 560.f * t, h = 120.f;
        Rectangle r = {640.f - w * 0.5f, 322.f - h * 0.5f - 6.f * (1.f - t), w, h};
        Color col = PCol(turn);
        DrawRectangle(0, 0, (int)VW, (int)VH, Fade(INK, 0.18f * a));
        if (w > 40.f) {
            Panel(r, 30.f, Fade(col, a), 5.f);
            Txt(title, 640.f, 322.f - (mode == Mode::Friend ? 12.f : 0.f), 46.f * std::min(1.f, t), Fade(WHITE, a));
            if (mode == Mode::Friend) Txt("PASS THE DEVICE  ·  TAP TO START", 640.f, 322.f + 30.f, 18.f, Fade(WHITE, a * 0.9f));
        }
    }

    void DrawGame() {
        DrawTable();
        DrawObstacles();
        DrawPens();

        if ((st == St::Aim || st == St::AiAim) && !world.pens[turn].falling) {
            const Pen& me = world.pens[turn];
            if (aimSet || st == St::AiAim) DrawAimGuide(me, aimAngle, aimPower, contact, PCol(turn));
            DrawStrikeRing(me, contact, PCol(turn));
        }
        if (st == St::AiThink || st == St::Banner) {
            float pulse = 1.f + 0.1f * std::sin(float(GetTime()) * 6.f);
            const Pen& me = world.pens[turn];
            DrawRing(me.pos, 52.f * pulse, 55.f * pulse, 0, 360, 36, Fade(PCol(turn), 0.55f));
        }

        for (const Particle& p : parts) DrawCircleV(p.pos, p.size * (p.life / p.maxLife), Fade(p.col, p.life / p.maxLife));
        for (const FloatText& f : floats) {
            float a = std::clamp(f.life / f.maxLife * 2.f, 0.f, 1.f);
            Txt(f.text, f.pos.x + 2.f, f.pos.y + 2.f, f.size, Fade(WHITE, a));
            Txt(f.text, f.pos.x, f.pos.y, f.size, Fade(f.col, a));
        }

        DrawHud();
        std::string status = StatusText();
        if (!status.empty()) Txt(status, 640.f, 580.f, 16.f, Fade(INK, 0.85f));
        DrawBottomPanel();

        if (st == St::Banner) DrawBanner();
        if (st == St::RoundEnd) DrawRoundEnd();
    }

    void DrawRoundEnd() {
        float t = EaseOut((stTime - 0.7f) / 0.35f);
        if (stTime < 0.7f) {                                   // let the pen drop first
            Txt(why == Why::Knockoff ? "KNOCKED OFF!" : "FOUL!", 640.f, 322.f, 64.f * EaseOut(stTime / 0.2f), DANGER);
            return;
        }
        DrawRectangle(0, 0, (int)VW, (int)VH, Fade(INK, 0.5f * t));
        Rectangle r = {290.f, 150.f + 20.f * (1.f - t), 700.f, 360.f};
        Color col = PCol(roundWinner);
        Panel(r, 32.f, CREAM, 5.f);
        RR({r.x, r.y, r.width, 92.f}, 28.f, col);
        DrawRectangle((int)r.x, (int)r.y + 50, (int)r.width, 42, col);

        std::string title, sub, primary;
        switch (endKind) {
            case EndKind::LevelClear: title = "LEVEL " + std::to_string(level) + " CLEARED!"; primary = "NEXT LEVEL"; break;
            case EndKind::GameOver:   title = "YOU LOST";                                     primary = "RETRY LEVEL " + std::to_string(level); break;
            case EndKind::RoundWon:   title = Name(roundWinner) + " WINS THE ROUND";           primary = "NEXT ROUND"; break;
            case EndKind::MatchOver:  title = Name(roundWinner) + " WINS THE MATCH!";          primary = "REMATCH"; break;
        }
        int loser = 1 - roundWinner;
        switch (why) {
            case Why::Knockoff:
                sub = Subj(roundWinner) + " knocked " + PossLower(loser) + " pen off the bench.";
                break;
            case Why::Foul:
                sub = "FOUL!  " + Poss(loser) + " own pen fell off during the strike.";
                break;
            case Why::MissPush:
                sub = Subj(loser) + " kept missing - the pen was pushed off the edge.";
                break;
        }
        Txt(title, 640.f, r.y + 46.f, 38.f, WHITE);
        Txt(sub, 640.f, r.y + 140.f, 20.f, INK);

        if (mode == Mode::Friend) {
            Txt("P1  " + std::to_string(wins[0]) + "  -  " + std::to_string(wins[1]) + "  P2", 640.f, r.y + 192.f, 34.f, INK);
        } else {
            Txt("BEST LEVEL: " + std::to_string(bestLevel), 640.f, r.y + 192.f, 26.f, INK);
        }
        if (Button({r.x + 60.f, r.y + 240.f, 330.f, 72.f}, primary, col, WHITE, true, 28.f)) act = Act::Primary;
        if (Button({r.x + 410.f, r.y + 240.f, 230.f, 72.f}, "MAIN MENU", PANELC, INK, true, 24.f)) act = Act::ToMenu;
    }

    // ---------- menu screens
    void DrawMenu() {
        // decorative crossing pens
        Pen a, b;
        a.Init({400.f, 150.f}, -0.35f);
        b.Init({880.f, 150.f}, kPi + 0.35f);
        DrawPenShape(a, P1COL, true);  DrawPenShape(b, P2COL, true);
        DrawPenShape(a, P1COL, false); DrawPenShape(b, P2COL, false);
        DrawRing({a.pos.x - 38.f, a.pos.y + 13.f}, 9.f, 12.f, 0, 360, 24, WHITE);
        DashedLine({470.f, 140.f}, {800.f, 140.f}, 12.f, 9.f, 4.f, Fade(INK, 0.55f));

        float bob = std::sin(float(GetTime()) * 2.f) * 3.f;
        Txt("KOLOM KHELA", 646.f, 226.f + bob + 4.f, 92.f, Fade(INK, 0.30f));
        Txt("KOLOM KHELA", 640.f, 220.f + bob, 92.f, INK);
        Txt("SCHOOL BENCH  ·  PEN FIGHT", 640.f, 282.f, 24.f, Fade(INK, 0.8f));

        if (Button({440.f, 330.f, 400.f, 74.f}, "VS COMPUTER", P1COL, WHITE, true, 32.f)) act = Act::StartCampaign;
        if (Button({440.f, 420.f, 400.f, 74.f}, "VS FRIEND", P2COL, WHITE, true, 32.f)) act = Act::StartFriend;
        if (Button({440.f, 510.f, 400.f, 62.f}, "HOW TO PLAY", PANELC, INK, true, 26.f)) act = Act::HowTo;
        if (Button({1090.f, 640.f, 150.f, 50.f}, snd.Muted() ? "SND OFF" : "SND ON", PANELC, INK, true, 20.f)) act = Act::ToggleSound;
        Txt("BEST CAMPAIGN LEVEL: " + std::to_string(bestLevel), 640.f, 618.f, 18.f, Fade(INK, 0.75f));
    }

    void DrawHowTo() {
        Rectangle r = {140.f, 50.f, 1000.f, 610.f};
        Panel(r, 30.f, CREAM, 5.f);
        Txt("HOW TO PLAY", 640.f, 100.f, 44.f, INK);
        const char* lines[] = {
            "Two players sit at opposite ends of the bench and take turns flicking their pen.",
            "DRAG on the bench to aim (pull back like a slingshot), set the STRIKE POINT, press LAUNCH.",
            "Knock the other pen off the edge of the bench to win the round.",
            "FOUL: if your own pen falls off the bench during your strike, you lose at once.",
            "MISS: a flick that never touches the enemy pen wastes your turn.",
            "Miss again and again and your pen is pushed closer to the edge - then off it!",
            "CAP end = spin   ·   CENTRE = full knockback   ·   TIP end = precision (longer guide).",
            "VS COMPUTER is an endless campaign: the bench shrinks, ink bottles appear,",
            "and the AI aims, powers and spins its shots more cleverly every level.",
            "VS FRIEND is pass & play, first to 2 rounds.",
            "Keys:  LEFT / RIGHT strike point   ·   SPACE launch   ·   ESC menu",
        };
        float y = 160.f;
        for (const char* l : lines) { Txt(l, 640.f, y, 21.f, INK); y += 40.f; }
        if (Button({490.f, 590.f, 300.f, 56.f}, "BACK", P2COL, WHITE, true, 28.f)) act = Act::ToMenu;
    }
};

}  // namespace

int main(void) {
    Game game;
    game.Run();
    return 0;
}
