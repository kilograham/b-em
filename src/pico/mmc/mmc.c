#include "mmc.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#define PIN_CLK  PICO_SD_CLK_PIN
#define PIN_MOSI PICO_SD_CMD_PIN
#define PIN_MISO PICO_SD_DAT0_PIN
#define PIN_CS   22

static volatile uint8_t port = 0;

void mmc_init() {
   gpio_init(PIN_CS);
   gpio_put(PIN_CS, 0);  // Always selected
   gpio_set_dir(PIN_CS, GPIO_OUT);

   gpio_init(PIN_CLK);
   gpio_put(PIN_CLK, 1);
   gpio_set_dir(PIN_CLK, GPIO_OUT);
   gpio_set_pulls(PIN_CLK, false, true);

   gpio_init(PIN_MOSI);
   gpio_put(PIN_MOSI, 1);
   gpio_set_dir(PIN_MOSI, GPIO_OUT);
   gpio_set_pulls(PIN_MOSI, true, false);

   gpio_init(PIN_MISO);
   gpio_set_dir(PIN_MISO, GPIO_IN);
   gpio_set_pulls(PIN_MISO, true, false); // This pullup is crucial!
}

uint8_t mmc_read(uint16_t addr) {
   if (gpio_get(PIN_MISO)) {
      return port | 0x80;
   } else {
      return port & 0x7f;
   }
}

void mmc_write(uint16_t addr, uint8_t val) {
   port = val;
   gpio_put(PIN_CLK,  port & 2);
   gpio_put(PIN_MOSI, port & 1);
}
