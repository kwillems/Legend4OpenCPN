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
set(PKG_VERSION 0.1.1)
set(PKG_PRERELEASE "")
set(DISPLAY_NAME Legend)
set(PLUGIN_API_NAME Legend)

set(PKG_SUMMARY "Configurable legend and notes for OpenCPN")
set(PKG_DESCRIPTION [=[
Displays configurable PNG legends and Markdown notes in OpenCPN.
]=])

set(PKG_AUTHOR "Koen Willems")
set(PKG_IS_OPEN_SOURCE "yes")
set(PKG_HOMEPAGE "")
set(PKG_INFO_URL "")

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------

set(SRC
    src/legend_pi.h
    src/legend_pi.cpp

    third_party/md4c/src/md4c.c
    third_party/md4c/src/md4c-html.c
    third_party/md4c/src/entity.c
)

set(PKG_API_LIB api-18)

macro(late_init)
    include_directories(
        ${CMAKE_SOURCE_DIR}/third_party/md4c/src
    )
endmacro()

macro(add_plugin_libraries)
endmacro()
