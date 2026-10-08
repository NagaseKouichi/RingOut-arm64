#ifndef DOLRECOMP_NATIVE_REGIONS_H
#define DOLRECOMP_NATIVE_REGIONS_H
/* --native-regions <file>: listed hot guest regions run as native C (see native_regions.c). */
#include <stdbool.h>
#include "common/types.h"

/* Reads the region list ("START END" in hex per line, # comments). false = unreadable or malformed. */
bool native_regions_load(const char* path);
unsigned native_regions_count(void);
/* Forget the loaded list (tests run several lists in one process). */
void native_regions_reset(void);
/* Post-pass over the written chunk files (after --native-skin). true unless a file could not be read or written. */
bool native_regions_apply(const char* chunks_dir);
#endif
