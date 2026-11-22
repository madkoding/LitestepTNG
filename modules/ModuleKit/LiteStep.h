//-------------------------------------------------------------------------------------------------
// /ModuleKit/LiteStep.h
// The nModules Project
//
// Confines LSAPI routines to a namespace, and provides extra parsing functions.
//-------------------------------------------------------------------------------------------------
#pragma once

#include "Color.h"
#include "Distance.hpp"

#include "../Utilities/Common.h"
#include <memory>
#include <utility>

#include <functional>
#include <ShlObj.h>

#include "../../lsapi/lsapi.h"

constexpr DWORD MAGIC_DWORD = 0x4D4B5244;

namespace LiteStep {
  using BANGCOMMANDPROC = BangCommandW;

  inline BOOL AddBangCommand(LPCWSTR pwzCommand, BANGCOMMANDPROC proc) { return AddBangCommandW(pwzCommand, proc); }
  inline BOOL RemoveBangCommand(LPCWSTR pwzCommand) { return RemoveBangCommandW(pwzCommand); }
  inline BOOL GetToken(LPCWSTR pwzString, LPWSTR pwzToken, LPCWSTR* pwzNextToken, BOOL useBrackets) { return GetTokenW(pwzString, pwzToken, pwzNextToken, useBrackets); }
  inline int CommandTokenize(LPCWSTR pwzString, LPWSTR* lpwzBuffers, DWORD dwNumBuffers, LPWSTR pwzExtraParameters) { return CommandTokenizeW(pwzString, lpwzBuffers, dwNumBuffers, pwzExtraParameters); }
  inline HINSTANCE LSExecute(HWND owner, LPCWSTR pwzCommand, int nShowCmd) { return LSExecuteW(owner, pwzCommand, nShowCmd); }
  inline HBITMAP LoadLSImage(LPCWSTR pwzImage, LPCWSTR pwzFile) { return LoadLSImageW(pwzImage, pwzFile); }
  inline BOOL LSGetLitestepPathW(LPWSTR pwzPath, size_t cchPath) { return ::LSGetLitestepPathW(pwzPath, cchPath); }
  inline void LSSetVariable(LPCWSTR pwzKeyName, LPCWSTR pwzValue) { LSSetVariableW(pwzKeyName, pwzValue); }
  inline LPVOID LCOpen(LPCWSTR pwzPath) { return LCOpenW(pwzPath); }
  inline BOOL LCReadNextConfig(LPVOID pFile, LPCWSTR pwzConfig, LPWSTR pwzValue, size_t cchValue) { return LCReadNextConfigW(pFile, pwzConfig, pwzValue, cchValue); }
  inline BOOL GetRCBoolDef(LPCWSTR pwzKeyName, BOOL bDefault) { return GetRCBoolDefW(pwzKeyName, bDefault); }
  inline double GetRCDouble(LPCWSTR pwzKeyName, double dDefault) { return GetRCDoubleW(pwzKeyName, dDefault); }
  inline float GetRCFloat(LPCWSTR pwzKeyName, float fDefault) { return GetRCFloatW(pwzKeyName, fDefault); }
  inline int GetRCInt(LPCWSTR pwzKeyName, int nDefault) { return GetRCIntW(pwzKeyName, nDefault); }
  inline __int64 GetRCInt64(LPCWSTR pwzKeyName, __int64 nDefault) { return GetRCInt64W(pwzKeyName, nDefault); }
  inline BOOL GetRCLine(LPCWSTR pwzKeyName, LPWSTR pwzValue, UINT maxLen, LPCWSTR pwzDefault) { return GetRCLineW(pwzKeyName, pwzValue, maxLen, pwzDefault); }
  inline BOOL GetRCString(LPCWSTR pwzKeyName, LPWSTR pwzValue, LPCWSTR pwzDefault, int maxLen) { return GetRCStringW(pwzKeyName, pwzValue, pwzDefault, maxLen); }
  inline BOOL GetRCBool(LPCWSTR pwzKeyName, BOOL fIfFound) { return GetRCBoolW(pwzKeyName, fIfFound); }
  inline HWND GetLitestepWnd() { return ::GetLitestepWnd(); }
  inline BOOL AddBangCommandEx(LPCWSTR pwzCommand, BangCommandExW proc) { return AddBangCommandExW(pwzCommand, proc); }
  inline HINSTANCE LSExecuteEx(HWND owner, LPCWSTR pwzOperation, LPCWSTR pwzCommand, LPCWSTR pwzArgs, LPCWSTR pwzDirectory, int nShowCmd) { return LSExecuteExW(owner, pwzOperation, pwzCommand, pwzArgs, pwzDirectory, nShowCmd); }
  inline int LCTokenize(LPCWSTR pwzString, LPWSTR* lpwzBuffers, DWORD dwNumBuffers, LPWSTR pwzExtraParameters) { return LCTokenizeW(pwzString, lpwzBuffers, dwNumBuffers, pwzExtraParameters); }
  inline HRESULT EnumLSData(UINT uInfo, FARPROC pfnCallback, LPARAM lParam) { return EnumLSDataW(uInfo, pfnCallback, lParam); }

  using ::LCClose;
  using ::LCOpenW;
  using ::LCReadNextConfigW;
  using ::LCTokenizeW;
  using ::ENUMBANGSPROC;
  using ::ENUMMODULESPROC;
  using ::LSNOTIFYICONDATA;
  using ::PLSNOTIFYICONDATA;
  using ::PCLSNOTIFYICONDATA;
  using ::LPLSNOTIFYICONDATA;
  using ::SYSTRAYINFOEVENT;
  using ::LPSYSTRAYINFOEVENT;

  // Fetching of prefixed data types
  bool GetPrefixedRCBool(LPCTSTR prefix, LPCTSTR keyName, bool defaultValue);
  IColorVal* GetPrefixedRCColor(LPCTSTR prefix, LPCTSTR keyName, const IColorVal* defaultValue);
  double GetPrefixedRCDouble(LPCTSTR prefix, LPCTSTR keyName, double defaultValue);
  float GetPrefixedRCFloat(LPCTSTR prefix, LPCTSTR keyName, float defaultValue);
  int GetPrefixedRCInt(LPCTSTR prefix, LPCTSTR keyName, int defaultValue);
  __int64 GetPrefixedRCInt64(LPCTSTR prefix, LPCTSTR keyName, __int64 defaultValue);
  bool GetPrefixedRCLine(LPCTSTR prefix, LPCTSTR keyName, LPTSTR buffer, LPCTSTR defaultValue, size_t cchBuffer);
  UINT GetPrefixedRCMonitor(LPCTSTR prefix, LPCTSTR keyName, UINT defaultValue);
  Distance GetPrefixedRCDistance(LPCTSTR prefix, LPCTSTR keyName, Distance defaultValue);
  bool GetPrefixedRCString(LPCTSTR prefix, LPCTSTR keyName, LPTSTR buffer, LPCTSTR defaultValue, size_t cchBuffer);

  // Utility functions
  void IterateOverLines(LPCTSTR keyName, std::function<void (LPCTSTR line)> callback);
  void IterateOverTokens(LPCTSTR line, std::function<void (LPCTSTR token)> callback);
  void IterateOverLineTokens(LPCTSTR keyName, std::function<void (LPCTSTR token)> callback);
  void IterateOverCommandLineTokens(LPCTSTR prefix, LPCTSTR keyName, std::function<void (LPCTSTR token)> callback);

  // Parsing functions
  bool ParseBool(LPCTSTR boolString);
  IColorVal* ParseColor(LPCTSTR colorString, const IColorVal* defaultValue);
  UINT ParseMonitor(LPCTSTR monitorString, UINT defaultValue);

  using TaskHandle = LSTASKHANDLE;

  namespace detail {
    struct TaskThunk {
      std::function<void()> work;
      std::function<void(bool)> completion;
    };

    inline void CALLBACK RunTaskThunk(LPVOID context) {
      auto thunk = static_cast<TaskThunk*>(context);
      if (thunk && thunk->work) {
        thunk->work();
      }
    }

    inline void CALLBACK CompleteTaskThunk(LPVOID context, BOOL cancelled) {
      std::unique_ptr<TaskThunk> thunk(static_cast<TaskThunk*>(context));
      if (thunk && thunk->completion) {
        thunk->completion(cancelled != FALSE);
      }
    }
  }

  inline TaskHandle PostTask(LSTASKEXECUTEPROC executeProc, void* executeContext,
      LSTASKCOMPLETIONPROC completionProc, void* completionContext) {
    return LSPostTask(executeProc, executeContext, completionProc, completionContext);
  }

  inline TaskHandle PostTask(std::function<void()> work, std::function<void(bool)> completion)
  {
    if (!work) {
      return 0;
    }
    auto thunk = new (std::nothrow) detail::TaskThunk{ std::move(work), std::move(completion) };
    if (!thunk) {
      return 0;
    }
    TaskHandle handle = LSPostTask(detail::RunTaskThunk, thunk, detail::CompleteTaskThunk, thunk);
    if (handle == 0) {
      delete thunk;
    }
    return handle;
  }

  inline TaskHandle PostTask(std::function<void()> work, std::function<void()> completion)
  {
    if (!completion) {
      return PostTask(std::move(work), std::function<void(bool)>());
    }
    return PostTask(std::move(work), [fn = std::move(completion)](bool cancelled) mutable {
      if (!cancelled) {
        fn();
      }
    });
  }

  inline bool CancelTask(TaskHandle handle) {
    return handle != 0 && LSCancelTask(handle) != FALSE;
  }

  inline bool WaitTask(TaskHandle handle, DWORD timeout = INFINITE) {
    return handle == 0 ? false : (LSWaitTask(handle, timeout) != FALSE);
  }
}

