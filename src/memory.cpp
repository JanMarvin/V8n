#include <cstdint>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

/* Kept apart from the R headers, which do not mix well with windows.h */
uint64_t physical_memory(){
#ifdef _WIN32
  MEMORYSTATUSEX status;
  status.dwLength = sizeof(status);
  if(GlobalMemoryStatusEx(&status) == 0)
    return 0;
  return static_cast<uint64_t>(status.ullTotalPhys);
#else
  long pages = sysconf(_SC_PHYS_PAGES);
  long page_size = sysconf(_SC_PAGESIZE);
  if(pages <= 0 || page_size <= 0)
    return 0;
  return static_cast<uint64_t>(pages) * static_cast<uint64_t>(page_size);
#endif
}
