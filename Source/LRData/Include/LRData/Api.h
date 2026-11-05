#pragma once

// UBT defines LRDATA_API as DLLEXPORT (or DLLIMPORT for the modules using it) to share the module's symbols between
// the editor's shared libraries. Those come from UE's platform headers, which these sources don't include: defined
// here the same way. Outside UBT LRDATA_API is nothing.
#ifndef DLLEXPORT
#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#define DLLIMPORT __declspec(dllimport)
#else
#define DLLEXPORT __attribute__((visibility("default")))
#define DLLIMPORT __attribute__((visibility("default")))
#endif
#endif
#ifndef LRDATA_API
#define LRDATA_API
#endif
