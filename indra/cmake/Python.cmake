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

# On Windows the registry names the native installations; a Cygwin or MSYS
# python on the PATH would otherwise be found first.
if(WINDOWS)
  set(Python3_FIND_REGISTRY FIRST)
endif()

# We always want to find the active virtual env first
set(Python3_FIND_VIRTUALENV FIRST)

# The interpreter is for the tests that spawn a Python peer; without it they
# are registered disabled. Nothing that builds or packages the viewer runs it.
find_package(Python3 COMPONENTS Interpreter)
