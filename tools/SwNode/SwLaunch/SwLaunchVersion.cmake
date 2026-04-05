# SwLaunch version source of truth.
#
# Override from configure time when needed, for example:
#   cmake -S . -B build-swlaunch-validate -DSWLAUNCH_VERSION_PATCH=1
#   cmake -S . -B build-swlaunch-validate -DSWLAUNCH_VERSION_PRERELEASE=rc1

set(SWLAUNCH_VERSION_MAJOR "1" CACHE STRING "SwLaunch semantic version major")
set(SWLAUNCH_VERSION_MINOR "0" CACHE STRING "SwLaunch semantic version minor")
set(SWLAUNCH_VERSION_PATCH "0" CACHE STRING "SwLaunch semantic version patch")
set(SWLAUNCH_VERSION_TWEAK "0" CACHE STRING "SwLaunch file version tweak")
set(SWLAUNCH_VERSION_PRERELEASE "" CACHE STRING "SwLaunch prerelease label without leading dash")
set(SWLAUNCH_VERSION_METADATA "" CACHE STRING "SwLaunch build metadata without leading plus")

foreach(_swlaunch_part
        SWLAUNCH_VERSION_MAJOR
        SWLAUNCH_VERSION_MINOR
        SWLAUNCH_VERSION_PATCH
        SWLAUNCH_VERSION_TWEAK)
    if(NOT "${${_swlaunch_part}}" MATCHES "^[0-9]+$")
        message(FATAL_ERROR "${_swlaunch_part} must be numeric, got '${${_swlaunch_part}}'")
    endif()
endforeach()

set(SWLAUNCH_VERSION_CORE
    "${SWLAUNCH_VERSION_MAJOR}.${SWLAUNCH_VERSION_MINOR}.${SWLAUNCH_VERSION_PATCH}")
set(SWLAUNCH_FILE_VERSION
    "${SWLAUNCH_VERSION_MAJOR}.${SWLAUNCH_VERSION_MINOR}.${SWLAUNCH_VERSION_PATCH}.${SWLAUNCH_VERSION_TWEAK}")

set(SWLAUNCH_VERSION_STRING "${SWLAUNCH_VERSION_CORE}")
if(NOT "${SWLAUNCH_VERSION_PRERELEASE}" STREQUAL "")
    string(APPEND SWLAUNCH_VERSION_STRING "-${SWLAUNCH_VERSION_PRERELEASE}")
endif()
if(NOT "${SWLAUNCH_VERSION_METADATA}" STREQUAL "")
    string(APPEND SWLAUNCH_VERSION_STRING "+${SWLAUNCH_VERSION_METADATA}")
endif()
