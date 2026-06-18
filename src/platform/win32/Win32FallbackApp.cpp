#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <mmsystem.h>

#include "core/AppSession.h"
#include "core/AudioEngineStub.h"
#include "core/BackgroundAudioImportJob.h"
#include "core/BackgroundWaveformAnalysisJob.h"
#include "core/TimelineClipLane.h"
#include "core/WaveformAnalysisRegenerator.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr int playButtonId = 1001;
constexpr int stopButtonId = 1002;
constexpr int audioButtonId = 1003;
constexpr int saveButtonId = 1004;
constexpr int openButtonId = 1005;
constexpr int smokeTimerId = 2001;

constexpr COLORREF background = RGB(16, 17, 20);
constexpr COLORREF panelBackground = RGB(25, 27, 32);
constexpr COLORREF panelOutline = RGB(48, 52, 58);
constexpr COLORREF textPrimary = RGB(242, 243, 238);
constexpr COLORREF textSecondary = RGB(174, 180, 170);
constexpr COLORREF accent = RGB(128, 216, 120);
constexpr COLORREF accentWarm = RGB(228, 184, 106);

std::wstring widen(const std::string& value)
{
    return std::wstring(value.begin(), value.end());
}

std::string narrow(const std::wstring& value)
{
    return std::string(value.begin(), value.end());
}

std::filesystem::path fallbackProjectPath()
{
    return std::filesystem::temp_directory_path() / "ProjectNameFallback.project";
}

std::filesystem::path fallbackImportSourcePath()
{
    return std::filesystem::temp_directory_path() / "ProjectNameFallbackImport.wav";
}

std::filesystem::path fallbackImportProjectPath()
{
    return std::filesystem::temp_directory_path() / "ProjectNameFallbackImport.project";
}

std::wstring describeProjectPath(const std::filesystem::path& path)
{
    return path.wstring();
}

void writeFourCc(std::ofstream& file, const char (&id)[5])
{
    file.write(id, 4);
}

void writeUInt16LittleEndian(std::ofstream& file, std::uint16_t value)
{
    const char bytes[] = {
        static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU),
    };
    file.write(bytes, 2);
}

void writeUInt32LittleEndian(std::ofstream& file, std::uint32_t value)
{
    const char bytes[] = {
        static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU),
        static_cast<char>((value >> 16U) & 0xffU),
        static_cast<char>((value >> 24U) & 0xffU),
    };
    file.write(bytes, 4);
}

bool writeImportSmokeWav(const std::filesystem::path& path)
{
    constexpr std::uint32_t sampleRate = 44100;
    constexpr std::uint16_t channelCount = 1;
    const std::vector<std::int16_t> samples { 12000, 0, -12000, 6000 };
    const auto dataByteCount = static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
    const auto blockAlign = static_cast<std::uint16_t>(channelCount * sizeof(std::int16_t));
    const auto byteRate = sampleRate * blockAlign;
    const auto riffPayloadByteCount = 4U + (8U + 16U) + (8U + dataByteCount);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
        return false;

    writeFourCc(file, "RIFF");
    writeUInt32LittleEndian(file, riffPayloadByteCount);
    writeFourCc(file, "WAVE");
    writeFourCc(file, "fmt ");
    writeUInt32LittleEndian(file, 16);
    writeUInt16LittleEndian(file, 1);
    writeUInt16LittleEndian(file, channelCount);
    writeUInt32LittleEndian(file, sampleRate);
    writeUInt32LittleEndian(file, byteRate);
    writeUInt16LittleEndian(file, blockAlign);
    writeUInt16LittleEndian(file, 16);
    writeFourCc(file, "data");
    writeUInt32LittleEndian(file, dataByteCount);

    for (const auto sample : samples)
        writeUInt16LittleEndian(file, static_cast<std::uint16_t>(sample));

    return static_cast<bool>(file);
}

void fillRect(HDC dc, const RECT& rect, COLORREF colour)
{
    auto* brush = CreateSolidBrush(colour);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void drawPanel(HDC dc, RECT rect, const wchar_t* title, const wchar_t* subtitle, const std::vector<std::wstring>& rows)
{
    fillRect(dc, rect, panelBackground);

    auto* pen = CreatePen(PS_SOLID, 1, panelOutline);
    auto* oldPen = SelectObject(dc, pen);
    auto* oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, textPrimary);

    RECT titleRect { rect.left + 14, rect.top + 12, rect.right - 14, rect.top + 34 };
    DrawTextW(dc, title, -1, &titleRect, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);

    SetTextColor(dc, textSecondary);
    RECT subtitleRect { rect.left + 14, rect.top + 36, rect.right - 14, rect.top + 56 };
    DrawTextW(dc, subtitle, -1, &subtitleRect, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);

    auto rowTop = rect.top + 70;
    for (const auto& rowText : rows)
    {
        if (rowTop + 28 > rect.bottom - 12)
            break;

        RECT row { rect.left + 14, rowTop, rect.right - 14, rowTop + 26 };
        fillRect(dc, row, RGB(37, 40, 48));
        RECT textRect { row.left + 10, row.top, row.right - 10, row.bottom };
        DrawTextW(dc, rowText.c_str(), -1, &textRect, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
        rowTop += 34;
    }
}

void drawTimelineClipLane(HDC dc, RECT workspace, const projectname::TimelineClipLaneLayout& layout)
{
    if (layout.clips.empty())
        return;

    RECT laneRect {
        workspace.left + 18,
        workspace.top + 96,
        workspace.right - 18,
        std::min(workspace.bottom - 18, workspace.top + 116 + std::max(54, layout.contentHeightPixels))
    };
    if (laneRect.right <= laneRect.left || laneRect.bottom <= laneRect.top)
        return;

    fillRect(dc, laneRect, RGB(21, 23, 28));
    auto* outlinePen = CreatePen(PS_SOLID, 1, panelOutline);
    auto* oldPen = SelectObject(dc, outlinePen);
    auto* oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, laneRect.left, laneRect.top, laneRect.right, laneRect.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(outlinePen);

    auto* gridPen = CreatePen(PS_SOLID, 1, RGB(37, 40, 48));
    oldPen = SelectObject(dc, gridPen);
    for (auto x = laneRect.left + 10; x < laneRect.right; x += 48)
    {
        MoveToEx(dc, x, laneRect.top + 8, nullptr);
        LineTo(dc, x, laneRect.bottom - 8);
    }
    SelectObject(dc, oldPen);
    DeleteObject(gridPen);

    RECT clipArea { laneRect.left + 10, laneRect.top + 8, laneRect.right - 10, laneRect.bottom - 8 };
    SetBkMode(dc, TRANSPARENT);

    if (layout.loopRange.has_value() && layout.loopRange->visible)
    {
        const auto& loop = *layout.loopRange;
        RECT rawLoop {
            clipArea.left + loop.x,
            clipArea.top,
            clipArea.left + loop.x + loop.width,
            clipArea.bottom
        };

        RECT loopRect {};
        if (IntersectRect(&loopRect, &rawLoop, &clipArea))
        {
            fillRect(dc, loopRect, RGB(52, 45, 32));
            auto* loopPen = CreatePen(PS_SOLID, 1, accentWarm);
            oldPen = SelectObject(dc, loopPen);
            MoveToEx(dc, loopRect.left, loopRect.top, nullptr);
            LineTo(dc, loopRect.left, loopRect.bottom);
            MoveToEx(dc, loopRect.right - 1, loopRect.top, nullptr);
            LineTo(dc, loopRect.right - 1, loopRect.bottom);
            SelectObject(dc, oldPen);
            DeleteObject(loopPen);
        }
    }

    for (const auto& item : layout.clips)
    {
        RECT rawClip {
            clipArea.left + item.x,
            clipArea.top + item.y,
            clipArea.left + item.x + item.width,
            clipArea.top + item.y + item.height
        };
        if (rawClip.top >= clipArea.bottom)
            break;

        RECT clipRect {};
        if (!IntersectRect(&clipRect, &rawClip, &clipArea))
            continue;

        fillRect(dc, clipRect, RGB(34, 37, 43));
        outlinePen = CreatePen(PS_SOLID, 1, panelOutline);
        oldPen = SelectObject(dc, outlinePen);
        oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, clipRect.left, clipRect.top, clipRect.right, clipRect.bottom);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(outlinePen);

        RECT label { clipRect.left + 8, clipRect.top + 4, clipRect.right - 8, clipRect.top + 22 };
        if (item.waveform.state != projectname::WaveformThumbnailState::ready)
        {
            SetTextColor(dc, accentWarm);
            const auto* text = item.waveform.state == projectname::WaveformThumbnailState::missingAnalysis
                ? L"Analysis missing"
                : L"Analysis unreadable";
            DrawTextW(dc, text, -1, &label, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
            continue;
        }

        SetTextColor(dc, textPrimary);
        const auto clipName = widen(item.waveform.clip.name);
        DrawTextW(dc, clipName.c_str(), -1, &label, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);

        RECT wave { clipRect.left + 8, clipRect.top + 28, clipRect.right - 8, clipRect.bottom - 6 };
        const auto width = std::max(0, static_cast<int>(wave.right - wave.left));
        const auto columns = projectname::makeWaveformPeakColumns(item.waveform.summary, static_cast<std::size_t>(width));
        if (columns.empty())
            continue;

        auto* wavePen = CreatePen(PS_SOLID, 1, accent);
        oldPen = SelectObject(dc, wavePen);
        const auto centerY = (wave.top + wave.bottom) / 2;
        const auto halfHeight = std::max(1, static_cast<int>((wave.bottom - wave.top) / 2 - 2));

        for (std::size_t index = 0; index < columns.size(); ++index)
        {
            const auto x = wave.left + static_cast<int>(index);
            const auto height = std::max(1, static_cast<int>(columns[index] * static_cast<float>(halfHeight)));
            MoveToEx(dc, x, centerY - height, nullptr);
            LineTo(dc, x, centerY + height);
        }

        SelectObject(dc, oldPen);
        DeleteObject(wavePen);
    }
}

class WaveTonePlayer
{
public:
    ~WaveTonePlayer()
    {
        stop();
    }

    bool play()
    {
        stop();

        WAVEFORMATEX format {};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = 2;
        format.nSamplesPerSec = 44100;
        format.wBitsPerSample = 16;
        format.nBlockAlign = static_cast<WORD>(format.nChannels * format.wBitsPerSample / 8);
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

        if (waveOutOpen(&device_, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
            return false;

        projectname::AudioEngineStub audioEngine;
        audioEngine.prepare(static_cast<double>(format.nSamplesPerSec));
        audioEngine.setGeneratedToneFrequencyHz(440.0);
        audioEngine.setGeneratedToneGain(0.18f);

        const auto frameCount = static_cast<int>(format.nSamplesPerSec * 2);
        samples_.assign(static_cast<std::size_t>(frameCount * format.nChannels), 0);
        audioEngine.startGeneratedClip(static_cast<double>(frameCount) / static_cast<double>(format.nSamplesPerSec));
        audioEngine.renderInterleavedInt16(samples_.data(), frameCount, format.nChannels);

        header_ = {};
        header_.lpData = reinterpret_cast<LPSTR>(samples_.data());
        header_.dwBufferLength = static_cast<DWORD>(samples_.size() * sizeof(std::int16_t));

        if (waveOutPrepareHeader(device_, &header_, sizeof(header_)) != MMSYSERR_NOERROR)
        {
            stop();
            return false;
        }

        prepared_ = true;

        if (waveOutWrite(device_, &header_, sizeof(header_)) != MMSYSERR_NOERROR)
        {
            stop();
            return false;
        }

        return true;
    }

    void stop()
    {
        if (device_ == nullptr)
            return;

        waveOutReset(device_);

        if (prepared_)
        {
            waveOutUnprepareHeader(device_, &header_, sizeof(header_));
            prepared_ = false;
        }

        waveOutClose(device_);
        device_ = nullptr;
        header_ = {};
        samples_.clear();
    }

private:
    HWAVEOUT device_ = nullptr;
    WAVEHDR header_ {};
    bool prepared_ = false;
    std::vector<std::int16_t> samples_;
};

struct AppState
{
    projectname::AppSession session;
    WaveTonePlayer player;
    std::wstring status = L"Ready - press Play for a generated tone";
    HWND playButton = nullptr;
    HWND stopButton = nullptr;
    HWND audioButton = nullptr;
    HWND saveButton = nullptr;
    HWND openButton = nullptr;
    bool smokeTest = false;
    bool smokeAudio = false;
    bool smokeProject = false;
    bool smokeImport = false;
    int exitCode = 0;
    projectname::TimelineClipLaneLayout timelineClipLane;
};

void refreshTimelineClipLane(AppState& state, const std::filesystem::path& packageDirectory)
{
    state.timelineClipLane = projectname::buildImportedAudioTimelineClipLane(state.session.getProject(), packageDirectory);
}

bool saveFallbackProject(AppState& state, const std::filesystem::path& packageDirectory)
{
    std::string error;
    state.session.getProject().setName("Win32 Fallback Project");
    state.session.setTempoBpm(128.0);

    if (!state.session.saveProjectPackage(packageDirectory, error))
    {
        state.status = L"Save failed: " + widen(error);
        return false;
    }

    state.status = L"Saved project package: " + describeProjectPath(packageDirectory);
    refreshTimelineClipLane(state, packageDirectory);
    return true;
}

bool openFallbackProject(AppState& state, const std::filesystem::path& packageDirectory)
{
    std::string error;
    if (!state.session.loadProjectPackage(packageDirectory, error))
    {
        state.status = L"Open failed: " + widen(error);
        return false;
    }

    state.status = L"Opened project package: " + describeProjectPath(packageDirectory);
    refreshTimelineClipLane(state, packageDirectory);
    return true;
}

bool runProjectSmoke(AppState& state)
{
    const auto packageDirectory = std::filesystem::temp_directory_path() / "ProjectNameFallbackSmoke.project";
    std::error_code ignored;
    std::filesystem::remove_all(packageDirectory, ignored);

    if (!saveFallbackProject(state, packageDirectory))
        return false;

    projectname::AppSession loadedSession;
    std::string error;
    if (!loadedSession.loadProjectPackage(packageDirectory, error))
    {
        state.status = L"Smoke open failed: " + widen(error);
        std::filesystem::remove_all(packageDirectory, ignored);
        return false;
    }

    const auto matches = loadedSession.getProject() == state.session.getProject();
    std::filesystem::remove_all(packageDirectory, ignored);

    if (!matches)
    {
        state.status = L"Smoke project round trip mismatch";
        return false;
    }

    state.status = L"Project save/load smoke passed";
    return true;
}

bool runImportSmoke(AppState& state)
{
    const auto packageDirectory = fallbackImportProjectPath();
    const auto sourceWavPath = fallbackImportSourcePath();
    std::error_code ignored;
    std::filesystem::remove_all(packageDirectory, ignored);
    std::filesystem::remove(sourceWavPath, ignored);

    if (!writeImportSmokeWav(sourceWavPath))
    {
        state.status = L"Import smoke could not write source WAV";
        return false;
    }

    projectname::BackgroundAudioImportRequest request;
    request.project = state.session.getProject();
    request.packageDirectory = packageDirectory;
    request.sourceWavPath = sourceWavPath;

    projectname::BackgroundAudioImportJob job(std::move(request));
    job.start();
    const auto startedProgress = job.getProgress();
    if (startedProgress.phase == projectname::BackgroundAudioImportPhase::pending)
    {
        state.status = L"Import smoke progress did not start";
        std::filesystem::remove(sourceWavPath, ignored);
        std::filesystem::remove_all(packageDirectory, ignored);
        return false;
    }

    auto result = job.waitForResult();
    const auto completedProgress = job.getProgress();

    if (result.cancelled || !result.import.has_value())
    {
        state.status = L"Import smoke failed: " + widen(result.error);
        std::filesystem::remove(sourceWavPath, ignored);
        std::filesystem::remove_all(packageDirectory, ignored);
        return false;
    }

    if (completedProgress.phase != projectname::BackgroundAudioImportPhase::completed
        || completedProgress.percent != 100
        || completedProgress.framesTotal == 0
        || completedProgress.framesProcessed != completedProgress.framesTotal
        || completedProgress.bytesTotal == 0
        || completedProgress.bytesProcessed != completedProgress.bytesTotal)
    {
        state.status = L"Import smoke progress did not complete";
        std::filesystem::remove(sourceWavPath, ignored);
        std::filesystem::remove_all(packageDirectory, ignored);
        return false;
    }

    projectname::AudioEngineStub audioEngine;
    audioEngine.prepare(result.import->preparedClip.sampleRateHz);
    audioEngine.setPreparedMonoClipSamples(result.import->preparedClip.samples);
    audioEngine.setTimelinePositionSamples(0);
    audioEngine.startScheduledPreparedMonoClip(2);

    std::vector<float> rendered(8, 1.0f);
    audioEngine.render(rendered.data(), nullptr, static_cast<int>(rendered.size()));
    const auto preStartSilent = rendered[0] == 0.0f && rendered[1] == 0.0f;
    const auto clipRendered = std::any_of(rendered.begin() + 2,
                                          rendered.begin() + 6,
                                          [](float sample)
                                          {
                                              return sample != 0.0f;
                                          });
    const auto postEndSilent = rendered[6] == 0.0f && rendered[7] == 0.0f;

    projectname::AppSession loadedSession;
    std::string error;
    const auto loaded = loadedSession.loadProjectPackage(packageDirectory, error);
    const auto copiedFileExists = std::filesystem::is_regular_file(result.import->copiedAudioPath);
    const auto waveformSummaryExists = std::filesystem::is_regular_file(result.import->waveformSummaryPath)
        && result.import->clip.analysisPath.rfind("analysis/", 0) == 0;
    const auto projectsMatch = loaded && loadedSession.getProject() == result.project;

    auto waveformRegenerated = false;
    if (waveformSummaryExists)
    {
        std::filesystem::remove(result.import->waveformSummaryPath, ignored);
        projectname::BackgroundWaveformAnalysisRequest regenerationRequest;
        regenerationRequest.project = result.project;
        regenerationRequest.packageDirectory = packageDirectory;
        projectname::BackgroundWaveformAnalysisJob regenerationJob(std::move(regenerationRequest));
        regenerationJob.start();
        auto regenerated = regenerationJob.waitForResult();
        waveformRegenerated = !regenerated.cancelled
            && regenerated.regeneration.status == projectname::WaveformRegenerationStatus::regenerated
            && std::filesystem::is_regular_file(result.import->waveformSummaryPath);
    }

    if (projectsMatch)
    {
        state.session.replaceProject(std::move(result.project));
        refreshTimelineClipLane(state, packageDirectory);
    }

    std::filesystem::remove(sourceWavPath, ignored);
    std::filesystem::remove_all(packageDirectory, ignored);

    if (!copiedFileExists || !waveformSummaryExists || !waveformRegenerated || !projectsMatch || !preStartSilent || !clipRendered || !postEndSilent)
    {
        state.status = L"Import smoke verification failed";
        return false;
    }

    state.status = L"Audio import smoke passed";
    return true;
}

void layoutChildWindows(HWND window, AppState& state)
{
    RECT bounds {};
    GetClientRect(window, &bounds);

    MoveWindow(state.playButton, 24, 24, 88, 34, TRUE);
    MoveWindow(state.stopButton, 122, 24, 88, 34, TRUE);
    MoveWindow(state.saveButton, 220, 24, 88, 34, TRUE);
    MoveWindow(state.openButton, 318, 24, 88, 34, TRUE);
    MoveWindow(state.audioButton, bounds.right - 148, 24, 124, 34, TRUE);
}

void paintMainWindow(HWND window, AppState& state, HDC dc)
{
    RECT bounds {};
    GetClientRect(window, &bounds);
    fillRect(dc, bounds, background);

    RECT topBar { 12, 12, bounds.right - 12, 74 };
    fillRect(dc, topBar, RGB(23, 25, 29));

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, textPrimary);

    RECT tempo { 424, 22, 540, 60 };
    DrawTextW(dc, L"Tempo 120", -1, &tempo, DT_SINGLELINE | DT_LEFT | DT_VCENTER);

    RECT signature { 544, 22, 624, 60 };
    DrawTextW(dc, L"4/4", -1, &signature, DT_SINGLELINE | DT_LEFT | DT_VCENTER);

    RECT position { 624, 22, 780, 60 };
    DrawTextW(dc, state.session.getTransport().isPlaying() ? L"Playing bar 1" : L"Stopped at bar 1",
              -1, &position, DT_SINGLELINE | DT_LEFT | DT_VCENTER);

    SetTextColor(dc, textSecondary);
    RECT status { 784, 22, bounds.right - 160, 60 };
    DrawTextW(dc, state.status.c_str(), -1, &status, DT_SINGLELINE | DT_RIGHT | DT_VCENTER | DT_END_ELLIPSIS);

    const auto leftWidth = 220;
    const auto rightWidth = 240;
    const auto mixerHeight = 82;
    const auto deviceHeight = 140;
    const auto gap = 10;

    RECT mixer { 12, bounds.bottom - mixerHeight - 12, bounds.right - 12, bounds.bottom - 12 };
    RECT device { 12, mixer.top - gap - deviceHeight, bounds.right - 12, mixer.top - gap };
    RECT middle { 12, topBar.bottom + gap, bounds.right - 12, device.top - gap };
    RECT browser { middle.left, middle.top, middle.left + leftWidth, middle.bottom };
    RECT inspector { middle.right - rightWidth, middle.top, middle.right, middle.bottom };
    RECT workspace { browser.right + gap, middle.top, inspector.left - gap, middle.bottom };

    drawPanel(dc, browser, L"Browser", L"Search-first project and device navigation",
              { L"Samples", L"Plugins", L"Presets", L"Project assets" });
    drawPanel(dc, workspace, L"Session / Arrangement", L"Clips, lanes, and timeline editing",
              { L"Track 1: Generated Tone", L"Grid: 4 bars", L"Arrangement editing next" });
    drawTimelineClipLane(dc, workspace, state.timelineClipLane);
    drawPanel(dc, inspector, L"Inspector", L"Selected object properties",
              { L"Project: " + widen(state.session.getProject().getName()), L"Tempo follows transport", L"Missing media recovery later" });
    drawPanel(dc, device, L"Device Panel", L"Track device chain and editor area",
              { L"Generated tone source", L"Device slots are placeholders", L"Built-in devices next milestones" });
    drawPanel(dc, mixer, L"Mixer", L"Track and master controls",
              { L"Track meter placeholder", L"Master meter placeholder", L"Mute, solo, pan later" });
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* state = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message)
    {
    case WM_CREATE:
    {
        auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<AppState*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

        state->playButton = CreateWindowW(L"BUTTON", L"Play", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                          0, 0, 0, 0, window, reinterpret_cast<HMENU>(playButtonId),
                                          GetModuleHandleW(nullptr), nullptr);
        state->stopButton = CreateWindowW(L"BUTTON", L"Stop", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                          0, 0, 0, 0, window, reinterpret_cast<HMENU>(stopButtonId),
                                          GetModuleHandleW(nullptr), nullptr);
        state->audioButton = CreateWindowW(L"BUTTON", L"Audio/MIDI", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                           0, 0, 0, 0, window, reinterpret_cast<HMENU>(audioButtonId),
                                           GetModuleHandleW(nullptr), nullptr);
        state->saveButton = CreateWindowW(L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                          0, 0, 0, 0, window, reinterpret_cast<HMENU>(saveButtonId),
                                          GetModuleHandleW(nullptr), nullptr);
        state->openButton = CreateWindowW(L"BUTTON", L"Open", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                          0, 0, 0, 0, window, reinterpret_cast<HMENU>(openButtonId),
                                          GetModuleHandleW(nullptr), nullptr);
        layoutChildWindows(window, *state);

        if (state->smokeAudio)
        {
            state->session.play();
            state->exitCode = state->player.play() ? 0 : 2;
        }

        if (state->smokeProject)
            state->exitCode = runProjectSmoke(*state) ? 0 : 3;

        if (state->smokeImport)
            state->exitCode = runImportSmoke(*state) ? 0 : 4;

        if (state->smokeTest || state->smokeAudio || state->smokeProject || state->smokeImport)
            SetTimer(window, smokeTimerId, 750, nullptr);

        return 0;
    }
    case WM_SIZE:
        if (state != nullptr)
            layoutChildWindows(window, *state);
        return 0;

    case WM_COMMAND:
        if (state == nullptr)
            break;

        switch (LOWORD(wParam))
        {
        case playButtonId:
            state->session.play();
            state->status = state->player.play()
                ? L"Generated tone rendered and sent to the default waveOut device"
                : L"Could not open the default waveOut device";
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case stopButtonId:
            state->session.stop();
            state->player.stop();
            state->status = L"Stopped";
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case audioButtonId:
            MessageBoxW(window,
                        L"Fallback build uses the default Windows waveOut device.\n\n"
                        L"The JUCE app target owns the full cross-platform Audio/MIDI setup dialog.",
                        L"Audio/MIDI Setup",
                        MB_OK | MB_ICONINFORMATION);
            return 0;
        case saveButtonId:
            saveFallbackProject(*state, fallbackProjectPath());
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case openButtonId:
            openFallbackProject(*state, fallbackProjectPath());
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        default:
            break;
        }
        break;

    case WM_TIMER:
        if (wParam == smokeTimerId)
        {
            KillTimer(window, smokeTimerId);
            DestroyWindow(window);
            return 0;
        }
        break;

    case WM_PAINT:
        if (state != nullptr)
        {
            PAINTSTRUCT paint {};
            auto* dc = BeginPaint(window, &paint);
            paintMainWindow(window, *state, dc);
            EndPaint(window, &paint);
            return 0;
        }
        break;

    case WM_DESTROY:
        if (state != nullptr)
        {
            state->player.stop();
            PostQuitMessage(state->exitCode);
        }
        else
        {
            PostQuitMessage(1);
        }
        return 0;

    default:
        break;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand)
{
    AppState state;
    const auto commandLine = std::wstring(GetCommandLineW());
    state.smokeTest = commandLine.find(L"--smoke-test") != std::wstring::npos;
    state.smokeAudio = commandLine.find(L"--smoke-audio") != std::wstring::npos;
    state.smokeProject = commandLine.find(L"--smoke-project") != std::wstring::npos;
    state.smokeImport = commandLine.find(L"--smoke-import") != std::wstring::npos;

    WNDCLASSW windowClass {};
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = L"ProjectNameWin32FallbackWindow";
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));

    if (RegisterClassW(&windowClass) == 0)
        return 1;

    auto* window = CreateWindowExW(0,
                                   windowClass.lpszClassName,
                                   L"ProjectName - Native Fallback",
                                   WS_OVERLAPPEDWINDOW,
                                   CW_USEDEFAULT,
                                   CW_USEDEFAULT,
                                   1280,
                                   800,
                                   nullptr,
                                   nullptr,
                                   instance,
                                   &state);

    if (window == nullptr)
        return 1;

    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}
