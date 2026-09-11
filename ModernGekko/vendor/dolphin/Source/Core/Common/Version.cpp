// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/Version.h"

#include <string>

#include "Common/scmrev.h"

namespace Common
{
#define EMULATOR_NAME "Dolphin"

#ifdef _DEBUG
#define BUILD_TYPE_STR "Debug "
#elif defined DEBUGFAST
#define BUILD_TYPE_STR "DebugFast "
#else
#define BUILD_TYPE_STR ""
#endif

const std::string& GetEmulatorName()
{
  static const std::string emulator_name = EMULATOR_NAME;
  return emulator_name;
}

const std::string& GetScmRevStr()
{
  static const std::string scm_rev_str = EMULATOR_NAME " "
  // Note this macro can be empty if the master branch does not exist.
#if 1 - SCM_COMMITS_AHEAD_MASTER - 1 != 0
                                                       "[" SCM_BRANCH_STR "] "
#endif

#ifdef __INTEL_COMPILER
      BUILD_TYPE_STR SCM_DESC_STR "-ICC";
#else
      BUILD_TYPE_STR SCM_DESC_STR;
#endif
  return scm_rev_str;
}

const std::string& GetScmRevGitStr()
{
  // THE NETPLAY HANDSHAKE AND THE .DTM REVISION BOTH KEY OFF THIS STRING, and
  // both compare it byte for byte -- so it has to identify the RELEASE, not the
  // machine that compiled it.
  //
  // Upstream sets SCM_REV_STR from `git rev-parse HEAD` in the source tree.
  // Here that is unstable across build environments, not merely across commits:
  //
  //   * on this workstation, ModernGekko/ is a NESTED git repo whose .git is
  //     not tracked by the outer one, so git answers with the vendored Dolphin
  //     commit -- 1873066167f3..., fixed forever;
  //   * on a CI runner the nested .git does not exist in a fresh clone, git
  //     walks up to the outer repo, and the answer is the RingOut commit --
  //     different on every push.
  //
  // The Linux and Deck packages are built here; the Windows package is built on
  // CI. Two builds of THE SAME RELEASE therefore disagreed, and a Linux player
  // and a Windows player on identical versions were refused at connect with
  // "The server and client's NetPlay versions are incompatible." Measured, not
  // theorised: that is exactly how a desktop-to-laptop session failed.
  //
  // Keyed on VERSION instead. Every build of 1.5.2 now shakes hands as 1.5.2 --
  // wherever it was compiled, and by whom -- which makes "both use the same
  // release" true advice rather than a hope. It also stops a rebuild silently
  // breaking compatibility with binaries already in players' hands.
  //
  // What this does NOT weaken: two different builds of one version can now
  // connect, but the CompatibilityFingerprint in netplay_compatibility.cpp
  // still compares the disc hash, the module's chunk hashes, the ABI versions
  // and sizeof(CPUState) -- the things that actually have to agree for the
  // emulated state to stay identical.
#ifdef MODERNGEKKO_PROJECT_VERSION
  static const std::string scm_rev_git_str = "RingOut " MODERNGEKKO_PROJECT_VERSION;
#else
  static const std::string scm_rev_git_str = SCM_REV_STR;
#endif
  return scm_rev_git_str;
}

const std::string& GetScmDescStr()
{
  static const std::string scm_desc_str = SCM_DESC_STR;
  return scm_desc_str;
}

const std::string& GetScmBranchStr()
{
  static const std::string scm_branch_str = SCM_BRANCH_STR;
  return scm_branch_str;
}

const std::string& GetUserAgentStr()
{
  static const std::string user_agent_str = EMULATOR_NAME "/" SCM_DESC_STR;
  return user_agent_str;
}

const std::string& GetScmDistributorStr()
{
  static const std::string scm_distributor_str = SCM_DISTRIBUTOR_STR;
  return scm_distributor_str;
}

const std::string& GetScmUpdateTrackStr()
{
  static const std::string scm_update_track_str = SCM_UPDATE_TRACK_STR;
  return scm_update_track_str;
}

const std::string& GetNetplayDolphinVer()
{
#ifdef _WIN32
  static const std::string netplay_dolphin_ver = SCM_DESC_STR " Win";
#elif __APPLE__
  static const std::string netplay_dolphin_ver = SCM_DESC_STR " Mac";
#else
  static const std::string netplay_dolphin_ver = SCM_DESC_STR " Lin";
#endif
  return netplay_dolphin_ver;
}

int GetScmCommitsAheadMaster()
{
  // Note this macro can be empty if the master branch does not exist.
  return SCM_COMMITS_AHEAD_MASTER + 0;
}

}  // namespace Common
