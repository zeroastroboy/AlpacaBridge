# WeeWX vendor support
# WeeWX data is retrieved via HTTP JSON feed

if(ALPACACORE_ENABLE_WEEWX)
    message(STATUS "Building WeeWX observing conditions driver support")

    set(WEEWX_SOURCES
        weewx_observingconditions_driver.cpp
    )

    add_library(alpacacore_weewx STATIC ${WEEWX_SOURCES})

    target_include_directories(alpacacore_weewx PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )

    if(WIN32)
        target_link_libraries(alpacacore_weewx PRIVATE winhttp)
        message(STATUS "WeeWX driver: WinHTTP support enabled")
    else()
        find_package(CURL QUIET)
        if(NOT CURL_FOUND)
            if(ALPACACORE_ENABLE_ALL_VENDORS AND NOT ALPACACORE_REQUIRE_WEEWX)
                message(WARNING "WeeWX requested via ALPACACORE_ENABLE_ALL_VENDORS, but libcurl was not found. Skipping WeeWX vendor.")
                return()
            endif()

            message(FATAL_ERROR
                "ALPACACORE_ENABLE_WEEWX=ON requires libcurl, but CMake could not find CURL.\n"
                "Install libcurl development files or configure with -DALPACACORE_ENABLE_WEEWX=OFF.")
        endif()

        target_link_libraries(alpacacore_weewx PRIVATE CURL::libcurl)
        message(STATUS "WeeWX driver: libcurl HTTP JSON feed support enabled")
    endif()

endif()
