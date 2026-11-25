#pragma once
#include <xaudio2.h>
#include <wrl/client.h>
#include <vector>
#include <cstdint>

namespace helix::audio {

using Microsoft::WRL::ComPtr;

// A single loopable procedural voice (e.g. engine hum, wind) whose pitch and
// volume can be driven every frame from gameplay state (throttle, altitude,
// airspeed) without needing external sound assets.
class ProceduralLoop {
public:
    bool Create(IXAudio2* engine, float baseFrequencyHz, float sampleRate = 44100.f);
    void SetVolume(float v);
    void SetPitchMultiplier(float mult); // 1.0 = base frequency
    void Start();
    void Stop();

private:
    // XAudio2 voices are NOT IUnknown-based (no AddRef/Release) — they are
    // owned by the IXAudio2 engine and freed via DestroyVoice(), so a raw
    // pointer + explicit destroy is required here; ComPtr cannot be used.
    IXAudio2SourceVoice* voice_ = nullptr;
    std::vector<int16_t> pcm_;

public:
    ~ProceduralLoop();
};

// One-shot event sounds (weapon fire, switch clicks, impacts) generated
// procedurally so the engine is audio-complete without shipped assets.
enum class SfxType {
    SwitchClick, WarningTone, RadarPing, LaserFire, CannonFire,
    MissileLaunch, ShieldImpact, HullImpact, Explosion, GearDeploy
};

class AudioEngine {
public:
    bool Initialize();
    void Shutdown();

    void PlaySfx(SfxType type, float volume = 1.f);

    // Continuous loops driven by simulation state.
    void UpdateEngineHum(float throttle01, bool boosting, bool damaged);
    void UpdateAtmosphereWind(float airspeed, float altitude);
    void UpdateReentryRoar(float heatIntensity01);

    void SetListener(const struct HELIX_AUDIO_LISTENER* listener) { (void)listener; }

private:
    ComPtr<IXAudio2> xaudio_;
    IXAudio2MasteringVoice* masteringVoice_ = nullptr;

    ProceduralLoop engineIdleLoop_;
    ProceduralLoop engineBoostLoop_;
    ProceduralLoop windLoop_;
    ProceduralLoop reentryLoop_;

    void PlayOneShotTone(float frequencyHz, float durationSeconds, float volume,
                          float attack = 0.005f, float decay = 0.08f);
};

} // namespace helix::audio
