// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace EMM
{
void InstallExceptionHandler();
void UninstallExceptionHandler();
bool IsExceptionHandlerSupported();
// True between InstallExceptionHandler() and UninstallExceptionHandler().
// Dolphin's own design assumes exactly one owner -- the Windows implementation
// asserts on a second install -- so a second would-be owner has to be able to
// ask rather than install blindly.
bool IsExceptionHandlerInstalled();
}  // namespace EMM
