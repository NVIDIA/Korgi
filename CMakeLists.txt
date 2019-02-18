cmake_minimum_required(VERSION 3.10)

project(korgi)

add_executable(korgi src/main.cpp src/control_surface_map.cpp)

set(OUTPUT_PATH ${CMAKE_CURRENT_LIST_DIR}/bin)

# Need ALSA for MIDI on Linux
if (UNIX)
    find_package(ALSA REQUIRED)
    if (ALSA_FOUND)
        include_directories(${ALSA_INCLUDE_DIR})
        target_link_libraries(korgi ${ALSA_LIBRARIES})
    endif (ALSA_FOUND)
endif (UNIX)

set_target_properties(korgi PROPERTIES 
	RUNTIME_OUTPUT_DIRECTORY_DEBUG ${OUTPUT_PATH}
	RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${OUTPUT_PATH}
	RUNTIME_OUTPUT_DIRECTORY_RELEASE ${OUTPUT_PATH}
	RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${OUTPUT_PATH}
)
