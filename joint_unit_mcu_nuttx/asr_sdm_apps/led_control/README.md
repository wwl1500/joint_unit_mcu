# LED Control Application for XIAO RP2350

一个为XIAO RP2350 开发板设计的板载 LED 控制应用，采用 NuttX 外部应用的方式集成。提供命令行控制与可复用的简单 API。

## 功能

-  LED 开关控制
-  自定义闪烁间隔（毫秒）

## 目录结构

```
led_control/
├── Makefile            # 构建配置
├── Make.defs           # 应用注册
├── Kconfig             # 配置选项
├── led_control.h       # 公共 API 头文件
├── led_control_main.c  # 主程序实现
├── README.md           # 本说明
└── scripts/
    └── setup_led_control.sh  # 自动化部署脚本
```

## 环境与依赖

```bash
sudo apt install python3-pip python3-yaml
```

## 获取 NuttX 依赖

```bash
cd joint_unit_mcu_nuttx
python3 download_nuttx_repos.py
在上面脚本运行完成后，可以选择删除/joint_unit_mcu_nuttx下的nuttx和apps目录
最好在joint_unit_mcu_nuttx目录下运行下列命令，下载nuttx最新版本，可以支持xiao-rp2350开发板
git clone https://github.com/apache/nuttx.git nuttx
git clone https://github.com/apache/nuttx-apps apps
```


## 使用方法（usbnsh模式：使用usb线将开发板与计算机相连）

### 1. 一键部署
```bash
# 进入 led_control 目录
cd joint_unit_mcu_nuttx/asr_sdm_apps/led_control

# 运行自动化脚本
./scripts/setup_led_control.sh

# 烧录固件
cp ../../nuttx/nuttx.uf2 /media/$USER/RP2350/(/path/to/board/boot/partition/)

# 连接串口测试（别的串口工具也可以）
picocom -b 115200 /dev/ttyACM0
```
### 2. 手动部署

```bash
# 1) 准备外部应用目录（可放在任意位置）
mkdir -p /path/to/external_apps/led_control

# 2) 复制应用文件
cp -r joint_unit_mcu_nuttx/asr_sdm_apps/led_control/* /path/to/external_apps/led_control/

# 3) 在 apps 下创建符号链接（将外部应用接入 NuttX 应用树）
cd joint_unit_mcu_nuttx/apps
ln -sf /path/to/external_apps/led_control led_control

# 4) 选择板级配置
cd ../nuttx
tools/configure.sh xiao-rp2350:usbnsh

# 5) 生成应用配置（扫描 apps 下的外部应用）
cd ../apps
./tools/mkkconfig.sh

# 6) 启用所需配置（LED 驱动与本应用）
cd ../nuttx
echo "CONFIG_EXAMPLES_LED_CONTROL=y" >> .config
echo "CONFIG_USERLED=y" >> .config
echo "CONFIG_USERLED_LOWER=y" >> .config
echo "# CONFIG_ARCH_LEDS is not set" >> .config

# 7) 更新配置并构建
make olddefconfig
make -j$(nproc)

# 8) 烧录固件（根据你的挂载路径替换）
cp nuttx.uf2 /path/to/board/boot/partition/

# 9) 连接串口并测试（别的串口工具也可以）
picocom -b 115200 /dev/ttyACM0
```


### 3. 测试
#### 命令行
```bash
nsh> led_control            # 显示帮助
nsh> led_control on         # 打开 LED
nsh> led_control off        # 关闭 LED
nsh> led_control blink      # 默认 500ms 闪烁
nsh> led_control blink 200  # 200ms 闪烁
# 闪烁过程中按 Ctrl+C 可立即停止
```

## 必要配置

```bash
# LED 驱动支持
CONFIG_USERLED=y
CONFIG_USERLED_LOWER=y
# CONFIG_ARCH_LEDS is not set

# LED 控制应用
CONFIG_EXAMPLES_LED_CONTROL=y
CONFIG_EXAMPLES_LED_CONTROL_PRIORITY=100
CONFIG_EXAMPLES_LED_CONTROL_STACKSIZE=2048

# 建议：USB 串口（若使用 USBNSH）
CONFIG_USBDEV=y
CONFIG_NSH_USBCONSOLE=y
CONFIG_NSH_USBCONDEV="/dev/ttyACM0"
```

## 故障排除
- **脚本运行报错**:可以尝试再次运行
- **应用未注册**：检查符号链接是否正确，运行 `./tools/mkkconfig.sh`
- **配置未启用**：确认上述 `CONFIG_...` 选项已写入并 `make olddefconfig`
- **链接错误（board_userled_*）**：确保 `CONFIG_ARCH_LEDS=n` 且 `CONFIG_USERLED_LOWER=y`


## 参考

- NuttX 文档（外部应用）：https://nuttx.apache.org/docs/latest/guides/building_nuttx_with_app_out_of_src_tree.html#make-export
- XIAO RP2350 NuttX：
  https://wiki.seeedstudio.com/cn/xiao_rp2350_nuttx/

## 许可证

Apache License 2.0