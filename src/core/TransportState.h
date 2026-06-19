// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

namespace projectname
{
struct TimeSignature
{
    int numerator = 4;
    int denominator = 4;

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] bool operator==(const TimeSignature& other) const noexcept = default;
};

class TransportState
{
public:
    static constexpr double minTempoBpm = 20.0;
    static constexpr double maxTempoBpm = 300.0;

    [[nodiscard]] bool isPlaying() const noexcept;
    void play() noexcept;
    void stop() noexcept;
    void togglePlayback() noexcept;

    void setTempoBpm(double tempoBpm) noexcept;
    [[nodiscard]] double getTempoBpm() const noexcept;

    [[nodiscard]] bool setTimeSignature(int numerator, int denominator) noexcept;
    [[nodiscard]] TimeSignature getTimeSignature() const noexcept;

    void setPositionBeats(double positionBeats) noexcept;
    [[nodiscard]] double getPositionBeats() const noexcept;
    void rewind() noexcept;

    void advanceSeconds(double seconds) noexcept;
    void advanceSamples(int numSamples, double sampleRate) noexcept;

private:
    bool playing_ = false;
    double tempoBpm_ = 120.0;
    TimeSignature timeSignature_;
    double positionBeats_ = 0.0;
};
} // namespace projectname
