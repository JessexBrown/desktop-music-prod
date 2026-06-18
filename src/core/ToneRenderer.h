#pragma once

namespace projectname
{
class ToneRenderer
{
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    void setFrequencyHz(double frequencyHz) noexcept;
    [[nodiscard]] double getFrequencyHz() const noexcept;

    void setGain(float gain) noexcept;
    [[nodiscard]] float getGain() const noexcept;

    [[nodiscard]] float renderSample() noexcept;
    void render(float* left, float* right, int numSamples) noexcept;

private:
    void updatePhaseDelta() noexcept;

    double sampleRate_ = 44100.0;
    double frequencyHz_ = 440.0;
    double phaseRadians_ = 0.0;
    double phaseDeltaRadians_ = 0.0;
    float gain_ = 0.12f;
};
} // namespace projectname
