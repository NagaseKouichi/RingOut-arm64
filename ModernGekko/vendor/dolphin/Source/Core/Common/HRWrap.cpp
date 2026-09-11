// Copyright 2021 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "HRWrap.h"

#ifdef __MINGW32__
#include <windows.h>

#include "Common/StringUtil.h"

namespace Common
{
std::string GetHResultMessage(HRESULT hr)
{
  // What winrt::hresult_error::message() does underneath. FORMAT_MESSAGE_
  // ALLOCATE_BUFFER makes the system allocate, so the buffer is freed with
  // LocalFree rather than delete.
  LPWSTR buffer = nullptr;
  const DWORD length = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, static_cast<DWORD>(hr), 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
  if (length == 0 || buffer == nullptr)
  {
    if (buffer != nullptr)
      LocalFree(buffer);
    return "Unknown error";
  }

  std::wstring message(buffer, length);
  LocalFree(buffer);

  // FormatMessage returns text ending in CR LF; the callers format it inline.
  while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n'))
    message.pop_back();

  return WStringToUTF8(message);
}
}  // namespace Common

#else

namespace Common
{
std::string GetHResultMessage(HRESULT hr)
{
  auto err = winrt::hresult_error(hr);
  return winrt::to_string(err.message());
}
std::string GetHResultMessage(const winrt::hresult& hr)
{
  return GetHResultMessage(hr.value);
}
}  // namespace Common

#endif
