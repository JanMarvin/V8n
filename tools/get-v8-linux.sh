download_libs() {
# On debian CI we want to test against system libv8
if [ "$USER" = "salsaci" ]; then
  return;
fi

# Detect libcxx
if echo "$CXX" | grep -Fq "clang"; then
if printf "#include <string>\n#ifndef _LIBCPP_VERSION\n#error not libcxx\n#endif" | $CXX -E -xc++ - >/dev/null 2>&1; then
IS_LIBCXX="with libc++"
fi
fi

# Alpine
if ldd --version 2>&1 | grep -Fq musl; then
IS_MUSL=1
fi


# Gets the R target architecture in case of qemu-containers, e.g
# https://hub.docker.com/r/i386/debian
# Which reports uname -m: x86_64 (only i386 seems to have this issue)
RARCH=$(${R_HOME}/bin/Rscript -e 'cat(R.Version()$arch)')
case $RARCH in
  x86_64)
    ARCH="amd64"
    ;;
  arm64 | aarch64)
    ARCH="arm64"
    ;;
  powerpc64le)
    ARCH="ppc64"
    ;;
  *)
    echo "Unexpected architecture: $RARCH"
    return;
    ;;
esac

echo "Target architecture: $ARCH $IS_LIBCXX"
if [ "$IS_MUSL" ]; then
  URL="https://github.com/jeroen/build-v8-static/releases/download/12.4.254.21/v8-alpine-${ARCH}.tar.gz"
elif [ "$IS_LIBCXX" ]; then
  URL="https://github.com/jeroen/build-v8-static/releases/download/12.4.254.21/v8-debian-12-libcxx-${ARCH}.tar.gz"
elif [ "$ARCH" = "amd64" ]; then
  URL="https://github.com/jeroen/build-v8-static/releases/download/12.4.254.21/v8-rocky-8-amd64.tar.gz"
else
  URL="https://github.com/jeroen/build-v8-static/releases/download/12.4.254.21/v8-debian-11-${ARCH}.tar.gz"
fi
if [ ! -f ".deps/lib/libv8_monolith.a" ]; then
  echo "Getting bundle: $(basename $URL)"
  if ${R_HOME}/bin/R -s -e "options(timeout = 600); download.file('$URL', 'libv8.tar.gz', mode = 'wb', quiet = TRUE)"; then
    tar xf libv8.tar.gz
    rm -f libv8.tar.gz
    mv v8 .deps
  fi
fi
if [ -f ".deps/lib/libv8_monolith.a" ]; then
  PKG_CFLAGS="-I${PWD}/.deps/include"
  PKG_LIBS="-L${PWD}/.deps/lib -lv8_monolith"
fi
}

download_libs
