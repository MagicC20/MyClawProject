# 03. 内核模块与 Hello World

> 写第一个能跑起来的内核模块。  
> 这是从"应用软件"到"底软"的关键一步——你的代码第一次在内核态运行。

---

## 第一个内核模块

### 📄 hello.c

```c
// 🔴 内核态代码
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

static int __init hello_init(void)
{
    printk(KERN_INFO "Hello from kernel! I'm a module.\n");
    return 0;  // 返回 0 表示初始化成功
}

static void __exit hello_exit(void)
{
    printk(KERN_INFO "Goodbye from kernel.\n");
}

module_init(hello_init);   // 注册初始化函数
module_exit(hello_exit);   // 注册退出函数

MODULE_LICENSE("GPL");                  // 必需：声明许可证
MODULE_AUTHOR("Your Name");             // 可选
MODULE_DESCRIPTION("A hello module");  // 可选
```

### 📄 Makefile

```makefile
# 一个标准的内核模块 Makefile
obj-m += hello.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
```

### 编译 + 加载 + 卸载

```bash
# 编译
$ make
make -C /lib/modules/5.15.0-89-generic/build M=/tmp/hello modules
make[1]: Entering directory '/usr/src/linux-headers-5.15.0-89-generic'
  CC [M]  /tmp/hello/hello.o
  Building modules, stage 2.
  MODPOST 1 modules
  CC [M]  /tmp/hello/hello.mod.o
  LD [M]  /tmp/hello/hello.ko
make[1]: Leaving directory '/usr/src/linux-headers-5.15.0-89-generic'

# 加载（注意：需要 root 权限）
$ sudo insmod hello.ko

# 查看是否加载
$ lsmod | grep hello
hello                  12288  0

# 查看输出
$ sudo dmesg | tail -1
[12345.678901] Hello from kernel! I'm a module.

# 卸载
$ sudo rmmod hello

# 再看输出
$ sudo dmesg | tail -1
[12346.789012] Goodbye from kernel.
```

🎉 **恭喜，你的代码第一次在内核里跑了！**

---

## 关键概念解释

### `module_init` 和 `module_exit`

这两个宏做了什么？看 `include/linux/module.h`：

```c
// 简化版源码
#define module_init(initfn)                    \
    static inline initcall_t __inittest(void)  \
    { return initfn; }                         \
    int init_module(void) __attribute__((alias("__inittest")));

#define module_exit(exitfn)                    \
    static inline exitcall_t __exittest(void)  \
    { return exitfn; }                         \
    void cleanup_module(void) __attribute__((alias("__exittest")));
```

核心思想：
- `module_init` 宏展开后，模块被加载时，**内核会调用 `init_module` 函数**
- `module_init(hello_init)` 让 `init_module` 成为 `hello_init` 的别名
- 这样内核就能找到你的初始化函数

### `__init` 和 `__exit` 的作用

这两个不是普通的关键字，是**链接器标记**：

```c
static int __init hello_init(void) { ... }
//              ^^^^^^ 标记这个函数只在初始化时用
```

链接器看到 `__init` 后，会把这个函数放到特殊的 section，**模块加载完成后，这段内存会被释放**。这样可以节省内核内存。

⚠️ 所以 `__init` 函数在模块加载后**不能再被调用**！

同理 `__exit` 函数在模块**静态编译进内核**时会被丢弃（因为没有卸载机会）。

### `printk` vs `printf`

```c
printk(KERN_INFO "Hello\n");   // 内核态
printf("Hello\n");             // 用户态
```

`printk` 的关键差异：

| 特性 | `printk` | `printf` |
|---|---|---|
| 运行上下文 | 内核态 | 用户态 |
| 缓冲区 | 内核环形缓冲区（可用 `dmesg` 查看）| stdout 缓冲区 |
| 日志级别 | 必须指定（KERN_INFO 等）| 无 |
| 是否能睡眠 | **不能**（持锁时不能调 printk）| 不确定 |
| 浮点支持 | **不支持**（内核禁用 FPU）| 支持 |

`printk` 的日志级别：

```c
#define KERN_EMERG   "<0>"   // 系统不可用
#define KERN_ALERT   "<1>"   // 必须立即处理
#define KERN_CRIT    "<2>"   // 严重错误
#define KERN_ERR     "<3>"   // 错误
#define KERN_WARNING "<4>"   // 警告
#define KERN_NOTICE  "<5>"   // 正常但重要
#define KERN_INFO    "<6>"   // 普通信息
#define KERN_DEBUG   "<7>"   // 调试信息
```

只有优先级高于控制台日志级别（默认 `KERN_WARNING`）的消息才会打到屏幕。

**小技巧**：开发期间可以用 `pr_info`/`pr_err` 等简化宏（不用拼日志级别字符串）：
```c
pr_info("Hello\n");       // 等价于 printk(KERN_INFO "Hello\n");
pr_err("oops: %d\n", x);  // 等价于 printk(KERN_ERR "oops: %d\n", x);
```

### `MODULE_LICENSE` 为什么必需

```c
MODULE_LICENSE("GPL");  // 或 "GPL v2"、"BSD"、"MIT"、"Dual BSD/GPL" 等
```

这不是法律声明，是**技术标识**：
- 如果你写 `"Proprietary"` 或不写 → 内核会打 `Tainted kernel`（内核污染标记）
- 受污染的内核：某些调试功能会被禁用，社区不接受 bug 报告
- **生产环境驱动通常声明 `GPL` 或 `Dual BSD/GPL`**

---

## 几个常见踩坑

### 1. 编译时报 `fatal error: linux/init.h: 没有那个文件或目录`

缺少内核头文件：

```bash
# Ubuntu/Debian
sudo apt install linux-headers-$(uname -r)

# CentOS/RHEL
sudo yum install kernel-devel-$(uname -r)
```

### 2. 加载时 `insmod: ERROR: could not insert module hello.ko: Unknown symbol`

模块用了未导出的符号。检查：
- 是不是缺了 `#include`？
- 是不是用了 GPL-only 的符号但没声明 `MODULE_LICENSE("GPL")`？

### 3. 卸载时 `rmmod: ERROR: Module hello is in use`

模块被引用了。先看谁在用：
```bash
$ lsmod | grep hello
hello   12288   1   other_module  ← 说明 other_module 依赖 hello

# 强制卸载（危险）
$ sudo rmmod --force hello
```

### 4. `dmesg` 看不到 printk 输出

可能是日志级别太低。用 `sudo dmesg --level=info` 或 `sudo cat /proc/kmsg` 查看。

---

## 模块参数

模块可以接收参数：

```c
// 🔴 内核态
#include <linux/module.h>
#include <linux/kernel.h>

static int debug = 0;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Enable debug mode (0=off, 1=on)");

static int count = 1;
module_param(count, int, 0444);
MODULE_PARM_DESC(count, "Number of greetings");

static char *name = "world";
module_param(name, charp, 0644);
MODULE_PARM_DESC(name, "Who to greet");

static int __init hello_init(void)
{
    int i;
    for (i = 0; i < count; i++) {
        if (debug)
            pr_info("Hello %s! (debug mode, iter %d)\n", name, i);
        else
            pr_info("Hello %s!\n", name);
    }
    return 0;
}
module_init(hello_init);

MODULE_LICENSE("GPL");
```

加载时传参：

```bash
$ sudo insmod hello.ko debug=1 count=3 name="MagicC"
$ sudo dmesg | tail -3
Hello MagicC! (debug mode, iter 0)
Hello MagicC! (debug mode, iter 1)
Hello MagicC! (debug mode, iter 2)
```

---

## 内核模块的生命周期

```
insmod hello.ko
   ↓
load_module()            ← 内核根据 .ko 文件加载段
   ↓
init_module()            ← 调用你的 hello_init()
   ↓
[ 模块运行中 ]            ← 提供功能
   ↓
rmmod hello
   ↓
cleanup_module()         ← 调用你的 hello_exit()
   ↓
模块从内核移除
```

每个阶段内核都会做严格检查，**任何一个错误都会拒绝加载或卸载**。

---

## 本章小结

✅ **本章速查表**

| 元素 | 用途 |
|---|---|
| `module_init(fn)` | 注册初始化函数（模块加载时调用）|
| `module_exit(fn)` | 注册退出函数（模块卸载时调用）|
| `__init` | 标记函数仅初始化用，加载后内存可释放 |
| `__exit` | 标记函数仅卸载用，静态编译时丢弃 |
| `printk(KERN_XXX "...")` | 内核态打印，支持日志级别 |
| `pr_info` / `pr_err` | printk 的简化宏 |
| `MODULE_LICENSE("GPL")` | **必需**，否则内核被污染 |
| `module_param` | 声明模块参数 |
| `insmod` / `rmmod` / `lsmod` | 加载/卸载/查看模块的命令 |

### 自检清单

- [ ] 能独立写出 hello.c 和 Makefile
- [ ] 能解释 `__init` 和 `__exit` 的作用
- [ ] 知道 `printk` 和 `printf` 的关键区别
- [ ] 知道为什么 `MODULE_LICENSE` 是必需的
- [ ] 能用模块参数传值

下一章 [04-内存管理与基础 API](./04-内存管理与基础API.md)，我们学习内核内存分配和数据传输。