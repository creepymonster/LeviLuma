# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/user/LeviLuma/next/simulator/build/_deps/lvgl_theme_shadcn-src"
  "/home/user/LeviLuma/next/simulator/build/_deps/lvgl_theme_shadcn-build"
  "/home/user/LeviLuma/next/simulator/build/_deps/lvgl_theme_shadcn-subbuild/lvgl_theme_shadcn-populate-prefix"
  "/home/user/LeviLuma/next/simulator/build/_deps/lvgl_theme_shadcn-subbuild/lvgl_theme_shadcn-populate-prefix/tmp"
  "/home/user/LeviLuma/next/simulator/build/_deps/lvgl_theme_shadcn-subbuild/lvgl_theme_shadcn-populate-prefix/src/lvgl_theme_shadcn-populate-stamp"
  "/home/user/LeviLuma/next/simulator/build/_deps/lvgl_theme_shadcn-subbuild/lvgl_theme_shadcn-populate-prefix/src"
  "/home/user/LeviLuma/next/simulator/build/_deps/lvgl_theme_shadcn-subbuild/lvgl_theme_shadcn-populate-prefix/src/lvgl_theme_shadcn-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/user/LeviLuma/next/simulator/build/_deps/lvgl_theme_shadcn-subbuild/lvgl_theme_shadcn-populate-prefix/src/lvgl_theme_shadcn-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/user/LeviLuma/next/simulator/build/_deps/lvgl_theme_shadcn-subbuild/lvgl_theme_shadcn-populate-prefix/src/lvgl_theme_shadcn-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
