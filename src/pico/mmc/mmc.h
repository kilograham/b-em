#ifndef _MMC_H
#define _MMC_H

#include <inttypes.h>

void mmc_init();
uint8_t mmc_read(uint16_t addr);
void mmc_write(uint16_t addr, uint8_t val);

#endif
