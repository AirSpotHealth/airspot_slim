cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(airspot_slim)

# =============================================================================
# Main Application
# =============================================================================
target_sources(app PRIVATE src/main.c)

# =============================================================================
# Drivers
# =============================================================================
target_sources(app PRIVATE src/drivers/button_driver.c)
target_sources(app PRIVATE src/drivers/led_driver.c)
target_sources(app PRIVATE src/drivers/i2c_bus.c)
target_sources(app PRIVATE src/drivers/sht40.c)
target_sources(app PRIVATE src/drivers/lps22hh.c)
target_sources(app PRIVATE src/drivers/stcc4.c)
target_sources(app PRIVATE src/drivers/battery.c)
target_sources(app PRIVATE src/drivers/qi_charger.c)
target_sources(app PRIVATE src/drivers/spi_flash.c)

# =============================================================================
# Services
# =============================================================================
target_sources(app PRIVATE src/services/ble_service.c)
target_sources(app PRIVATE src/services/airspot_nus.c)
target_sources(app PRIVATE src/services/protocol.c)
target_sources(app PRIVATE src/services/sensor_manager.c)
target_sources(app PRIVATE src/services/user_settings.c)
target_sources(app PRIVATE src/services/data_log.c)
target_sources(app PRIVATE src/services/ble_mode_service.c)

# Mesh sensor (only when BT_MESH is enabled)
if(CONFIG_BT_MESH)
  target_sources(app PRIVATE src/services/mesh_sensor.c)
endif()

# =============================================================================
# Include Directories
# =============================================================================
target_include_directories(app PRIVATE include)
