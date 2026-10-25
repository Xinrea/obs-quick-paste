cmake_minimum_required(VERSION 3.28)

project(obs-quick-paste VERSION 1.0.0 LANGUAGES CXX)

option(BUILD_PLUGIN "Build the OBS Studio plugin" ON)
include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets)

function(quick_paste_target_defaults target)
  target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(${target} PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /utf-8 /permissive-)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

if(BUILD_PLUGIN)
  include(ObsSdk)
  add_library(obs-quick-paste MODULE
    src/plugin-main.cpp
    src/paste-event-filter.cpp
    src/paste-target.cpp
    src/clipboard-reader.cpp
    src/image-storage.cpp
    src/obs-source-factory.cpp
    src/source-layout.cpp
    src/plugin-settings.cpp
  )
  quick_paste_target_defaults(obs-quick-paste)
  target_link_libraries(obs-quick-paste PRIVATE OBS::libobs OBS::obs-frontend-api)
  target_compile_definitions(obs-quick-paste PRIVATE
    PLUGIN_NAME="obs-quick-paste"
    PLUGIN_VERSION="${PROJECT_VERSION}"
  )
  set_target_properties(obs-quick-paste PROPERTIES PREFIX "")
  include(PluginPackaging)
endif()

if(BUILD_TESTING)
  find_package(Qt6 6.2 REQUIRED COMPONENTS Test)

  function(quick_paste_test name)
    add_executable(${name} "tests/${name}.cpp" ${ARGN})
    quick_paste_target_defaults(${name})
    target_link_libraries(${name} PRIVATE Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
      TIMEOUT 60
    )
    if(WIN32)
      # Tests use the SDK in place; this is not runtime deployment.
      set_property(TEST ${name} APPEND PROPERTY ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
  endfunction()

  quick_paste_test(test-clipboard-storage
    src/clipboard-reader.cpp src/image-storage.cpp)
  quick_paste_test(test-paste-event-filter
    src/paste-event-filter.cpp src/paste-target.cpp src/clipboard-reader.cpp)
  quick_paste_test(test-source-layout src/source-layout.cpp)
  quick_paste_test(test-plugin-settings src/plugin-settings.cpp)
endif()
