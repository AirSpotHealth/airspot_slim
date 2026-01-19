/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Data Log Module - CO2 Data Logging to SPI Flash
 *
 * Stores CO2 measurements in circular buffer on external SPI flash.
 * Compatible with the original AirSpot firmware's history format.
 *
 * Optimized with metadata sector for O(1) startup recovery.
 */

#ifndef DATA_LOG_H
#define DATA_LOG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Flash geometry (matches SPI flash driver) */
#define FLASH_TOTAL_SIZE (2 * 1024 * 1024) /* 2 MB total */
#define FLASH_SECTOR_SIZE (4 * 1024)       /* 4 KB */
#define FLASH_PAGE_SIZE 256                /* 256 bytes */

/*
 * IMPORTANT: External flash partition layout
 *   0x00000 - 0x64000 (400KB): mcuboot_secondary - reserved for DFU
 *   0x64000 - 0x65000 (4KB):   metadata sector - for fast recovery
 *   0x65000 - 0x200000 (~1.6MB): history data
 *
 * Data log module uses metadata + history data areas only!
 */
#define FLASH_DFU_OFFSET 0x64000      /* DFU secondary ends here */
#define FLASH_METADATA_OFFSET 0x64000 /* Metadata sector start */
#define FLASH_HISTORY_OFFSET 0x65000  /* History data starts after metadata */
#define FLASH_SIZE                                                             \
  (FLASH_TOTAL_SIZE - FLASH_HISTORY_OFFSET) /* ~1.6MB for history */

/* Metadata sector constants */
#define METADATA_SECTOR_SIZE FLASH_SECTOR_SIZE /* 4KB */
#define METADATA_ENTRY_SIZE 32                 /* 32 bytes per entry */
#define METADATA_ENTRY_COUNT                                                   \
  (METADATA_SECTOR_SIZE / METADATA_ENTRY_SIZE) /* 128 entries */
#define METADATA_MAGIC 0xAA55AA55              /* Valid entry marker */

/* Derived constants for history data */
#define FLASH_SECTOR_COUNT (FLASH_SIZE / FLASH_SECTOR_SIZE)
#define FLASH_PAGE_COUNT (FLASH_SIZE / FLASH_PAGE_SIZE)
#define FLASH_HALF_PAGE_COUNT (FLASH_PAGE_COUNT * 2)
#define FLASH_PAGE_OF_SECTOR (FLASH_SECTOR_SIZE / FLASH_PAGE_SIZE)

/* Record storage */
#define RECORDS_PER_PAGE 32
#define HISTORY_SIZE 256 /* Page size for history records */

/* Timestamp validation thresholds */
#define SYNCED_TIME_THRESHOLD 0x10000000
#define INVALID_TIMESTAMP_F 0xFFFFFFFF
#define INVALID_TIMESTAMP_0 0x00000000

/* Get sector address (offset by FLASH_HISTORY_OFFSET to avoid DFU/metadata
 * areas) */
#define FLASH_ADDR_OF_SECTOR(sector)                                           \
  (FLASH_HISTORY_OFFSET + (sector) * FLASH_SECTOR_SIZE)

/* Get sector from address */
#define FLASH_SECTOR_OF_ADDR(addr)                                             \
  (((addr) - FLASH_HISTORY_OFFSET) / FLASH_SECTOR_SIZE)

/*
 * =============================================================================
 * Metadata Sector Structures (for O(1) startup recovery)
 * =============================================================================
 */

/**
 * @brief Metadata entry structure (32 bytes)
 *
 * Stored in append-only log pattern in metadata sector.
 * 128 entries fit in one 4KB sector before erase needed.
 * CRC32 ensures integrity after power loss.
 */
typedef struct __attribute__((packed)) {
  uint32_t magic;         /* METADATA_MAGIC = valid entry */
  uint32_t seq_num;       /* Incrementing sequence for finding latest */
  uint16_t cur_sector;    /* Current write position - sector */
  uint8_t cur_page;       /* Current write position - page */
  uint8_t cur_count;      /* Current write position - record count */
  uint16_t fst_sector;    /* First (oldest) record - sector */
  uint8_t fst_page;       /* First (oldest) record - page */
  uint8_t flags;          /* Reserved flags */
  uint32_t total_records; /* Total records written (for stats) */
  uint32_t timestamp;     /* When metadata was written */
  uint8_t reserved[4];    /* Padding */
  uint32_t crc32;         /* CRC32 of bytes 0-27 */
} metadata_entry_t;

/* Compile-time check that metadata entry is exactly 32 bytes */
_Static_assert(sizeof(metadata_entry_t) == METADATA_ENTRY_SIZE,
               "metadata_entry_t must be 32 bytes");

/**
 * @brief Record type enum for data log
 */
typedef enum {
  RECORD_TYPE_CO2 = 0,
  RECORD_TYPE_BATTERY_LOW,
  RECORD_TYPE_CALIBRATION,
  RECORD_TYPE_SENSOR_ERROR,
} record_type_t;

/**
 * @brief Fixed Length Record Structure (8 bytes)
 * Matches old firmware exactly for app compatibility.
 */
typedef struct __attribute__((packed)) {
  uint32_t timestamp; /* Unix timestamp (seconds since epoch) */
  uint16_t value;     /* CO2 value (big-endian) or error code */
  uint8_t type;       /* Record type (record_type_t) */
  uint8_t reserved;   /* Reserved for future use */
} record_t;

#define RECORD_SIZE (sizeof(record_t)) /* Should be 8 bytes */

/**
 * @brief Data Log Record Page (256 bytes)
 *
 * A wrapper for 32 records that can be accessed as an array
 * or as a raw buffer for flash read/write operations.
 */
typedef union {
  struct {
    record_t records[RECORDS_PER_PAGE];
  };
  uint8_t buf[HISTORY_SIZE];
} record_page_t;

/**
 * @brief Storage area descriptor for circular buffer
 *
 * Tracks current position in the circular buffer.
 */
typedef struct {
  uint16_t sector; /* Current sector (0 to FLASH_SECTOR_COUNT-1) */
  uint8_t page;    /* Page within sector (0 to FLASH_PAGE_OF_SECTOR-1) */
  uint8_t count;   /* Record count within page (0 to RECORDS_PER_PAGE-1) */
} store_area_t;

/* Check if two areas are equal */
#define STORE_AREA_EQUALS(ptr_a, ptr_b)                                        \
  ((ptr_a)->sector == (ptr_b)->sector && (ptr_a)->page == (ptr_b)->page)

/* Get flash address for a history area (includes offset to avoid DFU area) */
#define GET_HIS_ADDR(p_area)                                                   \
  (FLASH_HISTORY_OFFSET + (p_area)->sector * FLASH_SECTOR_SIZE +               \
   (p_area)->page * FLASH_PAGE_SIZE)

/* Endian swap macros */
#define SWAP_ENDIAN16(x) ((uint16_t)(((x) >> 8) | ((x) << 8)))
#define SWAP_ENDIAN32(x)                                                       \
  ((uint32_t)((((x) & 0x000000FF) << 24) | (((x) & 0x0000FF00) << 8) |         \
              (((x) & 0x00FF0000) >> 8) | (((x) & 0xFF000000) >> 24)))

/**
 * @brief Data log request structure (from BLE)
 */
typedef struct {
  uint16_t half_page_number;
} record_request_t;

/*
 * =============================================================================
 * Public API
 * =============================================================================
 */

/**
 * @brief Initialize data log module
 *
 * Initializes SPI flash and recovers existing data log records.
 *
 * @return 0 on success, negative errno on failure
 */
int data_log_init(void);

/**
 * @brief Store a CO2 measurement record
 *
 * @param timestamp Unix timestamp
 * @param co2_ppm CO2 value in PPM
 * @param type Record type (see record_type_t)
 * @return 0 on success, negative errno on failure
 */
int data_log_store_record(uint32_t timestamp, uint16_t co2_ppm,
                          record_type_t type);

/**
 * @brief Get the total number of stored data log records
 *
 * @return Number of records
 */
uint32_t data_log_get_count(void);

/**
 * @brief Get the current half-page number
 *
 * Used by the app to know where to start requesting history.
 *
 * @return Current half-page number (0 to FLASH_HALF_PAGE_COUNT-1)
 */
uint16_t data_log_get_current_half_page(void);

/**
 * @brief Request data log data for a specific half-page
 *
 * Sets up a request that will be processed by the data log task.
 * The data will be sent via protocol_send_history_data().
 *
 * @param half_page_number Half-page number (0 to FLASH_HALF_PAGE_COUNT-1)
 */
void data_log_request(uint16_t half_page_number);

/**
 * @brief Check if there's a pending data log request
 *
 * @return true if a request is pending
 */
bool data_log_has_pending_request(void);

/**
 * @brief Process pending data log request
 *
 * Reads the requested half-page from flash and returns it.
 * Should be called from the data log task when a request is pending.
 *
 * @param out_page Output buffer for the page data
 * @param out_count Output for record count in the half-page
 * @param out_half_page Output for the half-page number
 * @return 0 on success, negative errno on failure
 */
int data_log_process_request(record_page_t *out_page, uint8_t *out_count,
                             uint16_t *out_half_page);

/**
 * @brief Erase all data log data
 *
 * Erases all sectors used for data log storage.
 * This is a blocking operation that may take several seconds.
 *
 * @return 0 on success, negative errno on failure
 */
int data_log_erase_all(void);

/**
 * @brief Flag to trigger erase all from another context
 *
 * Set this flag to trigger an erase from the main loop.
 */
void data_log_request_erase_all(void);

/**
 * @brief Check if erase all is pending
 */
bool data_log_is_erase_pending(void);

/*
 * =============================================================================
 * Utility Functions
 * =============================================================================
 */

/**
 * @brief Check if a record is valid
 *
 * @param record The record to check
 * @return true if valid, false otherwise
 */
bool data_log_is_valid_record(const record_t *record);

/**
 * @brief Check if a page is valid
 *
 * A page is valid if the first record is not erased (0xFF) or empty (0x00).
 *
 * @param page The page to check
 * @return true if valid, false otherwise
 */
bool data_log_is_page_valid(const record_page_t *page);

/**
 * @brief Get the start timestamp from a page
 *
 * @param page The page
 * @return Start timestamp, or 0 if no valid records
 */
uint32_t data_log_get_start_timestamp(const record_page_t *page);

/**
 * @brief Get the end timestamp from a page
 *
 * @param page The page
 * @return End timestamp, or 0 if no valid records
 */
uint32_t data_log_get_end_timestamp(const record_page_t *page);

/**
 * @brief Get record count from a page
 *
 * @param page The page
 * @return Number of valid records (0 to RECORDS_PER_PAGE)
 */
uint8_t data_log_get_record_count(const record_page_t *page);

#ifdef __cplusplus
}
#endif

#endif /* DATA_LOG_H */
