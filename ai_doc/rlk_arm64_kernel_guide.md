# runninglinuxkernel_5.0 ARM64 内核实验指南

> 涵盖编译配置、QEMU 启动、根文件系统、模块加载、启动流程等核心概念

---

## 一、编译环境

### 1.1 正确命令

```bash
export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-
make rlk_defconfig          # 或 debian_defconfig（两个完全相同）
make -j$(nproc)
```

### 1.2 常见错误：`ARCH=arm` + `arm-none-eabi-`

```
arch/Kconfig:10: can't open file "arch/arm/Kconfig"
```

**原因**：本仓库只有 ARM64 完整架构代码，`arch/arm/` 下仅有 DTS 文件和 Makefile，缺少 Kconfig 等核心文件。ARM64 完整架构在 `arch/arm64/` 下。

`arm-none-eabi-gcc` 是裸机编译器，不能用来编译 Linux 内核。必须用 `aarch64-linux-gnu-gcc`。

### 1.3 `rlk_defconfig` = `debian_defconfig`

7 个 `run_*.sh` 脚本实际就两类：

| 脚本 | Rootfs | 用途 |
|------|--------|------|
| `run_busybox.sh arm64` | `_install_arm64/` 目录 | 极简系统，busybox init，快速调试 |
| `run_debian_arm64.sh` / `run_rlk_arm64.sh` | `rootfs_debian_arm64.ext4` | 完整 Debian，能 apt-get |
| `run_debian_riscv.sh` / `run_rlk_riscv.sh` | `rootfs_debian_riscv.ext4` | RISC-V Debian |
| `run_debian_x86_64.sh` / `run_rlk_x86_64.sh` | `rootfs_debian_x86_64.ext4` | x86_64 Debian |

`run_rlk_*.sh` 与 `run_debian_*.sh` 完全相同（diff 为空），只是别名。

---

## 二、GCC 9 + binutils 2.34 链接错误修复

### 2.1 错误现象

```
kernel/time/clocksource.o:(.init.setup+0x0): dangerous relocation: unsupported relocation
security/keys/keyring.o:(__patchable_function_entries+0x0): dangerous relocation: unsupported relocation
...
make: *** [Makefile:1021: vmlinux] Error 1
```

### 2.2 根因分析

完整的因果链：

```
CONFIG_RANDOMIZE_BASE=y  (KASLR)
    → select RELOCATABLE
        → ARM64 Makefile 设置 LDFLAGS_vmlinux += -shared
            ↓
GCC 9.4 配合 -fpatchable-function-entry=2
    → 在 __patchable_function_entries 等处生成 R_AARCH64_ABS64 绝对重定位
            ↓
    -shared 模式（共享库）拒绝 R_AARCH64_ABS64
    → ld 报 "dangerous relocation: unsupported relocation"
```

### 2.3 修复方案

**文件**：`arch/arm64/Makefile`

```diff
- LDFLAGS_vmlinux += -shared -Bsymbolic -z notext -z norelro \
+ LDFLAGS_vmlinux += -pie -Bsymbolic -z notext \
```

**原理**：`-pie`（Position Independent Executable）允许 `R_AARCH64_ABS64` 存在，内核自有的重定位代码在启动时会修补它们。这是 Linux 5.4 上游的做法。

### 2.4 注意：`nokaslr` 不能替代这个修复

脚本启动参数里的 `nokaslr` 只是运行时禁用地址随机化，不影响编译。没有 `-pie` 修复，`make` 照样报错。

---

## 三、ARM64 平台配置与设备树

### 3.1 为什么 menuconfig 里没有 `System Type`？

ARM32 有 `System Type` 菜单用来选择具体板子（vexpress、imx……），ARM64 没有。原因：

| | ARM 32-bit | ARM64 |
|---|---|---|
| 内核镜像 | 每板一个 zImage | **单一 Image 通用** |
| 硬件描述 | 编译时写死 | 运行时由 DTB 传入 |
| menuconfig | 有 System Type | 无，平台信息在 DTB |

ARM64 的内核镜像可以在任何 ARM64 平台上启动，硬件信息由 **Device Tree Blob (DTB)** 在启动时描述。

### 3.2 QEMU 需要手动编译 DTB 吗？

**不需要。** QEMU 的 `-M virt` 是虚拟通用平台，运行时自动生成 DTB 传给内核。

只有 `-M vexpress-a9` 这类特定板子才需要手动提供 `-dtb vexpress-v2p-ca9.dtb`。

---

## 四、rootfs（根文件系统）

### 4.1 为什么必须要有 rootfs？

内核初始化的最后一步是执行第一个用户态进程。没有 rootfs → Kernel Panic：

```c
// init/main.c
run_init_process("/sbin/init");   // 先试这个
run_init_process("/etc/init");    // 再试
run_init_process("/bin/init");
run_init_process("/bin/sh");      // 最后兜底

panic("No working init found.");
```

内核只管资源管理（内存、调度、驱动），不提供 shell、命令、编译器。这些全在 rootfs 里。

### 4.2 rootfs 里有什么？

```
rootfs_debian_arm64.ext4
├── /sbin/init          → /lib/systemd/systemd   ← PID 1
├── /bin/bash, /bin/ls  ← 用户态命令
├── /lib/aarch64-linux-gnu/  ← 动态库 (.so)
├── /etc/passwd, /etc/shadow ← 用户账户
├── /etc/systemd/       ← systemd 配置
├── /lib/modules/5.0.0+/    ← 你编译的内核模块 .ko
└── dev/, proc/, sys/  ← 虚拟文件系统挂载点
```

### 4.3 `/sbin/init` 在磁盘上是什么？

一个 **ELF 可执行文件**（Debian 官方预编译的 systemd），和 `ls`、`bash` 完全一样类型：

```
磁盘上的字节结构：
┌──────────────┐
│ ELF Header   │ 7f 45 4c 46 → 魔数，标识 "我是可执行文件"
├──────────────┤
│ .text  代码段 │ AArch64 机器指令
├──────────────┤
│ .rodata 常量  │ 字符串 "Started %s", "/sbin/init" ...
├──────────────┤
│ .data  全局变量│
├──────────────┤
│ .dynamic 依赖 │ libc.so.6, libsystemd.so.0 ...
└──────────────┘
```

### 4.4 内核如何拉起 init

```
kernel_init()
  → run_init_process("/sbin/init")
    → do_execve("/sbin/init", ...)
      ├─ 打开文件
      ├─ 发现是 symlink → 解析到 /lib/systemd/systemd
      ├─ 读 ELF Header，确认是 ARM64 可执行文件
      ├─ 加载 .text（代码）和 .data（数据）到虚拟内存
      ├─ 加载依赖的 .so (libc.so, libsystemd.so ...)
      └─ 设置 PC = ELF entry point → 返回用户态
          → systemd 的 main() 开始执行，PID = 1
```

### 4.5 内核 Image 不在 ext4 里

QEMU 启动时传了两样独立的东西：

```
QEMU -kernel arch/arm64/boot/Image     ← 内核本身（QEMU 加载到内存）
     -drive file=rootfs_xxx.ext4       ← 根文件系统（内核挂载为 /）
```

**Image** 是 QEMU 直接加载的，不在 ext4 里。只有 `.ko` 模块在 ext4 的 `/lib/modules/` 里。

### 4.6 "实际开发中没挂载 rootfs"是误解

rootfs 的挂载不是不存在，是**被内核自动执行了**：

```
QEMU:
  -append "root=/dev/vda rootfstype=ext4 rw"
            ↑ 内核读到这个 → 自动 mount

真实开发板:
  U-Boot bootargs: root=/dev/mmcblk0p2 rootwait rw
                                  ↑ 内核读到这个 → 自动 mount
```

### 4.7 ARM32 vs ARM64：`/dev/console` 设备节点

| | Busybox (ARM32) | Debian (ARM64) |
|---|---|---|
| Init 系统 | `/linuxrc`（busybox） | systemd |
| `/dev` 管理 | 手动 mknod 静态创建 | devtmpfs 自动创建 |
| 需手动创建 console | ✅ 是 | ❌ 否 |

Busybox 的 init 不会自动管理 `/dev`，必须提前用 `mknod dev/console c 5 1` 创建。Debian 的 systemd 启动时自动 `mount -t devtmpfs devtmpfs /dev`，所有设备节点自动生成。

---

## 五、内核模块 (`=y` vs `=m`)

### 5.1 编入内核 vs 模块

```
= y ──→ 编进 vmlinux/Image ──→ 内核启动时自动存在 ──→ 不在 /lib/modules/
= m ──→ 编译为 .ko 文件    ──→ 安装到 /lib/modules/5.0.0+/
```

### 5.2 `=m` 模块的加载方式

| 触发方式 | 条件 | 例子 |
|---------|------|------|
| **自动加载**（udev） | 硬件存在，内核发 uevent | `cpufreq-dt.ko`、`arm-cci.ko` |
| **手动加载** | 无对应硬件或按需使用 | `ntfs.ko`、`fuse.ko` |

自动加载机制：

```
硬件被探测 → 内核发 uevent → udev 收到 → 查 modules.alias → modprobe → .ko 加载
```

大部分 `=m` 的模块只要硬件存在就会自动加载，不需要手动 `insmod`。

---

## 六、QEMU 启动流程

### 6.1 `./run_debian_arm64.sh run` 背后发生了什么

```
① QEMU 启动
   qemu-system-aarch64 -M virt -cpu max -kernel Image -drive file=rootfs.ext4 ...
   ↓
② QEMU 内置 mini-loader
   把 Image 加载到虚拟内存，自动生成 DTB，附加 bootargs
   ↓
③ 内核入口 arch/arm64/kernel/head.S
   ↓
④ start_kernel() → ... → kernel_init()
   ↓
⑤ 解析 bootargs → "root=/dev/vda rootfstype=ext4 rw"
   ↓
⑥ mount_root() → 挂载 rootfs_debian_arm64.ext4
   ↓
⑦ run_init_process("/sbin/init") → do_execve → systemd (PID 1)
   ↓
⑧ systemd 读 /etc/systemd/ → 启动 getty@ttyAMA0, sshd, udevd ...
   ↓
⑨ benshushu login: ← 你在操作
```

### 6.2 QEMU vs 真实硬件对照

| 阶段 | 真实硬件 | QEMU |
|------|---------|------|
| Boot ROM | SoC 掩膜 ROM 固化代码 | 无 |
| SPL | SRAM 中初始化 DDR | 无 |
| U-Boot | 加载 kernel + DTB | QEMU 内置 mini-loader |
| DTB 来源 | 编译好的 `.dtb` | QEMU 运行时自动生成 |
| **内核代码** | **完全一样** | **完全一样** |
| **用户态 (init~)** | **完全一样** | **完全一样** |
| rootfs 挂载 | `root=/dev/mmcblk0p2` | `root=/dev/vda` |

**内核入口点（`start_kernel`）之后，QEMU 和真机完全相同。**

---

## 七、常见问题速查

| 问题 | 原因 | 解决 |
|------|------|------|
| `arch/arm/Kconfig` 找不到 | 仓库只有 ARM64 | 用 `ARCH=arm64` |
| `dangerous relocation` | GCC 9 `-shared` 拒绝 ABS64 | `-shared` → `-pie`（已修复） |
| `menuconfig` 太小 | 终端不够 19×80 | 拉大窗口或用 `make oldconfig` |
| 没有 System Type | ARM64 不支持板级配置 | 靠 DTB，无需配置 |
| `canot find rootfs image` | 还没执行 `build_rootfs` | `sudo ./run_debian_arm64.sh build_rootfs` |
| `qemu-system-aarch64: not found` | 没装 QEMU | `sudo apt install qemu-system-arm` |
| Serial Getty 重启 | Ctrl+C 杀了 getty | systemd 自动重启，正常行为 |

---

## 八、完整命令速查

```bash
# 首次编译
export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-
make rlk_defconfig
make -j$(nproc)

# 增量编译（改代码后）
make -j$(nproc)

# 重新配置
make menuconfig              # 图形界面（需 19×80 终端）
make oldconfig               # 逐行文本配置

# 制作 rootfs 镜像
sudo ./run_debian_arm64.sh build_rootfs

# 更新 rootfs 镜像（不改 rootfs 内容，只更新内核模块）
sudo ./run_debian_arm64.sh update_rootfs

# 启动
./run_debian_arm64.sh run

# GDB 调试
./run_debian_arm64.sh run debug

# 退出 QEMU
Ctrl+A, X
```
