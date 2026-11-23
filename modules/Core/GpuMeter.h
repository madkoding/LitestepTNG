//-------------------------------------------------------------------------------------------------
// /Core/GpuMeter.h
// The nModules Project
//
// GPU load and video memory of the main graphics card, for the [GPU] / [VRAM] text functions.
//-------------------------------------------------------------------------------------------------
#pragma once

namespace GpuMeter {
  void Start();
  void Stop();

  // 0..100. Zero until the first sample, or when the system has no such counters.
  int UsagePercent();
  int MemoryPercent();
  double MemoryUsedGB();
}
