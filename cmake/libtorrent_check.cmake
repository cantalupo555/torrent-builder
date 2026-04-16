if(LIBTORRENT_VERSION VERSION_LESS 2.0.10)
    message(FATAL_ERROR
        "libtorrent-rasterbar >= 2.0.10 is required.\n"
        "Detected version: ${LIBTORRENT_VERSION}.\n"
        "See README.md for build instructions."
    )
endif()
