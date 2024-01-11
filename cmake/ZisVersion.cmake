###
### ZiS: define version number and string
###

include_guard(DIRECTORY)

file(READ "${CMAKE_SOURCE_DIR}/VERSION.txt" ZIS_VERSION_STRING)
string(REGEX MATCH "([0-9]+)\\.([0-9]+)\\.([0-9]+)" ZIS_VERSION_STRING ${ZIS_VERSION_STRING})
set(ZIS_VERSION_MAJOR ${CMAKE_MATCH_1})
set(ZIS_VERSION_MINOR ${CMAKE_MATCH_2})
set(ZIS_VERSION_PATCH ${CMAKE_MATCH_3})

message(STATUS "ZiS version ${ZIS_VERSION_STRING}")
