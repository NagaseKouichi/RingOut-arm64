// Copyright 2018 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Dolphin's own WASAPI backend is MSVC-only in practice: it pulls in wil/, the
// Windows Implementation Library, which does not survive a MinGW/libc++ build
// (it breaks inside libc++'s own <atomic> internals). Excluded on MinGW.
//
// Nothing needs a stub. SoundStream declares `static bool IsValid() { return
// false; }`, so WASAPIStream::IsValid() resolves to the inherited base and the
// backend simply never offers itself -- the same path every non-Windows build
// already takes. Audio still works: cubeb is enabled here, and cubeb's own
// Windows backend is WASAPI.
// This header is not self-contained without these: it derives from SoundStream,
// and every declaration below needs them. Upstream has them inside the _WIN32
// block, which only works because the .cpp is not compiled off Windows and
// every other includer happens to pull SoundStream.h in first.
#include <string>
#include <vector>

#include "AudioCommon/SoundStream.h"

#if defined(_WIN32) && !defined(__MINGW32__)

// clang-format off
#include <Windows.h>
#include <mmreg.h>
#include <objbase.h>
#include <wil/resource.h>
// clang-format on

#include <atomic>
#include <thread>
#include <wrl/client.h>

struct IAudioClient;
struct IAudioRenderClient;
struct IMMDevice;
struct IMMDeviceEnumerator;

#endif

class WASAPIStream final : public SoundStream
{
#if defined(_WIN32) && !defined(__MINGW32__)
public:
  explicit WASAPIStream();
  ~WASAPIStream() override;
  bool Init() override;
  bool SetRunning(bool running) override;

  static bool IsValid();
  static std::vector<std::string> GetAvailableDevices();
  static Microsoft::WRL::ComPtr<IMMDevice> GetDeviceByName(std::string_view name);

private:
  void SoundLoop();

  u32 m_frames_in_buffer = 0;
  std::atomic<bool> m_running = false;
  std::thread m_thread;

  // CoUninitialize must be called after all WASAPI COM objects have been destroyed,
  // therefore this member must be located before them, as first class fields are destructed last
  wil::unique_couninitialize_call m_coinitialize{false};

  Microsoft::WRL::ComPtr<IMMDeviceEnumerator> m_enumerator;
  Microsoft::WRL::ComPtr<IAudioClient> m_audio_client;
  Microsoft::WRL::ComPtr<IAudioRenderClient> m_audio_renderer;
  wil::unique_event_nothrow m_need_data_event;
  WAVEFORMATEXTENSIBLE m_format;
#endif  // _WIN32
};
