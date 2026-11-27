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
#include "GpuMeter.h"
#include <strsafe.h>
#include <ctime>
#include <locale.h>
#include <string>


// Month and day names (%B %A %b %a) in the themes' language ($Language$, see step.rc).
// nullptr = the C runtime's default, as before.
static _locale_t sTimeLocale = nullptr;

static void LoadTimeLocale() {
  static const struct { LPCWSTR code; LPCWSTR locale; } kLocales[] = {
    { L"en", L"en-US" }, { L"es", L"es-ES" }, { L"pt", L"pt-BR" }, { L"fr", L"fr-FR" },
    { L"de", L"de-DE" }, { L"it", L"it-IT" }, { L"zh", L"zh-CN" }, { L"ja", L"ja-JP" } };
  WCHAR language[16];
  LiteStep::GetPrefixedRCString(L"", L"Language", language, L"en", _countof(language));
  // "pt-BR" counts as "pt", as for the language files (lsapi's include falls back the same way).
  wchar_t *dash = wcschr(language, L'-');
  if (dash != nullptr) {
    *dash = L'\0';
  }
  for (const auto &entry : kLocales) {
    if (_wcsicmp(language, entry.code) == 0) {
      sTimeLocale = _wcreate_locale(LC_TIME, entry.locale);
      break;
    }
  }
}

static void FreeTimeLocale() {
  if (sTimeLocale != nullptr) {
    _free_locale(sTimeLocale);
    sTimeLocale = nullptr;
  }
}


static size_t __cdecl Time(LPCWSTR /* name */, UCHAR numArgs, LPWSTR *args, LPWSTR dest, size_t cchDest) {
  std::time_t t = std::time(nullptr);
  std::tm now;
  localtime_s(&now, &t);
  LPCWSTR format = numArgs == 0 ? L"%H:%M" : args[0];
  if (sTimeLocale != nullptr) {
    return _wcsftime_l(dest, cchDest, format, &now, sTimeLocale);
  }
  return std::wcsftime(dest, cchDest, format, &now);
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

// [GPU], [VRAM], [VRAMUsedGB]: load and dedicated memory of the main graphics card,
// sampled by GpuMeter's own thread.
static size_t __cdecl Gpu(LPCWSTR, UCHAR, LPWSTR*, LPWSTR dest, size_t cchDest) {
  StringCchPrintfW(dest, cchDest, L"%d", GpuMeter::UsagePercent());
  return wcslen(dest);
}

static size_t __cdecl Vram(LPCWSTR, UCHAR, LPWSTR*, LPWSTR dest, size_t cchDest) {
  StringCchPrintfW(dest, cchDest, L"%d", GpuMeter::MemoryPercent());
  return wcslen(dest);
}

static size_t __cdecl VramUsedGB(LPCWSTR, UCHAR, LPWSTR*, LPWSTR dest, size_t cchDest) {
  StringCchPrintfW(dest, cchDest, L"%.1f", GpuMeter::MemoryUsedGB());
  return wcslen(dest);
}

void TextFunctions::_Register() {
  LoadTimeLocale();
  RegisterDynamicTextFunction(L"Time", 0, Time, true);
  RegisterDynamicTextFunction(L"Time", 1, Time, true);
  RegisterDynamicTextFunction(L"CPU", 0, Cpu, true);
  RegisterDynamicTextFunction(L"Memory", 0, Memory, true);
  RegisterDynamicTextFunction(L"MemoryUsedGB", 0, MemoryUsedGB, true);
  RegisterDynamicTextFunction(L"GPU", 0, Gpu, true);
  RegisterDynamicTextFunction(L"VRAM", 0, Vram, true);
  RegisterDynamicTextFunction(L"VRAMUsedGB", 0, VramUsedGB, true);
  _Sample();
  GpuMeter::Start();

  RegisterDynamicTextFunction(L"WindowTitle", 1, [] (LPCWSTR /* name */, UCHAR /* numArgs */, LPWSTR* args, LPWSTR dest, size_t cchDest) -> size_t {
    HWND window = FindWindowW(args[0], nullptr);
    if (window) {
      return GetWindowTextW(window, dest, (int)cchDest);
    }
    return 0;
  }, true);
}


void TextFunctions::_Unregister() {
  GpuMeter::Stop();

  UnRegisterDynamicTextFunction(L"GPU", 0);
  UnRegisterDynamicTextFunction(L"VRAM", 0);
  UnRegisterDynamicTextFunction(L"VRAMUsedGB", 0);
  UnRegisterDynamicTextFunction(L"Time", 0);
  UnRegisterDynamicTextFunction(L"Time", 1);
  FreeTimeLocale();
  UnRegisterDynamicTextFunction(L"WindowTitle", 1);
  UnRegisterDynamicTextFunction(L"CPU", 0);
  UnRegisterDynamicTextFunction(L"Memory", 0);
  UnRegisterDynamicTextFunction(L"MemoryUsedGB", 0);
}