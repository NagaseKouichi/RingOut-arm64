# MinGW case-alias headers

Every file here is one line: `#include <lowercase.h>`.

Dolphin includes a number of Windows SDK headers using the SDK's own display
casing — `<Windows.h>`, `<SetupAPI.h>`, `<XInput.h>`, `<WinSock2.h>` and the
rest. On Windows that resolves, because NTFS is case-insensitive. MinGW-w64's
Linux sysroot ships those same headers with lowercase names, and a Linux
filesystem is case-sensitive, so each of those includes fails to resolve when
cross-compiling.

`../mingw-x86_64.cmake` puts this directory first on the include path, so the
SDK casing resolves to the lowercase file that actually exists.

**This is worth knowing before you conclude the cross-build is unworkable.** The
failures read as missing Windows SDK headers, which points at a broken or
incomplete sysroot. The sysroot is fine. It is only the casing.

To add another: create `<TheirCasing>.h` containing `#include <theircasing.h>`.
Nothing needs to be registered anywhere.
