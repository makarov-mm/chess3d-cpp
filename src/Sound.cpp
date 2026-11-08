#include "Sound.h"

#include "Win32.h"

#include <mmsystem.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <random>
#include <utility>

namespace {

constexpr int Rate = 44100;
constexpr double TwoPi = 2 * std::numbers::pi;

using Samples = std::vector<float>;

Samples Silence(double seconds) { return Samples(static_cast<std::size_t>(seconds * Rate), 0.0f); }

void Add(Samples& target, const Samples& source, double at, float gain = 1)
{
    const auto offset = static_cast<std::size_t>(at * Rate);
    if (target.size() < offset + source.size())
        target.resize(offset + source.size(), 0.0f);
    for (std::size_t i = 0; i < source.size(); ++i)
        target[offset + i] += source[i] * gain;
}

// A knock of wood on wood: a short burst of filtered noise and two quickly damped resonances.
Samples Knock(double pitch, std::uint32_t seed)
{
    Samples samples = Silence(0.16);
    std::minstd_rand random(seed);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    float filtered = 0;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const double t = static_cast<double>(i) / Rate;
        filtered += (noise(random) - filtered) * 0.35f;
        const double burst = filtered * std::exp(-t / 0.006);
        const double body = 0.55 * std::sin(TwoPi * 620 * pitch * t) * std::exp(-t / 0.028) + 0.3 * std::sin(TwoPi * 1480 * pitch * t) * std::exp(-t / 0.012) +
                            0.45 * std::sin(TwoPi * 170 * pitch * t) * std::exp(-t / 0.045);
        const double attack = std::min(1.0, t / 0.0008);
        samples[i] = static_cast<float>((burst * 0.9 + body) * attack);
    }
    return samples;
}

// A small bell: inharmonic partials dying away at their own rates.
Samples Bell(double frequency, double length)
{
    Samples samples = Silence(length);
    const double partials[4][3] = {{1.0, 1.0, 1.0}, {2.0, 0.45, 0.6}, {2.76, 0.3, 0.4}, {5.4, 0.12, 0.25}};
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const double t = static_cast<double>(i) / Rate;
        double value = 0;
        for (const auto& [ratio, gain, decay] : partials)
            value += gain * std::sin(TwoPi * frequency * ratio * t) * std::exp(-t / (length * 0.35 * decay));
        samples[i] = static_cast<float>(value * std::min(1.0, t / 0.004));
    }
    return samples;
}

std::vector<std::uint8_t> Wave(const Samples& samples, float volume)
{
    float peak = 1e-6f;
    for (float sample : samples)
        peak = std::max(peak, std::abs(sample));
    const auto dataSize = static_cast<std::uint32_t>(samples.size() * 2);
    std::vector<std::uint8_t> wave(44 + dataSize);
    auto write32 = [&](std::size_t at, std::uint32_t value) { std::memcpy(&wave[at], &value, 4); };
    auto write16 = [&](std::size_t at, std::uint16_t value) { std::memcpy(&wave[at], &value, 2); };
    std::memcpy(&wave[0], "RIFF", 4);
    write32(4, 36 + dataSize);
    std::memcpy(&wave[8], "WAVEfmt ", 8);
    write32(16, 16);
    write16(20, 1); // PCM
    write16(22, 1); // mono
    write32(24, Rate);
    write32(28, Rate * 2);
    write16(32, 2);
    write16(34, 16);
    std::memcpy(&wave[36], "data", 4);
    write32(40, dataSize);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto value = static_cast<std::int16_t>(std::clamp(samples[i] / peak * volume, -1.0f, 1.0f) * 32000);
        std::memcpy(&wave[44 + i * 2], &value, 2);
    }
    return wave;
}

} // namespace

SoundBank::SoundBank()
{
    auto index = [](Effect effect) { return std::to_underlying(effect); };

    m_waves[index(Effect::Move)] = Wave(Knock(1.0, 1), 0.55f);

    Samples capture = Knock(1.15, 2);
    Add(capture, Knock(0.9, 3), 0.05, 0.8f);
    m_waves[index(Effect::Capture)] = Wave(capture, 0.7f);

    Samples castle = Knock(1.0, 4);
    Add(castle, Knock(1.08, 5), 0.13, 0.9f);
    m_waves[index(Effect::Castle)] = Wave(castle, 0.6f);

    m_waves[index(Effect::Check)] = Wave(Bell(1046.5, 0.9), 0.3f);

    Samples chord = Silence(1.8);
    const double notes[4] = {523.25, 659.25, 783.99, 1046.5};
    for (int i = 0; i < 4; ++i)
        Add(chord, Bell(notes[i], 1.5), i * 0.11, 0.8f);
    m_waves[index(Effect::GameOver)] = Wave(chord, 0.35f);

    Samples click = Silence(0.03);
    for (std::size_t i = 0; i < click.size(); ++i) {
        const double t = static_cast<double>(i) / Rate;
        click[i] = static_cast<float>(std::sin(TwoPi * 2600 * t) * std::exp(-t / 0.004));
    }
    m_waves[index(Effect::Click)] = Wave(click, 0.18f);
}

void SoundBank::Play(Effect effect) const
{
    if (!enabled)
        return;
    const auto& wave = m_waves[std::to_underlying(effect)];
    PlaySoundW(reinterpret_cast<LPCWSTR>(wave.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}
