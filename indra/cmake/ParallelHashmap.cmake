# -*- cmake -*-
include_guard()

add_library(ll::parallel-hashmap INTERFACE IMPORTED)

find_path(PARALLEL_HASHMAP_INCLUDE_DIR parallel_hashmap/phmap.h)
target_include_directories(ll::parallel-hashmap SYSTEM INTERFACE ${PARALLEL_HASHMAP_INCLUDE_DIR})
