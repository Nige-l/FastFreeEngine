vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO LuaJIT/LuaJIT
    REF a553b3de243b1ae07bdb21da4bdab77148793f76  # 2026-02-27
    SHA512 5c504989e6a0726277143ecb1eb25e08e20803141dd5cfe83ea716b23bde2542e21310ae350da34ef3b3a10f4709e45426f7ad05ef08983dd01f370b83199dac
    HEAD_REF master
    PATCHES
        msvcbuild.patch
        003-do-not-set-macosx-deployment-target.patch
)

vcpkg_cmake_get_vars(cmake_vars_file)
include("${cmake_vars_file}")

if(VCPKG_DETECTED_MSVC)
    set(VSCMD_ARG_TGT_ARCH "${VCPKG_TARGET_ARCHITECTURE}")
    if(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
        if(DEFINED ENV{PROCESSOR_ARCHITEW6432})
            set(host_arch $ENV{PROCESSOR_ARCHITEW6432})
        else()
            set(host_arch $ENV{PROCESSOR_ARCHITECTURE})
        endif()
        if(host_arch MATCHES "(amd|AMD)64")
            set(ENV{VSCMD_ARG_HOST_ARCH} "x64")
        endif()
    endif()

    vcpkg_list(SET options)
    if (VCPKG_LIBRARY_LINKAGE STREQUAL "static")
        list(APPEND options "MSVCBUILD_OPTIONS=static")
    endif()

    vcpkg_install_nmake(SOURCE_PATH "${SOURCE_PATH}"
        PROJECT_NAME "${CMAKE_CURRENT_LIST_DIR}/Makefile.nmake"
        OPTIONS
            ${options}
    )

    if (VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic")
        vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/luajit/luaconf.h" "defined(LUA_BUILD_AS_DLL)" "1")
    endif()

    file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/luajit.pc" DESTINATION "${CURRENT_PACKAGES_DIR}/lib/pkgconfig")
    if(NOT VCPKG_BUILD_TYPE)
        file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/luajit.pc" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig")
    endif()

    vcpkg_copy_pdbs()

    # jit including the specific vmdef.lua generated during the build
    file(COPY "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/src/jit" DESTINATION "${CURRENT_PACKAGES_DIR}/tools/luajit/lua")

else()
    vcpkg_list(SET options)
    if(VCPKG_CROSSCOMPILING)
        # --- FFE overlay: rebuild buildvm with PE support for Windows/MinGW cross-compilation ---
        #
        # The upstream vcpkg luajit host package (x64-linux with buildvm-64 feature) builds
        # buildvm-x64 with TARGET_SYS=Linux. That binary only supports ELF output and
        # fails with "no PE object support for this target" when cross-compiling for Windows.
        #
        # Fix: when the target is Windows (MinGW), rebuild buildvm from source on the host
        # with TARGET_SYS=Windows. This causes the host GCC to compile buildvm_peobj.c with
        # LJ_TARGET_WINDOWS=1 (via -DLUAJIT_OS=LUAJIT_OS_WINDOWS), enabling PE output.
        # The freshly built PE-capable buildvm replaces BUILDVM_X for this cross-build only.
        #
        # minilua (HOST_LUA) only runs DynASM Lua scripts and does not need PE awareness,
        # so the pre-built Linux minilua from the host package is reused as normal.
        if(VCPKG_TARGET_IS_WINDOWS)
            message(STATUS "[FFE/luajit overlay] Target is Windows/MinGW: rebuilding buildvm with PE object support")

            set(BUILDVM_PE_WORKDIR "${CURRENT_BUILDTREES_DIR}/buildvm-pe-host")
            file(MAKE_DIRECTORY "${BUILDVM_PE_WORKDIR}")
            file(COPY "${SOURCE_PATH}/src/" DESTINATION "${BUILDVM_PE_WORKDIR}/src")
            file(COPY "${SOURCE_PATH}/dynasm/" DESTINATION "${BUILDVM_PE_WORKDIR}/dynasm")

            # Step 1: build minilua (needed to generate buildvm_arch.h via DynASM)
            vcpkg_execute_required_process(
                COMMAND make -C "${BUILDVM_PE_WORKDIR}/src" "host/minilua"
                    "HOST_CC=gcc" "Q="
                WORKING_DIRECTORY "${BUILDVM_PE_WORKDIR}"
                LOGNAME "luajit-buildvm-pe-minilua"
            )

            # Step 2: build buildvm with TARGET_SYS=Windows so it gains PE output capability.
            # CROSS= sets the target cross-compiler prefix used for target architecture probing.
            # HOST_CC=gcc builds the buildvm host tool itself with Windows OS defines.
            vcpkg_execute_required_process(
                COMMAND make -C "${BUILDVM_PE_WORKDIR}/src" "host/buildvm"
                    "HOST_CC=gcc"
                    "CROSS=x86_64-w64-mingw32-"
                    "TARGET_SYS=Windows"
                    "Q="
                WORKING_DIRECTORY "${BUILDVM_PE_WORKDIR}"
                LOGNAME "luajit-buildvm-pe-build"
            )

            # Step 3: smoke-test PE object generation before continuing
            execute_process(
                COMMAND "${BUILDVM_PE_WORKDIR}/src/host/buildvm" -m peobj -o "${BUILDVM_PE_WORKDIR}/test_vm.o"
                RESULT_VARIABLE _buildvm_pe_result
            )
            if(NOT _buildvm_pe_result EQUAL 0)
                message(FATAL_ERROR "[FFE/luajit overlay] PE object smoke test failed — buildvm cannot generate PE objects. Cross-compilation for Windows is not possible.")
            endif()
            message(STATUS "[FFE/luajit overlay] buildvm PE object support verified")

            # TARGET_SYS, EXECUTABLE_SUFFIX, FILE_T and INSTALL_DEP must reach BOTH the
            # build AND install targets. vcpkg_build_make only passes OPTIONS to 'all',
            # NOT to 'install' (a vcpkg bug). We pass them via vcpkg_configure_make OPTIONS
            # so the configure script embeds them into COMMON_OPTIONS in Makefile.vcpkg.
            #
            # FILE_T=luajit.exe: LuaJIT top-level Makefile hard-codes FILE_T=luajit but
            # Windows builds produce luajit.exe. We override it so 'make install' finds
            # the correct source binary to install.
            # INSTALL_DEP=src/luajit.exe: similarly, the dependency check must find .exe.
            list(APPEND options
                "LJARCH=${VCPKG_TARGET_ARCHITECTURE}"
                "BUILDVM_X=${BUILDVM_PE_WORKDIR}/src/host/buildvm"
                "HOST_LUA=${CURRENT_HOST_INSTALLED_DIR}/manual-tools/${PORT}/minilua${VCPKG_HOST_EXECUTABLE_SUFFIX}"
                "TARGET_SYS=Windows"
                "EXECUTABLE_SUFFIX=${VCPKG_TARGET_EXECUTABLE_SUFFIX}"
            )
        else()
            # Non-Windows cross-compile (e.g., arm64-osx from x64-linux):
            # use pre-built tools from host package (upstream behaviour)
            list(APPEND options
                "LJARCH=${VCPKG_TARGET_ARCHITECTURE}"
                "BUILDVM_X=${CURRENT_HOST_INSTALLED_DIR}/manual-tools/${PORT}/buildvm-${VCPKG_TARGET_ARCHITECTURE}${VCPKG_HOST_EXECUTABLE_SUFFIX}"
                "HOST_LUA=${CURRENT_HOST_INSTALLED_DIR}/manual-tools/${PORT}/minilua${VCPKG_HOST_EXECUTABLE_SUFFIX}"
            )
        endif()
    else()
        # Native build: TARGET_SYS and EXECUTABLE_SUFFIX go via make_options as normal.
        # Nothing extra needed here — vcpkg_configure_make will use defaults.
    endif()

    # make_options: passed to vcpkg_install_make OPTIONS (build step only, NOT install).
    # Matches upstream: EXECUTABLE_SUFFIX is always included here.
    # For Windows cross-build, TARGET_SYS/EXECUTABLE_SUFFIX are ALSO in COMMON_OPTIONS
    # via configure OPTIONS above (to reach the install target).
    vcpkg_list(SET make_options "EXECUTABLE_SUFFIX=${VCPKG_TARGET_EXECUTABLE_SUFFIX}")
    set(strip_options "") # cf. src/Makefile
    if(VCPKG_TARGET_IS_OSX)
        vcpkg_list(APPEND make_options "TARGET_SYS=Darwin")
        set(strip_options " -x")
    elseif(VCPKG_TARGET_IS_IOS)
        vcpkg_list(APPEND make_options "TARGET_SYS=iOS")
        set(strip_options " -x")
    elseif(VCPKG_TARGET_IS_LINUX)
        vcpkg_list(APPEND make_options "TARGET_SYS=Linux")
    elseif(VCPKG_TARGET_IS_WINDOWS AND NOT VCPKG_CROSSCOMPILING)
        # Native Windows build (MSVC already handled above; this is native MinGW on Windows).
        vcpkg_list(APPEND make_options "TARGET_SYS=Windows")
        set(strip_options " --strip-unneeded")
    elseif(VCPKG_TARGET_IS_WINDOWS AND VCPKG_CROSSCOMPILING)
        # Cross-build for Windows: TARGET_SYS/EXECUTABLE_SUFFIX already in configure OPTIONS.
        set(strip_options " --strip-unneeded")
    endif()

    set(dasm_archs "")
    if("buildvm-32" IN_LIST FEATURES)
        string(APPEND dasm_archs " arm x86")
    endif()
    if("buildvm-64" IN_LIST FEATURES)
        string(APPEND dasm_archs " arm64 x64")
    endif()

    file(COPY "${CMAKE_CURRENT_LIST_DIR}/configure" DESTINATION "${SOURCE_PATH}")
    vcpkg_configure_make(SOURCE_PATH "${SOURCE_PATH}"
        COPY_SOURCE
        OPTIONS
            "BUILDMODE=${VCPKG_LIBRARY_LINKAGE}"
            ${options}
        OPTIONS_RELEASE
            "DASM_ARCHS=${dasm_archs}"
    )
    vcpkg_install_make(
        MAKEFILE "Makefile.vcpkg"
        OPTIONS
            ${make_options}
            "TARGET_AR=${VCPKG_DETECTED_CMAKE_AR} rcus"
            "TARGET_STRIP=${VCPKG_DETECTED_CMAKE_STRIP}${strip_options}"
    )
endif()

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/lib/lua"
    "${CURRENT_PACKAGES_DIR}/debug/share"
    "${CURRENT_PACKAGES_DIR}/lib/lua"
    "${CURRENT_PACKAGES_DIR}/share/lua"
    "${CURRENT_PACKAGES_DIR}/share/man"
)

file(REMOVE "${CURRENT_PACKAGES_DIR}/bin/luajit-symlink" "${CURRENT_PACKAGES_DIR}/debug/bin/luajit-symlink")

# vcpkg_copy_tools looks for luajit${VCPKG_TARGET_EXECUTABLE_SUFFIX}.
# When cross-compiling for Windows, the LuaJIT top-level Makefile installs the binary
# as "luajit" (the INSTALL_TNAME we pass) regardless of the .exe suffix — because the
# install destination name comes from INSTALL_TNAME, not from the source FILE_T.
# Rename to luajit.exe so vcpkg_copy_tools and the post-build validator both find it.
if(VCPKG_TARGET_IS_WINDOWS AND VCPKG_CROSSCOMPILING)
    if(EXISTS "${CURRENT_PACKAGES_DIR}/bin/luajit")
        file(RENAME "${CURRENT_PACKAGES_DIR}/bin/luajit" "${CURRENT_PACKAGES_DIR}/bin/luajit.exe")
    endif()
    if(EXISTS "${CURRENT_PACKAGES_DIR}/debug/bin/luajit")
        file(RENAME "${CURRENT_PACKAGES_DIR}/debug/bin/luajit" "${CURRENT_PACKAGES_DIR}/debug/bin/luajit.exe")
    endif()
endif()

vcpkg_copy_tools(TOOL_NAMES luajit AUTO_CLEAN)

vcpkg_fixup_pkgconfig()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYRIGHT")
