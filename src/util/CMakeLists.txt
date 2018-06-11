cmake_minimum_required (VERSION 3.1.0)
PROJECT (REST_VOL_UTIL C)

set (REST_VOL_SRCS
    ${REST_VOL_SRCS}
    ${REST_VOL_UTIL_SOURCE_DIR}/rest_vol_hash_table.c
    PARENT_SCOPE
)

set (REST_VOL_HDRS
    ${REST_VOL_HDRS}
    ${REST_VOL_UTIL_SOURCE_DIR}/rest_vol_hash_table.h
    ${REST_VOL_UTIL_SOURCE_DIR}/rest_vol_hash_string.h
    PARENT_SCOPE
)