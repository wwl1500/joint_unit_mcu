############################################################################
# apps/icm42688_test/Makefile
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = icm42688_test
PRIORITY  = $(CONFIG_EXAMPLES_ICM42688_TEST_PRIORITY)
STACKSIZE = $(CONFIG_EXAMPLES_ICM42688_TEST_STACKSIZE)
MODULE    = $(CONFIG_EXAMPLES_ICM42688_TEST)

# Main entry
MAINSRC = icm42688_test_main.c

# 头文件搜索路径：当前应用目录与 ICM42688 驱动目录
INCDIR  += .
INCDIR  += $(APPDIR)/asr_sdm_drivers/icm42688

# Pull in the driver source directly to simplify linking (external driver tree)
# Path: <repo>/joint_unit_mcu_nuttx/asr_sdm_drivers/icm42688/icm42688.c
CSRCS += ../../asr_sdm_drivers/icm42688/icm42688.c

include $(APPDIR)/Application.mk



INCDIR  += /home/wwl/joint_unit_mcu/joint_unit_mcu_nuttx/asr_sdm_drivers/icm42688
