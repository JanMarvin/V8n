.onLoad <- function(libname, pkgname) {
  set_callback_env(asNamespace(pkgname))
}

.onAttach <- function(libname, pkgname) {
  packageStartupMessage(paste("Using V8 engine", version()))
  if (icu_missing())
    packageStartupMessage(
      "ICU data of libv8 not found: Intl and locale functions give errors. ",
      "Set options(V8n.icu_data) to the file icudtl.dat before loading V8n."
    )
}
