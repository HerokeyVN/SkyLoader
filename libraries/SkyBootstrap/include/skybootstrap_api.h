#pragma once

#include <stdint.h>

#ifdef _WIN32
#ifdef SKYBOOTSTRAP_BUILD
#define SKYBOOTSTRAP_API __declspec(dllexport)
#else
#define SKYBOOTSTRAP_API
#endif
#define SKYBOOTSTRAP_CALL __stdcall
#else
#define SKYBOOTSTRAP_API
#define SKYBOOTSTRAP_CALL
#endif

#define SKYBOOTSTRAP_API_VERSION 3u

typedef struct SkyBootstrapApi {
  uint32_t version;
  void (SKYBOOTSTRAP_CALL *log)(const char *message);
} SkyBootstrapApi;

// Optional exports implemented by plugins loaded through SkyBootstrap.
typedef int (SKYBOOTSTRAP_CALL *SkyPluginInitFn)(const SkyBootstrapApi *api);
typedef void (SKYBOOTSTRAP_CALL *SkyPluginShutdownFn)(void);
// Bootstrap exports. SkyLoader normally talks to the named pipe instead of
// resolving these functions remotely.
#ifdef __cplusplus
extern "C" {
#endif
SKYBOOTSTRAP_API int SKYBOOTSTRAP_CALL SkyBootstrapLoadPluginW(const wchar_t *path);
SKYBOOTSTRAP_API int SKYBOOTSTRAP_CALL SkyBootstrapUnloadPluginW(const wchar_t *path);
SKYBOOTSTRAP_API uint32_t SKYBOOTSTRAP_CALL SkyBootstrapPluginCount(void);
#ifdef __cplusplus
}
#endif
