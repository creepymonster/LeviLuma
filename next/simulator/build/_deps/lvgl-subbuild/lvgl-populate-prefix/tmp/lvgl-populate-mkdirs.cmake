# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/user/LeviLuma/next/simulator/build/_deps/lvgl-src"
  "/home/user/LeviLuma/next/simulator/build/_deps/lvgl-build"
  "/home/user/LeviLuma/next/simulator/build/_deps/lvgl-subbuild/lvgl-populate-prefix"
  "/home/user/LeviLuma/next/simulator/build/_deps/lvgl-subbuild/lvgl-populate-prefix/tmp"
  "/home/user/LeviLuma/next/simulator/build/_deps/lvgl-subbuild/lvgl-populate-prefix/src/lvgl-populate-stamp"
  "/home/user/LeviLuma/next/simulator/build/_deps/lvgl-subbuild/lvgl-populate-prefix/src"
  "/home/user/LeviLuma/next/simulator/build/_deps/lvgl-subbuild/lvgl-populate-prefix/src/lvgl-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/user/LeviLuma/next/simulator/build/_deps/lvgl-subbuild/lvgl-populate-prefix/src/lvgl-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/user/LeviLuma/next/simulator/build/_deps/lvgl-subbuild/lvgl-populate-prefix/src/lvgl-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
