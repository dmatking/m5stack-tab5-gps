set(EXTRA_REQUIRES "")
set(EXTRA_SRCS "sd_card.c" "ui_overlay.c" "tile_flash.c" "map_view.c" "tile_cache.c" "tile_synth.c" "touch.c" "")
set(EXTRA_DEFINES "")

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/main.cmake.extra")
    include(${CMAKE_CURRENT_LIST_DIR}/main.cmake.extra)
endif()

idf_component_register(
    SRCS "main.c" "board_m5stack_tab5.c" "board_defaults.c" ${EXTRA_SRCS}
    INCLUDE_DIRS "."
    REQUIRES ${EXTRA_REQUIRES}
)

if(EXTRA_DEFINES)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ${EXTRA_DEFINES})
endif()
