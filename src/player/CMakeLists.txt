file(GLOB PLAYER_SRC_LIST
        *.cpp
        *.c
        *.h
)

target_sources(${PROJECT_NAME} PRIVATE ${PLAYER_SRC_LIST})
