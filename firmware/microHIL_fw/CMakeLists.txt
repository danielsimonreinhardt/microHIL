cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME microHIL_fw)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Der CompositeBuilder-Header muss auch fuer die separat gebaute
# USB_Device_Library sichtbar sein: usbd_core.c und usbd_ctlreq.c ziehen ihn
# ein, sobald USE_USBD_COMPOSITE gesetzt ist (siehe usbd_conf.h). Die
# INTERFACE-Library stm32cubemx wird von allen Zielen eingebunden.
target_include_directories(stm32cubemx INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/ST/STM32_USB_Device_Library/Class/CompositeBuilder/Inc
)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    Core/Src/protocol.c
    Core/Src/can_if.c
    Core/Src/slcan.c
    # Composite-Builder der ST-USB-Device-Library. Aus dem passenden
    # Cube-FW-Paket (F4 V1.28.3) nachtraeglich hinzugefuegt, weil CubeMX das
    # Composite-Device fuer die F4-Serie nicht selbst generiert. Bewusst hier
    # und nicht in cmake/stm32cubemx/CMakeLists.txt, weil CubeMX diese Datei
    # regeneriert.
    Middlewares/ST/STM32_USB_Device_Library/Class/CompositeBuilder/Src/usbd_composite_builder.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    # (CompositeBuilder/Inc haengt an der stm32cubemx-INTERFACE-Library, s. o.)
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
    # USE_USBD_COMPOSITE steht in USB_DEVICE/Target/usbd_conf.h, damit es auch
    # die separat gebaute USB_Device_Library erreicht.
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
