# AI 芯片底层软件工程师 — 入职准备计划

> 入职日期：2025 年 10 月 8 日  
> 当前水平：4 年 Linux 应用软件经验，本科 CS，有 OS/组成原理基础，无驱动经验  
> 目标：入职前达到"能独立读懂驱动代码 + 能在指导下完成简单驱动任务"的状态  

---

## 📅 整体时间线

| 阶段 | 时间 | 主题 |
|---|---|---|
| 第一阶段 | 9 月 1 日 – 9 月 14 日（Week 1~2） | Linux 内核基础 + 字符设备驱动 |
| 第二阶段 | 9 月 15 日 – 9 月 28 日（Week 3~4） | 平台总线 / 中断 / 设备树 |
| 第三阶段 | 9 月 29 日 – 10 月 5 日（Week 5~6） | AI 芯片底软专项 + 工具链 + 总结 |
| 冲刺 | 10 月 6 日 – 10 月 7 日 | 整体回顾 + 心态准备 |

---

## 第一阶段：Linux 内核基础 + 字符设备驱动

### Week 1（9/1 – 9/7）：内核基础概念重建

**目标**：建立对 Linux 内核的整体认知，补足理论缺口

**Day 1 – 2：内核架构概览**
- 阅读：《Linux 内核设计与实现》（LKD）前三章，或 Linux Kernel Documentation 入门
- 理解：内核空间 vs 用户空间、进程调度、内存管理、虚拟文件系统
- 任务：画一张内核子系统关系图（自己理解用，不用给别人看）

**Day 3 – 4：内核模块基础**
- 学会写一个最简单的内核模块：`init`/`exit`/`printk`
- 编译内核模块（用 `make -C /lib/modules/$(uname -r)/build M=$(pwd) modules`）
- 理解 `MODULE_LICENSE`、`EXPORT_SYMBOL` 的作用
- **动手**：写一个加载时打印 "Hello from kernel" 的模块

**Day 5：内核内存管理基础**
- 理解 `kmalloc`/`vmalloc`/`kzalloc` 的区别
- 理解 slab allocator 的基本概念
- 理解 `copy_from_user`/`copy_to_user`（驱动里最常用的安全边界函数）
- **动手**：在模块里用 `kzalloc` 分配内存，用 `copy_from_user` 实现一个简单系统调用封装

**Day 6 – 7：周期回顾**
- 画概念图：描述一个系统调用从用户态到内核态的完整路径
- 理解什么是上下文（process context vs interrupt context）

---

### Week 2（9/8 – 9/14）：字符设备驱动入门

**目标**：独立完成一个字符设备驱动的完整实现

**Day 8 – 9：字符设备框架**
- 理解 `struct file_operations`、`struct cdev`
- 学会用 `alloc_chrdev_region` / `register_chrdev` 注册设备号
- 理解 `/dev` 节点和 `udev` 机制
- **动手**：注册一个字符设备，实现 `open`/`release`/`read`/`write` 四个基本操作

**Day 10 – 11：ioctl 接口**
- 理解 ioctl 的设计思想（为什么需要它）
- 定义 `_IO`/`_IOR`/`_IOW`/`_IOWR` 宏
- 实现一个自定义 ioctl 命令（比如返回驱动版本号）
- 写一个对应的用户态测试程序调用这个 ioctl
- **动手**：在驱动里加 ioctl，在用户态用 C 程序调用它，验证通信正常

**Day 12 – 13：内核调试工具**
- `printk` + 日志级别（`dmesg` 查看）
- GDB 调试内核模块：`gdb vmlinux` + `add-symbol-file`
- `perf` / `ftrace` 入门（理解函数跟踪的基本用法）
- 内核配置：`CONFIG_DEBUG_INFO` + `CONFIG_KGDB`
- **动手**：用 GDB 在驱动里下一个断点，单步走一遍 `open` 的执行流程

**Day 14：第一阶段总结**
- 独立完成项目：实现一个完整字符设备驱动（含 ioctl + 用户态测试程序 + Makefile）
- 整理代码到 GitHub（作为正式作品支撑简历）
- 写一份简短的驱动设计文档（300 字以内）

---

## 第二阶段：平台总线 / 中断 / 设备树

### Week 3（9/15 – 9/21）：平台总线与设备树

**目标**：理解 AI 芯片中最常用的驱动模型

**Day 15 – 17：平台驱动（Platform Driver）**
- 理解 `platform_device` 和 `platform_driver` 的匹配机制
- 理解 `probe`/`remove` 函数的调用时机
- 理解 resource 管理（IORESOURCE_MEM、IORESOURCE_IRQ）
- **动手**：把第一阶段的字符设备改写成平台驱动，注册一个 platform driver

**Day 18 – 19：设备树（Device Tree）**
- 理解设备树的基本语法（.dts 文件）
- 理解 compatible、reg、interrupts 属性
- 学会在 QEMU 模拟环境里跑一个简单设备树（不用真实硬件）
- 理解设备树节点和 platform_device 的对应关系
- **动手**：为一个假设的"AI 加速器"设备编写简单的 .dts 节点

**Day 20 – 21：中断处理**
- 理解 Linux 中断框架：`request_irq`/`free_irq`
- 顶半部（Top Half）vs 底半部（Bottom Half）：tasklet、workqueue
- 理解 `irqreturn_t` 和中断处理函数的写法
- 理解线程化中断（threaded IRQ）
- **动手**：在平台驱动里注册一个中断处理函数，用 `tasklet` 实现底半部

---

### Week 4（9/22 – 9/28）：高级驱动概念 + 内核同步

**目标**：掌握驱动中必需的同步原语和进阶概念

**Day 22 – 23：Spinlock 与原子操作**
- 理解 spinlock 的实现原理（关中断 + CAS）
- 理解 spinlock vs mutex 的使用场景
- 理解 `atomic_t`、`atomic64_t` 原子操作
- **动手**：在驱动里用 spinlock 保护一个共享计数器，实现一个简单的 proc 接口读取

**Day 24 – 25：内存映射与 DMA**
- 理解 `mmap` 在驱动中的实现
- 理解 `remap_pfn_range` 和页表映射
- DMA 基本概念：`dma_alloc_coherent`/`dma_map_single`
- 理解缓存一致性问题（Cache Coherency）
- **动手**：为之前写的驱动实现 `mmap` 接口，用户态程序可以直接映射内核缓冲区

**Day 26 – 27：Workqueue 与定时器**
- `schedule_work` / `INIT_WORK` / `flush_work`
- `init_timer` / `add_timer` 内核定时器
- 理解底软中常用的"延时下半部"模式
- **动手**：在驱动里加入一个定时器，定期打印状态信息

**Day 28：第二阶段总结**
- 独立完成项目：实现一个"平台驱动 + 中断 + mmap + workqueue"的完整驱动
- 把代码整理好推到 GitHub
- 写设计文档

---

## 第三阶段：AI 芯片底软专项 + 工具链 + 总结

### Week 5（9/29 – 10/5）：AI 芯片专项 + 行业知识

**Day 29 – 30：AI 芯片架构基础**
- 了解 AI 芯片的基本组成：计算单元（PE Array）、DMA 引擎、存储子系统
- 理解 NPU/DSA 的基本编程模型：指令预取、计算图映射、Tensor 布局
- 阅读目标公司芯片的公开寄存器手册或技术博客（如果找得到）
- 理解"芯片 bring-up"的整体流程：silicon bring-up → 固件加载 → 驱动加载 → 基础验证

**Day 31 – 32：Linux 内核子系统的深度理解**
- 重点深入：DMA 子系统、IRQ 子系统、内存管理子系统
- 理解 Linux kernel DMA API 的演进（从老 API 到 `dmaengine` 框架）
- 理解 `iommu` 的概念（AI 芯片常用）

**Day 33 – 34：调试与问题定位**
- 理解 `crash` 工具（内核崩溃转储分析）
- 理解 `perf` 的使用：热点分析、缓存分析
- 理解 `eBPF` 跟踪（advanced but useful）
- 学会读内核 oops/panic 信息
- **动手**：制造一个故意的 bug（比如空指针解引用），看 oops 输出并分析

**Day 35 – 36：行业工具链**
- 了解 AI 框架侧（PyTorch/TensorFlow）如何调用底层加速器
- 理解用户态 SDK 的基本结构（通常封装为 .so）
- 理解固件（firmware）和驱动的边界与交互
- 如果公司用 RTOS（FreeRTOS/Zephyr），简单了解任务调度和中断模型

**Day 37 – 38：简历与面试准备**
- 把 GitHub 上的驱动项目整理成简历亮点
- 准备"没有驱动经验如何证明自己能学"的回答框架
- 模拟几个常见问题：
  - "描述一个你在应用层解决过的最复杂的技术问题"
  - "Linux 驱动和应用程序最大的区别是什么"
  - "中断处理的流程是什么"
- 准备一个"你对这个岗位的理解"的回答

---

### Week 6（10/6 – 10/7）：冲刺收尾

**Day 39 – 40：整体回顾**
- 把前 5 周学的所有概念过一遍，用自己的话写成笔记
- 整理一个"速查表"：常用内核 API、常见问题与排查方法
- 跑通一遍自己写的所有驱动代码，确保没有 warning
- 推送所有代码到 GitHub

**Day 41：心态与准备**
- 准备好入职要带的工具：笔记本（装好 Linux）、一本纸质的内核书
- 把第一天要问 team lead 的问题列出来：
  - "这个芯片的底软模块主要分哪几块？"
  - "当前最紧急的 deliverable 是什么？"
  - "驱动开发用的是什么内核版本？"
  - "有没有内部的驱动代码风格规范？"
- 准备好入职第一天的开发环境（Linux VM 或双系统）

---

## 📊 每日学习模板

```
[日期] Day X

✅ 今日目标：
   1. [具体可交付]
   2. [具体可交付]

📖 学习内容：
   - 主题：
   - 参考资料：

💻 实践：
   - 代码：
   - 实验结果：

❓ 问题记录（不懂的立刻记下来）：
   1.
   2.

📝 明日待办：
   -
```

---

## 🛠 推荐工具与环境

| 工具 | 用途 | 备注 |
|---|---|---|
| Ubuntu 22.04 LTS + kernel 6.x | 开发和测试环境 | 用 `apt` 源码装内核，或下载 prebuilt headers |
| QEMU + ARM vexpress | 免硬件模拟 ARM 环境 | 适合玩设备树和驱动 |
| GDB (gdbserver) | 调试内核模块 | 配合 kgdb 或 qemu gdb stub |
| Visual Studio Code + Remote SSH | 代码编辑 | 配合 ccls/LSP 读内核源码 |
| 《Linux Device Drivers》LDD3 | 经典教材 | 有中文版，章节按需阅读 |
| 《Linux Kernel Development》LKD | 理论补充 | Robert Love 著 |

---

## 🎯 验收标准

入职前达到以下状态就算合格：

- [ ] 能独立写一个字符设备驱动（含 ioctl + 用户态测试程序）
- [ ] 能理解 platform driver 和设备树的匹配原理
- [ ] 能看懂 `copy_from_user`、`kmalloc`、`request_irq` 等常见 API
- [ ] 有至少 2 个驱动项目在 GitHub 上（代码 + README）
- [ ] 能用自己的话解释：中断处理流程、系统调用从用户态到内核态的路径
- [ ] 知道 DMA 的基本概念和 `dma_alloc_coherent` 的用法
- [ ] 心态上做好"从应用层转到系统层"的准备

---

## ⚠️ 注意事项

1. **不要追求看完所有东西** — 内核太大，选定方向深入比广撒网更重要
2. **代码优先于理论** — 先让代码跑通，再去理解背后的原理
3. **遇到问题先自己搜** — 用 `linux-device-driver` `site:stackoverflow.com` 等关键词组合
4. **保存所有实验记录** — 过程文档比结果更重要，入职后可以作为内部分享的素材
5. **和你的 JD 保持对照** — 每个要求都是潜在面试题，每个工具都是潜在工作场景

---

*Plan generated on 2025-08-25. Good luck with the new job! 🚀*
