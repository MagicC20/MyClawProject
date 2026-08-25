# 04. 内存管理与基础 API

> 驱动代码里 80% 的 bug 都和内存有关。  
> 这一章把内核内存管理的核心概念讲清楚，让你以后看到 `kmalloc`、`vmalloc`、`copy_from_user` 这些 API 不会发怵。

---

## 内核内存全景

```
虚拟地址空间（单个进程视角）

高地址 ┌─────────────────┐
      │   内核栈 (8K~16K)  │  ← 每个线程/中断都有自己的栈
      ├─────────────────┤
      │   内核模块代码    │  ← insmod 加载的代码
      ├─────────────────┤
      │   vmalloc 区     │  ← 大块、不连续物理内存
      ├─────────────────┤
      │   kmalloc 区     │  ← 小块、连续物理内存（slab）
      ├─────────────────┤
      │   直接映射区      │  ← 物理内存的 1:1 映射
低地址 └─────────────────┘
```

应用软件工程师熟悉的 `malloc`，在内核里被分成三种：

| 函数 | 用途 | 物理连续 | 适用场景 |
|---|---|---|---|
| `kmalloc` | 物理连续的内存 | ✅ 是 | 小块分配（<128KB），DMA |
| `vmalloc` | 虚拟连续，物理可能不连续 | ❌ 否 | 大块分配，但不能 DMA |
| `__get_free_page` | 直接分配页 | ✅ 是 | 大块连续内存 |

---

## kmalloc：最常用的分配函数

```c
// 🔴 内核态
#include <linux/slab.h>

void *kmalloc(size_t size, gfp_t flags);
void kfree(const void *obj);
```

### GFP 标志（最常用的几个）

| 标志 | 含义 | 使用场景 |
|---|---|---|
| `GFP_KERNEL` | 可能睡眠，正常分配 | 进程上下文，绝大多数情况 |
| `GFP_ATOMIC` | 不能睡眠，必须立即返回 | 中断上下文、持锁时 |
| `GFP_DMA` | 分配 DMA 区域（低地址） | 老式 DMA 设备 |
| `GFP_KERNEL | __GFP_ZERO` | 分配并清零（等价于 `kzalloc`）|

⚠️ **核心规则**：中断上下文里**绝对不能用** `GFP_KERNEL`，会死锁！必须用 `GFP_ATOMIC`。

### 实际使用

```c
struct my_data {
    int id;
    char name[32];
    void *ptr;
};

// 方法 1：kmalloc + memset
struct my_data *p = kmalloc(sizeof(*p), GFP_KERNEL);
if (!p)
    return -ENOMEM;
memset(p, 0, sizeof(*p));

// 方法 2：kzalloc（推荐）
struct my_data *p = kzalloc(sizeof(*p), GFP_KERNEL);
if (!p)
    return -ENOMEM;
```

### kmalloc 的尺寸限制

```
kmalloc 最大可分配（取决于架构）：
  - x86_64:    4 MB
  - arm64:     4 MB
  - 实际上通常建议 < 128 KB

更大的需求：用 alloc_pages() 或 vmalloc()
```

---

## vmalloc：大块但不连续的内存

```c
// 🔴 内核态
#include <linux/vmalloc.h>

void *vmalloc(unsigned long size);
void vfree(const void *addr);
```

适用场景：
- 大块内存（>128 KB）
- 不需要 DMA
- 不需要物理连续

```c
char *buf = vmalloc(1024 * 1024);  // 1 MB
if (!buf)
    return -ENOMEM;

// ... 使用 ...
memset(buf, 0, 1024 * 1024);

vfree(buf);
```

⚠️ **vmalloc 的开销**比 kmalloc 大：需要建立页表，性能略差。除非真的需要大块，否则优先用 kmalloc。

---

## 用户态 ↔ 内核态数据传输

这是驱动里**最高频**的操作：用户态进程调用 `read`/`write`/`ioctl`，数据要传给驱动。

### 三组函数

| 函数 | 方向 | 用途 |
|---|---|---|
| `copy_from_user(to, from, n)` | 用户 → 内核 | read() 里读用户数据 |
| `copy_to_user(to, from, n)` | 内核 → 用户 | read() 里回数据 |
| `get_user(val, ptr)` | 用户 → 内核 | 读单个变量 |
| `put_user(val, ptr)` | 内核 → 用户 | 写单个变量 |

### 为什么要用这些函数？不能用 memcpy 吗？

**绝对不能用 memcpy！** 原因：

1. **用户指针可能无效**：用户传进来的指针可能是 NULL、野指针、已释放的
2. **安全漏洞**：恶意程序可以利用 memcpy 做提权攻击
3. **跨地址空间**：用户态和内核态地址空间不同，直接 memcpy 会访问错误的内存

`copy_from_user` 会**检查用户指针的合法性**，遇到无效地址会返回未拷贝的字节数（而不是崩溃）。

### 标准 read/write 模板

```c
// 🔴 内核态
static ssize_t my_read(struct file *filp, char __user *buf,
                       size_t count, loff_t *ppos)
{
    int ret;

    // 假设我们要读内核缓冲区 data[0..data_len]
    if (*ppos >= data_len)
        return 0;  // EOF

    if (count > data_len - *ppos)
        count = data_len - *ppos;

    // 把内核数据拷到用户空间
    ret = copy_to_user(buf, data + *ppos, count);
    if (ret)
        return -EFAULT;

    *ppos += count;
    return count;  // 返回实际读的字节数
}

static ssize_t my_write(struct file *filp, const char __user *buf,
                        size_t count, loff_t *ppos)
{
    int ret;

    // 拷贝用户数据到内核
    ret = copy_from_user(kernel_buf, buf, count);
    if (ret)
        return -EFAULT;

    return count;
}
```

### `__user` 标记的作用

```c
char __user *buf;
```

这个 `__user` 是**给编译器和 sparse 工具看的**：
- 编译器会发出警告，如果你在内核态代码里**直接解引用** `__user` 指针
- sparse 工具可以做静态检查，揪出"忘记 copy_from_user"的安全漏洞

**正确**：`copy_from_user(kernel_buf, buf, count);`
**错误**：`memcpy(kernel_buf, buf, count);` ← sparse 会报警告

---

## 内存映射：mmap

有时数据太大，拷贝代价高。可以用 mmap 让用户进程**直接映射内核缓冲区**到自己的地址空间——零拷贝。

```c
// 🔴 内核态 — mmap 实现
#include <linux/mm.h>

static int my_mmap(struct file *filp, struct vm_area_struct *vma)
{
    unsigned long size = vma->vm_end - vma->vm_start;
    unsigned long pfn;

    // 假设我们有个内核缓冲区 kernel_buf（用 kmalloc 分配）
    pfn = virt_to_phys(kernel_buf) >> PAGE_SHIFT;

    // 把内核页映射到用户空间
    if (remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot))
        return -EAGAIN;

    return 0;
}
```

用户态调用：

```c
// 🟢 用户态
int fd = open("/dev/mychip", O_RDWR);
void *ptr = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
// 现在 ptr 直接指向设备内存，可以像普通指针一样读写
ptr[0] = 0x1234;
```

⚠️ mmap 的细节（vm_area_struct、remap_pfn_range 等）后面 [12-DMA与内存映射](./12-DMA与内存映射.md) 会展开。

---

## 进程上下文 vs 中断上下文

这是内核内存分配的一个**核心约束**：

```
进程上下文（Process Context）：
  - 正在执行一个系统调用
  - 可以睡眠、调度
  - 可以用 GFP_KERNEL

中断上下文（Interrupt Context）：
  - 正在执行中断处理函数
  - 不能睡眠（没有进程可调度）
  - 必须用 GFP_ATOMIC
  - 不能用 mutex（只能用 spinlock）
```

判断当前上下文的方法：

```c
// 🔴 内核态
if (in_interrupt()) {  // 或 in_irq()、in_softirq()
    // 中断上下文，必须用 GFP_ATOMIC
    p = kmalloc(sizeof(*p), GFP_ATOMIC);
} else {
    // 进程上下文
    p = kmalloc(sizeof(*p), GFP_KERNEL);
}
```

---

## 常见内存错误

| 错误 | 现象 | 怎么避免 |
|---|---|---|
| 中断上下文用 `GFP_KERNEL` | 系统 hang（死锁） | 严格用 `GFP_ATOMIC` |
| 忘记 `kfree` | 内存泄漏（内核内存没法回收）| 用 kref 或工具检查 |
| `kfree` 同一个指针两次 | 内核 panic | 释放后置 NULL |
| 越界访问 | 静默踩内存，触发 oops | 用 `kmemleak`/`KASAN` |
| 忘记 `copy_from_user` | 安全漏洞 + 段错误 | 严格用 `__user` 标注 |

---

## 本章小结

✅ **本章速查表**

| API | 用途 |
|---|---|
| `kmalloc(size, GFP_KERNEL)` | 分配小块物理连续内存（进程上下文） |
| `kmalloc(size, GFP_ATOMIC)` | 同上，但可在中断上下文用 |
| `kzalloc(...)` | kmalloc + 自动清零 |
| `vmalloc(size)` | 分配大块虚拟连续内存 |
| `kfree(ptr)` / `vfree(ptr)` | 释放 |
| `copy_from_user(to, from, n)` | 用户 → 内核，安全拷贝 |
| `copy_to_user(to, from, n)` | 内核 → 用户，安全拷贝 |
| `__user` 标记 | 标注用户空间指针，编译器会检查 |

### 自检清单

- [ ] 能解释 kmalloc 和 vmalloc 的关键区别
- [ ] 知道为什么必须用 copy_from_user 而不是 memcpy
- [ ] 知道 GFP_KERNEL 和 GFP_ATOMIC 的使用场景
- [ ] 知道进程上下文 vs 中断上下文的差异
- [ ] 能写出标准 read/write 的代码框架

下一章 [05-字符设备驱动](./05-字符设备驱动.md)，我们写一个完整的字符设备驱动。