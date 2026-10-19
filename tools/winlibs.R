# R CMD INSTALL runs make again for symbols.rds after the build and the cleanup.
# Downloading libv8 again there would make R CMD check attribute the symbols of
# libv8 to the package, so the download is skipped once the objects exist.
if(!file.exists("engine.o") && !file.exists("../.deps/libv8/include/v8.h")){
  unlink("../.deps", recursive = TRUE)
  url <- if(grepl("aarch", R.version$platform)){
    "https://github.com/r-windows/bundles/releases/download/v8-14.6.202.33/v8-14.6.202.33-clang-aarch64.tar.xz"
  } else if(grepl("clang", Sys.getenv("R_COMPILED_BY"))){
    "https://github.com/r-windows/bundles/releases/download/v8-14.6.202.33/v8-14.6.202.33-clang-x86_64.tar.xz"
  } else if(getRversion() >= "4.4"){
    "https://github.com/r-windows/bundles/releases/download/v8-14.6.202.33/v8-14.6.202.33-ucrt-x86_64.tar.xz"
  } else {
    "https://github.com/r-windows/bundles/releases/download/v8-11.9.169.6/v8-11.9.169.6-ucrt-x86_64.tar.xz"
  }
  options(timeout = 600)
  download.file(url, basename(url), mode = "wb", quiet = TRUE)
  dir.create("../.deps", showWarnings = FALSE)
  untar(basename(url), exdir = "../.deps", tar = "internal")
  unlink(basename(url))
  setwd("../.deps")
  file.rename(list.files(), "libv8")
}
