/* Kept apart from the R and V8 headers, which clash with windows.h */
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <pthread.h>
#endif
#include <cstdint>
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

/* The lowest address of the stack of the current thread as the system
 * reports it, or 0 if it does not. Stacks grow downwards on the platforms
 * of R. V8 finds the stack in the same ways. */
uintptr_t stack_low_end(){
#ifdef _WIN32
  /* The allocation of the stack, which contains this variable, begins at
   * its lowest reserved address */
  const char on_stack = 0;
  MEMORY_BASIC_INFORMATION memory;
  if(VirtualQuery(&on_stack, &memory, sizeof(memory)) == 0)
    return 0;
  return reinterpret_cast<uintptr_t>(memory.AllocationBase);
#elif defined(__APPLE__)
  pthread_t self = pthread_self();
  uintptr_t top = reinterpret_cast<uintptr_t>(pthread_get_stackaddr_np(self));
  size_t size = pthread_get_stacksize_np(self);
  if(top == 0 || size == 0 || size >= top)
    return 0;
  return top - size;
#elif defined(__linux__)
  pthread_attr_t attr;
  void* address = nullptr;
  size_t size = 0;
  if(pthread_getattr_np(pthread_self(), &attr) != 0)
    return 0;
  int error = pthread_attr_getstack(&attr, &address, &size);
  pthread_attr_destroy(&attr);
  if(error != 0)
    return 0;
  return reinterpret_cast<uintptr_t>(address);
#else
  return 0;
#endif
}
