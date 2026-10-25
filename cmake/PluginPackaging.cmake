include_guard(GLOBAL)

if(APPLE)
  # A loadable CFBundle, not a .dylib renamed to .plugin.
  set_target_properties(obs-quick-paste PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION plugin
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.xinrea.obs-quick-paste"
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
    INSTALL_RPATH "@executable_path/../Frameworks"
    # Use host-relative paths before Xcode signs the bundle. Rewriting RPATH
    # during install/CPack would invalidate that signature.
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH_USE_LINK_PATH FALSE
  )
  file(GLOB_RECURSE _plugin_resources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/data/*")
  foreach(_resource IN LISTS _plugin_resources)
    file(RELATIVE_PATH _relative "${CMAKE_CURRENT_SOURCE_DIR}/data" "${_resource}")
    get_filename_component(_directory "${_relative}" DIRECTORY)
    target_sources(obs-quick-paste PRIVATE "${_resource}")
    set_source_files_properties("${_resource}" PROPERTIES
      MACOSX_PACKAGE_LOCATION "Resources/${_directory}")
  endforeach()
  target_sources(obs-quick-paste PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
  set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources)
  install(TARGETS obs-quick-paste LIBRARY DESTINATION . COMPONENT Plugin)
elseif(WIN32)
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Only 64-bit Windows is supported.")
  endif()
  install(TARGETS obs-quick-paste RUNTIME DESTINATION obs-plugins/64bit
    LIBRARY DESTINATION obs-plugins/64bit)
  install(DIRECTORY data/ DESTINATION data/obs-plugins/obs-quick-paste)
  install(FILES LICENSE DESTINATION data/obs-plugins/obs-quick-paste)
else()
  include(GNUInstallDirs)
  install(TARGETS obs-quick-paste LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/obs-plugins")
  install(DIRECTORY data/ DESTINATION "${CMAKE_INSTALL_DATADIR}/obs/obs-plugins/obs-quick-paste")
  install(FILES LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/obs/obs-plugins/obs-quick-paste")
endif()

# Deliberately no runtime dependency collection, macdeployqt or windeployqt:
# OBS provides libobs, the frontend API and Qt in the host process.
set(CPACK_PACKAGE_NAME obs-quick-paste)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "obs-quick-paste contributors")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_GENERATOR ZIP)
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_VERBATIM_VARIABLES YES)

if(APPLE)
  set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${PROJECT_VERSION}-macos-${CMAKE_OSX_ARCHITECTURES}")
  set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/installers/CPackProjectConfig.cmake")
  set(CPACK_PRODUCTBUILD_IDENTIFIER "com.xinrea.obs-quick-paste")
  # OBS 32 arm64 scans the user's Application Support .plugin directory, not
  # /Library. Installer's user domain resolves /Library relative to the home.
  set(CPACK_PRODUCTBUILD_DOMAINS TRUE)
  set(CPACK_PRODUCTBUILD_DOMAINS_ANYWHERE FALSE)
  set(CPACK_PRODUCTBUILD_DOMAINS_USER TRUE)
  set(CPACK_PRODUCTBUILD_DOMAINS_ROOT FALSE)
  set(CPACK_COMPONENT_PLUGIN_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/installers/Plugin-component.plist")
  # productbuild requires a recognized resource extension, unlike ZIP/NSIS.
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "${CMAKE_CURRENT_BINARY_DIR}/installer-license.txt" COPYONLY)
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_BINARY_DIR}/installer-license.txt")
  set(CPACK_RESOURCE_FILE_WELCOME "${CMAKE_CURRENT_SOURCE_DIR}/cmake/installers/macos-welcome.txt")
  set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/cmake/installers/macos-welcome.txt")
elseif(WIN32)
  set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${PROJECT_VERSION}-windows-x64")
  # The stock NSIS template removes shared install directories and creates a
  # generic uninstaller. Our small template keeps CPack's file manifest but
  # never removes an OBS-owned directory or touches the OBS uninstaller.
  list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/installers")
  set(CPACK_NSIS_PACKAGE_NAME "OBS Quick Paste ${PROJECT_VERSION} (unsigned)")
  set(CPACK_NSIS_DISPLAY_NAME "OBS Quick Paste")
  set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "obs-quick-paste")
  set(CPACK_NSIS_UNINSTALL_NAME "uninstall-obs-quick-paste")
endif()
include(CPack)
