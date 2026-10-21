// SoundSynth.hpp  -  KOLOM KHELA
// 100% procedural audio: every sound is synthesised in memory as 16-bit mono
// PCM @ 44100 Hz and handed to raylib via LoadSoundFromWave. No audio files.
#pragma once

#include "raylib.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

class SoundSynth {
public:
    enum Id { WoodTap, PenClick, Flick, FallThud, UiClick, Victory, Defeat, Foul, kCount };
    static constexpr int kRate = 44100;

    // Call after InitAudioDevice().
    void Init() {
        if (ready_ || !IsAudioDeviceReady()) return;
        Build(WoodTap,  MakeWoodTap(165.f));
        Build(PenClick, MakePenClick());
        Build(Flick,    MakeFlick());
        Build(FallThud, MakeFallThud());
        Build(UiClick,  MakeUiClick());
        Build(Victory,  MakeVictory());
        Build(Defeat,   MakeDefeat());
        Build(Foul,     MakeFoul());
        ready_ = true;
    }

    // Call before CloseAudioDevice().
    void Unload() {
        if (!ready_) return;
        for (auto& v : voices_) {
            for (int i = 1; i < kPool; ++i) UnloadSoundAlias(v.pool[i]);
            UnloadSound(v.pool[0]);
        }
        ready_ = false;
    }

    void Play(Id id, float volume = 1.f, float pitch = 1.f) {
        if (!ready_ || muted_) return;
        Voice& v = voices_[id];
        Sound& s = v.pool[v.next];
        v.next = (v.next + 1) % kPool;
        SetSoundVolume(s, std::clamp(volume, 0.f, 1.f));
        SetSoundPitch(s, pitch);
        PlaySound(s);
    }

    void SetMuted(bool m) { muted_ = m; }
    bool Muted() const { return muted_; }

private:
    static constexpr int kPool = 5;   // overlapping voices per sound (aliases share the PCM buffer)
    struct Voice {
        std::array<Sound, kPool> pool{};
        int next = 0;
    };
    std::array<Voice, kCount> voices_{};
    bool ready_ = false;
    bool muted_ = false;
    uint32_t rng_ = 0x9E3779B9u;

    using Buf = std::vector<float>;
    static constexpr float kTwoPi = 6.28318530717958647692f;

    float Noise() {                      // xorshift32 -> [-1, 1]
        rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
        return (rng_ & 0xFFFFFF) / float(0x7FFFFF) - 1.f;
    }

    static Buf Silence(float seconds) { return Buf(size_t(seconds * kRate), 0.f); }

    void Build(Id id, const Buf& s) {
        Wave w{};
        w.frameCount = (unsigned int)s.size();
        w.sampleRate = kRate;
        w.sampleSize = 16;
        w.channels   = 1;
        short* pcm = (short*)RL_MALLOC(s.size() * sizeof(short));
        float peak = 0.f;
        for (float f : s) peak = std::max(peak, std::fabs(f));
        const float g = peak > 0.95f ? 0.95f / peak : 1.f;
        for (size_t i = 0; i < s.size(); ++i)
            pcm[i] = (short)(std::clamp(s[i] * g, -1.f, 1.f) * 32767.f);
        w.data = pcm;

        Voice& v = voices_[id];
        v.pool[0] = LoadSoundFromWave(w);
        UnloadWave(w);
        for (int i = 1; i < kPool; ++i) v.pool[i] = LoadSoundAlias(v.pool[0]);
        v.next = 0;
    }

    // ---- synthesis -------------------------------------------------------

    // Dull "knock" of a knuckle / pen on a wooden bench.
    Buf MakeWoodTap(float f0) {
        Buf b = Silence(0.14f);
        float lp = 0.f;
        for (size_t i = 0; i < b.size(); ++i) {
            float t = float(i) / kRate;
            float body = std::sin(kTwoPi * f0 * t * (1.f + 0.18f * std::exp(-t * 45.f))) * std::exp(-t * 48.f);
            float over = 0.45f * std::sin(kTwoPi * f0 * 2.43f * t) * std::exp(-t * 85.f);
            lp += 0.18f * (Noise() - lp);
            float click = lp * std::exp(-t * 260.f) * 0.9f;
            b[i] = 0.8f * body + over + click;
        }
        return b;
    }

    // Bright plastic "tik" of two ball-point pens colliding.
    Buf MakePenClick() {
        Buf b = Silence(0.11f);
        float lp = 0.f;
        for (size_t i = 0; i < b.size(); ++i) {
            float t = float(i) / kRate;
            float n = Noise();
            lp += 0.35f * (n - lp);
            float hp = n - lp;                                  // high-passed noise
            float tone = 0.50f * std::sin(kTwoPi * 1930.f * t) * std::exp(-t * 95.f)
                       + 0.34f * std::sin(kTwoPi * 3170.f * t) * std::exp(-t * 120.f)
                       + 0.20f * std::sin(kTwoPi * 4710.f * t) * std::exp(-t * 150.f);
            float low  = 0.30f * std::sin(kTwoPi * 420.f * t) * std::exp(-t * 70.f);
            b[i] = tone + low + hp * std::exp(-t * 520.f) * 0.7f;
        }
        return b;
    }

    // Finger flick / snap on the pen.
    Buf MakeFlick() {
        Buf b = Silence(0.10f);
        float lp = 0.f, lp2 = 0.f;
        for (size_t i = 0; i < b.size(); ++i) {
            float t = float(i) / kRate;
            float n = Noise();
            lp  += 0.22f * (n - lp);
            lp2 += 0.04f * (lp - lp2);
            float band = lp - lp2;
            b[i] = band * 1.8f * std::exp(-t * 60.f)
                 + 0.55f * std::sin(kTwoPi * 380.f * t) * std::exp(-t * 55.f);
        }
        return b;
    }

    // Pen dropping off the bench onto the floor: thud + a few rattling bounces.
    Buf MakeFallThud() {
        Buf b = Silence(0.55f);
        float phase = 0.f, lp = 0.f;
        for (size_t i = 0; i < b.size(); ++i) {
            float t = float(i) / kRate;
            float f = 42.f + 90.f * std::exp(-t * 10.f);
            phase += kTwoPi * f / kRate;
            float att = std::min(1.f, t / 0.004f);
            lp += 0.035f * (Noise() - lp);
            b[i] = std::sin(phase) * std::exp(-t * 7.f) * att * 0.95f + lp * std::exp(-t * 20.f) * 1.6f;
        }
        const float rattles[3] = {0.13f, 0.21f, 0.28f};
        for (int r = 0; r < 3; ++r) {
            size_t s0 = size_t(rattles[r] * kRate);
            for (size_t i = s0; i < b.size() && i < s0 + 2600; ++i) {
                float t = float(i - s0) / kRate;
                b[i] += (0.28f - 0.07f * r) * std::sin(kTwoPi * (1500.f - 220.f * r) * t) * std::exp(-t * 110.f);
            }
        }
        return b;
    }

    Buf MakeUiClick() {
        Buf b = Silence(0.07f);
        for (size_t i = 0; i < b.size(); ++i) {
            float t = float(i) / kRate;
            float f = 780.f + 900.f * std::min(1.f, t / 0.03f);
            b[i] = 0.6f * std::sin(kTwoPi * f * t) * std::exp(-t * 50.f);
        }
        return b;
    }

    void AddBell(Buf& b, float t0, float f, float amp, float decay) {
        size_t i0 = size_t(t0 * kRate);
        for (size_t i = i0; i < b.size(); ++i) {
            float t = float(i - i0) / kRate;
            float env = std::exp(-t * decay) * (1.f - std::exp(-t * 700.f));
            if (env < 1e-4f) break;
            float v = std::sin(kTwoPi * f * t)
                    + 0.45f * std::sin(kTwoPi * f * 2.0f * t) * std::exp(-t * 2.f)
                    + 0.22f * std::sin(kTwoPi * f * 3.01f * t) * std::exp(-t * 4.f)
                    + 0.10f * std::sin(kTwoPi * f * 5.43f * t) * std::exp(-t * 8.f);
            b[i] += amp * env * v;
        }
    }

    // Rising C-major arpeggio with a shimmering final chord.
    Buf MakeVictory() {
        Buf b = Silence(2.1f);
        AddBell(b, 0.00f, 523.25f, 0.45f, 3.5f);
        AddBell(b, 0.13f, 659.25f, 0.45f, 3.5f);
        AddBell(b, 0.26f, 783.99f, 0.45f, 3.5f);
        AddBell(b, 0.40f, 1046.5f, 0.50f, 2.2f);
        AddBell(b, 0.40f, 659.25f, 0.30f, 1.8f);
        AddBell(b, 0.40f, 783.99f, 0.30f, 1.8f);
        AddBell(b, 0.40f, 523.25f, 0.30f, 1.6f);
        return b;
    }

    // Falling minor line, soft triangle wave.
    Buf MakeDefeat() {
        Buf b = Silence(1.5f);
        const float notes[4] = {392.f, 329.63f, 293.66f, 196.f};
        for (int n = 0; n < 4; ++n) {
            size_t i0 = size_t(n * 0.2f * kRate);
            float phase = 0.f;
            for (size_t i = i0; i < b.size(); ++i) {
                float t = float(i - i0) / kRate;
                float env = std::exp(-t * (n == 3 ? 2.4f : 5.5f)) * std::min(1.f, t / 0.01f);
                if (env < 1e-4f) break;
                phase += kTwoPi * notes[n] / kRate;
                float tri = (2.f / 3.14159265f) * std::asin(std::sin(phase));
                b[i] += 0.42f * env * (tri + 0.2f * std::sin(phase * 2.f));
            }
        }
        return b;
    }

    // Referee buzzer for fouls.
    Buf MakeFoul() {
        Buf b = Silence(0.42f);
        float p1 = 0.f, p2 = 0.f;
        for (size_t i = 0; i < b.size(); ++i) {
            float t = float(i) / kRate;
            p1 += kTwoPi * 142.f / kRate;
            p2 += kTwoPi * 149.f / kRate;
            float saw1 = std::fmod(p1 / kTwoPi, 1.f) * 2.f - 1.f;
            float saw2 = std::fmod(p2 / kTwoPi, 1.f) * 2.f - 1.f;
            float gate = (t < 0.18f || t > 0.22f) ? 1.f : 0.f;     // short stutter
            float env = std::min(1.f, t / 0.005f) * std::exp(-t * 3.2f);
            b[i] = 0.5f * (saw1 + saw2) * 0.6f * env * gate;
        }
        return b;
    }
};
