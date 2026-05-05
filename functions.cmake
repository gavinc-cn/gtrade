function(get_glibc_version VAR)
    execute_process(COMMAND getconf GNU_LIBC_VERSION
            OUTPUT_VARIABLE GLIBC_VERSION_OUTPUT
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE)

    if(GLIBC_VERSION_OUTPUT)
        set(${VAR} "${GLIBC_VERSION_OUTPUT}" PARENT_SCOPE)
    else()
        message(WARNING "Failed to determine glibc version.")
    endif()
endfunction()

function(get_git_commit OUT_VAR)
    execute_process(
            COMMAND git rev-parse HEAD
            OUTPUT_VARIABLE GIT_COMMIT_HASH
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(SUBSTRING "${GIT_COMMIT_HASH}" 0 8 GIT_COMMIT_HASH)
    if(GIT_COMMIT_HASH)
        set(${OUT_VAR} ${GIT_COMMIT_HASH} PARENT_SCOPE)
    else()
        set(${OUT_VAR} "0" PARENT_SCOPE)
    endif()
endfunction()

function(get_current_date OUT_VAR)
    if(WIN32)
        execute_process(
                COMMAND PowerShell -Command "(Get-Date).ToString('yyyyMMdd')"
                OUTPUT_VARIABLE CURRENT_DATE
                OUTPUT_STRIP_TRAILING_WHITESPACE
        )
    elseif(UNIX)
        execute_process(
                COMMAND date +%Y%m%d
                OUTPUT_VARIABLE CURRENT_DATE
                OUTPUT_STRIP_TRAILING_WHITESPACE
        )
    endif()
    if(CURRENT_DATE)
        set(${OUT_VAR} ${CURRENT_DATE} PARENT_SCOPE)
    else()
        message(WARNING "Failed to get date")
        set(${OUT_VAR} 19700101 PARENT_SCOPE)
    endif()
endfunction()

function(create_version OUT_VAR)
    get_git_commit(GIT_COMMIT_HASH)
    get_current_date(CURRENT_DATE)
    set(${OUT_VAR} ${CURRENT_DATE}.${GIT_COMMIT_HASH} PARENT_SCOPE)
endfunction()