
cmake_minimum_required(VERSION 3.18)

project(keyclick)

add_executable(
  keyclick WIN32
  keyclick.cpp
  keyclick.rc
  resource.h
)

target_compile_options(
  keyclick
  PUBLIC /W4
)

target_link_options(
  keyclick
  PRIVATE /SUBSYSTEM:WINDOWS
)
