cmake_minimum_required(VERSION 3.10)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Define VITASDK to point at your SDK")
  endif()
endif()

project(vhdb C)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "VHDB")
set(VITA_TITLEID "VHDB00001")
set(VITA_VERSION "01.00")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O3 -std=gnu99")

include_directories(${CMAKE_SOURCE_DIR}/../include)

add_executable(vhdb
  main.c
  ${CMAKE_SOURCE_DIR}/../core/vhdb.c
  ${CMAKE_SOURCE_DIR}/../core/status.c
  ${CMAKE_SOURCE_DIR}/../core/sfo.c
  ${CMAKE_SOURCE_DIR}/../core/md5.c
  ${CMAKE_SOURCE_DIR}/../core/installed.c
)

target_link_libraries(vhdb
  vita2d
  SceDisplay_stub
  SceGxm_stub
  SceSysmodule_stub
  SceCtrl_stub
  SceCommonDialog_stub
  SceAppMgr_stub
  ScePgf_stub
  SceIofilemgr_stub
  freetype
  png
  jpeg
  z
  m
)

vita_create_self(eboot.bin vhdb UNSAFE)

vita_create_vpk(vhdb.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE sce_sys/icon0.png sce_sys/icon0.png
  FILE sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
)
