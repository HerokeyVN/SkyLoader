#pragma once

#include <string>
#include <vector>

struct LoaderResult {
  unsigned loadedPlugins{};
  std::wstring message;
  bool ok{};
};

// Owns the launch-side policy after Sky.exe has been located. The Win32 UI
// supplies configuration and renders LoaderResult; it does not know remote
// allocation, Bootstrap pipe timing, or plugin load ordering.
class LoaderController {
 public:
  LoaderController(std::wstring bootstrapPath, std::vector<std::wstring> plugins);

  LoaderResult installBootstrap(unsigned long processId) const;
  LoaderResult installBootstrapAndPlugins(unsigned long processId) const;
  LoaderResult loadPlugin(unsigned long processId, const std::wstring& pluginPath) const;
  LoaderResult unloadPlugin(unsigned long processId, const std::wstring& pluginPath) const;

 private:
  bool injectDll(unsigned long processId, const std::wstring& path, std::wstring& error) const;
  bool sendPipeCommand(const std::string& command, std::string& response) const;
  bool sendLoadCommand(const std::wstring& path) const;
  bool sendUnloadCommand(const std::wstring& path) const;

  std::wstring bootstrapPath_;
  std::vector<std::wstring> plugins_;
};
