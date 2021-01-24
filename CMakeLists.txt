cmake_minimum_required(VERSION 2.8)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(Max ASM C CXX)
include("${VITASDK}/share/vita.cmake" REQUIRED)
set(VITA_APP_NAME "Max Payne")
set(VITA_TITLEID  "MAXP00000")
set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -Wl,-q -Wall -O2 -mfloat-abi=softfp -std=gnu11")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++11")

add_executable(max.elf
  main.c
  util.c
  so_util.c
  imports.c
  hooks/opengl.c
  hooks/openal.c
  hooks/game.c
  memcpy_neon.S
)

target_link_libraries(max.elf
  -Wl,--whole-archive pthread -Wl,--no-whole-archive
  stdc++
  m
  openal
  SDL2
  pib
  kubridge_stub
  SceAppMgr_stub
  SceAudio_stub
  SceCtrl_stub
  SceCommonDialog_stub
  SceDisplay_stub
  SceGxm_stub
  SceHid_stub
  SceSysmodule_stub
  ScePower_stub
  SceTouch_stub
  SceMotion_stub
)

vita_create_self(eboot.bin max.elf UNSAFE)
vita_create_vpk(Max.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE ${CMAKE_SOURCE_DIR}/sce_sys/icon0.png sce_sys/icon0.png
       ${CMAKE_SOURCE_DIR}/sce_sys/livearea/contents/bg0.png sce_sys/livearea/contents/bg0.png
       ${CMAKE_SOURCE_DIR}/sce_sys/livearea/contents/default_gate.png sce_sys/livearea/contents/default_gate.png
       ${CMAKE_SOURCE_DIR}/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
)

add_custom_target(copy
  COMMAND cp eboot.bin E:/app/${VITA_TITLEID}/eboot.bin
  DEPENDS eboot.bin
)
