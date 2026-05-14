# Inject a favicon link into the Qt-generated WASM HTML.
# Idempotent: skips if the link is already present.
# Invoked from the OpenTodo target's POST_BUILD step with -DHTML=<path>.

# Strip wrapping double quotes that cmd.exe sometimes leaves on the value
# when the path contains spaces.
string(REGEX REPLACE "^\"|\"$" "" HTML "${HTML}")

if(NOT EXISTS "${HTML}")
    message(WARNING "patch_wasm_favicon: ${HTML} does not exist; skipping")
    return()
endif()

file(READ "${HTML}" _html)

if(_html MATCHES "rel=\"icon\"")
    return()
endif()

# Prefer the SVG (crisp at any DPI) with a PNG fallback.
set(_inject "    <link rel=\"icon\" type=\"image/svg+xml\" href=\"favicon.svg\">\n    <link rel=\"icon\" type=\"image/png\" sizes=\"32x32\" href=\"favicon.png\">\n  ")

# Insert just before </head>.
string(REPLACE "</head>" "${_inject}</head>" _patched "${_html}")

if(_patched STREQUAL _html)
    message(WARNING "patch_wasm_favicon: no </head> tag found; skipping")
    return()
endif()

file(WRITE "${HTML}" "${_patched}")
message(STATUS "patch_wasm_favicon: injected favicon links into ${HTML}")
