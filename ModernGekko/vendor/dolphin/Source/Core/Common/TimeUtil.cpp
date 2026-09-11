// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/TimeUtil.h"

#include <cstring>
#include <ctime>
#include <optional>

#include "Common/Logging/Log.h"

namespace Common
{
std::optional<std::tm> LocalTime(std::time_t time)
{
  std::tm local_time;
// _WIN32, not _MSC_VER: localtime_s is the UCRT's, so MinGW has it too, while
// localtime_r is POSIX and MinGW does not. Note the argument orders are
// reversed between the two -- localtime_s takes the output first.
#ifdef _WIN32
  if (localtime_s(&local_time, &time) != 0)
#else
  if (localtime_r(&time, &local_time) == NULL)
#endif
  {
    ERROR_LOG_FMT(COMMON, "Failed to convert time to local time: {}", std::strerror(errno));
    return std::nullopt;
  }
  return local_time;
}
}  // Namespace Common
