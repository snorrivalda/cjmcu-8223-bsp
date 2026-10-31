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

#ifndef H_BSP_
#define H_BSP_

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>

/* Section placement macros used by the core. */
#define sec_data_core   __attribute__((section(".data.core")))
#define sec_bss_core    __attribute__((section(".bss.core")))
#define bssnz_t

/** Defined in MCU linker script. */
extern uint8_t _ram_start;

#define RAM_SIZE        0x4000  /* nRF51822 QFAA: 16 kB */

/*
 * P0.23 is the LED pin used by other CJMCU-8223 projects.  On some boards
 * the LED is wired through the LIS3DH instead and this pin does nothing.
 */
#define LED_BLINK_PIN   23

#ifdef __cplusplus
}
#endif

#endif
