set(CMAKE_CXX_STANDARD 17)
include_directories(include/)
include_directories(base/include)
add_library(
  suspend STATIC

   autosuspend.c
   autosuspend_wakeup_count.cpp
   base/file.cpp
   base/strings.cpp
)
