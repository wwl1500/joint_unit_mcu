/****************************************************************************
 * apps/led_control/led_control.h
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __APPS_LED_CONTROL_LED_CONTROL_H
#define __APPS_LED_CONTROL_LED_CONTROL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>
#include <stdbool.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* LED Control Commands
 *
 * These symbolic constants are used to represent the high-level actions
 * that the command-line interface and any embedding application can request.
 * They do not directly map to driver ioctls; instead, they are translated
 * by the implementation into the appropriate driver operations.
 */
#define LED_CMD_ON          1   /**< Turn LED on command */
#define LED_CMD_OFF         2   /**< Turn LED off command */
#define LED_CMD_BLINK       3   /**< Blink LED command */

/* LED IDs for XIAO RP2350
 *
 * For boards exposing multiple user LEDs, additional IDs could be added
 * here in the future. For the current XIAO RP2350 target we only expose
 * the single user LED provided by the board design.
 */
#define LED_USER_LED        0   /**< User LED identifier */

/* Default timing definitions (in milliseconds)
 *
 * The default blink period is selected to be visually comfortable and to
 * avoid excessive CPU wakeups while still being responsive to Ctrl+C.
 */
#define LED_BLINK_DEFAULT   500 /**< Default blink period in milliseconds */

/* LED Device Path
 *
 * The user LED lower-half is exposed as a character device. The application
 * opens this device and uses ioctls to manipulate discrete LED instances.
 * If your board uses a different path, adjust this macro accordingly.
 */
#define LED_DEVICE_PATH     "/dev/userleds" /**< LED device file path */

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

int led_control_init(void);

int led_control_deinit(void);

int led_control_set(uint8_t led_id, bool state);

int led_control_blink(uint8_t led_id, uint16_t period);

int led_control_main(int argc, char *argv[]);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __APPS_LED_CONTROL_LED_CONTROL_H */