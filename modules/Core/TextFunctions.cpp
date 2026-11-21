/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *  TextFormatting.cpp
 *  The nModules Project
 *
 *  Built-in text functions.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#include "../ModuleKit/LiteStep.h"
#include "TextFunctions.h"
#include "ParsedText.hpp"
#include <strsafe.h>
#include <ctime>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <vector>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#pragma comment(lib, "windowsapp.lib")


static size_t __cdecl Time(LPCWSTR /* name */, UCHAR numArgs, LPWSTR *args, LPWSTR dest, size_t cchDest) {
  std::time_t t = std::time(nullptr);
  std::tm now;
  localtime_s(&now, &t);
  return std::wcsftime(dest, cchDest, numArgs == 0 ? L"%H:%M" : args[0], &now);
}


// --- System meters -----------------------------------------------------------
// Values are sampled once per second (see _Sample) and cached, so evaluating the
// text in many labels never hits the system APIs more than once per tick.
static int sCpuPercent = 0;
static int sMemPercent = 0;
static double sMemUsedGB = 0.0;
static ULONGLONG sLastIdle = 0, sLastKernel = 0, sLastUser = 0;

static ULONGLONG FileTimeToU64(const FILETIME &ft) {
  return (ULONGLONG(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

void TextFunctions::_Sample() {
  FILETIME idleFt, kernelFt, userFt;
  if (GetSystemTimes(&idleFt, &kernelFt, &userFt)) {
    ULONGLONG idle = FileTimeToU64(idleFt), kernel = FileTimeToU64(kernelFt), user = FileTimeToU64(userFt);
    if (sLastKernel != 0) {
      // Kernel time already includes idle time.
      ULONGLONG total = (kernel - sLastKernel) + (user - sLastUser);
      ULONGLONG busy = total - (idle - sLastIdle);
      sCpuPercent = total ? int((busy * 100 + total / 2) / total) : 0;
    }
    sLastIdle = idle; sLastKernel = kernel; sLastUser = user;
  }

  MEMORYSTATUSEX mem = { sizeof(mem) };
  if (GlobalMemoryStatusEx(&mem)) {
    sMemPercent = int(mem.dwMemoryLoad);
    sMemUsedGB = double(mem.ullTotalPhys - mem.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
  }
}

static size_t __cdecl Cpu(LPCWSTR, UCHAR, LPWSTR*, LPWSTR dest, size_t cchDest) {
  StringCchPrintfW(dest, cchDest, L"%d", sCpuPercent);
  return wcslen(dest);
}

static size_t __cdecl Memory(LPCWSTR, UCHAR, LPWSTR*, LPWSTR dest, size_t cchDest) {
  StringCchPrintfW(dest, cchDest, L"%d", sMemPercent);
  return wcslen(dest);
}

static size_t __cdecl MemoryUsedGB(LPCWSTR, UCHAR, LPWSTR*, LPWSTR dest, size_t cchDest) {
  StringCchPrintfW(dest, cchDest, L"%.1f", sMemUsedGB);
  return wcslen(dest);
}


// --- Media session (Windows GSMTC) and master volume ---------------------------
// A background thread owns the current media session (Spotify, Tidal, any app that
// reports to Windows). It caches "ARTIST - TITLE" and the track position, and runs
// the transport commands queued by the !Media* bangs, so neither label evaluation
// nor a bang ever blocks the shell thread on the media app.
//
// This talks to the session directly instead of sending VK_MEDIA_* keys: media keys
// are routed to apps by explorer.exe, which is not running when litestep is the shell.
enum class MediaCmd { PlayPause, Play, Pause, Stop, Next, Prev, Seek };
struct MediaRequest { MediaCmd cmd; int permille; };

// What MediaGetPermille / MediaSetPermille refer to.
static const int MEDIA_VOLUME = 0;
static const int MEDIA_TRACK = 1;

static std::mutex sNpMutex;
static std::wstring sNowPlaying;              // guarded by sNpMutex
static std::vector<MediaRequest> sMediaQueue; // guarded by sNpMutex
static std::atomic<int> sTrackPermille{ -1 }; // -1 = no session or no timeline
static std::atomic<bool> sNpRun{ false };
static std::thread sNpThread;

static void QueueMedia(MediaCmd cmd, int permille = 0) {
  std::lock_guard<std::mutex> lk(sNpMutex);
  sMediaQueue.push_back({ cmd, permille });
}

static void NowPlayingLoop() {
  try { winrt::init_apartment(winrt::apartment_type::multi_threaded); } catch (...) {}
  using namespace winrt::Windows::Media::Control;
  typedef GlobalSystemMediaTransportControlsSessionPlaybackStatus PlaybackStatus;

  GlobalSystemMediaTransportControlsSessionManager mgr{ nullptr };
  int tick = 0;
  while (sNpRun.load()) {
    std::vector<MediaRequest> requests;
    { std::lock_guard<std::mutex> lk(sNpMutex); requests.swap(sMediaQueue); }

    try {
      if (!mgr) mgr = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
      auto session = mgr.GetCurrentSession();
      if (session) {
        for (const MediaRequest &request : requests) {
          switch (request.cmd) {
          case MediaCmd::PlayPause: session.TryTogglePlayPauseAsync().get(); break;
          case MediaCmd::Play:      session.TryPlayAsync().get(); break;
          case MediaCmd::Pause:     session.TryPauseAsync().get(); break;
          case MediaCmd::Stop:      session.TryStopAsync().get(); break;
          case MediaCmd::Next:      session.TrySkipNextAsync().get(); break;
          case MediaCmd::Prev:      session.TrySkipPreviousAsync().get(); break;
          case MediaCmd::Seek: {
            auto timeline = session.GetTimelineProperties();
            auto start = timeline.StartTime();
            auto end = timeline.EndTime();
            if (end > start) {
              __int64 target = start.count() + (end - start).count() * request.permille / 1000;
              session.TryChangePlaybackPositionAsync(target).get();
            }
            break;
          }
          }
        }

        // Track position. Apps only report it now and then, so while playing we
        // extrapolate from the time of their last report.
        int permille = -1;
        auto timeline = session.GetTimelineProperties();
        auto start = timeline.StartTime();
        auto end = timeline.EndTime();
        if (end > start) {
          auto position = timeline.Position();
          auto info = session.GetPlaybackInfo();
          if (info && info.PlaybackStatus() == PlaybackStatus::Playing) {
            position += winrt::clock::now() - timeline.LastUpdatedTime();
          }
          if (position < start) position = start;
          if (position > end) position = end;
          permille = int((position - start).count() * 1000 / (end - start).count());
        }
        sTrackPermille.store(permille);

        // Title/artist: every ~2s, and right after a command (track may have changed).
        if (tick % 8 == 0 || !requests.empty()) {
          std::wstring np;
          auto props = session.TryGetMediaPropertiesAsync().get();
          std::wstring title(props.Title().c_str());
          std::wstring artist(props.Artist().c_str());
          if (!title.empty() && !artist.empty()) np = artist + L" - " + title;
          else np = title;
          std::lock_guard<std::mutex> lk(sNpMutex);
          sNowPlaying = np;
        }
      } else {
        sTrackPermille.store(-1);
        std::lock_guard<std::mutex> lk(sNpMutex);
        sNowPlaying.clear();
      }
    } catch (...) {
      mgr = nullptr; // start over with a fresh manager on the next pass
    }

    ++tick;
    // ~250ms between passes, but wake up early when a command is queued.
    for (int i = 0; i < 5 && sNpRun.load(); ++i) {
      Sleep(50);
      std::lock_guard<std::mutex> lk(sNpMutex);
      if (!sMediaQueue.empty()) break;
    }
  }
}

static size_t __cdecl NowPlaying(LPCWSTR, UCHAR, LPWSTR*, LPWSTR dest, size_t cchDest) {
  std::lock_guard<std::mutex> lk(sNpMutex);
  StringCchCopyW(dest, cchDest, sNowPlaying.c_str());
  return wcslen(dest);
}


/// <summary>
/// Opens the master volume control of the default playback device, or nullptr.
/// The caller releases it. Looked up on every call so a device change is followed.
/// </summary>
static IAudioEndpointVolume *OpenEndpointVolume() {
  IMMDeviceEnumerator *enumerator = nullptr;
  IMMDevice *device = nullptr;
  IAudioEndpointVolume *volume = nullptr;
  if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
      __uuidof(IMMDeviceEnumerator), (void**)&enumerator))) {
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device))) {
      device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, (void**)&volume);
      device->Release();
    }
    enumerator->Release();
  }
  return volume;
}


/// <summary>
/// Current value of a media quantity in 0..1000, or -1 when it is not available.
/// what: 0 = master volume, 1 = position inside the current track.
/// Exported for other modules (the nLabel slider).
/// </summary>
EXPORT_CDECL(int) MediaGetPermille(int what) {
  if (what == MEDIA_TRACK) {
    return sTrackPermille.load();
  }
  if (what == MEDIA_VOLUME) {
    int result = -1;
    if (IAudioEndpointVolume *volume = OpenEndpointVolume()) {
      float level = 0.0f;
      if (SUCCEEDED(volume->GetMasterVolumeLevelScalar(&level))) {
        result = int(level * 1000.0f + 0.5f);
      }
      volume->Release();
    }
    return result;
  }
  return -1;
}


/// <summary>
/// Sets a media quantity from a 0..1000 value. Same "what" as MediaGetPermille.
/// </summary>
EXPORT_CDECL(void) MediaSetPermille(int what, int permille) {
  if (permille < 0) permille = 0;
  if (permille > 1000) permille = 1000;
  if (what == MEDIA_TRACK) {
    QueueMedia(MediaCmd::Seek, permille);
  } else if (what == MEDIA_VOLUME) {
    if (IAudioEndpointVolume *volume = OpenEndpointVolume()) {
      volume->SetMasterVolumeLevelScalar(permille / 1000.0f, nullptr);
      volume->Release();
    }
  }
}


/// <summary>
/// !MediaVolume 0..100 sets the volume; a leading + or - (e.g. "+5") changes it.
/// </summary>
static void MediaVolumeBang(HWND, LPCWSTR args) {
  while (args && (*args == L' ' || *args == L'\t')) ++args;
  if (!args || !*args) return;
  int value = _wtoi(args) * 10;
  if (*args == L'+' || *args == L'-') {
    int current = MediaGetPermille(MEDIA_VOLUME);
    if (current < 0) return;
    value += current;
  }
  MediaSetPermille(MEDIA_VOLUME, value);
}


/// <summary>
/// !MediaSeek 0..100 jumps to that percentage of the current track.
/// </summary>
static void MediaSeekBang(HWND, LPCWSTR args) {
  if (args && *args) MediaSetPermille(MEDIA_TRACK, _wtoi(args) * 10);
}


void TextFunctions::_Register() {
  RegisterDynamicTextFunction(L"Time", 0, Time, true);
  RegisterDynamicTextFunction(L"Time", 1, Time, true);
  RegisterDynamicTextFunction(L"CPU", 0, Cpu, true);
  RegisterDynamicTextFunction(L"Memory", 0, Memory, true);
  RegisterDynamicTextFunction(L"MemoryUsedGB", 0, MemoryUsedGB, true);
  RegisterDynamicTextFunction(L"NowPlaying", 0, NowPlaying, true);
  _Sample();

  sNpRun.store(true);
  sNpThread = std::thread(NowPlayingLoop);

  LiteStep::AddBangCommand(L"!MediaPlayPause", [] (HWND, LPCWSTR) { QueueMedia(MediaCmd::PlayPause); });
  LiteStep::AddBangCommand(L"!MediaPlay", [] (HWND, LPCWSTR) { QueueMedia(MediaCmd::Play); });
  LiteStep::AddBangCommand(L"!MediaPause", [] (HWND, LPCWSTR) { QueueMedia(MediaCmd::Pause); });
  LiteStep::AddBangCommand(L"!MediaStop", [] (HWND, LPCWSTR) { QueueMedia(MediaCmd::Stop); });
  LiteStep::AddBangCommand(L"!MediaNext", [] (HWND, LPCWSTR) { QueueMedia(MediaCmd::Next); });
  LiteStep::AddBangCommand(L"!MediaPrev", [] (HWND, LPCWSTR) { QueueMedia(MediaCmd::Prev); });
  LiteStep::AddBangCommand(L"!MediaSeek", MediaSeekBang);
  LiteStep::AddBangCommand(L"!MediaVolume", MediaVolumeBang);

  RegisterDynamicTextFunction(L"WindowTitle", 1, [] (LPCWSTR /* name */, UCHAR /* numArgs */, LPWSTR* args, LPWSTR dest, size_t cchDest) -> size_t {
    HWND window = FindWindowW(args[0], nullptr);
    if (window) {
      return GetWindowTextW(window, dest, (int)cchDest);
    }
    return 0;
  }, true);
}


void TextFunctions::_Unregister() {
  static const LPCWSTR mediaBangs[] = { L"!MediaPlayPause", L"!MediaPlay", L"!MediaPause",
      L"!MediaStop", L"!MediaNext", L"!MediaPrev", L"!MediaSeek", L"!MediaVolume" };
  for (LPCWSTR bang : mediaBangs) {
    LiteStep::RemoveBangCommand(bang);
  }

  sNpRun.store(false);
  if (sNpThread.joinable()) sNpThread.join();

  UnRegisterDynamicTextFunction(L"Time", 0);
  UnRegisterDynamicTextFunction(L"Time", 1);
  UnRegisterDynamicTextFunction(L"WindowTitle", 1);
  UnRegisterDynamicTextFunction(L"CPU", 0);
  UnRegisterDynamicTextFunction(L"Memory", 0);
  UnRegisterDynamicTextFunction(L"MemoryUsedGB", 0);
  UnRegisterDynamicTextFunction(L"NowPlaying", 0);
}

