############################################################################
# apps/led_control/Makefile
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.  The
# ASF licenses this file to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance with the
# License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations
# under the License.
#
############################################################################

include $(APPDIR)/Make.defs

# LED Control Application

# Program name visible in NSH (matches the entry point built from MAINSRC)
PROGNAME = led_control

# Task parameters sourced from Kconfig (see Kconfig mirror symbols)
PRIORITY = $(CONFIG_EXAMPLES_LED_CONTROL_PRIORITY)
STACKSIZE = $(CONFIG_EXAMPLES_LED_CONTROL_STACKSIZE)

# Whether to include this app in the final build
MODULE = $(CONFIG_EXAMPLES_LED_CONTROL)

# Main entry source file for the application
MAINSRC = led_control_main.c

include $(APPDIR)/Application.mk
