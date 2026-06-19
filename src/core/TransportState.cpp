// SPDX-License-Identifier: AGPL-3.0-or-later

#include "TransportState.h"

#include <algorithm>
#include <cmath>

namespace projectname
{
namespace
{
[[nodiscard]] bool isSupportedDenominator(int denominator) noexcept
{
    return denominator == 1 || denominator == 2 || denominator == 4
        || denominator == 8 || denominator == 16 || denominator == 32;
}
} // namespace

bool TimeSignature::isValid() const noexcept
{
    return numerator >= 1 && numerator <= 32 && isSupportedDenominator(denominator);
}

bool TransportState::isPlaying() const noexcept
{
    return playing_;
}

void TransportState::play() noexcept
{
    playing_ = true;
}

void TransportState::stop() noexcept
{
    playing_ = false;
}

void TransportState::togglePlayback() noexcept
{
    playing_ = !playing_;
}

void TransportState::setTempoBpm(double tempoBpm) noexcept
{
    if (!std::isfinite(tempoBpm))
        return;

    tempoBpm_ = std::clamp(tempoBpm, minTempoBpm, maxTempoBpm);
}

double TransportState::getTempoBpm() const noexcept
{
    return tempoBpm_;
}

bool TransportState::setTimeSignature(int numerator, int denominator) noexcept
{
    const TimeSignature candidate { numerator, denominator };

    if (!candidate.isValid())
        return false;

    timeSignature_ = candidate;
    return true;
}

TimeSignature TransportState::getTimeSignature() const noexcept
{
    return timeSignature_;
}

void TransportState::setPositionBeats(double positionBeats) noexcept
{
    if (!std::isfinite(positionBeats))
        return;

    positionBeats_ = std::max(0.0, positionBeats);
}

double TransportState::getPositionBeats() const noexcept
{
    return positionBeats_;
}

void TransportState::rewind() noexcept
{
    positionBeats_ = 0.0;
}

void TransportState::advanceSeconds(double seconds) noexcept
{
    if (!playing_ || !std::isfinite(seconds) || seconds <= 0.0)
        return;

    positionBeats_ += seconds * tempoBpm_ / 60.0;
}

void TransportState::advanceSamples(int numSamples, double sampleRate) noexcept
{
    if (numSamples <= 0 || !std::isfinite(sampleRate) || sampleRate <= 0.0)
        return;

    advanceSeconds(static_cast<double>(numSamples) / sampleRate);
}
} // namespace projectname
