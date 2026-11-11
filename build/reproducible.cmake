# coop-buildid: one set of sources makes one exe, byte for byte, wherever and whenever it is built (Jep, 2026-11-03). The
# build scripts pass this file as CMAKE_PROJECT_INCLUDE, so it reaches every target (the game, tools/dcdata) without a
# change to Chronicle's files. Before it, two builds of one commit differed in the link time (COFF and debug directory
# stamps) and in the folders: __FILE__ (COOP_PRESENT_HERE) put the source path in .rdata, moving everything after it when
# its length differed, and the debug info's paths (the source tree, the build folder, deps\, nlohmann_json's source) went
# into the CodeView id lld makes from it, which the stripped exe keeps.
include_guard(GLOBAL)

# -ffile-prefix-map covers __FILE__ and the debug info alike. One quoted item each: the paths hold spaces.
function(dco_prefix_map from to)
    if(from)
        cmake_path(NORMAL_PATH from)
        string(REGEX REPLACE "/$" "" from "${from}")
        add_compile_options("-ffile-prefix-map=${from}=${to}")
    endif()
endfunction()

dco_prefix_map("${CMAKE_SOURCE_DIR}" "src")
dco_prefix_map("${CMAKE_BINARY_DIR}" "bin")
# The toolchain's deps\ folder (acquire.ps1 -Root): <deps>/llvm/<name>/bin/clang++.exe, SDL3 and the Vulkan headers beside it.
# Both as given and as its real path: the dev build's debug\deps is a junction to out\deps, and clang names its own headers
# (libc++, its resource dir, mingw's) by the real one (4018 of the first build's 5228 deps paths were libc++'s).
if(DCO_DEPS_DIR)
    dco_prefix_map("${DCO_DEPS_DIR}" "deps")
    file(REAL_PATH "${DCO_DEPS_DIR}" dco_deps_real)
    if(NOT dco_deps_real STREQUAL DCO_DEPS_DIR)
        dco_prefix_map("${dco_deps_real}" "deps")
    endif()
endif()
if(FETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON)
    dco_prefix_map("${FETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON}" "nlohmann_json")
endif()

# No link time in the exe (lld writes 0 for the COFF and debug directory stamps). The build scripts that pass this file are
# Windows's (llvm-mingw's lld); CMAKE_HOST_WIN32 is set whatever languages the top-level project(dcdecomp NONE) enables.
if(CMAKE_HOST_WIN32)
    add_link_options(-Wl,--no-insert-timestamp)
endif()
