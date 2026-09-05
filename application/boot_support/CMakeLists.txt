zephyr_library()
zephyr_library_sources(grtc_handoff.c)
zephyr_link_libraries(-Wl,--wrap=sys_clock_disable)
