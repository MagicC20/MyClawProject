# 06. ioctl 机制

> 如果 read/write 是数据流，那 ioctl 就是控制流。  
> AI 芯片驱动里，ioctl 是最重要的接口——启动计算、查询状态、设置参数，几乎都靠 ioctl。

---

## 为什么需要 ioctl

read/write 适合传输"数据"（连续的字节流）。但驱动经常需要：

- "重启设备"
- "设置工作模式"
- "查询固件版本"
- "启动一次推理"
- "读某个寄存器"

这些操作不是数据流，是**命令**。read/write 不适合表达这种语义。

ioctl（input/output control）就是为这种场景设计的：

```c
// 🟢 用户态
int fd = open("/dev/mychip", O_RDWR);

// 命令 1：读固件版本
int version;
ioctl(fd, MYCHIP_GET_VERSION, &version);

// 命令 2：启动一次计算
ioctl(fd, MYCHIP_START_COMPUTE, NULL);

// 命令 3：设置参数
struct mychip_config cfg = { .mode = 1, .batch_size = 32 };
ioctl(fd, MYCHIP_SET_CONFIG, &cfg);
```

---

## ioctl 命令的编码

ioctl 的命令是一个 `unsigned long`，包含三段信息：

```
 31      30   29~16    15~8     7~0
┌───────┬───┬───────┬───────┬───────┐
│ dir   │ s │  size  │  type │  nr   │
└───────┴───┴───────┴───────┴───────┘
  ↑读写  0~3
  方向    参数大小
```

Linux 提供了四个宏来构造命令：

```c
#include <asm/ioctl.h>

// "读"型：从内核读数据到用户
_IO(type, nr)             // 无参数
_IOR(type, nr, datatype) // 用户态 read, 内核 write（传指针给内核读）

// "写"型：从用户写数据到内核
_IOW(type, nr, datatype) // 用户态 write, 内核 read（传指针给内核写）
_IOWR(type, nr, datatype)// 双向
```

### 实战示例

```c
// 🔴 内核态 — 头文件
#define MYCHIP_MAGIC       'm'  // 唯一标识（用字符）
#define MYCHIP_GET_VERSION _IOR(MYCHIP_MAGIC, 0x01, int)
#define MYCHIP_RESET       _IO(MYCHIP_MAGIC,  0x02)
#define MYCHIP_SET_CONFIG  _IOW(MYCHIP_MAGIC, 0x03, struct mychip_config)
#define MYCHIP_START       _IO(MYCHIP_MAGIC,  0x04)
#define MYCHIP_MAX_NR      0x04

// 错误检查（unlocked_ioctl 返回值）
#define MYCHIP_ERR_BASE    (-EIO)
```

⚠️ **MYCHIP_MAGIC** 必须是唯一的字符（"magic number"）。可以用 ASCII 字符，但不能和其他驱动冲突。可以查 `Documentation/ioctl/ioctl-number.txt`。

### 推荐的命名和组织方式

```c
// 📄 mychip_ioctl.h（用户态和内核态共用）
#ifndef _MYCHIP_IOCTL_H_
#define _MYCHIP_IOCTL_H_

#define MYCHIP_MAGIC  'm'

struct mychip_config {
    int mode;
    int batch_size;
    unsigned long flags;
};

#define MYCHIP_GET_VERSION _IOR(MYCHIP_MAGIC, 0x01, int)
#define MYCHIP_RESET       _IO(MYCHIP_MAGIC,  0x02)
#define MYCHIP_SET_CONFIG  _IOW(MYCHIP_MAGIC, 0x03, struct mychip_config)
#define MYCHIP_GET_STATUS  _IOR(MYCHIP_MAGIC, 0x04, int)
#define MYCHIP_MAX_NR      0x04

#endif
```

---

## 内核态实现：unlocked_ioctl

### 老接口 vs 新接口

```c
// 🔴 内核态
// 老接口（已废弃）
int (*ioctl)(struct inode *, struct file *, unsigned int, unsigned long);

// 新接口（推荐）
long (*unlocked_ioctl)(struct file *, unsigned int, unsigned long);
```

新接口没有 `inode` 参数，因为 `filp->f_path.dentry->d_inode` 可以拿到。

### 标准实现模板

```c
// 🔴 内核态
#include <linux/ioctl.h>

static long mychardev_unlocked_ioctl(struct file *filp,
                                     unsigned int cmd, unsigned long arg)
{
    int ret = 0;
    int val;
    struct mychip_config cfg;

    // 检查命令的 magic 和序号
    if (_IOC_TYPE(cmd) != MYCHIP_MAGIC)
        return -ENOTTY;

    if (_IOC_NR(cmd) > MYCHIP_MAX_NR)
        return -ENOTTY;

    switch (cmd) {
    case MYCHIP_GET_VERSION:
        val = 0x00010000;  // 假设版本 1.0
        if (copy_to_user((int __user *)arg, &val, sizeof(val)))
            return -EFAULT;
        break;

    case MYCHIP_RESET:
        // 设备复位逻辑
        pr_info("mychardev: reset\n");
        break;

    case MYCHIP_SET_CONFIG:
        if (copy_from_user(&cfg, (struct mychip_config __user *)arg,
                           sizeof(cfg)))
            return -EFAULT;
        pr_info("mychardev: config mode=%d batch=%d\n",
                cfg.mode, cfg.batch_size);
        // 保存 cfg 到 filp->private_data
        break;

    default:
        ret = -ENOTTY;  // 不支持
    }

    return ret;
}
```

注意 `unlocked_ioctl` 的返回值类型是 `long`（不是 `int`），这是为了支持 64 位兼容。

---

## 64 位 ioctl 兼容

如果你的驱动要在 32 位和 64 位内核/应用之间工作，注意结构体对齐：

```c
// 🔴 内核态

// 错误：32 位应用传给 64 位内核，结构体布局可能不同
struct bad_struct {
    int a;
    void *p;  // 32位=4字节，64位=8字节！
};

// 正确：显式指定长度
struct good_struct {
    int a;
    __u64 p;  // 总是 8 字节
};

// 或者实现 compat_ioctl 给 32 位用
static long mychip_compat_ioctl(struct file *filp,
                                unsigned int cmd, unsigned long arg)
{
    // 把 32 位参数转成 64 位
    ...
    return mychardev_unlocked_ioctl(filp, cmd, compat_ptr(arg));
}
```

---

## 完整示例：给上一章的字符设备加 ioctl

在 `mychardev.c` 中加入：

```c
// 🔴 内核态 — 添加到 file_operations
static const struct file_operations mychardev_fops = {
    .owner          = THIS_MODULE,
    .open           = mychardev_open,
    .release        = mychardev_release,
    .read           = mychardev_read,
    .write          = mychardev_write,
    .unlocked_ioctl = mychardev_unlocked_ioctl,
};

// ioctl 实现
static long mychardev_unlocked_ioctl(struct file *filp,
                                     unsigned int cmd, unsigned long arg)
{
    int val;

    if (_IOC_TYPE(cmd) != MYCHIP_MAGIC)
        return -ENOTTY;
    if (_IOC_NR(cmd) > MYCHIP_MAX_NR)
        return -ENOTTY;

    switch (cmd) {
    case MYCHIP_GET_VERSION:
        val = 0x00010000;
        return copy_to_user((int __user *)arg, &val, sizeof(val)) ? -EFAULT : 0;

    case MYCHIP_RESET:
        memset(kernel_buf, 0, BUF_SIZE);
        pr_info("mychardev: buffer reset\n");
        return 0;

    default:
        return -ENOTTY;
    }
}
```

### 用户态测试程序

```c
// 🟢 用户态 — test_ioctl.c
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define MYCHIP_MAGIC  'm'
#define MYCHIP_GET_VERSION _IOR(MYCHIP_MAGIC, 0x01, int)
#define MYCHIP_RESET       _IO(MYCHIP_MAGIC,  0x02)
#define MYCHIP_MAX_NR      0x02

int main(void)
{
    int fd, version;

    fd = open("/dev/mychardev", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    if (ioctl(fd, MYCHIP_GET_VERSION, &version) == 0) {
        printf("Firmware version: 0x%x\n", version);
    } else {
        perror("ioctl GET_VERSION");
    }

    if (ioctl(fd, MYCHIP_RESET) == 0) {
        printf("Reset OK\n");
    } else {
        perror("ioctl RESET");
    }

    close(fd);
    return 0;
}
```

```bash
$ gcc test_ioctl.c -o test_ioctl
$ ./test_ioctl
Firmware version: 0x10000
Reset OK
```

---

## 设计 ioctl 的一些原则

### 1. 命令要语义化

```c
// ❌ 差的设计
#define CHIP_CMD_1 _IO('m', 1)
#define CHIP_CMD_2 _IOW('m', 2, int)

// ✅ 好的设计
#define CHIP_GET_TEMPERATURE _IOR('m', 1, int)
#define CHIP_SET_FREQUENCY   _IOW('m', 2, int)
```

### 2. 一个命令只做一件事

```c
// ❌ 不好：一个命令做太多
ioctl(fd, CHIP_DO_EVERYTHING, &big_struct);

// ✅ 好：拆成多个原子命令
ioctl(fd, CHIP_SET_MODE, &mode);
ioctl(fd, CHIP_SET_BATCH, &batch);
ioctl(fd, CHIP_START);
```

### 3. 错误处理要完善

```c
// 检查 magic
if (_IOC_TYPE(cmd) != MYCHIP_MAGIC)
    return -ENOTTY;  // ENOTTY = "不是字符设备"（传统含义）

// 检查序号
if (_IOC_NR(cmd) > MYCHIP_MAX_NR)
    return -ENOTTY;

// 检查指针有效性
if (!arg)
    return -EINVAL;

// 检查拷贝结果
if (copy_to_user(...))
    return -EFAULT;
```

### 4. 大数据结构考虑用 sysfs

ioctl 适合小命令。对于复杂的配置（>256 字节），考虑：
- sysfs 节点（`/sys/class/mychip/...`）
- debugfs（`/sys/kernel/debug/...`）
- configfs（双向配置）

---

## 本章小结

✅ **本章速查表**

| 元素 | 用途 |
|---|---|
| `_IO` / `_IOR` / `_IOW` / `_IOWR` | 构造 ioctl 命令 |
| `_IOC_TYPE` / `_IOC_NR` / `_IOC_DIR` / `_IOC_SIZE` | 分解命令 |
| `unlocked_ioctl` | 新版 ioctl 实现函数 |
| `compat_ioctl` | 32 位兼容（可选） |
| `MYCHIP_MAGIC` | 字符唯一标识 |
| `copy_to_user` / `copy_from_user` | ioctl 中的数据传输 |

### 自检清单

- [ ] 能用四个 _IO 宏构造命令
- [ ] 知道 magic number 的作用
- [ ] 能写一个完整的 unlocked_ioctl 实现
- [ ] 知道 32/64 位兼容要注意的点
- [ ] 理解 ioctl 和 read/write 的使用边界

下一章 [07-内核调试工具](./07-内核调试工具.md)，我们学习各种内核调试方法。