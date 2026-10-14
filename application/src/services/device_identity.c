#include <errno.h>
#include <hal/nrf_ficr.h>
#include <services/device_identity.h>

int device_identity_get(uint8_t serial[6])
{
  uint32_t low = nrf_ficr_deviceaddr_get(NRF_FICR, 0);
  uint16_t high = (uint16_t)nrf_ficr_deviceaddr_get(NRF_FICR, 1);
  if ((!low && !high) || (low == UINT32_MAX && high == UINT16_MAX)) return -ENODATA;
  serial[0] = high >> 8;
  serial[1] = high;
  for (unsigned i = 0; i < 4; ++i) serial[i + 2] = low >> (24 - 8 * i);
  return 0;
}
