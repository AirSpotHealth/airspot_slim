/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Data Log Module Implementation
 *
 * Stores CO2 measurements in circular buffer on external SPI flash.
 * Port of the original AirSpot firmware's history module to Zephyr.
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/spi_flash.h>
#include <services/data_log.h>

LOG_MODULE_REGISTER(data_log, CONFIG_AIRSPOT_LOG_LEVEL);

/* External watchdog feed function (defined in main.c) */
extern void watchdog_feed(void);

/*
 * =============================================================================
 * Private Variables
 * =============================================================================
 */

/* First (oldest) record position */
static store_area_t fst_store_area = {0};

/* Current (newest) record position */
static store_area_t cur_store_area = {0};

/* Sector data buffer for read-modify-write operations */
static uint8_t sector_data_buffer[FLASH_SECTOR_SIZE];

/* Current page buffer */
static record_page_t rec_page = {0};

/* Current record being built */
static record_t cur_record = {0};

/* Pending data log request */
static record_request_t pending_request = {0};
static volatile bool request_pending = false;

/* Erase all flag */
static volatile bool erase_all_pending = false;

/* Initialization flag */
static bool initialized = false;

/* Metadata tracking */
static uint32_t metadata_seq_num = 0;    /* Current sequence number */
static uint32_t total_records_count = 0; /* Total records written (for stats) */
static bool metadata_dirty = false; /* True when metadata needs to be written */

/* Mutex for thread safety */
static K_MUTEX_DEFINE(data_log_mutex);

/* Semaphore to signal pending requests (for faster main loop wake-up) */
static K_SEM_DEFINE(data_log_request_sem, 0, 1);

/*
 * =============================================================================
 * CRC32 Implementation (for metadata integrity)
 * =============================================================================
 */

/* CRC32 lookup table (polynomial 0xEDB88320) */
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
    0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
    0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
    0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
    0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
    0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
    0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
    0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
    0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
    0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7a9b, 0x5005713c, 0x270241aa,
    0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
    0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
    0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
    0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
    0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
    0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
    0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
    0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
    0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
    0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
    0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd706b3,
    0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d};

static uint32_t crc32_compute(const uint8_t *data, size_t len) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; i++) {
    crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFF;
}

/*
 * =============================================================================
 * Metadata Sector Functions (for O(1) startup recovery)
 * =============================================================================
 */

/**
 * @brief Validate a metadata entry using magic and CRC32
 */
static bool metadata_validate_entry(const metadata_entry_t *entry) {
  if (entry->magic != METADATA_MAGIC) {
    return false;
  }

  /* CRC is computed over bytes 0-27 (everything except CRC field) */
  uint32_t computed_crc = crc32_compute(
      (const uint8_t *)entry, sizeof(metadata_entry_t) - sizeof(uint32_t));
  return (computed_crc == entry->crc32);
}

/**
 * @brief Read latest valid metadata entry from metadata sector
 *
 * Scans the metadata sector (append-only log) to find the latest
 * valid entry by sequence number.
 *
 * @param entry Output buffer for the metadata entry
 * @return 0 if valid entry found, -ENOENT if no valid entries
 */
static int metadata_read_latest(metadata_entry_t *entry) {
  metadata_entry_t temp;
  uint32_t highest_seq = 0;
  bool found = false;

  /* Scan all entries in metadata sector */
  for (int i = 0; i < METADATA_ENTRY_COUNT; i++) {
    uint32_t addr = FLASH_METADATA_OFFSET + (i * METADATA_ENTRY_SIZE);

    int ret = spi_flash_read(addr, (uint8_t *)&temp, sizeof(temp));
    if (ret != 0) {
      continue;
    }

    /* Check if entry is valid */
    if (!metadata_validate_entry(&temp)) {
      continue;
    }

    /* Track highest sequence number */
    if (temp.seq_num > highest_seq || !found) {
      highest_seq = temp.seq_num;
      *entry = temp;
      found = true;
    }
  }

  if (!found) {
    return -ENOENT;
  }

  LOG_INF("Metadata: found entry seq=%u at cur[%u,%u,%u] fst[%u,%u]",
          entry->seq_num, entry->cur_sector, entry->cur_page, entry->cur_count,
          entry->fst_sector, entry->fst_page);

  return 0;
}

/**
 * @brief Find next available slot in metadata sector
 *
 * Returns the offset of the first empty (0xFF) entry.
 * Returns -1 if sector is full.
 */
static int metadata_find_next_slot(void) {
  metadata_entry_t temp;

  for (int i = 0; i < METADATA_ENTRY_COUNT; i++) {
    uint32_t addr = FLASH_METADATA_OFFSET + (i * METADATA_ENTRY_SIZE);

    int ret = spi_flash_read(addr, (uint8_t *)&temp, sizeof(temp));
    if (ret != 0) {
      continue;
    }

    /* Check if slot is empty (all 0xFF = erased flash) */
    if (temp.magic == 0xFFFFFFFF) {
      return i;
    }
  }

  return -1; /* Sector full */
}

/**
 * @brief Write current state to metadata sector
 *
 * Appends a new entry to the metadata sector. If sector is full,
 * erases it first before writing.
 *
 * @param timestamp Current timestamp (for stats)
 * @return 0 on success, negative errno on failure
 */
static int metadata_write(uint32_t timestamp) {
  metadata_entry_t entry;
  int slot = metadata_find_next_slot();

  /* If sector is full, erase and start over */
  if (slot < 0) {
    LOG_INF("Metadata sector full, erasing...");
    int ret = spi_flash_erase_sector(FLASH_METADATA_OFFSET);
    if (ret != 0) {
      LOG_ERR("Failed to erase metadata sector: %d", ret);
      return ret;
    }
    k_msleep(100); /* Wait for erase */
    slot = 0;
    metadata_seq_num = 0; /* Reset sequence after erase */
  }

  /* Build new entry */
  memset(&entry, 0, sizeof(entry));
  entry.magic = METADATA_MAGIC;
  entry.seq_num = ++metadata_seq_num;
  entry.cur_sector = cur_store_area.sector;
  entry.cur_page = cur_store_area.page;
  entry.cur_count = cur_store_area.count;
  entry.fst_sector = fst_store_area.sector;
  entry.fst_page = fst_store_area.page;
  entry.flags = 0;
  entry.total_records = total_records_count;
  entry.timestamp = timestamp;

  /* Compute CRC over bytes 0-27 */
  entry.crc32 =
      crc32_compute((const uint8_t *)&entry, sizeof(entry) - sizeof(uint32_t));

  /* Write entry */
  uint32_t addr = FLASH_METADATA_OFFSET + (slot * METADATA_ENTRY_SIZE);
  int ret = spi_flash_write(addr, (const uint8_t *)&entry, sizeof(entry));
  if (ret != 0) {
    LOG_ERR("Failed to write metadata: %d", ret);
    return ret;
  }

  metadata_dirty = false;
  LOG_DBG("Metadata written at slot %d, seq=%u", slot, entry.seq_num);

  return 0;
}

/*
 * =============================================================================
 * Helper Functions
 * =============================================================================
 */

/**
 * @brief Get a storage area with an offset
 *
 * Handles wrap-around in the circular buffer.
 */
static store_area_t get_store_area_with_offset(const store_area_t *cur,
                                               int16_t offset) {
  store_area_t result = *cur;

  /* Calculate linear position */
  int32_t linear_position =
      (result.sector * FLASH_PAGE_OF_SECTOR) + result.page + offset;

  /* Handle wrap-around */
  linear_position = (linear_position % FLASH_PAGE_COUNT + FLASH_PAGE_COUNT) %
                    FLASH_PAGE_COUNT;

  /* Calculate sector and page */
  result.sector = linear_position / FLASH_PAGE_OF_SECTOR;
  result.page = linear_position % FLASH_PAGE_OF_SECTOR;
  result.count = 0;

  return result;
}

/**
 * @brief Get the next storage area
 */
static store_area_t get_next_store_area(const store_area_t *cur) {
  return get_store_area_with_offset(cur, 1);
}

/**
 * @brief Read data from flash with chunking
 *
 * Reads data in chunks to avoid SPI buffer limitations.
 */
static int flash_read_chunked(uint32_t addr, uint8_t *buf, uint16_t len) {
  const uint16_t chunk_size = 128;
  uint16_t offset = 0;
  int ret;

  while (len > 0) {
    uint16_t bytes_to_read = (len > chunk_size) ? chunk_size : len;

    ret = spi_flash_read(addr + offset, buf + offset, bytes_to_read);
    if (ret != 0) {
      LOG_ERR("Flash read failed at 0x%08X: %d", addr + offset, ret);
      return ret;
    }

    offset += bytes_to_read;
    len -= bytes_to_read;
  }

  return 0;
}

/**
 * @brief Write data to flash with chunking
 *
 * Writes data in page-size chunks.
 */
static int flash_write_chunked(uint32_t addr, const uint8_t *buf,
                               uint16_t len) {
  const uint16_t chunk_size = SPI_FLASH_PAGE_SIZE;
  uint16_t offset = 0;
  int ret;

  while (len > 0) {
    uint16_t bytes_to_write = (len > chunk_size) ? chunk_size : len;

    ret = spi_flash_write(addr + offset, buf + offset, bytes_to_write);
    if (ret != 0) {
      LOG_ERR("Flash write failed at 0x%08X: %d", addr + offset, ret);
      return ret;
    }

    offset += bytes_to_write;
    len -= bytes_to_write;
    k_msleep(5); /* Allow flash time to complete */
  }

  return 0;
}

/**
 * @brief Format (erase and rewrite) a specific page in a sector
 *
 * Reads entire sector, erases it, then writes back all pages except
 * the specified one (which becomes blank for new data).
 */
static void format_store_area(store_area_t area) {
  store_area_t temp;
  uint32_t addr;
  uint8_t *buffer_ptr;

  LOG_INF("Formatting sector %d page %d", area.sector, area.page);

  /* Read entire sector page by page */
  for (uint8_t i = 0; i < FLASH_PAGE_OF_SECTOR; i++) {
    temp.sector = area.sector;
    temp.page = i;
    addr = GET_HIS_ADDR(&temp);
    buffer_ptr = sector_data_buffer + i * FLASH_PAGE_SIZE;
    flash_read_chunked(addr, buffer_ptr, HISTORY_SIZE);
    k_msleep(1);
  }

  /* Erase the sector */
  spi_flash_erase_sector(FLASH_ADDR_OF_SECTOR(area.sector));
  k_msleep(200); /* Wait for erase to complete */

  /* Write back all pages except the one being formatted */
  for (uint8_t i = 0; i < FLASH_PAGE_OF_SECTOR; i++) {
    if (i == area.page) {
      continue; /* Skip the page being formatted */
    }

    temp.page = i;
    addr = GET_HIS_ADDR(&temp);
    buffer_ptr = sector_data_buffer + i * FLASH_PAGE_SIZE;
    flash_write_chunked(addr, buffer_ptr, HISTORY_SIZE);
    k_msleep(5);
  }
}

/**
 * @brief Full scan recovery (fallback when metadata is missing/corrupted)
 *
 * Scans all sectors to find the oldest and newest records.
 * This is the original O(N) recovery path.
 */
static int data_log_recover_full_scan(void) {
  store_area_t area;
  record_page_t page;
  uint32_t fst_timestamp = UINT32_MAX;
  uint32_t lst_timestamp = 0;
  uint16_t found = 0;

  LOG_WRN("Full scan recovery (slow path)...");

  /* Initialize to invalid */
  fst_store_area.sector = 0xFFFF;
  fst_store_area.page = 0xFF;
  cur_store_area.sector = 0xFFFF;
  cur_store_area.page = 0xFF;

  /* Traverse all pages in flash */
  for (uint16_t sector = 0; sector < FLASH_SECTOR_COUNT; sector++) {
    for (uint8_t pg = 0; pg < FLASH_PAGE_OF_SECTOR; pg++) {
      area.sector = sector;
      area.page = pg;

      /* Read page */
      int ret = flash_read_chunked(GET_HIS_ADDR(&area), page.buf, HISTORY_SIZE);
      if (ret != 0) {
        continue;
      }

      /* Check if page is valid */
      if (!data_log_is_page_valid(&page)) {
        continue;
      }

      uint32_t start_ts = data_log_get_start_timestamp(&page);
      found++;
      total_records_count += data_log_get_record_count(&page);

      /* Skip records with unsynced timestamps */
      if (start_ts < SYNCED_TIME_THRESHOLD) {
        continue;
      }

      /* Update first (oldest) position */
      if (start_ts < fst_timestamp) {
        fst_timestamp = start_ts;
        fst_store_area.sector = sector;
        fst_store_area.page = pg;
      }

      /* Update current (newest) position */
      if (start_ts > lst_timestamp) {
        lst_timestamp = start_ts;
        cur_store_area.sector = sector;
        cur_store_area.page = pg;
        cur_store_area.count = data_log_get_record_count(&page);
      }
    }

    /* A blank/new board has no metadata and scans the entire 2 MiB flash.
     * Keep the watchdog alive only as each bounded sector scan progresses,
     * and let lower-priority initialization work run between sectors.
     */
    watchdog_feed();
    k_msleep(1);
  }

  /* Handle results */
  if (found != 0) {
    /* If current page is full, move to next */
    if (cur_store_area.count >= RECORDS_PER_PAGE) {
      cur_store_area = get_next_store_area(&cur_store_area);
    }
  } else {
    /* No existing records - start at beginning */
    fst_store_area.sector = 0;
    fst_store_area.page = 0;
    fst_store_area.count = 0;
    cur_store_area.sector = 0;
    cur_store_area.page = 0;
    cur_store_area.count = 0;
  }

  LOG_INF("Full scan recovered %d pages", found);

  return 0;
}

/**
 * @brief Recover data log state from flash on startup
 *
 * Uses fast metadata path (O(1)) if available, falls back to
 * full scan (O(N)) if metadata is missing or corrupted.
 */
static int data_log_recover(void) {
  metadata_entry_t meta = {0};

  LOG_INF("Recovering data log...");

  /* Try fast path: read metadata */
  int ret = metadata_read_latest(&meta);
  if (ret == 0) {
    /* Metadata found - use it! */
    LOG_INF("Fast recovery from metadata (O(1))");

    cur_store_area.sector = meta.cur_sector;
    cur_store_area.page = meta.cur_page;
    cur_store_area.count = meta.cur_count;
    fst_store_area.sector = meta.fst_sector;
    fst_store_area.page = meta.fst_page;
    fst_store_area.count = 0;

    metadata_seq_num = meta.seq_num;
    total_records_count = meta.total_records;

    /* Metadata is only checkpointed when a page fills (every 32 records), so
     * cur_count can lag behind records already written into the current page
     * since the last checkpoint. Resuming at the stale count would overwrite
     * those records on this NOR flash (writes only clear bits), corrupting
     * data. Re-scan the current page to recover the true write position. */
    {
      static record_page_t cur_page; /* 256B - avoid large stack frame */
      if (flash_read_chunked(GET_HIS_ADDR(&cur_store_area), cur_page.buf,
                             HISTORY_SIZE) == 0) {
        cur_store_area.count = data_log_get_record_count(&cur_page);
        if (cur_store_area.count >= RECORDS_PER_PAGE) {
          /* Current page is actually full - advance to the next page. */
          cur_store_area = get_next_store_area(&cur_store_area);
          if (flash_read_chunked(GET_HIS_ADDR(&cur_store_area), cur_page.buf,
                                 HISTORY_SIZE) == 0 &&
              cur_page.records[0].timestamp != INVALID_TIMESTAMP_F) {
            /* Stale data in the new page - erase before writing into it. */
            format_store_area(cur_store_area);
          }
        }
      }
    }

    LOG_INF("Current: sector %d, page %d, count %d", cur_store_area.sector,
            cur_store_area.page, cur_store_area.count);
    LOG_INF("First: sector %d, page %d", fst_store_area.sector,
            fst_store_area.page);
    LOG_INF("Total records: %u", total_records_count);

    return 0;
  }

  /* Fallback: full scan (first boot or corrupted metadata) */
  LOG_WRN("No valid metadata, performing full scan...");
  ret = data_log_recover_full_scan();
  if (ret != 0) {
    return ret;
  }

  /* Write metadata so next boot is fast */
  LOG_INF("Writing initial metadata...");
  metadata_write(0);

  LOG_INF("First: sector %d, page %d", fst_store_area.sector,
          fst_store_area.page);
  LOG_INF("Current: sector %d, page %d, count %d", cur_store_area.sector,
          cur_store_area.page, cur_store_area.count);

  return 0;
}

/**
 * @brief Convert half-page number to store_area
 */
static void set_store_area_from_half_page(store_area_t *area,
                                          uint16_t half_page_number) {
  uint16_t full_page_number = half_page_number / 2;

  area->sector = full_page_number / FLASH_PAGE_OF_SECTOR;
  area->page = full_page_number % FLASH_PAGE_OF_SECTOR;
  area->count = 0;
}

/*
 * =============================================================================
 * Public API Implementation
 * =============================================================================
 */

#if defined(CONFIG_AIRSPOT_FORENSIC_HISTORY)
static void dump_history_readonly(void) {
  uint8_t small[32], large[128];
  LOG_INF("FORENSIC: external flash writes/erases disabled");
  for (uint32_t addr = FLASH_METADATA_OFFSET;
       addr < FLASH_HISTORY_OFFSET + FLASH_SECTOR_SIZE; addr += sizeof(small)) {
    memset(small, 0xa5, sizeof(small));
    int ret = spi_flash_read(addr, small, sizeof(small));
    char hex[65];
    const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(small); i++) {
      hex[i * 2] = digits[small[i] >> 4];
      hex[i * 2 + 1] = digits[small[i] & 15];
    }
    hex[64] = 0;
    LOG_INF("RAWFLASH %06x rc=%d %s", addr, ret, hex);
    if (!(addr % 128)) {
      memset(large, 0x5a, sizeof(large));
      int rc = spi_flash_read(addr, large, sizeof(large));
      LOG_INF("RAWCOMPARE %06x rc=%d same_prefix=%d first=%02x%02x%02x%02x",
              addr, rc, !memcmp(small, large, sizeof(small)),
              large[0], large[1], large[2], large[3]);
    }
    watchdog_feed();
    k_msleep(25); /* Allow RTT host to drain without dropping the capture. */
  }
  LOG_INF("FORENSIC: raw capture complete");
  spi_flash_forensic_wake_probe();
}
#endif

int data_log_init(void) {
  if (initialized) {
    return 0;
  }

  /* Initialize SPI flash */
  int ret = spi_flash_init();
  if (ret != 0) {
    LOG_ERR("Failed to init SPI flash: %d", ret);
    return ret;
  }

  /* Wake flash from power-down if needed */
  spi_flash_power_up();
  k_msleep(1);

#if defined(CONFIG_AIRSPOT_FORENSIC_HISTORY)
  dump_history_readonly();
#endif

  /* Recover existing data log */
  ret = data_log_recover();
  if (ret != 0) {
    LOG_ERR("Failed to recover data log: %d", ret);
    return ret;
  }

  initialized = true;

  /* Put flash into deep power-down for power savings */
  spi_flash_power_down();
  LOG_INF("Data log module initialized (flash in sleep mode)");
  return 0;
}

int data_log_store_record(uint32_t timestamp, uint16_t co2_ppm,
                          record_type_t type) {
  if (!initialized) {
    return -ENODEV;
  }

  k_mutex_lock(&data_log_mutex, K_FOREVER);

  /* Wake flash from deep sleep */
  spi_flash_power_up();
  k_usleep(50); /* tRES1 = 30µs max */

  /* Populate current record */
  cur_record.timestamp = timestamp;
  cur_record.value = SWAP_ENDIAN16(co2_ppm); /* Store big-endian */
  cur_record.type = (uint8_t)type;
  cur_record.reserved = 0;

  LOG_DBG("Storing record: ts=%u, co2=%u, type=%d", timestamp, co2_ppm, type);

  /* Add record to page buffer */
  rec_page.records[cur_store_area.count] = cur_record;

  /* Calculate write address */
  uint32_t write_addr =
      GET_HIS_ADDR(&cur_store_area) + cur_store_area.count * RECORD_SIZE;

  /* Write single record to flash */
  int ret = spi_flash_write(write_addr, (uint8_t *)&cur_record, RECORD_SIZE);
  if (ret != 0) {
    LOG_ERR("Failed to write record: %d", ret);
    k_mutex_unlock(&data_log_mutex);
    return ret;
  }

  LOG_DBG("Written at sector %d, page %d, index %d", cur_store_area.sector,
          cur_store_area.page, cur_store_area.count);

  /* Increment record count */
  cur_store_area.count++;
  total_records_count++;

  /* Check if page is full */
  if (cur_store_area.count >= RECORDS_PER_PAGE) {
    /* Move to next page */
    cur_store_area = get_next_store_area(&cur_store_area);
    memset(&rec_page, 0, sizeof(rec_page));
    memset(&cur_record, 0, sizeof(cur_record));

    LOG_INF("Moving to next page: sector %d, page %d", cur_store_area.sector,
            cur_store_area.page);

    /* Check if new area already has data - if so, erase it */
    record_page_t check_page;
    flash_read_chunked(GET_HIS_ADDR(&cur_store_area), check_page.buf,
                       HISTORY_SIZE);

    if (check_page.records[0].timestamp != INVALID_TIMESTAMP_F) {
      /* Page has old data - format it */
      format_store_area(cur_store_area);
    }

    /* Update first pointer if we've wrapped around */
    if (STORE_AREA_EQUALS(&fst_store_area, &cur_store_area)) {
      fst_store_area = get_next_store_area(&cur_store_area);
    }

    /* Save metadata when page fills (every 32 records) */
    metadata_write(timestamp);
  }

  /* Put flash back to sleep */
  spi_flash_power_down();

  k_mutex_unlock(&data_log_mutex);
  return 0;
}

uint32_t data_log_get_count(void) {
  if (!initialized) {
    return 0;
  }

  k_mutex_lock(&data_log_mutex, K_FOREVER);

  uint32_t fst_addr = GET_HIS_ADDR(&fst_store_area);
  uint32_t cur_addr = GET_HIS_ADDR(&cur_store_area);

  uint32_t count;
  if (fst_addr == cur_addr) {
    count = cur_store_area.count; /* Same page */
  } else if (fst_addr < cur_addr) {
    count = ((cur_addr - fst_addr) / FLASH_PAGE_SIZE) * RECORDS_PER_PAGE +
            cur_store_area.count;
  } else {
    /* Wrapped around */
    count = ((FLASH_ADDR_OF_SECTOR(FLASH_SECTOR_COUNT) - fst_addr) /
                 FLASH_PAGE_SIZE +
             cur_addr / FLASH_PAGE_SIZE) *
                RECORDS_PER_PAGE +
            cur_store_area.count;
  }

  k_mutex_unlock(&data_log_mutex);
  return count;
}

uint16_t data_log_get_current_half_page(void) {
  if (!initialized) {
    return 0;
  }

  k_mutex_lock(&data_log_mutex, K_FOREVER);

  uint16_t result =
      (cur_store_area.sector * FLASH_PAGE_OF_SECTOR + cur_store_area.page) * 2 +
      (cur_store_area.count >= RECORDS_PER_PAGE / 2 ? 1 : 0);

  k_mutex_unlock(&data_log_mutex);
  return result;
}

void data_log_request(uint16_t half_page_number) {
  LOG_INF("Data log request: half-page %d", half_page_number);
  pending_request.half_page_number = half_page_number;
  request_pending = true;
  /* Signal semaphore to wake up main loop immediately */
  k_sem_give(&data_log_request_sem);
}

bool data_log_has_pending_request(void) { return request_pending; }

int data_log_process_request(record_page_t *out_page, uint8_t *out_count,
                             uint16_t *out_half_page) {
  if (!request_pending) {
    return -EINVAL;
  }

  if (!initialized) {
    return -ENODEV;
  }

  /* Validate half-page number */
  if (pending_request.half_page_number >= FLASH_HALF_PAGE_COUNT) {
    LOG_WRN("Invalid half-page number: %d", pending_request.half_page_number);
    request_pending = false;
    return -EINVAL;
  }

  k_mutex_lock(&data_log_mutex, K_FOREVER);

  /* Wake flash from deep sleep */
  spi_flash_power_up();
  k_usleep(50);

  store_area_t tx_area;
  set_store_area_from_half_page(&tx_area, pending_request.half_page_number);

  uint32_t read_addr = GET_HIS_ADDR(&tx_area);

  /* If odd half-page, read second half (offset by 128 bytes) */
  if (pending_request.half_page_number % 2) {
    read_addr += HISTORY_SIZE / 2;
  }

  LOG_DBG("Reading half-page at addr 0x%08X", read_addr);

  /* Read half page (128 bytes = 16 records) */
  int ret = flash_read_chunked(read_addr, out_page->buf, HISTORY_SIZE / 2);
  if (ret != 0) {
    LOG_ERR("Failed to read data log: %d", ret);
    k_mutex_unlock(&data_log_mutex);
    request_pending = false;
    return ret;
  }

  /* Count valid records in this half-page (max 16) */
  *out_count = 0;
  for (int i = 0; i < RECORDS_PER_PAGE / 2; i++) {
    if (data_log_is_valid_record(&out_page->records[i])) {
      (*out_count)++;
    } else {
      break;
    }
  }

  *out_half_page = pending_request.half_page_number;

  /* Put flash back to sleep */
  spi_flash_power_down();

  k_mutex_unlock(&data_log_mutex);
  request_pending = false;

  return 0;
}

void data_log_request_erase_all(void) {
  LOG_WRN("Erase all data log requested");
  erase_all_pending = true;
  /* Signal semaphore to wake up main loop immediately */
  k_sem_give(&data_log_request_sem);
}

bool data_log_is_erase_pending(void) { return erase_all_pending; }

int data_log_wait_for_request(k_timeout_t timeout) {
  return k_sem_take(&data_log_request_sem, timeout);
}

int data_log_erase_all(void) {
  if (!initialized) {
    return -ENODEV;
  }

  LOG_WRN("Erasing all data log...");

  k_mutex_lock(&data_log_mutex, K_FOREVER);

  /* Wake flash from deep sleep */
  spi_flash_power_up();
  k_usleep(50);

  /* Erase all sectors */
  for (uint16_t sector = 0; sector < FLASH_SECTOR_COUNT; sector++) {
    int ret = spi_flash_erase_sector(FLASH_ADDR_OF_SECTOR(sector));
    if (ret != 0) {
      LOG_ERR("Failed to erase sector %d: %d", sector, ret);
      /* Continue anyway */
    }

    /* Feed watchdog and yield periodically during long erase
     * Each sector erase takes ~50-100ms, and we have many sectors.
     * With 5s WDT timeout, feed every 32 sectors (~3s max)
     */
    if (sector % 32 == 0) {
      watchdog_feed();
      k_yield();
    }
  }

  /* Reset state */
  fst_store_area.sector = 0;
  fst_store_area.page = 0;
  fst_store_area.count = 0;
  cur_store_area.sector = 0;
  cur_store_area.page = 0;
  cur_store_area.count = 0;
  memset(&rec_page, 0, sizeof(rec_page));
  memset(&cur_record, 0, sizeof(cur_record));

  erase_all_pending = false;

  /* Put flash back to sleep */
  spi_flash_power_down();

  k_mutex_unlock(&data_log_mutex);

  LOG_INF("All data log erased");
  return 0;
}

/*
 * =============================================================================
 * Utility Functions
 * =============================================================================
 */

bool data_log_is_valid_record(const record_t *record) {
  if (!record) {
    return false;
  }
  return (record->timestamp != INVALID_TIMESTAMP_F &&
          record->timestamp != INVALID_TIMESTAMP_0);
}

bool data_log_is_page_valid(const record_page_t *page) {
  if (!page) {
    return false;
  }
  return data_log_is_valid_record(&page->records[0]);
}

uint32_t data_log_get_start_timestamp(const record_page_t *page) {
  if (!page) {
    return 0;
  }

  uint32_t first_unsynced = 0;

  for (int i = 0; i < RECORDS_PER_PAGE; i++) {
    const record_t *record = &page->records[i];

    if (!data_log_is_valid_record(record)) {
      continue;
    }

    /* Prefer synced timestamp */
    if (record->timestamp > SYNCED_TIME_THRESHOLD) {
      return record->timestamp;
    }

    /* Remember first unsynced as fallback */
    if (first_unsynced == 0) {
      first_unsynced = record->timestamp;
    }
  }

  return first_unsynced;
}

uint32_t data_log_get_end_timestamp(const record_page_t *page) {
  if (!page) {
    return 0;
  }

  uint32_t first_unsynced = 0;

  for (int i = RECORDS_PER_PAGE - 1; i >= 0; i--) {
    const record_t *record = &page->records[i];

    if (!data_log_is_valid_record(record)) {
      continue;
    }

    /* Prefer synced timestamp */
    if (record->timestamp > SYNCED_TIME_THRESHOLD) {
      return record->timestamp;
    }

    /* Remember first unsynced as fallback */
    if (first_unsynced == 0) {
      first_unsynced = record->timestamp;
    }
  }

  return first_unsynced;
}

uint8_t data_log_get_record_count(const record_page_t *page) {
  if (!page) {
    return 0;
  }

  uint8_t count = 0;
  for (int i = 0; i < RECORDS_PER_PAGE; i++) {
    if (!data_log_is_valid_record(&page->records[i])) {
      break;
    }
    count++;
  }
  return count;
}
