# Explicitly linked only by the separate bench-test build.
zephyr_library()
zephyr_library_include_directories(../../../application/include)
zephyr_library_sources(sensor_fault.c)
zephyr_link_libraries(-Wl,--wrap=stcc4_read)
