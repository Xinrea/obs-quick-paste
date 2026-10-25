# Prefer the public packages exported by OBS's Development install component.
# For an unpackaged SDK, provide BOTH OBS_SOURCE_DIR and OBS_SDK_DIR; see
# cmake/BUILDING.md. No OBS private UI headers or symbols are used.
include_guard(GLOBAL)

set(OBS_SOURCE_DIR "" CACHE PATH "OBS 32 source tree (fallback public headers)")
set(OBS_SDK_DIR "" CACHE PATH "OBS 32 binary/development SDK prefix")
set(OBS_BINARY_DIR "" CACHE PATH "Configured OBS build tree (fallback obsconfig.h)")

if(OBS_SDK_DIR)
  list(PREPEND CMAKE_PREFIX_PATH "${OBS_SDK_DIR}")
  if(APPLE)
    list(PREPEND CMAKE_PREFIX_PATH "${OBS_SDK_DIR}/Frameworks")
  endif()
endif()

find_package(libobs 32 QUIET CONFIG)
if(TARGET OBS::libobs)
  find_package(obs-frontend-api 32 QUIET CONFIG)
endif()
if(TARGET OBS::libobs AND TARGET OBS::obs-frontend-api)
  return()
endif()

if(NOT OBS_SOURCE_DIR OR NOT OBS_SDK_DIR)
  message(FATAL_ERROR
    "OBS 32 public SDK not found. Add its Development install prefix to "
    "CMAKE_PREFIX_PATH, run cmake -P cmake/BootstrapObs.cmake, or provide "
    "OBS_SOURCE_DIR and OBS_SDK_DIR (and OBS_BINARY_DIR for generated headers). "
    "Use -DBUILD_PLUGIN=OFF for Qt-only tests. See cmake/BUILDING.md.")
endif()

if(NOT EXISTS "${OBS_SOURCE_DIR}/libobs/obs-module.h"
    OR NOT EXISTS "${OBS_SOURCE_DIR}/frontend/api/obs-frontend-api.h")
  message(FATAL_ERROR "OBS_SOURCE_DIR must contain OBS 32 public libobs and frontend/api headers.")
endif()
file(STRINGS "${OBS_SOURCE_DIR}/libobs/obs-config.h" _obs_major
  REGEX "^#define LIBOBS_API_MAJOR_VER ")
if(NOT _obs_major MATCHES "LIBOBS_API_MAJOR_VER 32$")
  message(FATAL_ERROR "The fallback headers must come from OBS 32.")
endif()

find_path(OBS_CONFIG_INCLUDE_DIR obsconfig.h
  PATHS "${OBS_BINARY_DIR}/config" "${OBS_SDK_DIR}/include/obs"
    "${OBS_SDK_DIR}/Frameworks/libobs.framework/Headers"
  NO_DEFAULT_PATH REQUIRED)
find_library(OBS_CORE_LIBRARY NAMES obs libobs
  PATHS "${OBS_SDK_DIR}/lib" "${OBS_SDK_DIR}/bin/64bit"
    "${OBS_SDK_DIR}/Frameworks" "${OBS_SDK_DIR}/Contents/Frameworks"
  NO_DEFAULT_PATH REQUIRED)
find_library(OBS_FRONTEND_LIBRARY NAMES obs-frontend-api libobs-frontend-api obs-frontend-api.dylib
  PATHS "${OBS_SDK_DIR}/lib" "${OBS_SDK_DIR}/bin/64bit"
    "${OBS_SDK_DIR}/Frameworks" "${OBS_SDK_DIR}/Contents/Frameworks"
  NO_DEFAULT_PATH REQUIRED)

if(NOT TARGET OBS::libobs)
  add_library(OBS::libobs UNKNOWN IMPORTED)
  set_target_properties(OBS::libobs PROPERTIES
    IMPORTED_LOCATION "${OBS_CORE_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${OBS_SOURCE_DIR}/libobs;${OBS_CONFIG_INCLUDE_DIR}"
  )
  # OBS public vector headers use SIMDe on ARM. Usually supplied by obs-deps.
  if(APPLE OR CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64|ARM")
    find_path(OBS_SIMDE_INCLUDE_DIR simde/x86/sse2.h
      HINTS "${OBS_SDK_DIR}/include" REQUIRED)
    set_property(TARGET OBS::libobs APPEND PROPERTY
      INTERFACE_INCLUDE_DIRECTORIES "${OBS_SIMDE_INCLUDE_DIR}")
  endif()
endif()

if(NOT TARGET OBS::obs-frontend-api)
  add_library(OBS::obs-frontend-api UNKNOWN IMPORTED)
  set_target_properties(OBS::obs-frontend-api PROPERTIES
    IMPORTED_LOCATION "${OBS_FRONTEND_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${OBS_SOURCE_DIR}/frontend/api"
    INTERFACE_LINK_LIBRARIES OBS::libobs
  )
endif()
