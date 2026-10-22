// SPDX-License-Identifier: GPL-2.0-or-later
#include "Frei0rModule.H"
#include <dlfcn.h>
#include <map>
#include <mutex>
namespace nle {
struct Module {int references=0;void(*deinit)()=nullptr;};
static std::map<void*,Module> modules;
static std::mutex moduleMutex;
void *acquire_frei0r(const std::string &filename,f0r_plugin_info_t &info) {
  std::lock_guard<std::mutex> lock(moduleMutex);
  void *handle=dlopen(filename.c_str(),RTLD_NOW);if(!handle)return nullptr;
  auto it=modules.find(handle);
  if(it==modules.end()) {
    auto init=reinterpret_cast<int(*)()>(dlsym(handle,"f0r_init"));
    auto deinit=reinterpret_cast<void(*)()>(dlsym(handle,"f0r_deinit"));
    auto getInfo=reinterpret_cast<void(*)(f0r_plugin_info_t*)>(dlsym(handle,"f0r_get_plugin_info"));
    if(!init||!deinit||!getInfo||!init()){dlclose(handle);return nullptr;}
    modules[handle]={1,deinit};getInfo(&info);
  } else {++it->second.references;reinterpret_cast<void(*)(f0r_plugin_info_t*)>(dlsym(handle,"f0r_get_plugin_info"))(&info);}
  return handle;
}
void release_frei0r(void *handle) {
  if(!handle)return;std::lock_guard<std::mutex> lock(moduleMutex);
  auto it=modules.find(handle);if(it==modules.end())return;
  if(--it->second.references==0){it->second.deinit();modules.erase(it);}dlclose(handle);
}
}
