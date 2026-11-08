#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// Short sounds made up at start: wooden knocks for moves, a bell for check, a chord at the end.
class SoundBank {
public:
    enum class Effect { Move, Capture, Castle, Check, GameOver, Click, Count };

    SoundBank();
    void Play(Effect effect) const;

    bool enabled = true;

private:
    std::array<std::vector<std::uint8_t>, static_cast<std::size_t>(Effect::Count)> m_waves;
};
