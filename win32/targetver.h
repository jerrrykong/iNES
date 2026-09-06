#pragma once

// Minimum required platform. The minimum required platform is the earliest
// version of Windows, Internet Explorer etc. that has the features required
// to run the application.
//
// Modify the following definitions if you must target an earlier platform.
// See MSDN for the latest information about the corresponding values.
//
// NOTE: keep this file ASCII-only. Resource compiler (rc.exe) reads included
// headers with the default code page; non-ASCII comments used to break its
// preprocessor (#if counting) and caused "RC1022: expected '#endif'".

#ifndef WINVER                  // minimum platform: Windows Vista
#define WINVER 0x0600
#endif

#ifndef _WIN32_WINNT            // minimum platform: Windows Vista
#define _WIN32_WINNT 0x0600
#endif

#ifndef _WIN32_WINDOWS          // minimum platform: Windows 98
#define _WIN32_WINDOWS 0x0410
#endif

#ifndef _WIN32_IE               // minimum platform: Internet Explorer 7.0
#define _WIN32_IE 0x0700
#endif
