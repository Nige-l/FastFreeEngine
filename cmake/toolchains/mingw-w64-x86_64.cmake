set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Disable vcpkg's APPLOCAL_DEPS post-link step.
# That step invokes powershell.exe to copy DLLs next to the executable, which
# works on a native Windows host but fails on a Linux cross-compilation host
# because powershell.exe is not available.  The cross-built binaries are not
# run on this machine, so DLL co-location is not needed here.
set(VCPKG_APPLOCAL_DEPS OFF)
