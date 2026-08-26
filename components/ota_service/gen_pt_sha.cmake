# Generate partition_table_sha256.h from partitions.csv.
# Invoked by ota_service/CMakeLists.txt via a CMake script (-P) run.
# Usage: cmake -DSRC=<partitions.csv> -DOUT=<header.h> -P gen_pt_sha.cmake
file(SHA256 ${SRC} LERO_PT_HASH)
file(WRITE ${OUT} "#pragma once\n#define LERO_PARTITION_TABLE_SHA256 \"${LERO_PT_HASH}\"\n")
