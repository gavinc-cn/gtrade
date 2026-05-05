function(conan_auto_install)
    set(CONAN_OUTPUT_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/conan_out/Release")
    message(CONAN_OUTPUT_FOLDER=${CONAN_OUTPUT_FOLDER})

    # Build conan install command
    set(CONAN_CMD
        conan install ${CMAKE_CURRENT_SOURCE_DIR}
            -b missing
            -pr=${CMAKE_CURRENT_SOURCE_DIR}/conan_profile/win_release
            -s build_type=Release
            -of=${CONAN_OUTPUT_FOLDER}
            -d=full_deploy
            --deployer-folder=${CMAKE_CURRENT_SOURCE_DIR}/conan_lib/Release
    )

    # Execute conan install
    message(STATUS "Running: ${CONAN_CMD}")
    execute_process(
        COMMAND ${CONAN_CMD}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        RESULT_VARIABLE CONAN_RESULT
        OUTPUT_VARIABLE CONAN_OUTPUT
        ERROR_VARIABLE CONAN_ERROR
    )

    # Check result
    if(NOT CONAN_RESULT EQUAL 0)
        message(WARNING "=== Conan Install Failed ===")
        message(WARNING "Exit Code: ${CONAN_RESULT}")
        message(WARNING "Error Output:\n${CONAN_ERROR}")
        message(WARNING "Standard Output:\n${CONAN_OUTPUT}")
        message(FATAL_ERROR "Conan installation failed. Please check your Conan setup and try again.")
    endif()

    message(STATUS "Conan install completed successfully")
    message(STATUS "==========================")

    # Set toolchain file path in parent scope
    set(CMAKE_TOOLCHAIN_FILE "${CONAN_OUTPUT_FOLDER}/conan_toolchain.cmake" PARENT_SCOPE)

    # Also set the output folder in parent scope for reference
    set(CONAN_OUTPUT_FOLDER "${CONAN_OUTPUT_FOLDER}" PARENT_SCOPE)

    # Map all build configurations to use Release libraries from Conan
    # This allows Debug/RelWithDebInfo/MinSizeRel builds to link against Conan Release libraries
    # See: https://cmake.org/cmake/help/latest/variable/CMAKE_MAP_IMPORTED_CONFIG_CONFIG.html
    set(CMAKE_MAP_IMPORTED_CONFIG_DEBUG Release PARENT_SCOPE)
    set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release PARENT_SCOPE)
    set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL Release PARENT_SCOPE)
    message(STATUS "Conan: All CMake configs mapped to use Release libraries")
endfunction()
