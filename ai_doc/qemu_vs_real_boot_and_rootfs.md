# QEMU vs 真实硬件启动流程 & 根文件系统详解

## 一、Serial Getty 重启是怎么回事？

```
[  OK  ] Stopped Serial Getty on ttyAMA0.
[  OK  ] Started Serial Getty on ttyAMA0.
```

这不是错误。当你按 `Ctrl+C` 打断 login 提示符时：

```
benshushu login: ^C        ← 你按了 Ctrl+C，杀掉了 getty 进程
[  OK  ] Stopped Serial Getty on ttyAMA0.   ← systemd 发现 getty 死了
[  OK  ] Started Serial Getty on ttyAMA0.   ← systemd 立刻重启它
```

**原理**：systemd 管理所有服务，getty（登录进程）被配置为 `Restart=always`。任何原因导致 getty 退出，systemd 都会立刻重新拉起，保证终端始终可用。

---

## 二、为什么 ARM32 需要手动创建 `/dev/console`，ARM64 不用？

### Busybox rootfs（ARM32）—— 静态设备节点

```bash
# run_busybox.sh 中的检查
if [ ! -c $LROOT/$ROOTFS_ARM32/dev/console ]; then
    echo "please create console device node first"
    exit 1
fi
```

Busybox 的 rootfs 是一个普通目录 `_install_arm32/`，里面的 `/dev/` 是**提前用 `mknod` 手动创建**的静态节点：

```bash
mknod dev/console c 5 1    # 手动创建 console 设备节点
```

Busybox 的 init 不够智能，不会自动管理 `/dev`，必须提前准备好。

### Debian rootfs（ARM64）—— devtmpfs 自动管理

Debian rootfs 使用 **systemd**，启动时自动：

```
内核挂载 devtmpfs → /dev 目录自动填充
                 → console, ttyAMA0, sda, … 全部自动创建
```

所以**完全不需要手动 mknod**，内核驱动探测到硬件后自动在 `/dev` 下创建设备文件。

| | Busybox rootfs | Debian rootfs |
|---|---|---|
| Init 系统 | `/linuxrc`（busybox） | systemd |
| `/dev` 管理方式 | 手动 mknod 静态创建 | devtmpfs 自动创建 |
| 是否需要手动创建 console | ✅ 是 | ❌ 否 |
| rootfs 形态 | 普通目录 | ext4 磁盘镜像 |
| 大小 | ~几 MB | ~几百 MB |

---

## 三、根文件系统到底干什么？为什么必须有？

### 内核启动的最后一步

```
Bootloader → Kernel 初始化 → 挂载 rootfs → 执行 /sbin/init
                                                  ↑
                                          没有这个就 Kernel Panic
```

内核初始化完硬件之后，**必须**从根文件系统里找到并执行**第一个用户态进程**（PID 1）：

```c
// init/main.c 中的内核启动逻辑（简化）
static int __ref kernel_init(void *unused)
{
    // ... 内核初始化完成 ...

    // 尝试执行 init，顺序如下：
    if (ramdisk_execute_command)
        run_init_process(ramdisk_execute_command);
    if (execute_command)
        run_init_process(execute_command);

    run_init_process("/sbin/init");     // ← systemd 或 sysvinit
    run_init_process("/etc/init");
    run_init_process("/bin/init");
    run_init_process("/bin/sh");

    panic("No working init found.  Try passing init= option to kernel.");
    // ↑ 找不到 init 就 panic
}
```

### rootfs 里有啥？

```
rootfs_debian_arm64/
├── sbin/init          → /lib/systemd/systemd   # PID 1，第一个进程
├── bin/bash                                     # Shell
├── usr/bin/                                      # 用户命令 (ls, cat, …)
├── lib/aarch64-linux-gnu/                        # 动态库 (.so)
├── etc/passwd, etc/shadow                        # 用户账户
├── etc/systemd/                                  # systemd 配置
├── dev/                                          # 设备节点 (devtmpfs)
├── proc/, sys/, tmp/                             # 虚拟文件系统挂载点
└── lib/modules/$(uname -r)/                      # 内核模块 .ko
```

**内核只是一个"资源管理器"**，它不提供 shell、命令、编译器。这些全在 rootfs 里。

---

## 四、「实际开发中没有挂载根目录这一步」是真的吗？

**这是一个常见误解。** rootfs 不是"没有"，而是**被内核自动挂载了，你感知不到**。

### 内核命令行里藏着答案

```bash
# QEMU 的启动参数（来自 run_debian_arm64.sh）
root=/dev/vda rootfstype=ext4 rw
#  ↑ 这里就在指定根文件系统
```

实际开发中（比如嵌入式 Android/Linux 板子）：

```bash
# U-Boot 的 bootargs 环境变量
bootargs=console=ttyS0,115200 root=/dev/mmcblk0p2 rootwait rw
#                                           ↑ 根文件系统分区
```

### 真实的完整流程

```
开发板：
  U-Boot → 从 eMMC/mmcblk0p1 加载 kernel
         → 从 eMMC/mmcblk0p2 加载 dtb
         → bootargs 指定 root=/dev/mmcblk0p2  ← 这就是"挂载根目录"
         → kernel 读 bootargs，自动 mount

服务器/PC：
  GRUB → 从 /boot/vmlinuz 加载 kernel
       → 从 /boot/initrd.img 加载 initramfs（临时 rootfs）
       → kernel 启动，挂载 initramfs
       → initramfs 里的 init 加载磁盘驱动
       → pivot_root 切换到真正的磁盘 /dev/sda2  ← 这也是"挂载根目录"
```

**你"看不到"这个步骤，不是因为它不存在，而是它被自动化了。**

---

## 五、QEMU vs 真实硬件 启动流程完整对比

```
┌─────────────────────────────────────────────────────────────────────┐
│                        真实硬件 (ARM64 开发板)                        │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ① Boot ROM (SoC 内固化代码)                                        │
│     ├─ 初始化 DDR、时钟、最小外设                                    │
│     └─ 从 eMMC/SD/USB 加载 SPL/UBoot 到 SRAM                       │
│                                                                     │
│  ② SPL (Secondary Program Loader)                                   │
│     ├─ 初始化 DDR 控制器，让大容量 DDR 可用                          │
│     └─ 加载 U-Boot 到 DDR                                           │
│                                                                     │
│  ③ U-Boot (引导加载程序)                                             │
│     ├─ 初始化网卡、USB、存储等外设                                   │
│     ├─ 读取环境变量 (bootargs, bootcmd)                              │
│     ├─ 从存储加载 kernel Image 到内存 (0x80000)                     │
│     ├─ 从存储加载 DTB 到内存                                         │
│     └─ booti 0x80000 - 0x83000000  ← 跳转到内核                     │
│                                                                     │
│  ④ Kernel (Linux 内核)                                               │
│     ├─ 解压自身 (EFI stub / self-decompression)                     │
│     ├─ 初始化页表、异常向量、MMU、Cache                              │
│     ├─ setup_arch() → 解析 DTB → 发现外设                           │
│     ├─ 初始化驱动 (GIC, UART, eMMC, USB, …)                         │
│     ├─ 读 bootargs → root=/dev/mmcblk0p2                            │
│     ├─ 挂载根文件系统 (ext4 on eMMC)                                 │
│     └─ 执行 /sbin/init → systemd                                    │
│                                                                     │
│  ⑤ systemd (用户态)                                                  │
│     ├─ 从 /etc/systemd/ 读配置                                       │
│     ├─ 启动 getty@ttyS0, sshd, udevd, ...                           │
│     └─ 显示 login 提示符                                             │
│                                                                     │
│  DTB 来源: 编译时生成的 .dtb 文件 (与硬件匹配)                       │
│  存储介质: eMMC / SD 卡 / NAND Flash                                 │
│  console:  物理 UART 串口 (ttyS0, ttyAMA0)                           │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│                        QEMU (virt 机器)                              │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ① QEMU 自身                                                        │
│     ├─ 用户态模拟 SoC 硬件 (GICv3, UART, virtio-blk, …)             │
│     ├─ 用 -M virt 指定虚拟平台                                       │
│     └─ 用 -kernel 直接加载 Image（跳过了 Boot ROM / U-Boot）        │
│                                                                     │
│  ② QEMU 内置 Bootloader  (built-in)                                 │
│     ├─ QEMU 自带一个极简 loader                                      │
│     ├─ 把 Image 加载到虚拟内存                                       │
│     ├─ 自动生成 DTB（根据 -M -cpu -m 参数）                          │
│     └─ 跳转到内核入口 _text                                          │
│                                                                     │
│  ③ Kernel (Linux 内核)                                               │
│     ├─ 解压自身 (与真实硬件完全一致)                                  │
│     ├─ 初始化页表、异常向量、MMU、Cache                              │
│     ├─ setup_arch() → 解析 QEMU 生成的 DTB                          │
│     ├─ 初始化驱动 (virtio-blk, GICv3, PL011 UART, …)                │
│     ├─ 读 bootargs → root=/dev/vda rootfstype=ext4                  │
│     ├─ 挂载根文件系统 (ext4 on virtio 块设备)                        │
│     └─ 执行 /sbin/init → systemd                                    │
│                                                                     │
│  ④ systemd (用户态)                                                  │
│     ├─ 与真实硬件完全相同                                             │
│     ├─ 启动 getty@ttyAMA0, sshd, …                                  │
│     └─ 显示 login 提示符                                             │
│                                                                     │
│  DTB 来源: QEMU 运行时自动生成 (-M virt)                             │
│  "存储"介质: 宿主机的 .ext4 文件 (virtio-blk 模拟)                   │
│  console: 终端模拟 (nographic → 当前终端窗口)                         │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

## 关键差异对照表

| 阶段 | 真实硬件 | QEMU |
|------|---------|------|
| Boot ROM | SoC 内部掩膜 ROM 固化代码 | 无，QEMU 直接加载 |
| SPL | 在 SRAM 中运行，初始化 DDR | 无 |
| U-Boot / GRUB | 加载 kernel + DTB，传递 bootargs | QEMU 内置 mini-loader |
| DTB 来源 | 编译好的 `.dtb` 文件 | QEMU 根据 `-M` `-cpu` 运行时生成 |
| 存储 I/O | eMMC/SD/NVMe 控制器驱动 | virtio-blk 半虚拟化驱动 |
| 中断控制器 | GIC-500/GIC-600 物理 IP | QEMU 模拟 GICv3 |
| 串口 | PL011 物理 UART, 接 FTDI 转 USB | QEMU 输出到终端 stdout |
| **内核代码** | **完全一样** | **完全一样** |
| **init / systemd** | **完全一样** | **完全一样** |
| rootfs 挂载 | `root=/dev/mmcblk0p2` | `root=/dev/vda` |

## 核心结论

1. **QEMU 和真机共享同一段内核 C 代码**（arch/arm64/ 下），系统调用、驱动模型、内存管理完全一致
2. QEMU 跳过了 Boot ROM → SPL → U-Boot 这些**硬件相关的引导阶段**
3. Kernel 入口点之后（`start_kernel` → `rest_init` → `kernel_init` → `init`），**QEMU 和真机完全相同**
4. rootfs 无论真机还是 QEMU 都是**必须的**——内核需要从 rootfs 中找到 `/sbin/init` 来启动第一个用户进程
5. 你"看不到"挂载 rootfs 的步骤，是因为内核根据 `bootargs` 里的 `root=` 参数**自动执行了挂载**

## 本实验环境启动时序（一条命令背后发生了什么）

```bash
./run_debian_arm64.sh run
```

```
① QEMU 启动
   qemu-system-aarch64 -M virt -cpu max -kernel Image -drive file=rootfs.ext4 ...
   ↓
② QEMU mini-loader 把 Image 放到虚拟内存 0x40080000
   自动生成 virt 平台的 DTB，附加 bootargs
   ↓
③ 内核入口 (arch/arm64/kernel/head.S)
   ↓
④ start_kernel() → ... → kernel_init()
   ↓
⑤ 解析 bootargs → "root=/dev/vda rootfstype=ext4 rw"
   ↓
⑥ mount_root() → 挂载 rootfs_debian_arm64.ext4 → /dev/vda
   ↓
⑦ run_init_process("/sbin/init") → systemd
   ↓
⑧ systemd 读取 /etc/systemd/system/getty.target.wants/
   ↓
⑨ [  OK  ] Started Serial Getty on ttyAMA0.
   ↓
⑩ Debian GNU/Linux ... ttyAMA0
   benshushu login:  ← 你在操作这一步
```
