# TelaOS simulator: the firmware's own sources, LVGL, lv_conf.h, fonts and data/, built for PC.
# The firmware is only read, never written. See docs/SIMULATOR.md.
#
#   cmake -S sim -B build/sim && cmake --build build/sim -j
#   build/sim/telaos_sim                                                      # window, mouse = finger
#   build/sim/telaos_sim --headless --script sim/tests/tela_hello.sim --out shots
#
# TELAOS_DIR may point at another checkout: CI builds this simulator against the PR base
# and against the PR itself, to compare their screens.
cmake_minimum_required(VERSION 3.16)
project(telaos_sim C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS ON)  # gnu++2a, as in platformio.ini
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

get_filename_component(TELAOS_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(TELAOS_DIR "${TELAOS_DEFAULT}" CACHE PATH "TelaOS checkout to run (read-only)")
set(TELA_SCREEN_W 480 CACHE STRING "SCREEN_WIDTH (esp4848s040: 480)")
set(TELA_SCREEN_H 480 CACHE STRING "SCREEN_HEIGHT (esp4848s040: 480)")
if(NOT EXISTS "${TELAOS_DIR}/src/main.cpp")
  message(FATAL_ERROR "TelaOS not found at ${TELAOS_DIR}")
endif()

# Same defines as platformio.ini [common] for the firmware
set(TELA_DEFINES
  LV_CONF_INCLUDE_SIMPLE=1 LUA_32BITS=1 BOARD_HAS_PSRAM
  SCREEN_WIDTH=${TELA_SCREEN_W} SCREEN_HEIGHT=${TELA_SCREEN_H}
  TELAOS_SIM=1)

# --- LVGL exactly as vendored (lib/lvgl), configured by include/lv_conf.h.
# Drive "C:" (stdio under /littlefs on the watch) comes from sim_tela_fs.cpp instead of
# lv_fs_stdio.c: it reads the simulated LittleFS.
file(GLOB_RECURSE LVGL_SRC CONFIGURE_DEPENDS "${TELAOS_DIR}/lib/lvgl/src/*.c")
list(FILTER LVGL_SRC EXCLUDE REGEX "/libs/fsdrv/lv_fs_stdio\\.c$")
add_library(tela_lvgl STATIC ${LVGL_SRC})
target_include_directories(tela_lvgl PUBLIC "${TELAOS_DIR}/include" "${TELAOS_DIR}/lib/lvgl")
target_compile_definitions(tela_lvgl PUBLIC ${TELA_DEFINES})
target_compile_options(tela_lvgl PRIVATE -w)

# --- Lua as vendored (lib/lua), 32-bit numbers like on the watch
file(GLOB LUA_SRC "${TELAOS_DIR}/lib/lua/src/*.c")
add_library(tela_lua STATIC ${LUA_SRC})
target_include_directories(tela_lua PUBLIC "${TELAOS_DIR}/lib/lua/src")
target_compile_definitions(tela_lua PUBLIC LUA_32BITS=1)
target_compile_options(tela_lua PRIVATE -w)

# --- Firmware sources: everything except the board layer
file(GLOB_RECURSE TELA_SRC CONFIGURE_DEPENDS "${TELAOS_DIR}/src/*.cpp" "${TELAOS_DIR}/src/*.c")
list(FILTER TELA_SRC EXCLUDE REGEX "/src/hal/boards/")
list(FILTER TELA_SRC EXCLUDE REGEX "/src/hal/device\\.cpp$")  # replaced by sim_tela_device.cpp
list(APPEND TELA_SRC "${TELAOS_DIR}/lib/blowfish/bf_blowfish.c" "${TELAOS_DIR}/lib/md4c/md4c.c")
# SD card: the firmware's own host (no-op) code path
set_source_files_properties("${TELAOS_DIR}/src/hal/sdcard.cpp" "${TELAOS_DIR}/src/hal/lv_fs_sd.cpp"
  PROPERTIES COMPILE_DEFINITIONS LVGL_MOCK_ENABLED=1)
# Firmware code builds with its own warnings off (they belong to the firmware build);
# C++ gets <cstring> pre-included like TelaCI
set(TELA_CPP ${TELA_SRC})
list(FILTER TELA_CPP INCLUDE REGEX "\\.cpp$")
set(TELA_C ${TELA_SRC})
list(FILTER TELA_C INCLUDE REGEX "\\.c$")
set_source_files_properties(${TELA_CPP} PROPERTIES COMPILE_OPTIONS "-w;-include;cstring")
set_source_files_properties(${TELA_C} PROPERTIES COMPILE_OPTIONS "-w")

find_package(SDL2 REQUIRED)
find_package(PNG REQUIRED)

add_executable(telaos_sim
  ${TELA_SRC}
  sim_tela_main.cpp sim_tela_device.cpp sim_tela_shim.cpp sim_tela_fs.cpp
  sim_panel.cpp sim_window.cpp sim_robot.cpp sim_png.cpp)
# Order matters: sim shims first, real LVGL before TelaCI stubs (which carry a mock lvgl.h)
target_include_directories(telaos_sim PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/shim"
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${TELAOS_DIR}/src"
  "${TELAOS_DIR}/include"
  "${TELAOS_DIR}/lib/lvgl"
  "${TELAOS_DIR}/lib/lua/src"
  "${TELAOS_DIR}/lib/blowfish"
  "${TELAOS_DIR}/lib/md4c"
  "${TELAOS_DIR}/ci/stubs")
target_compile_definitions(telaos_sim PRIVATE TELAOS_DATA_DIR="${TELAOS_DIR}/data" PROGMEM=)
target_link_libraries(telaos_sim PRIVATE tela_lvgl tela_lua SDL2::SDL2 PNG::PNG mbedcrypto m)
target_link_options(telaos_sim PRIVATE -Wl,--wrap=time)  # wall clock = virtual clock (sim_tela_shim.cpp)
