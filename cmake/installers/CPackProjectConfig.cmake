# CPack reads this once per selected generator, after CPackConfig.cmake.
# Keep ZIP's bundle-at-root layout independent of the native installer layout.
if(CPACK_GENERATOR STREQUAL "productbuild")
  set(CPACK_PACKAGING_INSTALL_PREFIX "/Library/Application Support/obs-studio/plugins")
  set(CPACK_COMPONENTS_ALL Plugin)
  set(CPACK_COMPONENT_PLUGIN_REQUIRED TRUE)
  set(CPACK_COMPONENT_PLUGIN_HIDDEN TRUE)
  set(CPACK_COMPONENT_PLUGIN_DISPLAY_NAME "OBS Quick Paste")
endif()
