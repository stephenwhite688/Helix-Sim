#include "AudioEngine.h"
#include <cmath>
#include <algorithm>

#pragma comment(lib, "xaudio2.lib")

namespace helix::audio {

namespace {
constexpr float kPi = 3.14159265358979323846f;

// Generates one second of a band-limited sawtooth-ish waveform (a few
// summed harmonics) at the given frequency — used for the engine hum so it
// sounds like a turbine/engine rather than a pure sine.
std::vector<int16_t> GenerateEngineWave(float frequencyHz, float sampleRate) {
    int samples = static_cast<int>(sampleRate); // exactly 1 second, loopable
    std::vector<int16_t> pcm(samples);
    for (int i = 0; i < samples; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        float sample = 0.f;
        sample += 0.5f * std::sin(2.f * kPi * frequencyHz * t);
        sample += 0.25f * std::sin(2.f * kPi * frequencyHz * 2.f * t);
        sample += 0.15f * std::sin(2.f * kPi * frequencyHz * 3.f * t + 0.3f);
        sample += 0.10f * std::sin(2.f * kPi * frequencyHz * 5.f * t + 0.7f);
        pcm[i] = static_cast<int16_t>(std::clamp(sample, -1.f, 1.f) * 20000.f);
    }
    return pcm;
}

std::vector<int16_t> GenerateNoiseBurst(float durationSeconds, float sampleRate, uint32_t seed) {
    int samples = static_cast<int>(durationSeconds * sampleRate);
    std::vector<int16_t> pcm(samples);
    uint32_t state = seed ? seed : 1;
    for (int i = 0; i < samples; ++i) {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5; // xorshift32
        float n = (static_cast<float>(state % 65536) / 32768.f) - 1.f;
        float envelope = 1.f - static_cast<float>(i) / samples; // linear decay
        pcm[i] = static_cast<int16_t>(std::clamp(n * envelope, -1.f, 1.f) * 22000.f);
    }
    return pcm;
}

std::vector<int16_t> GenerateTone(float frequencyHz, float durationSeconds, float sampleRate,
                                   float attack, float decay) {
    int samples = static_cast<int>(durationSeconds * sampleRate);
    int attackSamples = static_cast<int>(attack * sampleRate);
    int decaySamples = static_cast<int>(decay * sampleRate);
    std::vector<int16_t> pcm(samples);
    for (int i = 0; i < samples; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        float envelope = 1.f;
        if (i < attackSamples && attackSamples > 0) envelope = static_cast<float>(i) / attackSamples;
        else if (i > samples - decaySamples && decaySamples > 0)
            envelope = static_cast<float>(samples - i) / decaySamples;
        float sample = std::sin(2.f * kPi * frequencyHz * t) * envelope;
        pcm[i] = static_cast<int16_t>(std::clamp(sample, -1.f, 1.f) * 24000.f);
    }
    return pcm;
}

WAVEFORMATEX MakeFormat(float sampleRate) {
    WAVEFORMATEX fmt{};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 1;
    fmt.nSamplesPerSec = static_cast<DWORD>(sampleRate);
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = fmt.nChannels * fmt.wBitsPerSample / 8;
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
    fmt.cbSize = 0;
    return fmt;
}
} // namespace

bool ProceduralLoop::Create(IXAudio2* engine, float baseFrequencyHz, float sampleRate) {
    pcm_ = GenerateEngineWave(baseFrequencyHz, sampleRate);
    WAVEFORMATEX fmt = MakeFormat(sampleRate);
    if (FAILED(engine->CreateSourceVoice(&voice_, &fmt))) return false;

    XAUDIO2_BUFFER buffer{};
    buffer.AudioBytes = static_cast<UINT32>(pcm_.size() * sizeof(int16_t));
    buffer.pAudioData = reinterpret_cast<const BYTE*>(pcm_.data());
    buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
    buffer.Flags = XAUDIO2_END_OF_STREAM;

    voice_->SubmitSourceBuffer(&buffer);
    voice_->SetVolume(0.f);
    return true;
}

ProceduralLoop::~ProceduralLoop() {
    if (voice_) voice_->DestroyVoice();
}

void ProceduralLoop::SetVolume(float v) { if (voice_) voice_->SetVolume(std::clamp(v, 0.f, 1.f)); }
void ProceduralLoop::SetPitchMultiplier(float mult) { if (voice_) voice_->SetFrequencyRatio(std::clamp(mult, 0.5f, 3.0f)); }
void ProceduralLoop::Start() { if (voice_) voice_->Start(); }
void ProceduralLoop::Stop() { if (voice_) voice_->Stop(); }

bool AudioEngine::Initialize() {
    if (FAILED(XAudio2Create(&xaudio_, 0, XAUDIO2_DEFAULT_PROCESSOR))) return false;
    if (FAILED(xaudio_->CreateMasteringVoice(&masteringVoice_))) return false;

    engineIdleLoop_.Create(xaudio_.Get(), 90.f);
    engineBoostLoop_.Create(xaudio_.Get(), 160.f);
    windLoop_.Create(xaudio_.Get(), 55.f);
    reentryLoop_.Create(xaudio_.Get(), 40.f);

    engineIdleLoop_.Start();
    engineBoostLoop_.Start();
    windLoop_.Start();
    reentryLoop_.Start();
    return true;
}

void AudioEngine::Shutdown() {
    engineIdleLoop_.Stop();
    engineBoostLoop_.Stop();
    windLoop_.Stop();
    reentryLoop_.Stop();
    if (xaudio_) xaudio_->StopEngine();
}

void AudioEngine::UpdateEngineHum(float throttle01, bool boosting, bool damaged) {
    throttle01 = std::clamp(throttle01, 0.f, 1.f);
    engineIdleLoop_.SetVolume(0.15f + 0.55f * throttle01);
    engineIdleLoop_.SetPitchMultiplier(0.8f + 0.6f * throttle01 * (damaged ? 0.85f : 1.f));
    engineBoostLoop_.SetVolume(boosting ? 0.6f : 0.f);
    engineBoostLoop_.SetPitchMultiplier(boosting ? 1.1f : 1.f);
}

void AudioEngine::UpdateAtmosphereWind(float airspeed, float altitude) {
    // Wind audible only inside atmosphere (altitude gates it externally by
    // simply calling this only when in atmosphere); volume scales with speed.
    float norm = std::clamp(airspeed / 300.f, 0.f, 1.f);
    windLoop_.SetVolume(norm * 0.5f);
    windLoop_.SetPitchMultiplier(0.9f + norm * 0.7f);
}

void AudioEngine::UpdateReentryRoar(float heatIntensity01) {
    float v = std::clamp(heatIntensity01, 0.f, 1.f);
    reentryLoop_.SetVolume(v * 0.8f);
    reentryLoop_.SetPitchMultiplier(0.9f + v * 0.5f);
}

void AudioEngine::PlayOneShotTone(float frequencyHz, float durationSeconds, float volume,
                                   float attack, float decay) {
    auto pcm = GenerateTone(frequencyHz, durationSeconds, 44100.f, attack, decay);
    WAVEFORMATEX fmt = MakeFormat(44100.f);
    IXAudio2SourceVoice* voice = nullptr;
    if (FAILED(xaudio_->CreateSourceVoice(&voice, &fmt))) return;

    // Leak-free one-shot: XAudio2 voices for short SFX are cheap to create
    // per call for this scale of game; a pooled-voice system is the natural
    // next optimization pass once SFX volume becomes a bottleneck.
    static std::vector<std::vector<int16_t>> keepAlive; // keeps buffer memory valid for voice lifetime
    keepAlive.push_back(std::move(pcm));
    auto& stored = keepAlive.back();

    XAUDIO2_BUFFER buffer{};
    buffer.AudioBytes = static_cast<UINT32>(stored.size() * sizeof(int16_t));
    buffer.pAudioData = reinterpret_cast<const BYTE*>(stored.data());
    buffer.Flags = XAUDIO2_END_OF_STREAM;

    voice->SetVolume(std::clamp(volume, 0.f, 1.f));
    voice->SubmitSourceBuffer(&buffer);
    voice->Start();
}

void AudioEngine::PlaySfx(SfxType type, float volume) {
    switch (type) {
        case SfxType::SwitchClick:    PlayOneShotTone(1200.f, 0.04f, volume, 0.001f, 0.03f); break;
        case SfxType::WarningTone:    PlayOneShotTone(880.f, 0.25f, volume, 0.01f, 0.1f); break;
        case SfxType::RadarPing:      PlayOneShotTone(2400.f, 0.08f, volume, 0.001f, 0.06f); break;
        case SfxType::LaserFire:      PlayOneShotTone(1800.f, 0.12f, volume, 0.001f, 0.1f); break;
        case SfxType::CannonFire:     PlayOneShotTone(220.f, 0.08f, volume, 0.001f, 0.06f); break;
        case SfxType::MissileLaunch:  PlayOneShotTone(150.f, 0.6f, volume, 0.02f, 0.4f); break;
        case SfxType::ShieldImpact:   PlayOneShotTone(500.f, 0.2f, volume, 0.001f, 0.15f); break;
        case SfxType::HullImpact:     PlayOneShotTone(120.f, 0.25f, volume, 0.001f, 0.2f); break;
        case SfxType::Explosion:      PlayOneShotTone(80.f, 0.9f, volume, 0.001f, 0.8f); break;
        case SfxType::GearDeploy:     PlayOneShotTone(300.f, 0.5f, volume, 0.02f, 0.3f); break;
    }
}

} // namespace helix::audio
