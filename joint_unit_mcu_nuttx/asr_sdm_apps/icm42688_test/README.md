# ICM-42688 Test Application (XIAO RP2350 / NuttX)

本应用用于在 XIAO RP2350 的 NuttX(usbnsh) 下验证 ICM-42688（I2C1, 0x68）驱动连通性与数据读取。

## 目录结构

```
icm42688_test/
├── Makefile
├── Make.defs
├── Kconfig
├── icm42688_test_main.c
└── scripts/
    └── setup_icm42688_test.sh
```

## 前置条件
- 已在项目中获取 NuttX 与 apps（参考 XIAO RP2350 NuttX 指南）
- ICM-42688 接至 I2C1 地址 0x68

## 一键部署
```bash
cd joint_unit_mcu_nuttx/asr_sdm_apps/icm42688_test/scripts
./setup_icm42688_test.sh            # 默认启用测试应用与 RP23xx I2C/I2C1
# 若需要 i2ctool，请添加环境变量 ENABLE_I2CTOOL=1
# ENABLE_I2CTOOL=1 ./setup_icm42688_test.sh
```
执行后将：
- 将本应用接入到 `apps/examples/icm42688_test`（符号链接）
- 运行 apps/mkkconfig 并配置 `xiao-rp2350:usbnsh`
- 启用 `CONFIG_EXAMPLES_ICM42688_TEST=y`、`CONFIG_RP23XX_I2C=y`、`CONFIG_RP23XX_I2C1=y`、`CONFIG_RP23XX_I2C_DRIVER=y` 并构建固件
- 可选：启用 `CONFIG_SYSTEM_I2CTOOL=y`（当 `ENABLE_I2CTOOL=1`）

## 手动步骤
```bash
# 1) 创建符号链接到 apps/examples
cd joint_unit_mcu_nuttx/apps
mkdir -p examples
ln -sf ../../asr_sdm_apps/icm42688_test examples/icm42688_test

# 2) 生成应用配置
./tools/mkkconfig.sh

# 3) 选择板级配置
cd ../nuttx
./tools/configure.sh xiao-rp2350:usbnsh

# 4) 启用本应用
echo "CONFIG_EXAMPLES_ICM42688_TEST=y" >> .config
make olddefconfig

# 5) 构建
make -j$(nproc)
```

## 在 NSH 测试
```bash
# i2ctool 验证 WHO_AM_I (0x47)
i2c dev 1; i2c read 0x68 0x75 1

# 运行测试应用（自动从驱动读取换算系数，使用 FIFO-only 读取）
icm42688_test
# 输出说明：
#  - [IOCTL] scales -> accelLSB=xxxx gyroLSB=yy.y  # 驱动返回的换算系数
#  - AX/AY/AZ: g；GX/GY/GZ: dps
#  - 驱动使用 FIFO-only 模式（固定16字节帧读取），参考裸跑程序实现

# 若启用 i2ctool，可做寄存器校验（命令可能名为 i2c）
# i2c dev 1; i2c read 0x68 0x75 1  # 期望 0x47
```

## 参考
- ICM-42688-P Datasheet: https://invensense.tdk.com/download-pdf/icm-42688-p-datasheet/
- NuttX I2C 工具: https://www.embeddedrelated.com/showarticle/1656.php
- XIAO RP2350 与 NuttX(usbnsh): https://wiki.seeedstudio.com/cn/xiao_rp2350_nuttx/

## 备注
- 测试应用通过 IOCTL 从驱动获取换算系数（无需应用内维护 FS 映射）
- 驱动使用 FIFO-only 模式（固定16字节帧读取），与裸跑程序实现保持一致
- 静止时各轴加速度应接近 0g（除重力方向），偏置会自动估计并扣除


