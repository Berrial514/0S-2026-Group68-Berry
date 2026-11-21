# 操作系统实验报告 - Lab4: 内核线程管理

## 实验基本信息
| 项目 | 内容 |
|------|------|
| **实验名称** | Lab 4: 内核线程管理 |
| **小组成员** | 肖文哲、税思远、殷彬淳 |
| **完成日期** | 2026-11-21 |

### 小组分工
| 成员 | 负责的模块/任务 | 负责的报告章节 |
|------|----------------|--------------|
| 肖文哲 | `alloc_proc`、`do_fork`、`copy_thread` 实现 | 模块一、PCB 初始化与线程创建 |
| 税思远 | `proc_run`、上下文切换机制分析 | 模块二、进程切换底层逻辑 |
| 殷彬淳 | 前序代码合并、QEMU 兼容性修复、报告统稿 | 整体逻辑、OS 原理对比、统稿 |

---

## 一、实验目的
理解操作系统内核中进程/线程的抽象方式，掌握进程控制块（PCB）的初始化、内核线程的创建流程，以及进程上下文切换的底层汇编机制。

## 二、实验环境
| 成员 | AI 编程工具 | 底层模型 |
|------|------------|---------|
| 肖文哲 | VS Code / DeepSeek Harness | DeepSeek-V4-Flash |
| 税思远 | Trae(AI IDE) / VS Code | DeepSeek-V4-Pro |
| 殷彬淳 | Cline（VS Code 插件） | DeepSeek-V4-Pro |

---

## 三、实验整体逻辑分析
本章实验围绕**内核线程的生命周期**展开，核心链路为：`alloc_proc`（分配并初始化 PCB） -> `do_fork`（分配资源、复制上下文、分配 PID 并加入链表） -> `proc_run`（切换页表、设置内核栈、底层 `switch_to` 汇编切换）。
此外，由于本工作区初始为空骨架，我们额外补齐了 Lab1~Lab3 的前序依赖（物理内存管理、页表机制、Trap 处理），确保内核能够正常启动。

---

## 四、实验内容与实现

### 1. 进程控制块的初始化 (`alloc_proc`)
**负责人：肖文哲**

**功能说明**：
`alloc_proc` 负责为新的进程申请一块 `struct proc_struct` 的内存，并对其所有字段进行安全初始化，将其状态设为 `PROC_UNINIT`。

**核心实现逻辑**：
- `state = PROC_UNINIT`（未初始化状态）。
- `pid = -1`（等待 `do_fork` 分配）。
- `kstack = 0`、`need_resched = 0`、`parent = NULL`、`mm = NULL`。
- 通过 `memset` 清空 `context`（上下文）和 `name`。
- `cr3 = boot_cr3` 指向内核页表，`tf = NULL`。

### 2. 内核线程的创建 (`do_fork`)
**负责人：肖文哲**

**功能说明**：
`do_fork` 负责复制当前进程的上下文，分配内核栈和 PID，最终将新进程放入调度队列。

**核心实现逻辑**：
1. 调用 `alloc_proc()` 分配 PCB。
2. 调用 `setup_kstack(proc)` 分配内核栈。
3. 调用 `copy_mm()` 和 `copy_thread()` 复制内存管理与 Trapframe 上下文（通过 `copy_thread` 设置 `context.ra = forkret`，`context.sp` 指向子线程的 `tf`）。
4. **临界区保护**：使用 `local_intr_save(intr_flag)` 关中断，调用 `get_pid()` 分配唯一 PID，将进程加入 `hash_list` 和 `proc_list`，最后 `local_intr_restore(intr_flag)` 开中断。
5. 调用 `wakeup_proc()` 唤醒新线程。

**Bug 与解决**：
- **Bug**：在 `kernel_thread` 中，最初将构造的 Trapframe 放在了当前内核栈顶。这导致覆盖了 `proc_init` 正在使用的活动栈帧，返回地址被破坏为 0，导致内核直接崩溃。
- **解决**：将 Trapframe 定义在 `kernel_thread` 函数的**局部变量**中，等 `copy_thread` 将其复制到子线程自己的内核栈后，再随函数返回自动销毁。

### 3. 进程切换 (`proc_run`)
**负责人：税思远**

**功能说明**：
`proc_run` 负责将 CPU 的控制权从当前进程（`current`）移交给目标进程（`proc`）。

**核心实现逻辑**：
- 判断 `proc != current`，如果不相等才执行切换。
- 关中断：`local_intr_save(intr_flag)`。
- 更新 `current = proc`，调用 `load_esp0` 设置 TSS 中的内核栈指针。
- 切换页表：`lcr3(current->cr3)`。
- 调用汇编级上下文切换：`switch_to(&(prev->context), &(next->context))`，保存 `ra, sp, s0~s11` 等被调用者保存寄存器。
- 恢复中断：`local_intr_restore(intr_flag)`。

---

## 五、测试与验证
**负责人：全体**

运行注入测试后，内核成功启动并输出：
```text
check_alloc_page() succeeded!
check_pgdir() succeeded!
check_boot_pgdir() succeeded!
[Test PMM] PASS
[Test Process Create] PASS
[Test Schedule] PASS
this initproc, pid = 1, name = "init"
To U: "Hello world!!".
To U: "en.., Bye, Bye. :)"
```

*(注：测试用例全部 PASS。最后的 `kernel panic ... process exit!!.` 是因为骨架自带的 `do_exit` 是空实现，在 `initproc` 生命周期结束时正常触发的，不影响 Lab4 的评分点。)*

---

## 六、实验总结与收获

### 1. 思考题回答
1. **`struct context` 和 `struct trapframe *tf` 的区别**：
   - `context` 用于内核线程之间的上下文切换（保存被调用者保存寄存器），通过 `switch_to` 汇编切换。
   - `trapframe` 用于保存用户态陷入内核态时的完整现场（包括所有通用寄存器、CSR 等），用于 `sret` 返回。
2. **ucore 是否给新线程分配了唯一 ID**：
   - 是。`get_pid()` 采用了一个特殊的轮转算法，兼顾了分配效率与安全性，保证在 `MAX_PID` 范围内不冲突。
3. **同步机制的作用**：
   - `local_intr_save/restore` 保证了在进程放入全局链表和分配 PID 时，不会被时钟中断打断，防止链表指针断裂。

### 2. OS 原理对比
| 实验知识点 | OS原理对应 | 关系与差异 |
|---|---|---|
| PCB 初始化与红黑树/链表管理 | 进程控制块 | ucore 使用双向链表管理就绪队列，用哈希表管理 PID。Linux 使用树结构优化查找。 |
| 内核栈分配与 `switch_to` | 上下文切换 | 原理课强调寄存器保存，实验中通过 `context` 结构体和汇编实现了具体的保存/恢复逻辑。 |
| `do_fork` 与进程创建 | Fork 系统调用 | ucore 的 `do_fork` 通过 `copy_thread` 复制父进程的 Trapframe，使得子进程从 `forkret` 返回。 |