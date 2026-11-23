//-------------------------------------------------------------------------------------------------
// /Core/MediaControl.cpp
// The nModules Project
//
// Media playback: the session being controlled, the transport bangs, the master volume
// and the list of players a theme can rotate between.
//-------------------------------------------------------------------------------------------------
#include "../ModuleKit/LiteStep.h"
#include "MediaControl.h"
#include "ParsedText.hpp"
#include <strsafe.h>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <vector>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#pragma comment(lib, "windowsapp.lib")

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

// --- Media players ----------------------------------------------------------------
// The theme lists the players it knows, in rotation order:
//   *MediaPlayer <Name> "<exe, or shell:AppsFolder\<id>>" [<text that identifies its session>]
// Only the ones that are installed are kept; several lines with the same name are
// alternative locations. One of them is "selected": the transport buttons, the track
// slider and [NowPlaying] then follow that player's media session instead of whichever
// one Windows considers current. !MediaPlayerNext selects the next one and runs
// "MediaPlayer<Name>Command" (the theme uses it to swap the icon); the choice is
// remembered in the registry. With no *MediaPlayer lines nothing changes.
struct MediaPlayer { std::wstring name, path, match; };

static std::vector<MediaPlayer> sPlayers; // shell thread only
static int sPlayerIndex = -1;             // shell thread only
static bool sPlayerCommandPending = false;
static std::wstring sPlayerMatch;         // guarded by sNpMutex
static std::wstring sPlayerPath;          // guarded by sNpMutex

static const WCHAR kPlayerRegKey[] = L"Software\\LiteStepTNG";
static const WCHAR kPlayerRegValue[] = L"MediaPlayer";


static bool PlayerInstalled(const std::wstring &path) {
  if (_wcsnicmp(path.c_str(), L"shell:", 6) == 0) {
    PIDLIST_ABSOLUTE item = nullptr;
    if (SUCCEEDED(SHParseDisplayName(path.c_str(), nullptr, &item, 0, nullptr))) {
      CoTaskMemFree(item);
      return true;
    }
    return false;
  }
  return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}


static void LaunchPlayer(const std::wstring &path) {
  if (path.empty()) return;
  if (_wcsnicmp(path.c_str(), L"shell:", 6) == 0) {
    ShellExecuteW(nullptr, L"open", L"explorer.exe", path.c_str(), nullptr, SW_SHOWNORMAL);
  } else {
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  }
}


static void RunPlayerCommand() {
  if (sPlayerIndex < 0) return;
  WCHAR key[128], command[MAX_LINE_LENGTH];
  StringCchPrintfW(key, _countof(key), L"MediaPlayer%sCommand", sPlayers[sPlayerIndex].name.c_str());
  if (LiteStep::GetRCLine(key, command, _countof(command), L"") && command[0] != L'\0') {
    LiteStep::LSExecute(nullptr, command, SW_SHOWNORMAL);
  }
}


static void SelectPlayer(int index, bool remember) {
  if (index < 0 || index >= (int)sPlayers.size()) return;
  sPlayerIndex = index;
  {
    std::lock_guard<std::mutex> lk(sNpMutex);
    sPlayerMatch = sPlayers[index].match;
    sPlayerPath = sPlayers[index].path;
    // Whatever was showing belonged to the previous player.
    sNowPlaying.clear();
  }
  sTrackPermille.store(-1);
  if (remember) {
    const std::wstring &name = sPlayers[index].name;
    RegSetKeyValueW(HKEY_CURRENT_USER, kPlayerRegKey, kPlayerRegValue, REG_SZ, name.c_str(),
      DWORD((name.size() + 1) * sizeof(WCHAR)));
  }
  DynamicTextChangeNotification(L"MediaPlayer", 0);
  DynamicTextChangeNotification(L"NowPlaying", 0);
  RunPlayerCommand();
}


/// <summary>
/// Reads the *MediaPlayer lines, keeps the players that exist and selects the one used
/// last time (or the first). Its command runs on the next timer tick, once the labels
/// it talks to have been created.
/// </summary>
static void LoadPlayers() {
  sPlayers.clear();
  sPlayerIndex = -1;

  LiteStep::IterateOverLines(L"*MediaPlayer", [] (LPCWSTR line) {
    WCHAR name[MAX_LINE_LENGTH] = L"", path[MAX_LINE_LENGTH] = L"", match[MAX_LINE_LENGTH] = L"";
    LPWSTR tokens[] = { name, path, match };
    LiteStep::CommandTokenize(line, tokens, _countof(tokens), nullptr);
    if (name[0] == L'\0' || path[0] == L'\0') return;

    for (const MediaPlayer &player : sPlayers) {
      if (_wcsicmp(player.name.c_str(), name) == 0) return; // an earlier location won
    }
    WCHAR expanded[MAX_PATH];
    if (ExpandEnvironmentStringsW(path, expanded, _countof(expanded)) == 0) return;
    if (PlayerInstalled(expanded)) {
      sPlayers.push_back({ name, expanded, match[0] != L'\0' ? match : name });
    }
  });

  if (sPlayers.empty()) {
    std::lock_guard<std::mutex> lk(sNpMutex);
    sPlayerMatch.clear();
    sPlayerPath.clear();
    return;
  }

  int index = 0;
  WCHAR saved[128];
  DWORD size = sizeof(saved);
  if (RegGetValueW(HKEY_CURRENT_USER, kPlayerRegKey, kPlayerRegValue, RRF_RT_REG_SZ, nullptr,
      saved, &size) == ERROR_SUCCESS) {
    for (int i = 0; i < (int)sPlayers.size(); ++i) {
      if (_wcsicmp(sPlayers[i].name.c_str(), saved) == 0) index = i;
    }
  }
  sPlayerIndex = index;
  {
    std::lock_guard<std::mutex> lk(sNpMutex);
    sPlayerMatch = sPlayers[index].match;
    sPlayerPath = sPlayers[index].path;
  }
  sPlayerCommandPending = true;
}


static size_t __cdecl MediaPlayerName(LPCWSTR, UCHAR, LPWSTR*, LPWSTR dest, size_t cchDest) {
  StringCchCopyW(dest, cchDest, sPlayerIndex >= 0 ? sPlayers[sPlayerIndex].name.c_str() : L"");
  return wcslen(dest);
}


// Classic Winamp does not report to Windows' media sessions; it is driven the way
// LiteStep themes always did it, through its main window.
static LRESULT WinampSend(HWND winamp, UINT message, WPARAM wParam, LPARAM lParam) {
  DWORD_PTR result = 0;
  SendMessageTimeoutW(winamp, message, wParam, lParam, SMTO_ABORTIFHUNG, 500, &result);
  return (LRESULT)result;
}

/// <summary>
/// One pass against a running Winamp: runs the queued commands and reads the title and
/// the position. Returns false if Winamp is not running.
/// </summary>
static bool WinampPass(const std::vector<MediaRequest> &requests) {
  const UINT ipcIsPlaying = 104, ipcOutputTime = 105, ipcJumpToTime = 106;
  HWND winamp = FindWindowW(L"Winamp v1.x", nullptr);
  if (winamp == nullptr) return false;

  for (const MediaRequest &request : requests) {
    switch (request.cmd) {
    case MediaCmd::Prev:  WinampSend(winamp, WM_COMMAND, 40044, 0); break;
    case MediaCmd::Play:  WinampSend(winamp, WM_COMMAND, 40045, 0); break;
    case MediaCmd::Pause: WinampSend(winamp, WM_COMMAND, 40046, 0); break;
    case MediaCmd::Stop:  WinampSend(winamp, WM_COMMAND, 40047, 0); break;
    case MediaCmd::Next:  WinampSend(winamp, WM_COMMAND, 40048, 0); break;
    case MediaCmd::PlayPause:
      // 1 = playing; 40046 toggles pause, 40045 starts from stopped.
      WinampSend(winamp, WM_COMMAND, WinampSend(winamp, WM_USER, 0, ipcIsPlaying) == 0 ? 40045 : 40046, 0);
      break;
    case MediaCmd::Seek: {
      LRESULT seconds = WinampSend(winamp, WM_USER, 1, ipcOutputTime);
      if (seconds > 0) WinampSend(winamp, WM_USER, WPARAM(seconds * request.permille), ipcJumpToTime);
      break;
    }
    }
  }

  LRESULT seconds = WinampSend(winamp, WM_USER, 1, ipcOutputTime);
  LRESULT position = WinampSend(winamp, WM_USER, 0, ipcOutputTime); // ms
  int permille = -1;
  if (seconds > 0 && position >= 0) {
    permille = int(position / seconds);
    if (permille > 1000) permille = 1000;
  }
  sTrackPermille.store(permille);

  // "12. Artist - Title - Winamp", possibly followed by " [Paused]".
  WCHAR caption[512] = L"";
  GetWindowTextW(winamp, caption, _countof(caption));
  std::wstring title(caption);
  size_t suffix = title.rfind(L" - Winamp");
  title = suffix != std::wstring::npos ? title.substr(0, suffix) : L"";
  size_t digits = 0;
  while (digits < title.size() && iswdigit(title[digits])) ++digits;
  if (digits > 0 && title.compare(digits, 2, L". ") == 0) title.erase(0, digits + 2);

  std::lock_guard<std::mutex> lk(sNpMutex);
  sNowPlaying = title;
  return true;
}


static void NowPlayingLoop() {
  try { winrt::init_apartment(winrt::apartment_type::multi_threaded); } catch (...) {}
  using namespace winrt::Windows::Media::Control;
  typedef GlobalSystemMediaTransportControlsSessionPlaybackStatus PlaybackStatus;

  GlobalSystemMediaTransportControlsSessionManager mgr{ nullptr };
  int tick = 0;
  while (sNpRun.load()) {
    std::vector<MediaRequest> requests;
    std::wstring match, path;
    {
      std::lock_guard<std::mutex> lk(sNpMutex);
      requests.swap(sMediaQueue);
      match = sPlayerMatch;
      path = sPlayerPath;
    }

    try {
      if (!mgr) mgr = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();

      // With a selected player, only its own session counts.
      GlobalSystemMediaTransportControlsSession session{ nullptr };
      if (match.empty()) {
        session = mgr.GetCurrentSession();
      } else {
        for (auto const &candidate : mgr.GetSessions()) {
          if (StrStrIW(candidate.SourceAppUserModelId().c_str(), match.c_str()) != nullptr) {
            session = candidate;
            break;
          }
        }
      }

      if (!session && StrStrIW(match.c_str(), L"winamp") != nullptr && WinampPass(requests)) {
        // Handled through Winamp's own window.
      } else if (session) {
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
        {
          std::lock_guard<std::mutex> lk(sNpMutex);
          sNowPlaying.clear();
        }
        // Pressing play while the selected player is closed opens it.
        for (const MediaRequest &request : requests) {
          if (request.cmd == MediaCmd::Play || request.cmd == MediaCmd::PlayPause) {
            LaunchPlayer(path);
            break;
          }
        }
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

static const LPCWSTR kMediaBangs[] = { L"!MediaPlayPause", L"!MediaPlay", L"!MediaPause",
    L"!MediaStop", L"!MediaNext", L"!MediaPrev", L"!MediaSeek", L"!MediaVolume",
    L"!MediaPlayerNext", L"!MediaPlayerLaunch", L"!MediaPlayer" };


void MediaControl::Register() {
  RegisterDynamicTextFunction(L"NowPlaying", 0, NowPlaying, true);
  RegisterDynamicTextFunction(L"MediaPlayer", 0, MediaPlayerName, true);
  LoadPlayers();

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

  LiteStep::AddBangCommand(L"!MediaPlayerNext", [] (HWND, LPCWSTR) {
    if (sPlayers.size() > 1) SelectPlayer((sPlayerIndex + 1) % (int)sPlayers.size(), true);
  });
  LiteStep::AddBangCommand(L"!MediaPlayerLaunch", [] (HWND, LPCWSTR) {
    if (sPlayerIndex >= 0) LaunchPlayer(sPlayers[sPlayerIndex].path);
  });
  LiteStep::AddBangCommand(L"!MediaPlayer", [] (HWND, LPCWSTR args) {
    WCHAR name[MAX_LINE_LENGTH] = L"";
    LiteStep::GetToken(args, name, nullptr, FALSE);
    for (int i = 0; i < (int)sPlayers.size(); ++i) {
      if (_wcsicmp(sPlayers[i].name.c_str(), name) == 0) SelectPlayer(i, true);
    }
  });
}


void MediaControl::Unregister() {
  for (LPCWSTR bang : kMediaBangs) {
    LiteStep::RemoveBangCommand(bang);
  }

  sNpRun.store(false);
  if (sNpThread.joinable()) sNpThread.join();

  UnRegisterDynamicTextFunction(L"MediaPlayer", 0);
  UnRegisterDynamicTextFunction(L"NowPlaying", 0);
}


/// <summary>
/// Once per second, from the core timer. Runs the selected player's command when it is
/// due: on the first tick after a load or a refresh, because the labels it talks to do
/// not exist while Core itself is loading.
/// </summary>
void MediaControl::Tick() {
  if (sPlayerCommandPending) {
    sPlayerCommandPending = false;
    RunPlayerCommand();
  }
  DynamicTextChangeNotification(L"NowPlaying", 0);
}


/// <summary>
/// The .rc files were read again: the player list, and the labels' default images.
/// </summary>
void MediaControl::Refresh() {
  LoadPlayers();
}
