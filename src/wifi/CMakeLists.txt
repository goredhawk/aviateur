file(GLOB_RECURSE WIFI_SRC_LIST
        *.cpp
        *.c
        *.h
)

target_sources(${PROJECT_NAME} PRIVATE ${WIFI_SRC_LIST})
