#ifndef DOLRECOMP_NATIVE_SKIN_H
#define DOLRECOMP_NATIVE_SKIN_H
/* --native-skin: native fast paths for SoulCalibur II's CPU skinning loops (see native_skin.c). */
#include <stdbool.h>
#include "analysis/code_section.h"

void native_skin_enable(bool on);
/* Post-pass over the written chunk files. true unless a file could not be read or written. */
bool native_skin_apply(const LoadedCodeSection* sections, u32 count, const char* chunks_dir);
#endif
