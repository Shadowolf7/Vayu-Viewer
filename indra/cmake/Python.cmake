include_guard()

# Allow explicit Python path via environment variable
if(DEFINED ENV{PYTHON})
    set(Python3_ROOT_DIR "$ENV{PYTHON}")
endif()

# If no explicit Python or active virtualenv, auto-detect repo root .venv
if(NOT DEFINED Python3_ROOT_DIR AND NOT DEFINED ENV{VIRTUAL_ENV})
    if(EXISTS "${CMAKE_SOURCE_DIR}/../.venv")
        set(Python3_ROOT_DIR "${CMAKE_SOURCE_DIR}/../.venv")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/.venv")
        set(Python3_ROOT_DIR "${CMAKE_SOURCE_DIR}/.venv")
    endif()
endif()

# On Windows, prefer registry entries to avoid Cygwin/MSYS Python
# The registry is searched first by default, which finds native Windows Python
# installations rather than Cygwin/MSYS Python
if(WINDOWS)
    set(Python3_FIND_REGISTRY FIRST CACHE STRING "Python search order")
endif()

# We always want to find the active virtual env first
set(Python3_FIND_VIRTUALENV FIRST)

# Find Python 3 interpreter
find_package(Python3 REQUIRED COMPONENTS Interpreter)
