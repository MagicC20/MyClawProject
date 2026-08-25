# 12. DMA 与内存映射

> AI 芯片驱动里 80% 的数据传输都是 DMA。  
> CPU 不参与数据搬运，效率比 memcpy 高一个数量级。

---

## DMA 是什么

**DMA（Direct Memory Access）**：硬件直接在设备和内存之间搬数据，CPU 不参与。

```
传统方式（CPU 参与）：
  设备 ──→ CPU ──→ 内存
       ↑    ↑
    中断  拷贝

DMA 方式（CPU 不参与）：
  设备 ──────→ 内存
       ↑
    DMA 引擎
    CPU：开始 DMA 后去干别的，等中断
```

为什么重要：
- CPU 不被数据传输占用，能做别的
- 大块数据传输效率高
- AI 芯片必备（模型权重几十 MB ~ 几 GB）

---

## DMA 映射：核心问题

DMA 引擎只能访问**物理地址**。但内核用的是**虚拟地址**。

```
内核视角：CPU 用虚拟地址 vaddr 访问数据
硬件视角：DMA 引擎用物理地址 paddr 访问数据

必须把 vaddr ↔ paddr 的映射关系告诉 DMA 引擎
```

这就是"DMA 映射"——告诉硬件"数据在这个物理地址"。

---

## 一致性 DMA 映射 vs 流式 DMA 映射

| 类型 | API | 何时用 |
|---|---|---|
| **一致性 DMA**（coherent）| `dma_alloc_coherent` | 长期映射，设备和 CPU 都反复访问（如描述符环形缓冲区）|
| **流式 DMA**（streaming）| `dma_map_single` / `dma_map_page` | 单次传输，CPU 只访问一次（如大块数据搬运）|

---

## 一致性 DMA：dma_alloc_coherent

```c
// 🔴 内核态
#include <linux/dma-mapping.h>

void *dma_alloc_coherent(struct device *dev, size_t size,
                         dma_addr_t *dma_handle, gfp_t gfp);
void dma_free_coherent(struct device *dev, size_t size,
                       void *cpu_addr, dma_addr_t dma_handle);
```

返回值：
- 函数返回值 `void *cpu_addr`：CPU 用的虚拟地址
- 输出参数 `dma_addr_t *dma_handle`：硬件用的 DMA 地址（物理地址或 IO 地址）

```c
// 🔴 内核态
struct mychip_priv {
    void *buf_cpu;        // CPU 用的地址
    dma_addr_t buf_dma;   // 硬件用的 DMA 地址
    size_t buf_size;
};

static int mychip_probe(struct platform_device *pdev)
{
    struct mychip_priv *priv;

    // 分配 4KB 一致性 DMA 缓冲区
    priv->buf_size = 4096;
    priv->buf_cpu = dma_alloc_coherent(&pdev->dev, priv->buf_size,
                                       &priv->buf_dma, GFP_KERNEL);
    if (!priv->buf_cpu)
        return -ENOMEM;

    // 现在可以用两种方式访问：
    //   CPU：  priv->buf_cpu
    //   DMA：  priv->buf_dma
    return 0;
}

static int mychip_remove(struct platform_device *pdev)
{
    struct mychip_priv *priv = platform_get_drvdata(pdev);
    dma_free_coherent(&pdev->dev, priv->buf_size,
                      priv->buf_cpu, priv->buf_dma);
    return 0;
}
```

### 一致性 DMA 的特性

- **缓存一致性**：CPU 和设备看到的数据始终一致（硬件处理 cache）
- **开销大**：分配时会做 cache flush 等操作
- **适合**：描述符环形缓冲区、控制结构（频繁读写的小结构）

---

## 流式 DMA：dma_map_single / dma_unmap_single

```c
// 🔴 内核态
#include <linux/dma-mapping.h>

dma_addr_t dma_map_single(struct device *dev, void *cpu_addr,
                          size_t size, enum dma_data_direction dir);
void dma_unmap_single(struct device *dev, dma_addr_t dma_addr,
                      size_t size, enum dma_data_direction dir);

// dma_data_direction
enum dma_data_direction {
    DMA_BIDIRECTIONAL = 0,
    DMA_TO_DEVICE    = 1,   // CPU → 设备
    DMA_FROM_DEVICE  = 2,   // 设备 → CPU
    DMA_NONE         = 3,
};
```

### 典型用法（一次传输）

```c
// 🔴 内核态
static void mychip_send(struct device *dev, void *data, size_t len)
{
    dma_addr_t dma_addr;
    int ret;

    // 1. 映射（CPU 地址 → DMA 地址）
    dma_addr = dma_map_single(dev, data, len, DMA_TO_DEVICE);
    ret = dma_mapping_error(dev, dma_addr);
    if (ret) {
        dev_err(dev, "DMA map failed\n");
        return;
    }

    // 2. 告诉硬件（写 DMA 地址到寄存器）
    iowrite32((u32)dma_addr, regs + 0x300);     // DMA 目的地址
    iowrite32(len, regs + 0x304);                // 长度
    iowrite32(0x1, regs + 0x308);                // 启动

    // 3. 等 DMA 完成（用中断或轮询）

    // 4. 反向同步（CPU 重新看到设备可能写过的内存）
    dma_unmap_single(dev, dma_addr, len, DMA_TO_DEVICE);
}
```

### 流式 DMA 的关键约束

⚠️ **必须** `map` → 使用 → `unmap`，配对使用！
⚠️ `unmap` 之前**不能**访问被 map 的内存（CPU 缓存可能是脏的）
⚠️ 大小必须 page 对齐（如果需要不分页，传 `DMA_ATTR_NO_KERNEL_MAPPING`）

### 单次 vs 分散-聚集 DMA

如果数据分散在多个不连续的缓冲区：

```c
// 方式 1：循环 dma_map_single 多次（多次启动 DMA）
// 方式 2：sg_dma API（一次启动，硬件自动跳多个缓冲区）

#include <linux/scatterlist.h>

struct scatterlist sg[4];
// 填充 sg 表
sg_init_table(sg, 4);

// 一次 map
nents = dma_map_sg(dev, sg, 4, DMA_TO_DEVICE);

// 一次启动
iowrite32(sg_dma_address(&sg[0]), regs + 0x300);
iowrite32(sg_dma_len(&sg[0]), regs + 0x304);
// 硬件自动处理 sg 链
```

---

## 缓存一致性（Cache Coherency）

### 问题来源

CPU 有缓存（cache），设备可能直接写主存（或 DMA 到主存）。两者可能不一致。

```
时间线：
  T1: CPU 写 data = 0x1234（写到 cache，未必到主存）
  T2: DMA 引擎搬 data 到设备（可能搬的是主存旧值 0x5678！）
  T3: 设备读到的数据是错的
```

### 内核提供的同步函数

```c
// 🔴 内核态
void dma_sync_single_for_cpu(struct device *dev, dma_addr_t dma_addr,
                             size_t size, enum dma_data_direction dir);
void dma_sync_single_for_device(struct device *dev, dma_addr_t dma_addr,
                                 size_t size, enum dma_data_direction dir);

// 范围同步
void dma_sync_sg_for_cpu(...);
void dma_sync_sg_for_device(...);
```

```c
// 例子：DMA 后 CPU 读
dma_sync_single_for_cpu(dev, dma_addr, len, DMA_FROM_DEVICE);
// 现在 CPU 读到的数据是最新的
```

### 什么时候需要 sync？

- **一致性 DMA**：不需要（硬件处理）
- **流式 DMA**：必须在 unmap 前 sync（如果你要在 unmap 后 CPU 访问）

---

## mmap：让用户态直接访问设备内存

有时数据**很大**（几十 MB），拷贝代价高。可以用 mmap 让用户进程直接访问设备缓冲区：

```c
// 🔴 内核态 — mmap 实现
#include <linux/mm.h>

static int mychip_mmap(struct file *filp, struct vm_area_struct *vma)
{
    struct mychip_priv *priv = filp->private_data;
    unsigned long pfn;
    size_t size = vma->vm_end - vma->vm_start;

    if (size > priv->buf_size)
        return -EINVAL;

    // 把内核缓冲区映射到用户空间
    pfn = virt_to_phys(priv->buf_cpu) >> PAGE_SHIFT;

    if (remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot))
        return -EAGAIN;

    return 0;
}
```

用户态：

```c
// 🟢 用户态
void *ptr = mmap(NULL, BUF_SIZE, PROT_READ | PROT_WRITE,
                 MAP_SHARED, fd, 0);
// ptr 直接指向设备缓冲区，零拷贝

memcpy(ptr, source_data, BUF_SIZE);  // 直接写到设备缓冲区

munmap(ptr, BUF_SIZE);
```

### mmap 的两种实现方式

1. **remap_pfn_range**：直接映射内核已有缓冲区（上面例子）
2. **vm_insert_page**：逐页插入，更灵活

⚠️ mmap 不适用于所有场景：
- 设备有缓存一致性问题（需 cache flush）
- 多进程映射同一个设备时要小心

---

## AI 芯片的 DMA 实战模式

### 模式 1：模型权重加载

```
用户态 SDK
  ↓
分配大块内存（几十 MB 模型参数）
  ↓
dma_map → 得到 dma_addr
  ↓
告诉硬件：把 dma_addr 处的数据搬到芯片内部 SRAM
  ↓
等中断
  ↓
dma_unmap
```

### 模式 2：推理输入输出

```
用户态
  ↓
mmap 输入缓冲区 → 直接写入（CPU 视角的指针）
  ↓
启动计算（CPU 写寄存器命令）
  ↓
硬件 DMA 把输入搬到芯片
  ↓
硬件计算
  ↓
硬件 DMA 把输出搬到 mmap 的输出缓冲区
  ↓
用户态读输出
```

### 模式 3：多通道 DMA（环形描述符）

```c
// 描述符环形缓冲区（一致性 DMA）
struct dma_desc {
    dma_addr_t src;
    dma_addr_t dst;
    u32 len;
    u32 flags;     // 中断使能、链尾等
};

// 一致性 DMA 分配多个描述符
descs_cpu = dma_alloc_coherent(dev, sizeof(struct dma_desc) * N,
                               &descs_dma, GFP_KERNEL);

// 告诉硬件描述符环
iowrite32(descs_dma, regs + 0x400);          // 描述符基址
iowrite32(N, regs + 0x404);                   // 描述符数量
iowrite32(0x1, regs + 0x408);                 // 启动

// ISR 中推进 ring head
// 当 head == tail 时，所有描述符用完
```

---

## IOMMU 和 DMA 域

现代 SoC 通常有 IOMMU（I/O MMU）：

```
没有 IOMMU：
  CPU 虚拟地址 → 物理地址 → DMA 直接访问物理地址
                ↑
            地址必须连续

有 IOMMU：
  CPU 虚拟地址 → 物理地址 → I/O 虚拟地址 → 设备访问 IOVA
                          ↑
                     可任意映射
```

IOMMU 的好处：
- DMA 可以用分散的物理内存
- 隔离：设备只能访问授权的地址
- 虚拟化支持

API：
```c
// 告诉驱动使用 DMA 域
int dma_set_mask_and_coherent(struct device *dev, u64 mask);
// mask = 设备能寻址的地址位数（如 0xffffffffffffffff = 64 位）
```

---

## 常见错误

### 1. dma_map 之后忘记 unmap

```c
// 错误
dma_addr = dma_map_single(...);
iowrite32(dma_addr, regs + ...);
// 函数返回，dma_addr 永远没有 unmap → 内存泄漏
```

正确：用 goto cleanup 或类似机制确保 unmap 被调用。

### 2. unmap 之前 CPU 访问数据

```c
// 错误
dma_map_single(dev, buf, len, DMA_TO_DEVICE);
buf[0] = 0x1234;  // ❌ CPU 在 DMA map 期间改了内存！
```

### 3. 大小不一致

`map` 用 size=1024，`unmap` 用 size=2048 → 内核 panic 或警告。

### 4. 跨 NUMA 节点的 DMA

高端服务器有多 NUMA 节点，DMA 缓冲区应该分配在设备所在的节点上：

```c
// 用 device 提供的 NUMA-aware 分配
void *buf = dma_alloc_coherent(dev, size, &dma, GFP_KERNEL);
// 内核会自动选择正确的 NUMA 节点
```

---

## 本章小结

✅ **本章速查表**

| API | 用途 |
|---|---|
| `dma_alloc_coherent` | 分配一致性 DMA 缓冲区 |
| `dma_free_coherent` | 释放一致性 DMA 缓冲区 |
| `dma_map_single` | 流式 DMA 映射 |
| `dma_unmap_single` | 流式 DMA 反映射 |
| `dma_map_sg` / `dma_unmap_sg` | 分散-聚集 DMA |
| `dma_sync_single_for_cpu` | 同步缓存让 CPU 看到最新数据 |
| `dma_sync_single_for_device` | 同步缓存让设备看到 CPU 写过的数据 |
| `dma_mapping_error` | 检查 DMA map 是否成功 |
| `dma_set_mask_and_coherent` | 设置设备能寻址的地址位数 |
| `remap_pfn_range` | mmap 实现 |

### 自检清单

- [ ] 能区分一致性 DMA 和流式 DMA
- [ ] 能用 dma_alloc_coherent 分配一致性缓冲区
- [ ] 能用 dma_map_single / unmap 做单次 DMA
- [ ] 知道为什么需要 dma_sync_*
- [ ] 知道 mmap 的实现方式
- [ ] 知道 IOMMU 的基本概念

下一章 [13-AI 芯片架构基础](./13-AI芯片架构基础.md)，我们进入 AI 芯片特有的部分。