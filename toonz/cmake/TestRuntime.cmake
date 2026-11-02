# SPDX-License-Identifier: BSD-3-Clause

# The application bundles Toonz dylibs beside its executable. Test executables
# stay in their own build directories, while inherited dylib install names use
# @executable_path. Give CTest the actual library directories on macOS without
# changing application packaging or copying libraries into each test directory.
function(ot_configure_native_test_runtime test_name)
    if(APPLE)
        set(test_library_dirs)
        foreach(library tnzcore tnzbase tnzext toonzlib tfarm tnzstdfx sound colorfx image toonzqt tnztools)
            list(APPEND test_library_dirs "$<TARGET_FILE_DIR:${library}>")
        endforeach()
        string(REPLACE ";" ":" test_library_path "${test_library_dirs}")
        set_tests_properties(${test_name} PROPERTIES
            ENVIRONMENT "DYLD_LIBRARY_PATH=${test_library_path}"
        )
    endif()
endfunction()
