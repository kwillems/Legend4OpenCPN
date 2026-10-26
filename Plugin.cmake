# ---------------------------------------------------------------------------
# Upload repositories
# ---------------------------------------------------------------------------

set(
    OCPN_TEST_REPO
    "Legend4OpenCPN/legend-alpha"
    CACHE STRING "Default repository for untagged builds"
)

set(
    OCPN_BETA_REPO
    "Legend4OpenCPN/legend-beta"
    CACHE STRING "Default repository for beta builds"
)

set(
    OCPN_RELEASE_REPO
    "Legend4OpenCPN/legend-prod"
    CACHE STRING "Default repository for release builds"
)

# ---------------------------------------------------------------------------
# Plugin identity
# ---------------------------------------------------------------------------

set(PKG_NAME legend_pi)
set(PKG_VERSION 0.2.0)
set(PKG_PRERELEASE "")
set(DISPLAY_NAME Legend)
set(PLUGIN_API_NAME Legend)

set(PKG_SUMMARY "Configurable legend and notes for OpenCPN")
set(PKG_DESCRIPTION [=[
Displays configurable PNG legends and Markdown notes in OpenCPN,
plus a dynamic legend of the waypoint symbols visible on screen.
]=])

set(PKG_AUTHOR "Koen Willems")
set(PKG_IS_OPEN_SOURCE "yes")
set(PKG_HOMEPAGE "https://github.com/kwillems/Legend4OpenCPN")
set(PKG_INFO_URL "https://github.com/kwillems/Legend4OpenCPN")

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------

# The template's CI scripts only fetch the opencpn-libs submodule.
if (NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/md4c/src/md4c.c")
    execute_process(
        COMMAND git submodule update --init third_party/md4c
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endif ()

set(SRC
    src/legend_pi.h
    src/legend_pi.cpp
    src/legend_chart.cpp
    src/legend_collect.cpp
    src/legend_navigation.cpp
    src/legend_notes.cpp
    src/legend_render.cpp
    src/legend_window.cpp
    src/legend_shared.h
    src/legend_shared.cpp
    src/legend_icons.h
    src/legend_icons.cpp
    src/legend_html_window.h
    src/legend_html_window.cpp
    src/chart_symbols.h
    src/chart_symbols.cpp
    src/ocharts_source.h
    src/ocharts_source.cpp
    src/s57_source.h
    src/s57_source.cpp
    src/display_settings.h
    src/display_settings.cpp
    src/chart_names.h
    src/chart_names.cpp
    src/note_places.h
    src/note_places.cpp
    src/note_editor.h
    src/note_editor.cpp
    src/light_character.h
    src/light_character.cpp
    src/vhf_areas.h
    src/vhf_areas.cpp
    src/layer_waypoints.h
    src/layer_waypoints.cpp
    src/chart_notice.h
    src/chart_notice.cpp
    src/vhf_highlight.h
    src/vhf_highlight.cpp
    src/shining_lights.h
    src/shining_lights.cpp
    src/symbol_meanings.h
    src/symbol_meanings.cpp
    src/geo.h
    src/geo.cpp

    third_party/md4c/src/md4c.c
    third_party/md4c/src/md4c-html.c
    third_party/md4c/src/entity.c
)

set(PKG_API_LIB api-18)

# Minimum version which should be loaded. Ignored if api < 22.
# Allows plugins using functions from 1.22+ API to be loaded by
# hosts only supporting 1.21; plugins then needs to check if host
# supports 1.22+ before using such functions.
# If undefined, plugin will only be loaded by host supporting the
# included api.
## set(MIN_API_VERSION 1.21)

macro(late_init)
    include_directories(
        ${CMAKE_SOURCE_DIR}/third_party/md4c/src
    )
endmacro()

macro(add_plugin_libraries)
    # The note editor uses wxRichTextCtrl, which is not among the
    # template's wxWidgets components (cmake/PluginLibs.cmake).
    if (NOT QT_ANDROID)
        find_package(wxWidgets REQUIRED richtext)
        target_link_libraries(${PACKAGE_NAME} ${wxWidgets_LIBRARIES})

        # Without /utf-8 MSVC turns "·" into the Windows code page
        # instead of UTF-8, and wxString::FromUTF8 then gives nothing.
        if (MSVC)
            target_compile_options(${PACKAGE_NAME} PRIVATE /utf-8)
        endif ()

        # Unit tests of the parts that do without OpenCPN: built with
        # "make legend-tests", run with "ctest" in the build folder.
        enable_testing()
        find_package(wxWidgets REQUIRED core base xml)
        add_executable(legend-tests EXCLUDE_FROM_ALL
            test/test_main.cpp
            test/light_character_test.cpp
            test/vhf_areas_test.cpp
            test/note_places_test.cpp
            test/symbol_meanings_test.cpp
            test/layer_waypoints_test.cpp
            test/geo_test.cpp
            src/light_character.cpp
            src/vhf_areas.cpp
            src/note_places.cpp
            src/symbol_meanings.cpp
            src/layer_waypoints.cpp
            src/geo.cpp
        )
        target_include_directories(legend-tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
        target_link_libraries(legend-tests ${wxWidgets_LIBRARIES})
        if (MSVC)
            target_compile_options(legend-tests PRIVATE /utf-8)
        endif ()
        add_test(NAME legend COMMAND legend-tests)
    endif ()
endmacro()
