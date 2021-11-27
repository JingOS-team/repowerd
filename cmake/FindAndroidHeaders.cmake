include(CheckIncludeFiles)

check_include_files(android-19/linux/android_alarm.h HAVE_ANDROID_HEADERS)

if(NOT HAVE_ANDROID_HEADERS)
    message(FATAL_ERROR "Could not find android headers")
endif()
