# 《奔跑吧Linux内核（第2版）卷2》ARM64 实验平台搭建指南

> 整理自原书 **第 3 章（292–317 页）** 与 **第 5 章（469–472 页）**。
> 凡书中原文步骤，标注页码并按原文给出命令；**书中没有写明的信息一律以「补充」标注来源，不编造**。
> 本指南的目标读者：要在自己的主机上一步一步照做的人。

---

## 0. 先看全景：这套环境是什么

```
你的真实电脑（Intel x86_64，Windows / Ubuntu 均可）
└─ 主机操作系统：Ubuntu Linux 20.04（书假设；Windows 需先装，见 1 节）
   └─ QEMU 4.2.0（软件模拟出一台 ARM64 电脑）
      └─ 模拟硬件：Cortex-A57 ×4、1GB 内存、virt 主板、虚拟磁盘、virtio 网卡
         └─ 你自己编译的 Linux 5.0 内核（ARM64，O0 优化，带调试符号）
            └─ Debian 用户空间（rootfs_debian.ext4：bash、apt、Kdump、SystemTap…）
               └─ 实验：insmod 内核模块 / GDB·Eclipse 单步调试 / Crash 宕机分析
```

**书中实验环境速查（292、470 页）：**

| 项目 | 书中配置 |
|---|---|
| 主机硬件 | Intel x86_64 处理器（5 章为 8GB 内存） |
| 主机系统 | Ubuntu Linux 20.04（5 章为 20.04.1） |
| 实验代码 | runninglinuxkernel_5.0（含作者脚本与 Debian ARM64 根文件系统） |
| QEMU | 4.2.0（`qemu-system-aarch64`） |
| 客户机内核 | Linux 5.0（ARM64） |
| 客户机系统 | Debian GNU/Linux buster/sid，登录 root 或 benshushu，密码 123 |
| 调试手段 | GDB 远程调试（1234 端口）、Eclipse 图形化调试 |

---

## 1. 准备主机（书假设 Ubuntu 20.04；Windows 用户先看这里）

- **书原文假设**：你已经有一台装 Linux 发行版的主机（292 页"只需要一台装了 Linux 发行版的计算机即可"），推荐 Ubuntu Linux 20.04。
- **补充（针对 Windows 主机）**：如果直接在这台 Windows 电脑上做，需要先获得一个 Ubuntu 20.04 环境，三选一：
  1. VMware / VirtualBox 里装 Ubuntu 20.04 虚拟机（最贴近书中体验，推荐）；
  2. WSL2 里装 Ubuntu（QEMU 的 `-nographic` 串口模式可用，但 9p 共享、图形界面体验可能有差异）；
  3. 一台现成的 Ubuntu 服务器/电脑。
- **磁盘**：书中 298 页明确要求主机至少有 **10GB 空余磁盘**（脚本用 `dd` 生成 8GB 镜像）。
- **内存**：虚拟机分配 1GB（书中命令 `-m 1024`），主机建议 ≥8GB（470 页按 8GB 配置）。

---

## 2. 安装工具（296 页）

```bash
$ sudo apt-get install qemu libncurses5-dev gcc-aarch64-linux-gnu build-essential git bison flex libssl-dev qemu-system-arm
```

安装完成后检查 QEMU 版本：

```bash
$ qemu-system-aarch64 --version
QEMU emulator version 4.2.0
Copyright (c) 2003-2019 Fabrice Bellard and the QEMU Project developers
```

- **补充（版本差异）**：在较新的 Ubuntu（22.04 之后）上，`libncurses5-dev` 可能已改名，用 `libncurses-dev` 代替；其他包一般仍存在。QEMU 版本高于 4.2.0 一般也能用，但启动输出可能略有不同。

---

## 3. 下载代码仓库（296–297 页）

书中原文只有两句（未印出完整 URL）：

```bash
$ cd runninglinuxkernel_5.0
$ git checkout rlk_5.0
```

- **补充（仓库地址，来自网络公开资料）**：异步社区图书页给出的本书配套源码下载地址为：
  - GitHub：`https://github.com/figozhang/runninglinuxkernel_5.0`
  - 码云镜像：`https://benshushu.coding.net/public/runninglinuxkernle_5.0/runninglinuxkernel_5.0/git/files`
  - 另一处资料提到 `https://github.com/runninglinuxkernel/runninglinuxkernel_5.0`
  - 若以上地址失效，以仓库 README 或"奔跑吧Linux社区"公众号发布的信息为准。
- 所以完整操作是（补充）：

```bash
$ git clone https://github.com/figozhang/runninglinuxkernel_5.0.git
$ cd runninglinuxkernel_5.0
$ git checkout rlk_5.0
```

- 仓库里应包含 `rootfs_debian_arm64.tar.xz`（296 页提到"目录下面有一个 rootfs_debian_arm64.tar.xz 文件"），以及作者写好的 `run_debian_arm64.sh` 脚本。

---

## 4. 编译内核并制作根文件系统（298–299 页）

整体流程（书中 298 页列出的 6 步）：**编译内核 → 编译内核模块 → 安装内核模块 → 安装内核头文件 → 安装编译内核模块必须依赖的文件 → 制作 ext4 根文件系统**。

作者已把这些步骤封装进脚本：

```bash
# 1) 编译内核（不需要管理员权限）
$ cd runninglinuxkernel_5.0
$ ./run_debian_arm64.sh build_kernel

# 2) 编译根文件系统（需要管理员权限）
$ sudo ./run_debian_arm64.sh build_rootfs
```

注意（298–299 页原文要点）：

- **编译内核需要几十分钟**，取决于主机算力。
- `rootfs_debian_arm64.tar.xz` 只是**半成品根文件系统**，必须经过上面 6 步（安装内核镜像、模块、头文件等）才能用。
- 编译根文件系统**需要管理员权限**（`sudo`），编译内核不需要。
- 执行完成会生成 **`rootfs_debian.ext4`** 根文件系统镜像。
- 脚本会用 `dd` 生成 **8GB** 镜像，因此**主机至少要有 10GB 空余磁盘**；想改大镜像可以修改 `run_debian_arm64.sh`。

**关于编译优化等级（294–295 页，重要）**：

- Linux 内核默认用 **O2** 优化等级编译。用 O2 编译的内核做 GDB 单步调试会出现**光标乱跳、变量显示 `<optimized out>`**（书中图 3.1 有演示）。
- 解决：把 linux-5.0 根目录 Makefile 里的 `O2` 改成 **O0**（关闭优化），但"这样编译会有问题，我们为此做了一些修改"——**作者在仓库里已处理**，直接用 `run_debian_arm64.sh build_kernel` 即可。
- **诚实提醒**：O0 编译会显著降低内核运行性能，**只为了单步调试而用**，生产/性能测试场景不要这么做（294–295 页原文强调）。

---

## 5. 启动 Debian 系统（300–301 页）

方式一（用脚本，推荐）：

```bash
$ ./run_debian_arm64.sh run
```

方式二（书中给出的完整 QEMU 命令）：

```bash
$ qemu-system-aarch64 -m 1024 -cpu cortex-a57 -smp 4 -M virt \
  -bios QEMU_EFI.fd -nographic \
  -kernel arch/arm64/boot/Image \
  -append "noinitrd root=/dev/vda rootfstype=ext4 rw crashkernel=256M" \
  -drive if=none,file=rootfs_debian.ext4,id=hd0 \
  -device virtio-blk-device,drive=hd0 \
  --fsdev local,id=kmod_dev,path=./kmodules,security_model=none \
  -device virtio-9p-device,fsdev=kmod_dev,mount_tag=kmod_mount
```

**参数含义（供理解，书中散见各页）**：

| 参数 | 含义 |
|---|---|
| `-m 1024` | 给虚拟机 1GB 内存 |
| `-cpu cortex-a57` | 模拟 Cortex-A57（ARM64）CPU |
| `-smp 4` | 4 核 |
| `-M virt` | 使用 virt 虚拟主板 |
| `-bios QEMU_EFI.fd` | UEFI 固件（见下方补充） |
| `-nographic` | 无图形界面，串口输出到当前终端 |
| `-kernel arch/arm64/boot/Image` | 加载编译出的 ARM64 内核镜像 |
| `-append "… crashkernel=256M"` | 内核启动参数；`crashkernel=256M` 是为第 5 章 Kdump 预留的捕获内存 |
| `-drive … rootfs_debian.ext4` | 把根文件系统镜像挂为虚拟硬盘 |
| `-device virtio-blk-device` | 虚拟磁盘用 virtio-blk 驱动（即 `root=/dev/vda`） |
| `--fsdev … ./kmodules` + `virtio-9p-device` | 通过 9p 把主机 `kmodules` 目录共享给虚拟机（挂载点 `/mnt`） |

启动后会看到内核启动日志，最终出现 `Welcome to Debian GNU/Linux buster/sid!` 和登录提示。

- **补充（QEMU_EFI.fd 来源，书中未说明）**：`QEMU_EFI.fd` 是 AArch64 虚拟机的 EDK2 UEFI 固件。常见获取方式：
  ```bash
  $ sudo apt install qemu-efi-aarch64    # 安装后位于 /usr/share/qemu-efi-aarch64/QEMU_EFI.fd
  ```
  也可以从 Linaro 快照站下载预编译版本。如果仓库/脚本自带，优先用仓库里的。

**登录与关闭（300–302 页）**：

- 用户名：`root` 或 `benshushu`；密码：`123`。
- 关闭虚拟机：在 Linux 主机的另一个终端里 `killall qemu-system-aarch64`，或在 QEMU 串口里按 `Ctrl+A` 再按 `X`。
- 第一次启动稍慢，耐心等登录提示。

---

## 6. 网络与在线安装软件包（302–303 页）

QEMU 用 **Virtio-net** 生成虚拟网卡，通过 **NAT** 和主机共享网络。登录后查看网络：

```bash
root@benshushu:~# ifconfig
# 会看到 enp0s1，inet 10.0.2.15（NAT 内部 IP）
# 以及回环 lo 127.0.0.1
```

更新软件仓库：

```bash
root@benshushu:~# apt update
```

**如果更新失败**（常见原因是虚拟机系统时间太旧，书中原文）：

```bash
root@benshushu:~# date -s 2019-04-25   # 书中示例；实际用"当天日期"
```

然后就可以在线安装软件包了，例如：

```bash
root@benshushu:~# apt install gcc
root@benshushu:~# apt install build-essential
```

---

## 7. 主机与虚拟机共享文件（304–305 页）

前提：QEMU 虚拟机和主机的 Linux 内核都使能 **NET_9P** 内核模块（书中说明；本书提供的内核已配置）。

测试方法（书中原文步骤）：

```bash
# 主机上：复制一个文件到共享目录
$ cp test.c runninglinuxkernel_5.0/kmodules

# 虚拟机里：检查 /mnt 下能否看到
root@benshushu:/# cd /mnt
root@benshushu:/mnt# ls
README  test.c
```

后续实验经常用到这个特性：把编译好的内核模块（.ko）或模块源码放进 QEMU 虚拟机。

---

## 8. 编译内核模块：两种方式（304–307 页）

### 方式 A：主机交叉编译（304–305 页）

在主机上为 ARM64 交叉编译，再把 .ko 共享进虚拟机：

```bash
$ cd hello_world                    # 内核模块源码目录
$ export ARCH=arm64
$ export CROSS_COMPILE=aarch64-linux-gnu-
$ make
```

然后把 `test.ko` 放到共享目录：

```bash
$ cp test.ko runninglinuxkernel_5.0/kmodules
```

> **诚实提醒**：书中 305 页原文写的是 `$scp test.ko runninglinuxkernel_5.0/kmodules`。`scp` 是远程复制命令，目标是本地目录，这更像书的笔误；**本地复制用 `cp` 即可**。

虚拟机里加载：

```bash
root@benshushu:/mnt# insmod test.ko
```

### 方式 B：虚拟机内本地编译（306–307 页）

```bash
# 1) 先安装编译工具
root@benshushu:~# apt install build-essential

# 2) 编译内核模块时指定虚拟机本地内核路径
#    BASEINCLUDE 指向 /lib/modules/$(uname -r)/build，它是一个链接文件，
#    通常指向已编译过的内核源码路径（如 /usr/src/linux）
BASEINCLUDE ?= /lib/modules/$(shell uname -r)/build

# 3) 编译并加载
root@benshushu:/mnt/hello_world# make
root@benshushu:/mnt/hello_world# insmod test.ko
```

---

## 9. GDB 命令行单步调试内核（308–309 页）

先安装多架构 GDB：

```bash
$ sudo apt install gdb-multiarch
```

方式一：用脚本启动（QEMU 加调试参数）：

```bash
$ ./run_debian_arm64.sh run debug
```

脚本实际执行的命令（308 页）：

```bash
$ qemu-system-aarch64 -m 1024 -cpu cortex-a57 -M virt -nographic \
  -kernel arch/arm64/boot/Image \
  -append "noinitrd sched_debug root=/dev/vda rootfstype=ext4 rw crashkernel=256M loglevel=8" \
  -drive if=none,file=rootfs_debian_arm64.ext4,id=hd0 \
  -device virtio-blk-device,drive=hd0 \
  --fsdev local,id=kmod_dev,path=./kmodules,security_model=none \
  -device virtio-9p-pci,fsdev=kmod_dev,mount_tag=kmod_mount \
  -S -s
```

两个关键参数（308 页原文）：

- **`-S`**：QEMU 冻结 CPU，直到远程 GDB 输入控制命令（否则内核会直接启动跑完）。
- **`-s`**：在 **1234 端口**接受 GDB 的调试连接。

> **诚实提醒**：308 页命令里的盘符写的是 `rootfs_debian_arm64.ext4`，而 300 页生成/使用是 `rootfs_debian.ext4`，书中前后不一致；**以你实际生成的文件名（rootfs_debian.ext4）为准**，若脚本生成的叫别的名字，看 `ls` 结果。

在**另一个终端**启动 GDB：

```bash
$ cd runninglinuxkernel_5.0
$ gdb-multiarch --tui vmlinux
(gdb) set architecture aarch64
(gdb) target remote localhost:1234
(gdb) b start_kernel
(gdb) c
```

GDB 接管内核运行，停在 `start_kernel` 断点后即可单步调试（书中图 3.3）。

---

## 10. Eclipse 图形化调试内核（可选，310–311 页）

背景：GDB 是命令行方式，想要图形化界面可用 Eclipse（跨平台开源 IDE，最初用于 Java，支持 C/C++ 等）。

1. **安装 Java 运行环境 + Eclipse CDT**：
   ```bash
   $ sudo apt install openjdk-13-jre
   ```
   从 Eclipse CDT 官网下载 x86_64 Linux 版压缩包，解压运行；`Help → About Eclipse` 可查版本（书中图 3.4）。

2. **创建项目**：`Window → Open Perspective → C/C++` → `File → New → Project` → 选 **Makefile Project with Existing Code**（书中图 3.5），项目名如 `rlk_5.0`，代码路径指向 `runninglinuxkernel_5.0`。

3. **配置调试**：`Run → Debug Configurations`，新建 **C/C++ Attach to Application**（书中图 3.6–3.8）：
   - **Main 标签页**：Project 选刚建的项目；C/C++ Application 选带符号表的 `vmlinux`；勾选 **Disable auto build**。
   - **Debugger 标签页**：Debugger 选 `gdbserver`；GDB debugger 填 `gdb-multiarch`。
     - **诚实提醒**：书中正文写"输入 aarch64-linux-gnu-gdb"，但配图（图 3.7）里显示的是 `gdb-multiarch`，两处不一致；**建议用 gdb-multiarch**（前面已安装）。
   - **Connection 子标签页**：Host name 填 `localhost`，Port 填 `1234`。

4. **运行**：先在一个终端 `./run_debian_arm64.sh run debug` 启动 QEMU；然后在 Eclipse 点 `Run → Debug History`（或快捷栏"小昆虫"图标，书中图 3.9）。

5. 在 Eclipse 的 **Debugger Console** 中输入：
   ```
   (gdb) file vmlinux
   (gdb) b start_kernel
   (gdb) c
   ```
   Eclipse 会把参数、局部变量、数据结构自动显示在 Variables 页（书中图 3.11），比纯命令行直观。

---

## 11. 单步调试 head.S（312–317 页）——理解"为什么只能停到 MMU 之后"

**现象（312 页）**：在 head.S 的 `stext`、`preserve_boot_args`、`create_page_tables`、`__cpu_setup`、`__primary_switch`、`enable_mmu`、`__primary_switched`、`start_kernel` 等函数都设断点，运行后 GDB **只停在 `__primary_switched`**（它在 `enable_mmu` 之后）——即**只能调试启动 MMU 之后的代码**。

**原因（313–315 页）**：涉及三个地址概念：

| 概念 | 含义 |
|---|---|
| 加载地址 | 代码存放的物理地址（ARM64 上电从 0x0 开始取指令） |
| 运行地址 | 程序实际运行时的地址 |
| 链接地址 | 编译链接时指定的地址（`objdump` 反汇编看到的地址；不管程序在哪运行都不变） |

- 链接地址与运行地址可以相同也可以不同：U-Boot 太大放不进 SRAM，只能放 DDR（链接地址设为 DDR，如 0x4000 0000），上电却从 0x0 运行，于是运行地址≠链接地址。
- **位置无关代码**（BL、B、MOV，基于 PC 相对偏移）在哪运行都能跑；**位置有关代码**（如 `ldr pc, =on_sdram` 绝对跳转）不行。
- 通过 LDR 绝对跳转到链接地址执行，就叫**重定位**。U-Boot 的重定位见书中图 3.14。
- Linux 内核同样需要重定位：内核映像被 U-Boot 放在物理地址 0x4008 0000，但 vmlinux 里 `stext` 的链接地址是虚拟地址（如 0xFFFF 0000 11A6 0000）。内核启动汇编在 `__primary_switch` 中启动 MMU 后，用 `ldr x8, =__primary_switched` 取链接地址、`br x8` 跳转，完成重定位（315 页、图 3.15）。**所以 MMU 开启前（还在物理地址运行时），GDB 的虚拟地址断点不生效。**

**解决办法（316–317 页）**：用 `add-symbol-file` 把 vmlinux 各段按**物理地址**加载符号表，再手动把 PC 设到物理地址：

```bash
# 终端 1：启动 QEMU + GDB（-S -s）
$ ./run_debian_arm64.sh run debug

# 终端 2：GDB
$ gdb-multiarch --tui
(gdb) set architecture aarch64
(gdb) target remote localhost:1234
(gdb) add-symbol-file vmlinux 0x40081000 -s .head.text 0x40080000 -s .init.text 0x41a60000 -s .rodata 0x41730000
(gdb) set $pc=0x41a60000
(gdb) b stext
(gdb) c
```

**这些地址怎么来的（316–317 页推导过程，别照搬！）**：

- QEMU 虚拟机上 **DDR 起始地址 = 0x4000 0000**（书中原文）。
- 内核链接脚本 `arch/arm64/kernel/vmlinux.lds.S`：`KIMAGE_VADDR = 0xFFFF 0000 1000 0000`，`TEXT_OFFSET = 0x80000`。
- 用 `aarch64-linux-gnu-readelf -S vmlinux` 查各段虚拟地址与偏移（书中图 3.16），然后 **物理起始地址 = 0x4000 0000 + 段偏移**：

| 段 | 虚拟地址 | 物理偏移 | 物理起始地址 |
|---|---|---|---|
| .head.text | 0xFFFF 0000 1008 0000 | 0x8 0000 | 0x4008 0000 |
| .text | 0xFFFF 0000 1008 1000 | 0x8 1000 | 0x4008 1000 |
| .rodata | 0xFFFF 0000 1173 0000 | 0x173 0000 | 0x4173 0000 |
| .init.text | 0xFFFF 0000 11A6 0000 | 0x1A6 0000 | 0x41A6 0000 |

> **书中 317 页原文提醒（诚实转述）**："注意，上述地址可能随着内核配置或者编译选项的不同有所不同，需要参考具体的分析方法来推导，不要直接照搬。"

---

## 12. 搭建 Kdump + Crash 实验环境（469–472 页）

**为什么不用真实 ARM 板（470 页原文要点）**：ARM 处理器在 PC/服务器领域用得少；ARM 公司只卖授权不卖芯片；流行的树莓派 3B+ 对 Kdump 支持不够好，不能直接当实验平台。所以本书用 **QEMU 虚拟机 + Debian 平台**。

**实验环境（470 页）**：主机 Ubuntu Linux 20.04.1、Intel 处理器、8GB 内存；QEMU 版本 4.2.0；内核版本 Linux 5.0。**前置知识（470 页）**：ARM64 寄存器与函数调用规则、QEMU+Debian 平台搭建方法、Kdump 和 Crash 工具的使用方法。

**步骤 1：确认 Kdump 服务已就绪（471 页）**

本书提供的 Debian 平台**已经配置好 Kdump 服务**，无需手动装：

```bash
root@benshushu:~# systemctl status kdump-tools
# 显示 active（active (exited)）即表示 Kdump 服务启动成功
```

> **诚实提醒**：第一次运行平台要**等几分钟**——QEMU 虚拟机里启动 Kdump 服务比较慢（471 页原文）。

**步骤 2：编译一个宕机案例内核模块（471 页）**

书中说"案例代码参见 4.5 节"——4.5 节不在你指定的页码范围内，**本文档不展开**；案例模块源码请查阅书中第 4 章 4.5 节或仓库样例。

**步骤 3：触发宕机（471 页）**

加载模块触发崩溃，输出里会出现：

```
[ 466.809921] Starting crashdump kernel...
[ 466.809892] Bye!
```

**步骤 4：自动转储（472 页）**

进入捕获内核后，自动调用 `makedumpfile` 进行内核信息转储（如 `makedumpfile -c -d 31 /proc/vmcore /var/crash/<日期>/dump…`），转储完成后自动重启生产内核（书中图 5.3）。

**步骤 5：用 Crash 分析（472 页）**

```bash
# 1) 主机上：把带调试符号的 vmlinux 复制到共享目录（虚拟机 /mnt 可见）
$ cp vmlinux runninglinuxkernel_5.0/kmodules

# 2) 虚拟机里：进入转储目录（以日期命名）
root@benshushu:/var/crash# ls
201904221429  kexec_cmdline

# 3) 用 crash 加载内核转储文件
root@benshushu:/var/crash/201904221429# crash dump.201904221429 /mnt/vmlinux
```

crash 会输出 KERNEL / DUMPFILE / CPUS / MEMORY / PANIC 等信息（书中图 5.3 后的输出示例，PANIC 为 "Unable to handle kernel NULL pointer dereference…"）。

> **书中原文说明（472 页）**：转储目录**以日期命名，这一点和 CentOS 操作系统略有不同**。（这就是全书提到 CentOS 的地方。）

---

## 13. 常见问题与诚实说明（汇总）

1. **Windows 主机怎么跟**：书的一切命令都是 Ubuntu 语法（apt、sudo）。Windows 上请先按第 1 节准备 Ubuntu 20.04 环境（虚拟机/WSL2/服务器）。
2. **版本旧**：书的生态是 2020 年的（Ubuntu 20.04、QEMU 4.2.0、内核 5.0、老 GCC）。2026 年现在的新系统上，apt 包名可能有变（如 `libncurses5-dev`→`libncurses-dev`）；QEMU 新版通常兼容。
3. **仓库/脚本是作者的"黑盒"**：`run_debian_arm64.sh` 内部具体做了什么（包括 O0 修改、QEMU_EFI.fd 路径、模块安装细节），**书中没有完整列出**；卡住时以仓库 README、脚本内容为准，必要时到"奔跑吧Linux社区"（微信公众号）找镜像或答疑。
4. **书中前后不一致处已如实标注**：`scp` 应为 `cp`；308 页盘符 `rootfs_debian_arm64.ext4` 与 300 页 `rootfs_debian.ext4` 不一致；Eclipse 章节正文 `aarch64-linux-gnu-gdb` 与配图 `gdb-multiarch` 不一致。**遇到不一致，按本文"建议"栏执行。**
5. **地址别照搬**：head.S 调试的物理地址依赖内核配置与编译选项，必须自己用 readelf 推导（317 页原文提醒）。
6. **O0 只用于调试**：性能低，正式场景用默认 O2。
7. **Kdump 首次启动慢**：等几分钟属正常。
8. **网络/时间**：apt 失败先 `date -s` 修正时间（书中的 2019-04-25 是示例，用当天日期）。

---

## 附录：信息来源

- 原书页码：292–317 页（第 3 章：打造 ARM64 实验平台 / 单步调试），469–472 页（第 5 章：搭建 Kdump 实验环境）。
- 仓库下载地址（补充）：异步社区图书页 https://labs.epubit.com/bookDetails?id=UBbf36922cbf9a （给出 github.com/figozhang/runninglinuxkernel_5.0 与 coding.net 镜像）
- QEMU_EFI.fd 来源（补充）：Debian/Ubuntu 的 `qemu-efi-aarch64` 软件包（kernel.org 的 qemu-arm64 howto：https://www.kernel.org/pub/linux/kernel/people/will/docs/qemu/qemu-arm64-howto.md ）
- 其余所有命令与步骤均来自上述书页原文，未做编造；书中未说明之处均已加「补充」并注明依据。
