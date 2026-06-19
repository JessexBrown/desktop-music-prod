// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ToneRenderer.h"

#include <algorithm>
#include <cmath>

namespace projectname
{
namespace
{
constexpr double twoPi = 6.28318530717958647692;
}

void ToneRenderer::prepare(double sampleRate) noexcept
{
    if (std::isfinite(sampleRate) && sampleRate > 0.0)
        sampleRate_ = sampleRate;

    updatePhaseDelta();
}

void ToneRenderer::reset() noexcept
{
    phaseRadians_ = 0.0;
}

void ToneRenderer::setFrequencyHz(double frequencyHz) noexcept
{
    if (!std::isfinite(frequencyHz))
        return;

    frequencyHz_ = std::clamp(frequencyHz, 20.0, 20000.0);
    updatePhaseDelta();
}

double ToneRenderer::getFrequencyHz() const noexcept
{
    return frequencyHz_;
}

void ToneRenderer::setGain(float gain) noexcept
{
    if (!std::isfinite(gain))
        return;

    gain_ = std::clamp(gain, 0.0f, 0.95f);
}

float ToneRenderer::getGain() const noexcept
{
    return gain_;
}

float ToneRenderer::renderSample() noexcept
{
    const auto sample = static_cast<float>(std::sin(phaseRadians_) * static_cast<double>(gain_));

    phaseRadians_ += phaseDeltaRadians_;
    if (phaseRadians_ >= twoPi)
        phaseRadians_ -= twoPi;

    return sample;
}

void ToneRenderer::render(float* left, float* right, int numSamples) noexcept
{
    if (numSamples <= 0 || left == nullptr)
        return;

    for (int index = 0; index < numSamples; ++index)
    {
        const auto sample = renderSample();
        left[index] = sample;

        if (right != nullptr)
            right[index] = sample;
    }
}

void ToneRenderer::updatePhaseDelta() noexcept
{
    phaseDeltaRadians_ = twoPi * frequencyHz_ / sampleRate_;
}
} // namespace projectname
