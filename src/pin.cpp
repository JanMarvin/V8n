/* Kept apart from the R and V8 headers, which clash with windows.h */
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <string>

static const char anchor = 0;

/* V8 runs worker threads, and R keeps finalizers of contexts, that point
 * into this library. Unloading it, as pkgload::load_all() does when it
 * reloads the package, would leave both pointing to unmapped memory. So
 * the library stays loaded until the process ends; a reload of a rebuilt
 * package loads a new copy next to it. */
void pin_library(){
#ifdef _WIN32
  HMODULE module;
  GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                     &anchor, &module);
#else
  Dl_info info;
  if(dladdr(&anchor, &info) != 0 && info.dli_fname != nullptr)
    dlopen(info.dli_fname, RTLD_LAZY | RTLD_NOLOAD | RTLD_NODELETE);
#endif
}

/* The file of the loaded library that contains an address, or "" */
std::string library_path(const void* address){
#ifdef _WIN32
  HMODULE module;
  char path[MAX_PATH];
  if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         static_cast<LPCSTR>(address), &module))
    return std::string();
  DWORD length = GetModuleFileNameA(module, path, MAX_PATH);
  if(length == 0 || length >= MAX_PATH)
    return std::string();
  return std::string(path, length);
#else
  Dl_info info;
  if(dladdr(address, &info) == 0 || info.dli_fname == nullptr)
    return std::string();
  return std::string(info.dli_fname);
#endif
}
