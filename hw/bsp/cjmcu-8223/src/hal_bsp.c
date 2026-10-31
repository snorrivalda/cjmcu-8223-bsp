/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include "os/mynewt.h"
#include "mynewt_cm.h"
#include "nrfx.h"
#include "hal/hal_system.h"
#include "hal/hal_bsp.h"
#include "hal/hal_flash.h"
#include "mcu/nrf51_hal.h"
#include "bsp/bsp.h"

#if MYNEWT_VAL(I2C_0)
#include "hal/hal_i2c.h"
#endif

#if MYNEWT_VAL(UART_0)
#include "uart/uart.h"
#include "uart_hal/uart_hal.h"

static struct uart_dev os_bsp_uart0;
static const struct nrf51_uart_cfg os_bsp_uart0_cfg = {
    .suc_pin_tx = MYNEWT_VAL(UART_0_PIN_TX),
    .suc_pin_rx = MYNEWT_VAL(UART_0_PIN_RX),
    .suc_pin_rts = MYNEWT_VAL(UART_0_PIN_RTS),
    .suc_pin_cts = MYNEWT_VAL(UART_0_PIN_CTS),
};
#endif

#if MYNEWT_VAL(I2C_0)
static const struct nrf51_hal_i2c_cfg os_bsp_i2c0_cfg = {
    .sda_pin = MYNEWT_VAL(I2C_0_PIN_SDA),
    .scl_pin = MYNEWT_VAL(I2C_0_PIN_SCL),
    .i2c_frequency = MYNEWT_VAL(I2C_0_FREQ_KHZ),
};
#endif

/** What memory to include in coredump. */
static const struct hal_bsp_mem_dump dump_cfg[] = {
    [0] = {
        .hbmd_start = &_ram_start,
        .hbmd_size = RAM_SIZE,
    }
};

const struct hal_bsp_mem_dump *
hal_bsp_core_dump(int *area_cnt)
{
    *area_cnt = sizeof(dump_cfg) / sizeof(dump_cfg[0]);
    return dump_cfg;
}

/**
 * Retrieves the flash device with the specified ID.  Returns NULL if no such
 * device exists.  Only the MCU internal flash (id 0) is present.
 */
const struct hal_flash *
hal_bsp_flash_dev(uint8_t id)
{
    if (id != 0) {
        return NULL;
    }
    return &nrf_flash_dev;
}

int
hal_bsp_power_state(int state)
{
    return 0;
}

/**
 * Retrieves the configured priority for the given interrupt. If no priority
 * is configured, returns the priority passed in.  The radio always gets the
 * highest priority.
 */
uint32_t
hal_bsp_get_nvic_priority(int irq_num, uint32_t pri)
{
    if (irq_num == RADIO_IRQn) {
        return 0;
    }
    return pri;
}

void
hal_bsp_init(void)
{
    int rc;

    (void)rc;

    /* Make sure system clocks have started. */
    hal_system_clock_start();

#if MYNEWT_VAL(UART_0)
    rc = os_dev_create((struct os_dev *)&os_bsp_uart0, "uart0",
                       OS_DEV_INIT_PRIMARY, 0, uart_hal_init,
                       (void *)&os_bsp_uart0_cfg);
    assert(rc == 0);
#endif

#if MYNEWT_VAL(TIMER_0)
    rc = hal_timer_init(0, NULL);
    assert(rc == 0);
#endif
#if MYNEWT_VAL(TIMER_1)
    rc = hal_timer_init(1, NULL);
    assert(rc == 0);
#endif
#if MYNEWT_VAL(TIMER_2)
    rc = hal_timer_init(2, NULL);
    assert(rc == 0);
#endif
#if MYNEWT_VAL(TIMER_3)
    rc = hal_timer_init(3, NULL);
    assert(rc == 0);
#endif

#if (MYNEWT_VAL(OS_CPUTIME_TIMER_NUM) >= 0)
    rc = os_cputime_init(MYNEWT_VAL(OS_CPUTIME_FREQ));
    assert(rc == 0);
#endif

#if MYNEWT_VAL(I2C_0)
    rc = hal_i2c_init(0, (void *)&os_bsp_i2c0_cfg);
    assert(rc == 0);
#endif
}

void
hal_bsp_deinit(void)
{
    Cortex_DisableAll();
}
