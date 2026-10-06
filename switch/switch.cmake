# Nintendo Switch (devkitA64 + libnx) target wiring.
#
# Configure with devkitPro's toolchain wrapper:
#   aarch64-none-elf-cmake -S . -B build-switch -G Ninja -DCMAKE_BUILD_TYPE=Release
# or simply run switch/build.sh, which drives the devkitPro Docker image.
#
# The Switch host is the desktop host with the desktop-only pieces left out:
# SDL2 from portlibs presents through SDL_Renderer (Mesa/nouveau underneath),
# and there is no desktop OpenGL presenter, launcher process relaunch, or
# internet netplay.

set(MMX_SWITCH_APP_TITLE "Mega Man X Recompiled" CACHE STRING "NRO title")
set(MMX_SWITCH_APP_AUTHOR "Thorhax" CACHE STRING "NRO author" FORCE)
# The Switch port's own release number (the upstream game is PROJECT_VERSION).
set(MMX_SWITCH_APP_VERSION "1.0.0" CACHE STRING "NRO version" FORCE)
option(MMX_SWITCH_LAUNCHER "Include the Dear ImGui launcher (Mods, Controls, Settings)" ON)
set(MMX_SWITCH_ICON "${CMAKE_CURRENT_LIST_DIR}/icon.jpg" CACHE FILEPATH "NRO icon (256x256 JPEG)")

function(mmx_switch_target target)
    target_sources(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/switch/switch_main.c)
    target_compile_definitions(${target} PRIVATE MMX_DESKTOP_ENTRY=mmx_desktop_main)
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/switch/compat)
    if(MMX_SWITCH_LAUNCHER)
        # The shared Dear ImGui launcher over GLES2. Switch has one window, so
        # the in-game launcher restarts the NRO into it (see host_relaunch.c).
        recomp_target_launcher_ui(${target} CONSOLE snes
            BOXART ${CMAKE_CURRENT_SOURCE_DIR}/recomp/launcher/boxart.tga)
        target_compile_definitions(${target} PRIVATE MMX_SWITCH_LAUNCHER=1
            IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS)
        set_source_files_properties(${RECOMP_UI_ROOT}/src/third_party/tinyfiledialogs.c
            PROPERTIES HEADER_FILE_ONLY ON)
        target_sources(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/switch/tinyfd_switch.c)
    else()
        # The mod plugins still share recomp-ui's plain C ABI headers.
        target_include_directories(${target} PRIVATE ${RECOMP_UI_ROOT}/src)
    endif()
    target_link_libraries(${target} PRIVATE nx m)
    set_target_properties(${target} PROPERTIES OUTPUT_NAME "MegaManX" SUFFIX ".elf")

    nx_generate_nacp(${target}.nacp
        NAME "${MMX_SWITCH_APP_TITLE}"
        AUTHOR "${MMX_SWITCH_APP_AUTHOR}"
        VERSION "${MMX_SWITCH_APP_VERSION}")
    if(EXISTS "${MMX_SWITCH_ICON}")
        nx_create_nro(${target} OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/MegaManX.nro
            NACP ${target}.nacp ICON "${MMX_SWITCH_ICON}")
    else()
        nx_create_nro(${target} OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/MegaManX.nro
            NACP ${target}.nacp)
    endif()
endfunction()
