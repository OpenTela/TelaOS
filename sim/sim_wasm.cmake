# sim_wasm.cmake — the simulator as WebAssembly (Emscripten): the same firmware, LVGL and Lua.
# Included by sim/CMakeLists.txt when it runs under emcmake. See docs/SIMULATOR.md, «В браузере».
#
#   emcmake cmake -S sim -B build/sim-web -G Ninja && cmake --build build/sim-web
#     build/sim-web/telaos_sim.html (+ .js, .wasm, .data): the watch in a page, mouse or finger
#   emcmake cmake -S sim -B build/sim-node -G Ninja -DTELA_SIM_WASM=node && cmake --build build/sim-node
#     node build/sim-node/telaos_sim.js --headless --script ...: the robot under Node.js on the
#     real files, so the WebAssembly build is checked by the same scripts and shots as the PC one
#
# SDL2 and libpng are Emscripten's ports. mbedtls, which the PC build takes from the system
# (libmbedtls-dev), is built from its sources here: Emscripten has no port of it.

include(FetchContent)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(GEN_FILES OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(tela_mbedtls
  GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
  GIT_TAG v2.28.8  # the version of Ubuntu 24.04's libmbedtls-dev, which the PC build links
  GIT_SHALLOW TRUE
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(tela_mbedtls)

# Emscripten is clang. The TelaOS line in lib/lvgl/src/widgets/span/lv_span.c calls
# lv_obj_get_ext_draw_size() without its header (core/lv_obj_draw_private.h): GCC lets a call
# without a declaration pass, clang 16+ stops. The call is right on wasm32 (int and int32_t),
# so here it is let through as GCC does, and lib/lvgl stays as it is
target_compile_options(tela_lvgl PRIVATE -Wno-error=implicit-function-declaration)
# glibc and the watch's newlib bring <time.h> in with other headers, Emscripten's musl does not.
# Firmware files that use time() or struct tm without <ctime> get it pre-included, as every
# firmware C++ file gets <cstring> (CMakeLists.txt)
set_property(SOURCE ${TELA_CPP} APPEND PROPERTY COMPILE_OPTIONS "-include;ctime")

set(TELA_WASM_PORTS -sUSE_SDL=2 -sUSE_LIBPNG=1)
target_compile_options(telaos_sim PRIVATE ${TELA_WASM_PORTS})
target_link_libraries(telaos_sim PRIVATE tela_lvgl tela_lua mbedcrypto)
# The PC gives the firmware 8 MB of stack; WebAssembly's default is 64 KB
target_link_options(telaos_sim PRIVATE ${TELA_WASM_PORTS} -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8MB)

if(TELA_SIM_WASM STREQUAL "node")
  target_link_options(telaos_sim PRIVATE -sENVIRONMENT=node -sNODERAWFS=1 -sEXIT_RUNTIME=1)
  return()
endif()

# The page owns the thread: main() hands it back once a frame with emscripten_sleep()
# (sim_tela_main.cpp). Only main() is on the stack then, so Asyncify rewrites main() alone,
# and SDL is told not to sleep inside its own calls (sim_window.cpp)
target_link_options(telaos_sim PRIVATE -sENVIRONMENT=web -sASYNCIFY
  -sASYNCIFY_ONLY=main
  "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/web/sim_web_shell.html"
  "SHELL:--preload-file ${TELAOS_DIR}/data@/data")
# Sample apps shown next to data/apps: HTML, CSS, DOM and animations on the watch
foreach(app IN LISTS TELA_SIM_WEB_APPS)
  target_link_options(telaos_sim PRIVATE
    "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/apps/${app}@/data/apps/${app}")
endforeach()
set_target_properties(telaos_sim PROPERTIES SUFFIX ".html")
set_property(TARGET telaos_sim APPEND PROPERTY LINK_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/web/sim_web_shell.html")
