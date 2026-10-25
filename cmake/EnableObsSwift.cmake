# OBS 32.0.4 unconditionally adds its Swift Metal renderer, but its project()
# enables only C/C++. Enable Swift before generation even though our SDK build
# requests only libobs and the frontend API, not the renderer.
enable_language(Swift)
