cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(incremental_app)

target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE src/drivers/button_driver.c)
target_sources(app PRIVATE src/drivers/led_driver.c)
target_sources(app PRIVATE src/drivers/i2c_bus.c)
target_sources(app PRIVATE src/drivers/sht40.c)
target_sources(app PRIVATE src/drivers/lps22hh.c)
target_sources(app PRIVATE src/drivers/stcc4.c)
target_sources(app PRIVATE src/drivers/battery.c)
target_sources(app PRIVATE src/drivers/qi_charger.c)

target_include_directories(app PRIVATE include)