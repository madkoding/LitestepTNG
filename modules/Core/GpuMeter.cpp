//-------------------------------------------------------------------------------------------------
// /Core/GpuMeter.cpp
// The nModules Project
//
// GPU load and video memory of the main graphics card.
//
// The numbers come from the same performance counters Task Manager reads: "GPU Engine"
// has one instance per process and engine (3D, video decode, copy...), "GPU Adapter
// Memory" one per adapter. With several adapters (integrated + discrete) the one with
// the most dedicated memory is measured. Sampling runs on its own thread: collecting a
// few hundred counter instances is too slow for the shell's message loop.
//-------------------------------------------------------------------------------------------------
#include "GpuMeter.h"

#include <Windows.h>
#include <dxgi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <shlwapi.h>
#include <strsafe.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "pdh.lib")

static std::atomic<int> sUsagePercent{ 0 };
static std::atomic<int> sMemoryPercent{ 0 };
static std::atomic<int> sMemoryUsedMB{ 0 };

static std::mutex sMutex;
static std::condition_variable sWake;
static bool sRun = false; // guarded by sMutex
static std::thread sThread;


/// <summary>
/// Finds the hardware adapter with the most dedicated memory.
/// </summary>
/// <param name="luidTag">Receives the adapter's id the way counter instances spell it.</param>
/// <returns>The adapter's dedicated memory in bytes, 0 if there is none.</returns>
static ULONGLONG FindMainAdapter(std::wstring &luidTag) {
  ULONGLONG best = 0;
  IDXGIFactory1 *factory = nullptr;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    return 0;
  }

  IDXGIAdapter1 *adapter = nullptr;
  for (UINT index = 0; factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index) {
    DXGI_ADAPTER_DESC1 desc;
    if (SUCCEEDED(adapter->GetDesc1(&desc)) && (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0
        && desc.DedicatedVideoMemory > best) {
      best = desc.DedicatedVideoMemory;
      WCHAR tag[64];
      StringCchPrintfW(tag, _countof(tag), L"luid_0x%08X_0x%08X",
        (UINT)desc.AdapterLuid.HighPart, (UINT)desc.AdapterLuid.LowPart);
      luidTag = tag;
    }
    adapter->Release();
  }
  factory->Release();
  return best;
}


/// <summary>
/// Reads every instance of a wildcard counter.
/// </summary>
static bool ReadCounter(PDH_HCOUNTER counter, std::vector<BYTE> &buffer,
    PDH_FMT_COUNTERVALUE_ITEM_W *&items, DWORD &count) {
  DWORD size = 0;
  count = 0;
  if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &size, &count, nullptr) != PDH_MORE_DATA) {
    return false;
  }
  buffer.resize(size);
  items = (PDH_FMT_COUNTERVALUE_ITEM_W *)buffer.data();
  return PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &size, &count, items) == ERROR_SUCCESS;
}


static void Loop() {
  std::wstring luidTag;
  const ULONGLONG totalMemory = FindMainAdapter(luidTag);

  PDH_HQUERY query = nullptr;
  PDH_HCOUNTER engines = nullptr, memory = nullptr;
  if (PdhOpenQueryW(nullptr, 0, &query) == ERROR_SUCCESS) {
    // The English names work whatever language Windows is in.
    PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &engines);
    PdhAddEnglishCounterW(query, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &memory);
    PdhCollectQueryData(query);
  }

  std::vector<BYTE> buffer;
  for (;;) {
    {
      std::unique_lock<std::mutex> lock(sMutex);
      if (sWake.wait_for(lock, std::chrono::seconds(1), [] { return !sRun; })) {
        break;
      }
    }
    if (query == nullptr || luidTag.empty() || PdhCollectQueryData(query) != ERROR_SUCCESS) {
      continue;
    }

    PDH_FMT_COUNTERVALUE_ITEM_W *items = nullptr;
    DWORD count = 0;

    // Instances look like "pid_1234_luid_0x00000000_0x0000D3A5_phys_0_eng_0_engtype_3D".
    // An engine's load is the sum over the processes using it; the card's load is its
    // busiest engine, which is what Task Manager shows.
    if (engines != nullptr && ReadCounter(engines, buffer, items, count)) {
      std::map<std::wstring, double> perEngine;
      for (DWORD i = 0; i < count; ++i) {
        LPCWSTR name = items[i].szName;
        LPCWSTR engine = name ? StrStrIW(name, L"_eng_") : nullptr;
        if (engine != nullptr && StrStrIW(name, luidTag.c_str()) != nullptr
            && items[i].FmtValue.CStatus == ERROR_SUCCESS) {
          perEngine[engine] += items[i].FmtValue.doubleValue;
        }
      }
      double busiest = 0.0;
      for (auto &engine : perEngine) {
        if (engine.second > busiest) {
          busiest = engine.second;
        }
      }
      sUsagePercent = busiest > 100.0 ? 100 : int(busiest + 0.5);
    }

    if (memory != nullptr && totalMemory != 0 && ReadCounter(memory, buffer, items, count)) {
      for (DWORD i = 0; i < count; ++i) {
        if (items[i].szName != nullptr && StrStrIW(items[i].szName, luidTag.c_str()) != nullptr
            && items[i].FmtValue.CStatus == ERROR_SUCCESS) {
          double used = items[i].FmtValue.doubleValue;
          int percent = int(used * 100.0 / double(totalMemory) + 0.5);
          sMemoryPercent = percent > 100 ? 100 : percent;
          sMemoryUsedMB = int(used / (1024.0 * 1024.0));
          break;
        }
      }
    }
  }

  if (query != nullptr) {
    PdhCloseQuery(query);
  }
}


void GpuMeter::Start() {
  {
    std::lock_guard<std::mutex> lock(sMutex);
    if (sRun) {
      return;
    }
    sRun = true;
  }
  sThread = std::thread(Loop);
}


void GpuMeter::Stop() {
  {
    std::lock_guard<std::mutex> lock(sMutex);
    sRun = false;
  }
  sWake.notify_all();
  if (sThread.joinable()) {
    sThread.join();
  }
}


int GpuMeter::UsagePercent() {
  return sUsagePercent;
}


int GpuMeter::MemoryPercent() {
  return sMemoryPercent;
}


double GpuMeter::MemoryUsedGB() {
  return double(sMemoryUsedMB) / 1024.0;
}
