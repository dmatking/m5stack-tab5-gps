# Required CMake Version
cmake_minimum_required(VERSION 3.16)

# esp32-wifi-prov bundles the captive-portal component nested inside its own
# component folder -- ESP-IDF's component scanner won't discover a component
# nested inside another component's folder, so copy it up to components/
# before the scan happens. Same trick used by every sibling project that
# pulls in this submodule.
file(COPY "${CMAKE_SOURCE_DIR}/components/wifi_prov/nordesems__esp-captive-portal"
     DESTINATION "${CMAKE_SOURCE_DIR}/components/")

# Set up ESP-IDF build environment
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Project name
project(m5stack-tab5-gps)

