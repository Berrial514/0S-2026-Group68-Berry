# 操作系统实验报告 - Lab2: 物理内存管理

## 实验基本信息
| 项目 | 内容 |
|------|------|
| **实验名称** | Lab 2: 物理内存管理 |
| **小组成员** | 肖文哲、税思远、殷彬淳 |
| **完成日期** | 2026-10-24 |

### 小组分工
| 成员 | 负责的模块/任务 | 负责的报告章节 |
|------|----------------|--------------|
| 肖文哲 | `page_init`实现、Best-Fit实现、`make grade`联调 | 模块一、模块二、测试截图、Prompt与Bug |
| 税思远 | First-Fit分析、GDB页表查询调试 | 模块三、分支任务、OS原理对比 |
| 殷彬淳 | 选做Challenge（Buddy）、Prompt汇总、统稿检查 | 整体逻辑、Challenge、Prompt汇总、排版 |

---

## 一、实验目的
理解物理内存的发现、初始化与分配机制，掌握页描述符管理，实现并接入 Best-Fit 分配器，并尝试探索 Buddy System 算法。

## 二、实验环境
| 成员 | AI 编程工具 | 底层模型 |
|------|------------|---------|
| 肖文哲 | VS Code / DeepSeek Harness | DeepSeek-V4-Flash |
| 税思远 | Trae(AI IDE) / VS Code / SSH Remote | DeepSeek-V4-Pro |
| 殷彬淳 | Cline（VS Code 插件） | DeepSeek-V4-Pro |

---

## 三、实验整体逻辑分析
**负责人：殷彬淳**

本章实验的核心主线是**物理内存的发现、组织与分配**。为了实现内存管理，实验遵循自底向上的逻辑链逐步展开：
1. **发现内存**：通过解析 OpenSBI 传来的 DTB（设备树），获取物理内存的基址和大小。
2. **描述内存**：调用 `page_init()`，建立 `Page` 描述符数组，剔除内核镜像和描述符自身占用的空间，将剩余可用物理页标记为空闲。
3. **管理内存**：定义 `pmm_manager` 接口框架，将页面分配、释放等具体算法抽象为函数指针。先实现了基础的 First-Fit，再实现了 Best-Fit，最后通过选做的 Buddy System 体会不同分配算法的优劣。整个流程环环相扣，构成了完整的内核物理内存管理子系统。

---

## 四、实验内容与实现

### 1. 物理内存初始化与页描述符 (`page_init`)
**负责人：肖文哲**
* **功能说明**：从DTB读取`mem_base`/`mem_size`，算出 `npage = PPN(mem_base + mem_size)` ，设置`va_pa_offset = 0xFFFFFFFF40000000`， `pages` 从 `ROUNDUP(end, PGSIZE)` 开始、大小 `(npage - nbase) * sizeof(struct Page)`，全数组先标 `SetPageReserved` ，排除 OpenSBI、内核镜像及描述符数组占用空间，把 `[PADDR(数组尾) 向上取整, mem_end 向下取整)` 这唯一一段完整页交给 `init_memmap()`。

*   **Prompt 设计**：
    ```markdown
    【prompt】
    在 RISC-V 64 位 ucore 内核中，实现物理内存管理的初始化函数 page_init()。
    位置：kern/mm/pmm.c，只补全这一个函数，不要重写整个文件。
    要求：
    (a) 设置 va_pa_offset 恰好等于 PHYSICAL_MEMORY_OFFSET (0xFFFFFFFF40000000)；
    (b) 设置 npage 等于 PPN(get_memory_base() + get_memory_size())；
    (c) 设置 pages，指向紧接在内核 end 之后按页对齐的虚拟地址；
    (d) 把所有描述符初始化为 reserved；
    (e) 打印物理内存范围；
    (f) 调用 init_memmap() 把真正的空闲页交给 pmm_manager。
    分支条件：
    - DTB 信息无效时 panic；
    - 没有可用完整页时不调用 init_memmap。
    必须使用 PADDR, ROUNDUP, ROUNDDOWN, SetPageReserved 等已有宏。
    ```
* **Bug 与解决**：
  1. 本机 QEMU 环境（QEMU 8.2.2/10.2.1）启动后 OpenSBI 直接卡死，无法加载内核。排查发现两个问题：一是原版 `entry.S` 只使用了 `lui sp, %hi(bootstacktop)`，漏掉了低 12 位导致栈指针指向非法地址，需补充 `addi sp, sp, %lo(bootstacktop)`；二是 QEMU 自带 OpenSBI 固件与部分版本不兼容，需替换为我们验证过的 `rustsbi-qemu.bin`。

### 2. Best-Fit 连续物理内存分配算法
**负责人：肖文哲**
*   **功能说明**：使用`best_fit_init` 清空链表，`best_fit_init_memmap` 按地址升序插入整块，`alloc` 遍历找 **`property >= n` 中最小者**，`list_del` 前先存 `prev`，剩余块挂回原位置，`free` 按地址插入后**先向后合并、再向前合并**。
*   **Prompt 设计**：
    ```markdown
    【prompt】
    在 RISC-V 64 位 ucore 内核中实现一个完整的 Best-Fit 物理页分配器，并把它接入 pmm_manager。
    位置：kern/mm/best_fit_pmm.c。
    要求：
    - 内部状态：一个 static free_area_t 实例，并定义 #define free_list / nr_free；
    - 实现 init, init_memmap, alloc_pages, free_pages, nr_free_pages；
    - 在 alloc_pages 中，在整条 free_list 中找 property >= n 且 property 最小的块；
    - 在 free_pages 中，插入空闲链表后，分别与前驱块和后继块合并；
    - 定义并注册 const struct pmm_manager best_fit_pmm_manager = { ... }；
    - 在 kern/mm/pmm.c 的 init_pmm_manager() 中将 pmm_manager 切换为 &best_fit_pmm_manager。
    ```
*   **改进空间**：按地址有序的链表让 free 的合并是 O(1) 邻居检查，但 alloc 是 O(n) 全表扫描，可以改成按 size 分桶的链表或者加大小索引；另外 best-fit 会留下大量小碎片，可以用 buddy 合并。

### 3. First-Fit 算法分析（思考题）
**负责人：税思远**
*   **核心逻辑**：`default_pmm.c` 实现的 First-Fit（首次适配）用一条**按物理地址升序**组织的环形双向链表管理空闲块，块头仍是该块首页的 `struct Page`（靠 `le2page`/`property` 从 `page_link` 逆向得到块大小）。初始化时清空表；分配时沿链表遍历找**第一个**满足 `property >= n` 的块；释放时插回链表并做**邻居合并**，保证空闲链表中相邻空闲块始终被合并成一个。
*   **改进空间**：有。① 分配是 `O(n)` 全链扫描，可把链表按 `property` **分桶**（每个 size 级一条链）或用树/索引，把最坏扫描降到 `O(log n)`；② 首次适配虽快，但总在地址低端切小块，长期会造成**头部大量小碎片**——可引入 **Next-Fit（旋转循环扫描）** 减少低端过切，或像本实验由调用方切到 **Best-Fit**（全局挑 `property` 最小者，降低碎片）；③ 释放只做**相邻合并**，无法应对物理不连续场景，可用 **buddy 合并** 思路进一步缓解外部碎片与内部碎片。

### 4. 选做 Challenge：Buddy System 分配算法
**负责人：殷彬淳**
*   **实现说明**：设计了一份 Prompt 让 AI 生成完整的 Buddy 分配器，实现了 `buddy_alloc_pages`、`buddy_free_pages` 等核心接口。但在将代码接入 WSL 的真实 ucore 环境跑 `make grade` 时，由于 QEMU 版本与项目原有启动链的兼容性问题（如上文 `entry.S` 与 RustSBI 导致的启动崩溃），最终未能顺利跑通评分脚本，仅完成了算法逻辑的设计与宿主机（Windows）的逻辑验证。
*   **Prompt 设计**：
    ```markdown
    【prompt 1（生成算法）】
    请参考 ucore 的 pmm_manager 框架，用 C 语言实现一个 Buddy System 物理内存分配器，存放在 buddy_pmm.c 中。
    要求：
    1. 定义 buddy_pmm_manager 结构体并导出。
    2. 空闲链表使用 free_list[11] 数组（最大支持 2^10 个连续页）。
    3. buddy_init_memmap：将传入的连续内存块按 2 的幂次拆解，挂到对应阶的空闲链表上。
    4. buddy_alloc_pages：根据请求页数向上取整到 2 的幂次，从对应阶查找，若没有则从更高阶拆分。返回起始 Page 指针。
    5. buddy_free_pages：释放页块，并循环检查其“伙伴”（地址异或当前阶大小）是否空闲，若空闲则合并为更高阶，直到无法合并。
    6. 实现 buddy_nr_free_pages 返回总空闲页数。

    【prompt 2（生成测试）】
    请为刚才生成的 Buddy System 编写测试函数 buddy_check()，要求覆盖以下场景：
    1. 正常分配：分配 1 页、2 页、4 页，检查返回地址是否按 2 的幂次对齐。
    2. 越界分配：请求大于当前最大连续块的页数，检查是否返回 NULL。
    3. 伙伴合并：分配两个相邻的 1 页块（互为伙伴），释放其中一个后再释放另一个，检查它们是否成功合并为一个 2 页块。
    4. 内存耗尽：循环分配直到返回 NULL，检查 nr_free 是否为 0。
    ```
*   **测试覆盖说明**：测试用例有效覆盖了 Buddy System 的核心行为：**2 的幂次对齐分配**保证了内存块的基础约束；**越界分配**测试了分配器的边界鲁棒性；**伙伴合并**测试了释放时能否正确通过异或运算找到伙伴并合并，这是避免内存碎片化的关键；**内存耗尽**测试了空闲链表的完整遍历和状态更新。

---

## 五、GDB 调试页表查询过程（分支任务）
**负责人：税思远**
*   **调试配置**：为了在 QEMU **源码级**看到 SV39 页表翻译，先用 `--enable-debug` 从源码编译一份 **QEMU 8.2.7**，再用 `gdb-multiarch.exe` 做**双 GDB** 附加：**会话1**（宿主机 x86 gdb）`attach` 到运行中的 QEMU 进程、断 QEMU 的软件 MMU 源码；**会话2**（riscv64 gdb）远程连 QEMU 的 `:1234`（`-s -S`）调 ucore 内核。
*   **关键调用路径**：断点命中最能说明“一次客户机访存 = 一次宿主机页表遍历”：
    ```
    riscv_cpu_tlb_fill          target/riscv/cpu_helper.c:1295   ← SV39 页表遍历入口
      └ riscv_get_physical_address  同样，static，逐级翻 VPN2→VPN1→VPN0 得 PTE→PPN
    probe_access_internal / get_page_addr_code_hostp   accel/tcg/cputlb.c
    ```
*   **模拟 TLB 与真实 TLB 的区别**：QEMU 这里的“TLB”是**软件/逻辑 TLB**——它没有硬件的 TLB、无需权限与页缓存管理。真实 TLB 是 CPU 内部一组**相联缓存**：命中即 O(1)、只有 TLB miss 才做页表遍历且常带 ASID/多级（L1/L2）缓存、需维护局部性替换与 shootdown（多核/IPI 使失效）。QEMU 则是每条指令都先查它的翻译缓存，miss 时调 `riscv_cpu_tlb_fill` **每次实打实跑一遍完整页表遍历并填充缓存**，没有硬件级的相联性和并发失效语义。

---

## 六、测试与验证
**负责人：肖文哲**

*   **`make qemu` 正常启动验证**：
    在执行了 `entry.S` 栈指针修复和 `rustsbi-qemu.bin` 替换后，QEMU 能成功加载内核并输出：
    ```text
    [rustsbi] Supervisor Address : 0x80200000
    ...
    (THU.CST) os is loading ...
    physical memory map:
      memory: 0x0000000008000000, [0x0000000080000000, 0x0000000087ffffff].
    memory management: best_fit_pmm_manager
    ```
*   **判分机制说明**：`make grade` 会把目录拷到 `/tmp/lab2_grade`，用 `tools/inject.py` 把 `tools/test_lab2.c` 注入成 `kern/test/`，在 `pmm_init()` 末尾自动调 `test_page_init(); test_best_fit();`，再用 `-serial file:.qemu.out` 抓串口输出去 grep `[Test 1] … PASS`。
*   **未通过原因**：本次实验在本地（WSL/Windows）验证 `make grade` 时，由于实验提供的 `grade.sh` 中使用的 QEMU 启动参数（`-device loader`）与本机的 QEMU 版本存在兼容性问题，导致 `qemu_out` 为空或超时，未能成功截取到 PASS 字符。代码逻辑已通过宿主机算法级测试，真实内核启动验证截图已作为附件提供。

---

## 七、实验总结与收获
**负责人：税思远主导，全体补充**
### 1. 知识点与 OS 原理对比
| 实验知识点 | OS原理对应 | 关系与差异 |
|-----------|-----------|-----------|
| SV39 三级页表遍历 | 分页 / 多级页表 | 原理一致：逐级索引、末级拿 PPN、拼 offset。差异在级数/页大小。 |
| `va_pa_offset` 线性映射 | 直接映射 / 恒等映射段 | 本实验靠单一偏移把 KVA 翻译到裸机 RAM，无逐页 PTE 换算开销。 |
| 页描述符数组 `pages[]` | 页表项 / 物理页账本 | OS 原理强调页表项（VA→PA），本实验补了**页描述符**；`le2page` 用成员偏移反解对象首地址是通用 `container_of` 思想。 |
| Best-Fit / First-Fit 分配 | 连续分配策略 | 都是空闲块链表 + 选择 + 剩余回挂。Best-Fit 碎片更小但 `O(n)` 全扫；First-Fit 快但头部分裂严重。 |
| 保留区排除 | OS 不可分配区 | 实验精确落地：只有 `[ROUNDUP(数组尾), mem_end)` 交给分配器，否则会把固件/内核二次分配。 |

### 2. OS 原理中重要但实验未覆盖的知识点
*   虚拟内存的按需分页与缺页异常处理。
*   进程地址空间的隔离与多级页表的高效管理。
*   SLUB/内存池等针对小对象的精细化分配器。

### 3. AI 协作开发经验与 Prompt 汇总
*   **环境排查**：AI 在定位“启动黑屏”时起到了关键作用，通过阅读 `entry.S` 和启动日志，发现了 `lui` 漏掉 `addi` 低位偏移的致命 Bug。
*   **代码生成**：使用 `[PROMPT]/[GUARANTEE]/[SPECIFICATION]/[RELY]` 模板式提示词，将 `page_init` 和 `Best-Fit` 的功能边界锁死，AI 一次生成的代码逻辑相对完整，后续只需微调。
*   **经验教训**：AI 生成的长段 C 代码（如嵌套的 `cprintf`）在通过命令行注入时极易破坏转义字符（如 `\n`），建议直接让 AI 修改文件，而不是通过终端文本处理。