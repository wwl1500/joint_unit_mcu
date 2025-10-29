/****************************************************************************
 * apps/led_control/led_control_main.c
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/leds/userled.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/time.h>

#include "led_control.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define LED_DEVICE_PATH "/dev/userleds"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static int g_led_fd = -1;            /* File descriptor to the user LED device */
static bool g_led_initialized = false; /* Guard to avoid re-opening device */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: check_for_input
 *
 * Description:
 *   Check if there's any input available (for stopping blink)
 *   Uses non-blocking select() system call to check if standard input has data to read
 *   Primarily used to detect Ctrl+C (ASCII ETX, value 3) to stop LED blinking
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   true  - Ctrl+C input detected, should stop blinking
 *   false - No input or input is not Ctrl+C
 *
 * Assumptions/Limitations:
 *   - Uses non-blocking I/O, will not block program execution
 *   - Only detects Ctrl+C (ASCII 3), other inputs are ignored
 *   - Uses select() system call for input detection
 *
 ****************************************************************************/

static bool check_for_input(void)
{
  fd_set readfds;
  struct timeval timeout;
  int ret;

  FD_ZERO(&readfds);
  FD_SET(STDIN_FILENO, &readfds);
  
  timeout.tv_sec = 0;   /* Non-blocking poll: return immediately */
  timeout.tv_usec = 0;

  ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);
  if (ret > 0 && FD_ISSET(STDIN_FILENO, &readfds))
    {
      char c;
      if (read(STDIN_FILENO, &c, 1) > 0)
        {
          if (c == 3) /* ASCII ETX, Ctrl+C */
            {
              return true;
            }
        }
    }
  
  return false;
}

/****************************************************************************
 * Name: led_control_init
 *
 * Description:
 *   Initialize the LED control system
 *   Opens the LED device file (/dev/userleds) and prepares it for LED control operations
 *   Uses global flags to avoid duplicate initialization
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   OK (0) - Initialization successful
 *   -errno - Initialization failed, error code is negative errno value
 *   -ENODEV: LED device does not exist
 *   -EACCES: Permission denied
 *   -EBUSY: Device already in use
 *
 * Assumptions/Limitations:
 *   - Must be called before any other LED control functions
 *   - Uses global variables g_led_fd and g_led_initialized for state management
 *   - Thread safety: not designed for concurrent calls from multiple threads
 *
 ****************************************************************************/

int led_control_init(void)
{
  if (g_led_initialized)
    {
      return OK;
    }

  /* Open the LED device */

  g_led_fd = open(LED_DEVICE_PATH, O_RDWR);
  if (g_led_fd < 0)
    {
      printf("ERROR: Failed to open %s: %d\n", LED_DEVICE_PATH, errno);
      return -errno;
    }

  g_led_initialized = true;
  printf("LED control initialized successfully\n");
  return OK;
}

/****************************************************************************
 * Name: led_control_deinit
 *
 * Description:
 *   Deinitialize the LED control system
 *   Closes the LED device file and cleans up related resources
 *   Resets global state flags for next use
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   OK (0) - Deinitialization successful
 *
 * Assumptions/Limitations:
 *   - Should be called after all LED operations are complete
 *   - Safe to call even if not initialized
 *   - Resets g_led_fd to -1 and g_led_initialized to false
 *
 ****************************************************************************/

int led_control_deinit(void)
{
  if (g_led_fd >= 0)
    {
      close(g_led_fd);
      g_led_fd = -1;
    }

  g_led_initialized = false;
  printf("LED control deinitialized\n");
  return OK;
}

/****************************************************************************
 * Name: led_control_set
 *
 * Description:
 *   Set LED state (on/off)
 *   Controls the physical state of the specified LED through ioctl system call
 *   For XIAO RP2350, LED is active-low, so the state is automatically inverted
 *   to provide intuitive behavior
 *
 * Input Parameters:
 *   led_id - LED identifier (use LED_USER_LED for XIAO RP2350)
 *   state  - LED state, true for on, false for off
 *
 * Returned Value:
 *   OK (0) - Set successful
 *   -errno - Set failed, error code is negative errno value
 *   -ENODEV: LED device not available
 *   -EINVAL: Invalid LED ID
 *   -EIO: I/O error
 *
 * Assumptions/Limitations:
 *   - LED control must be initialized before calling this function
 *   - LED state is automatically inverted for active-low LEDs (common on microcontroller boards)
 *   - Uses ULEDIOC_SETLED ioctl command to control LED
 *
 ****************************************************************************/

int led_control_set(uint8_t led_id, bool state)
{
  int ret;

  if (!g_led_initialized)
    {
      ret = led_control_init();
      if (ret < 0)
        {
          return ret;
        }
    }

  if (g_led_fd < 0)
    {
      return -ENODEV;
    }

  /* Use ioctl to control the LED */
  /* Note: Board LED is active-low; invert requested logical state. */

  struct userled_s led_state;
  led_state.ul_led = led_id;
  led_state.ul_on = !state;  /* Active-low inversion for intuitive API */

  ret = ioctl(g_led_fd, ULEDIOC_SETLED, (unsigned long)&led_state);
  if (ret < 0)
    {
      printf("ERROR: Failed to set LED %d to %s: %d\n", 
             led_id, state ? "ON" : "OFF", errno);
      return -errno;
    }

  printf("LED %d set to %s\n", led_id, state ? "ON" : "OFF");
  return OK;
}

/****************************************************************************
 * Name: led_control_blink
 *
 * Description:
 *   Make LED blink with specified period
 *   Creates a blinking pattern with the specified on/off period, function runs
 *   continuously until interrupted by Ctrl+C or an error occurs
 *   Uses cooperative input polling to maintain Ctrl+C responsiveness while
 *   avoiding excessive CPU usage
 *
 * Input Parameters:
 *   led_id - LED identifier (use LED_USER_LED for XIAO RP2350)
 *   period - Blink period in milliseconds (minimum 50ms recommended)
 *
 * Returned Value:
 *   OK (0) - Blinking completed successfully or interrupted by Ctrl+C
 *   -errno - Blinking failed, error code is negative errno value
 *   -ENODEV: LED device not available
 *   -EINVAL: Invalid LED ID or period
 *   -EIO: I/O error
 *
 * Assumptions/Limitations:
 *   - LED control must be initialized before calling this function
 *   - Function blocks until interrupted by Ctrl+C
 *   - Minimum recommended period is 50ms for visible blinking and to keep
 *     input polling responsive without excessive CPU usage
 *   - Uses 50ms sleep intervals for cooperative input checking
 *   - LED is automatically turned off when stopping blink
 *
 ****************************************************************************/

int led_control_blink(uint8_t led_id, uint16_t period)
{
  int ret;

  if (!g_led_initialized)
    {
      ret = led_control_init();
      if (ret < 0)
        {
          return ret;
        }
    }

  if (g_led_fd < 0)
    {
      return -ENODEV;
    }

  printf("LED %d blinking with period %dms (Press Ctrl+C to stop)\n", 
         led_id, period);

  /* Blink loop: alternate ON/OFF with cooperative input polling. */
  while (1)
    {
      /* Check for Ctrl+C input */
      if (check_for_input())
        {
          printf("\nStopping LED blink...\n");
          break;
        }

      /* LED ON */
      ret = led_control_set(led_id, true);
      if (ret < 0)
        {
          return ret;
        }

      /* Sleep with periodic check for input to keep Ctrl+C responsive. */
      for (int i = 0; i < period; i += 50)
        {
          if (check_for_input())
            {
              printf("\nStopping LED blink...\n");
              goto stop_blink;
            }
          usleep(50000); /* Sleep 50ms at a time */
        }

      /* LED OFF */
      ret = led_control_set(led_id, false);
      if (ret < 0)
        {
          return ret;
        }

      /* Mirror the above ON-delay with the same cooperative sleep. */
      for (int i = 0; i < period; i += 50)
        {
          if (check_for_input())
            {
              printf("\nStopping LED blink...\n");
              goto stop_blink;
            }
          usleep(50000); /* Sleep 50ms at a time */
        }
    }

stop_blink:
  /* Turn off LED when stopping */
  led_control_set(led_id, false);
  printf("LED blink stopped\n");

  return OK;
}

/****************************************************************************
 * Name: main
 *
 * Description:
 *   Main entry point for the LED control application
 *   Provides command-line interface for LED control operations, supports
 *   three modes: on, off, and blink
 *   Parses command-line arguments and executes corresponding LED control commands
 *
 * Input Parameters:
 *   argc - Number of command-line arguments (including program name)
 *   argv - Array of command-line argument strings
 *         argv[0]: Program name
 *         argv[1]: Command (on/off/blink)
 *         argv[2]: Blink period (only when command is blink)
 *
 * Returned Value:
 *   EXIT_SUCCESS (0) - Program executed successfully
 *   EXIT_FAILURE (-1) - Program execution failed
 *
 * Command Line Usage:
 *   led_control                    - Show usage information
 *   led_control on                 - Turn LED on
 *   led_control off                - Turn LED off
 *   led_control blink [period]     - Blink LED with optional period (default 500ms)
 *
 * Assumptions/Limitations:
 *   - Requires proper NuttX configuration (USERLED lower-half enabled)
 *   - LED device must be available at LED_DEVICE_PATH
 *   - Blink mode can be interrupted with Ctrl+C (ASCII 3)
 *   - Supported commands: on, off, blink
 *   - Blink period must be positive integer, invalid values use default
 *
 ****************************************************************************/

int main(int argc, char *argv[])
{
  int ret;
  int led_id = LED_USER_LED;
  int command = LED_CMD_ON;
  int period = LED_BLINK_DEFAULT;

  printf("Simple LED Control for XIAO RP2350\n");
  printf("==================================\n");

  /* Parse command line arguments */
  if (argc < 2)
    {
      printf("Usage: %s [on|off|blink [period_ms]]\n", argv[0]);
      printf("  on: Turn LED on\n");
      printf("  off: Turn LED off\n");
      printf("  blink [period]: Blink LED with specified period (default: %dms)\n", LED_BLINK_DEFAULT);
      return -1;
    }

  if (strcmp(argv[1], "on") == 0)
    {
      command = LED_CMD_ON;
    }
  else if (strcmp(argv[1], "off") == 0)
    {
      command = LED_CMD_OFF;
    }
  else if (strcmp(argv[1], "blink") == 0)
    {
      command = LED_CMD_BLINK;
      if (argc > 2)
        {
          period = atoi(argv[2]);
          if (period <= 0)
            {
              period = LED_BLINK_DEFAULT; /* Fallback to sane default */
            }
        }
    }
  else
    {
      printf("ERROR: Unknown command '%s'\n", argv[1]);
      printf("Usage: %s [on|off|blink [period_ms]]\n", argv[0]);
      printf("  on: Turn LED on\n");
      printf("  off: Turn LED off\n");
      printf("  blink [period]: Blink LED with specified period (default: %dms)\n", LED_BLINK_DEFAULT);
      return -1;
    }

  /* Initialize LED control */
  ret = led_control_init();
  if (ret < 0)
    {
      printf("ERROR: Failed to initialize LED control: %d\n", ret);
      return ret;
    }

  /* Execute command */
  switch (command)
    {
      case LED_CMD_ON:
        ret = led_control_set(led_id, true);
        break;

      case LED_CMD_OFF:
        ret = led_control_set(led_id, false);
        break;

      case LED_CMD_BLINK:
        ret = led_control_blink(led_id, period);
        break;

      default:
        printf("ERROR: Unknown command %d\n", command);
        ret = -EINVAL;
        break;
    }

  if (ret < 0)
    {
      printf("ERROR: Command failed: %d\n", ret);
    }

  /* Cleanup */
  led_control_deinit();

  printf("LED Control finished\n");
  return ret;
}