#!/bin/bash

############################################################################
# setup_led_control.sh
#
# LED Control Application Setup Script for XIAO RP2350
#
# This script automates the setup of the LED control application using
# NuttX external application approach.
#
# Usage: ./setup_led_control.sh [external_apps_path]
#
# If external_apps_path is not provided, it will use the default path:
# /home/$(whoami)/external_apps/led_control
#
# Expected repository layout (relative to project root):
#   joint_unit_mcu_nuttx/
#     ├── nuttx/        # NuttX kernel tree
#     └── apps/         # NuttX apps tree (previously 'nuttx-apps')
#
# Note: If your apps tree is named differently (e.g., 'nuttx-apps'), adjust
# the NUTTX_APPS_DIR variable below accordingly.
#
############################################################################

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default paths
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# From scripts/ to repo root: scripts -> led_control -> asr_sdm_apps -> joint_unit_mcu_nuttx -> repo root
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../../../" && pwd)"
NUTTX_DIR="$PROJECT_ROOT/joint_unit_mcu_nuttx"
ASR_APPS_DIR="$NUTTX_DIR/asr_sdm_apps/led_control"
# Select the apps directory name (default: apps)
NUTTX_APPS_DIR="$NUTTX_DIR/apps"
NUTTX_DIR="$NUTTX_DIR/nuttx"

# Default external apps path
DEFAULT_EXTERNAL_PATH="/home/$(whoami)/external_apps/led_control"
EXTERNAL_APPS_PATH="${1:-$DEFAULT_EXTERNAL_PATH}"

# Function to print colored output
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to check if command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Function to check prerequisites
check_prerequisites() {
    print_info "Checking prerequisites..."
    
    # Check if we're in the right directory
    if [ ! -d "$ASR_APPS_DIR" ]; then
        print_error "LED control application not found at $ASR_APPS_DIR"
        print_error "Please run this script from the project root directory"
        exit 1
    fi
    
    # Check if NuttX directory exists
    if [ ! -d "$NUTTX_DIR" ]; then
        print_error "NuttX directory not found at $NUTTX_DIR"
        print_error "Please run download_nuttx_repos.py first"
        exit 1
    fi
    
    # Check required commands
    local missing_commands=()
    
    if ! command_exists python3; then
        missing_commands+=("python3")
    fi
    
    if ! command_exists make; then
        missing_commands+=("make")
    fi
    
    if [ ${#missing_commands[@]} -ne 0 ]; then
        print_error "Missing required commands: ${missing_commands[*]}"
        print_error "Please install them first"
        exit 1
    fi
    
    print_success "Prerequisites check passed"
}

# Function to create external apps directory
create_external_directory() {
    print_info "Creating external application directory..."
    
    # Create parent directory if it doesn't exist
    local parent_dir="$(dirname "$EXTERNAL_APPS_PATH")"
    mkdir -p "$parent_dir"
    
    # Create external apps directory
    mkdir -p "$EXTERNAL_APPS_PATH"
    
    print_success "External directory created: $EXTERNAL_APPS_PATH"
}

# Function to copy application files
copy_application_files() {
    print_info "Copying application files..."
    
    # Copy all files from asr_sdm_apps/led_control to external directory
    cp -r "$ASR_APPS_DIR"/* "$EXTERNAL_APPS_PATH/"
    
    print_success "Application files copied successfully"
}

# Function to create symbolic link into the apps/ tree
create_symbolic_link() {
    print_info "Creating symbolic link..."
    
    # Ensure examples directory exists
    mkdir -p "$NUTTX_APPS_DIR/examples"

    # Remove existing link if it exists at the expected path
    if [ -L "$NUTTX_APPS_DIR/examples/led_control" ] || [ -e "$NUTTX_APPS_DIR/examples/led_control" ]; then
        rm -rf "$NUTTX_APPS_DIR/examples/led_control"
        print_info "Removed existing examples/led_control entry"
    fi
    
    # Create new symbolic link under apps/examples as required by Make.defs
    ln -sf "$EXTERNAL_APPS_PATH" "$NUTTX_APPS_DIR/examples/led_control"
    
    print_success "Symbolic link created: $NUTTX_APPS_DIR/examples/led_control -> $EXTERNAL_APPS_PATH"
}

# Function to run mkkconfig (generates app registration from Kconfig)
run_mkkconfig() {
    print_info "Running mkkconfig..."
    
    cd "$NUTTX_APPS_DIR"
    ./tools/mkkconfig.sh
    
    # 确保 external 示例被纳入 apps/Kconfig
    if ! grep -q "/examples/led_control/Kconfig" "$NUTTX_APPS_DIR/Kconfig" 2>/dev/null; then
        print_warning "apps/Kconfig 未包含 led_control，追加 source 条目"
        echo "source \"$NUTTX_APPS_DIR/examples/led_control/Kconfig\"" >> "$NUTTX_APPS_DIR/Kconfig"
    fi
    # 确保 system/nsh 被纳入 apps/Kconfig（提供 CONFIG_SYSTEM_NSH）
    if ! grep -q "/system/nsh/Kconfig" "$NUTTX_APPS_DIR/Kconfig" 2>/dev/null; then
        print_warning "apps/Kconfig 未包含 system/nsh，追加 source 条目"
        echo "source \"$NUTTX_APPS_DIR/system/nsh/Kconfig\"" >> "$NUTTX_APPS_DIR/Kconfig"
    fi
    # 确保 system/readline 被纳入 apps/Kconfig（提供 CONFIG_SYSTEM_READLINE/y 实现）
    if ! grep -q "/system/readline/Kconfig" "$NUTTX_APPS_DIR/Kconfig" 2>/dev/null; then
        print_warning "apps/Kconfig 未包含 system/readline，追加 source 条目"
        echo "source \"$NUTTX_APPS_DIR/system/readline/Kconfig\"" >> "$NUTTX_APPS_DIR/Kconfig"
    fi
    
    print_success "mkkconfig completed"
}

# Function to configure NuttX
configure_nuttx() {
    print_info "Configuring NuttX..."
    
    cd "$NUTTX_DIR"
    
    # Clean previous configuration
    if [ -f ".config" ]; then
        print_info "Cleaning previous configuration..."
        make distclean
    fi
    
    # Configure for XIAO RP2350 with USBNSH
    print_info "Configuring for xiao-rp2350:usbnsh..."
    if ! tools/configure.sh xiao-rp2350:usbnsh; then
        print_error "Failed to configure NuttX for xiao-rp2350:usbnsh"
        print_error "Please check if the board configuration exists"
        exit 1
    fi
    
    # Verify the configuration was applied correctly
    if [ ! -f ".config" ]; then
        print_error "Configuration file .config was not created"
        exit 1
    fi
    
    # Check if we're configuring for the right board
    if ! grep -q 'CONFIG_ARCH_BOARD="xiao-rp2350"' .config; then
        print_error "Configuration is not for xiao-rp2350 board"
        print_error "Current board: $(grep 'CONFIG_ARCH_BOARD=' .config || echo 'Not found')"
        exit 1
    fi
    
    print_success "NuttX configuration completed"
}

# Function to enable LED control
enable_led_control() {
    print_info "Enabling LED control application..."
    
    cd "$NUTTX_DIR"
    
    # 直接追加配置（不先跑 olddefconfig，避免首次依赖覆盖新增项）
    # Now add our specific configurations
    print_info "Adding LED control and NSH configurations..."
    
    # Clean previous keys to avoid duplicates
    sed -i "/^CONFIG_NSH_LIBRARY=/d" .config || true
    sed -i "/^CONFIG_SYSTEM_NSH=/d" .config || true
    sed -i "/^CONFIG_NSH_CONSOLE=/d" .config || true
    sed -i "/^CONFIG_NSH_USBCONSOLE=/d" .config || true
    sed -i "/^CONFIG_CDCACM=/d" .config || true
    sed -i "/^CONFIG_CDCACM_CONSOLE=/d" .config || true
    sed -i "/^CONFIG_SYSTEM_READLINE=/d" .config || true
    sed -i "/^CONFIG_SYSTEM_CLE=/d" .config || true
    sed -i "/^CONFIG_NSH_READLINE=/d" .config || true
    sed -i "/^CONFIG_NSH_CLE=/d" .config || true
    sed -i "/^CONFIG_INIT_ENTRY=/d" .config || true
    sed -i "/^CONFIG_INIT_ENTRYPOINT=/d" .config || true
    sed -i "/^CONFIG_INIT_ENTRYNAME=/d" .config || true
    sed -i "/^CONFIG_EXAMPLES_LED_CONTROL=/d" .config || true
    sed -i "/^CONFIG_USERLED=/d" .config || true
    sed -i "/^CONFIG_USERLED_LOWER=/d" .config || true
    sed -i "/^# CONFIG_ARCH_LEDS is not set/d" .config || true
    sed -i "/^CONFIG_ARCH_LEDS=/d" .config || true

    # Add NSH and USB console configurations
    echo "CONFIG_NSH_LIBRARY=y" >> .config
    echo "CONFIG_SYSTEM_NSH=y" >> .config
    echo "CONFIG_NSH_CONSOLE=y" >> .config
    echo "CONFIG_NSH_USBCONSOLE=y" >> .config
    echo "CONFIG_CDCACM=y" >> .config
    echo "CONFIG_CDCACM_CONSOLE=y" >> .config
    echo "CONFIG_SYSTEM_READLINE=y" >> .config
    echo "CONFIG_NSH_READLINE=y" >> .config
    echo "# CONFIG_NSH_CLE is not set" >> .config
    echo "CONFIG_INIT_ENTRY=y" >> .config
    echo "CONFIG_INIT_ENTRYPOINT=\"nsh_main\"" >> .config
    echo "CONFIG_INIT_ENTRYNAME=\"nsh_main\"" >> .config

    # Add LED control configuration
    echo "CONFIG_EXAMPLES_LED_CONTROL=y" >> .config
    echo "CONFIG_USERLED=y" >> .config
    echo "CONFIG_USERLED_LOWER=y" >> .config
    echo "# CONFIG_ARCH_LEDS is not set" >> .config
    
    # 在追加后再收敛依赖（跑两次更稳妥）
    print_info "Resolving configuration dependencies..."
    make olddefconfig
    make olddefconfig

    # Debug: Show current configuration status
    print_info "Current configuration status:"
    print_info "CONFIG_SYSTEM_NSH: $(grep '^CONFIG_SYSTEM_NSH=' .config || echo 'Not set')"
    print_info "CONFIG_NSH_USBCONSOLE: $(grep '^CONFIG_NSH_USBCONSOLE=' .config || echo 'Not set')"
    print_info "CONFIG_INIT_ENTRYPOINT: $(grep '^CONFIG_INIT_ENTRYPOINT=' .config || echo 'Not set')"
    print_info "CONFIG_EXAMPLES_LED_CONTROL: $(grep '^CONFIG_EXAMPLES_LED_CONTROL=' .config || echo 'Not set')"

    # Verify critical keys（接受 NSH 内核库或系统 NSH 任一满足）
    if ! grep -q '^CONFIG_SYSTEM_NSH=y' .config && ! grep -q '^CONFIG_NSH_LIBRARY=y' .config; then
        print_error "NSH 未启用（既无 CONFIG_SYSTEM_NSH 也无 CONFIG_NSH_LIBRARY）"
        print_error "当前配置内容："
        grep -E "CONFIG_SYSTEM_NSH|CONFIG_NSH_|CONFIG_INIT_" .config || true
        print_error "尝试手动启用："
        print_error "  cd $NUTTX_DIR"
        print_error "  make menuconfig"
        print_error "  在 System Type -> NSH Library 中启用 NSH"
        exit 1
    fi
    if ! grep -q '^CONFIG_NSH_USBCONSOLE=y' .config; then
        print_error "CONFIG_NSH_USBCONSOLE 未生效，请重试脚本或手动启用"
        print_error "当前配置内容："
        grep -E "CONFIG_NSH_USBCONSOLE|CONFIG_CDCACM" .config || true
        print_error "尝试手动启用："
        print_error "  cd $NUTTX_DIR"
        print_error "  make menuconfig"
        print_error "  在 System Type -> NSH Library -> NSH Console 中启用 USB Console"
        exit 1
    fi
    if ! grep -q '^CONFIG_INIT_ENTRYPOINT="nsh_main"' .config; then
        print_error "入口未设为 nsh_main，请重试脚本或手动启用"
        print_error "当前配置内容："
        grep -E "CONFIG_INIT_ENTRY" .config || true
        print_error "尝试手动启用："
        print_error "  cd $NUTTX_DIR"
        print_error "  make menuconfig"
        print_error "  在 System Type -> NSH Library 中设置入口点为 nsh_main"
        exit 1
    fi
    
    # 确保示例已启用（若被依赖覆盖则提示）
    if ! grep -q '^CONFIG_EXAMPLES_LED_CONTROL=y' .config; then
        print_error "EXAMPLES_LED_CONTROL 未生效，可能因 USERLED 或 ARCH_LEDS 冲突"
        print_error "当前关键项："
        grep -E "CONFIG_EXAMPLES_LED_CONTROL|CONFIG_USERLED|CONFIG_USERLED_LOWER|CONFIG_ARCH_LEDS" .config || true
        print_error "建议："
        print_error "  确保 # CONFIG_ARCH_LEDS is not set 且 CONFIG_USERLED=y, CONFIG_USERLED_LOWER=y"
        exit 1
    fi
    
    print_success "LED control application enabled"
}

# Function to build NuttX
build_nuttx() {
    print_info "Building NuttX..."
    
    cd "$NUTTX_DIR"
    
    # Get number of CPU cores
    local cores=$(nproc)
    print_info "Building with $cores cores..."
    
    # Build NuttX
    make -j"$cores"
    
    print_success "NuttX build completed"
}

# Function to show usage information
show_usage() {
    echo "LED Control Application Setup Script"
    echo ""
    echo "Usage: $0 [external_apps_path]"
    echo ""
    echo "Arguments:"
    echo "  external_apps_path    Path to external applications directory"
    echo "                       (default: $DEFAULT_EXTERNAL_PATH)"
    echo ""
    echo "Examples:"
    echo "  $0                                    # Use default path"
    echo "  $0 /home/user/my_external_apps       # Use custom path"
    echo ""
    echo "This script will:"
    echo "  1. Check prerequisites"
    echo "  2. Create external application directory"
    echo "  3. Copy application files"
    echo "  4. Create symbolic link"
    echo "  5. Run mkkconfig"
    echo "  6. Configure NuttX"
    echo "  7. Enable LED control"
    echo "  8. Build NuttX"
    echo ""
    echo "After successful completion, you can:"
    echo "  1. Flash nuttx.uf2 to your XIAO RP2350 board"
    echo "  2. Connect via serial terminal: picocom -b 115200 /dev/ttyACM0"
    echo "  3. Run LED control commands in NSH shell"
}

# Function to show next steps
show_next_steps() {
    echo ""
    print_success "Setup completed successfully!"
    echo ""
    print_info "Next steps:"
    echo "  1. Flash the firmware:"
    echo "     cp $NUTTX_DIR/nuttx.uf2 /path/to/board/boot/partition/"
    echo ""
    echo "  2. Connect to the board:"
    echo "     picocom -b 115200 /dev/ttyACM0"
    echo ""
    echo "  3. Test LED control:"
    echo "     nsh> led_control on"
    echo "     nsh> led_control off"
    echo "     nsh> led_control blink"
    echo "     nsh> led_control blink 200"
    echo ""
    print_info "For detailed usage instructions, see:"
    echo "  $EXTERNAL_APPS_PATH/README.md"
    echo ""
}

# Main function
main() {
    echo "=========================================="
    echo "LED Control Application Setup Script"
    echo "=========================================="
    echo ""
    
    # Show help if requested
    if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
        show_usage
        exit 0
    fi
    
    print_info "External apps path: $EXTERNAL_APPS_PATH"
    print_info "Project root: $PROJECT_ROOT"
    echo ""
    
    # Execute setup steps
    check_prerequisites
    create_external_directory
    copy_application_files
    create_symbolic_link
    run_mkkconfig
    configure_nuttx
    enable_led_control
    build_nuttx
    
    # 第二次运行关键步骤：在第一次构建后再启用一次并重编，稳定首次运行可能的依赖收敛问题
    print_info "进行第二次启用与编译以稳定依赖..."
    enable_led_control
    build_nuttx
    
    show_next_steps
}

# Run main function
main "$@"
