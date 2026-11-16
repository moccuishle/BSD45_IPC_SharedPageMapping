# S17：缓存系统移植（buffer cache / name cache / page cache / mmap）

> 状态：**已实现并验收**（Windows 17 套全绿）
> 基线：4.5BSD-Lite2 i386 端口，路线 B（把 4.5BSD 的缓存职责写进当前端口）
> 相关代码：`kernel/fs/vfs_bio.c`、`kernel/fs/vfs_cache.c`、`kernel/vm/pagecache.c`、
> `kernel/fs/vfs.c`、`kernel/fs/isofs.c`、`kernel/fs/tmpfs.c`、
> `kernel/sys/usermode.c`、`libexec/{vmstat,mmaptest}.c`

---

## 1. 为什么不是“把 4.5BSD 的 vfs_bio.c 加进构建”

`kernel/sys/` 里的 4.5BSD VFS/VM 文件是冲着完整 4.5BSD 内核核心写的：

| 文件 | 真实状态 |
|---|---|
| `kernel/sys/vfs_bio.c` | 函数体被删空（`bread()` 只有 `/* Body deleted. */ return (EIO);`） |
| `kernel/sys/vfs_cache.c` | 完整，但用 `(dvp)->v_id`，我们的 vnode 原本没有 `v_id` |
| `vfs_subr.c` / `vfs_lookup.c` / `vfs_vnops.c` / `vfs_syscalls.c` / `vfs_cluster.c` | 依赖 `ucred`/`mount`/`uio`/`VOP_*`/`vnode_if.h`/`sleep-wakeup`/`M_*`/`bdevsw->d_strategy` |
| `vm_page.c` / `vm_object.c` / `vm_fault.c` / `vm_pageout.c` / `vnode_pager.c` | 依赖完整 `vm_map`/`proc`/pager 注册；本端口只有 `pmap` |
| `kernel/fs/ufs/*` | 只有 `config.mk`，没有任何实现；也没有 ATA 硬盘驱动 |

因此采用**路线 B**：按 4.5BSD 的职责分层，把真正的缓存算法写进当前端口。
算法结构与 4.5BSD 一一对应，以后走路线 A（真·移植内核核心）时可以原样搬过去。

---

## 2. 分层

```
用户 read(2) / mmap(2)
        |
        v
  vnodeops->read            vfs.c (ISO) / tmpfs.c
        |
        v
  pagecache.c              4K 文件页缓存（= vm_object + vm_page + vnode_pager）
        |  miss: vnodeops->readraw
        v
  vfs_bio.c                2K 块缓冲（= vfs_bio.c 的 buffer cache）
        |  miss: atapi_read
        v
  ATAPI CD (只读)
```

- **vfs_bio.c**：`bread/breada/brelse/bwrite/bdwrite`，hash + LRU，64 个 2K 块。
- **vfs_cache.c**：目录项缓存 `(dv->v_id, name) -> vnode`，create/remove/rename 时 purge。
- **pagecache.c**：`(key, pindex) -> 4K 页`，hash + LRU + 引用计数，`mmap` 的页源。
- **vfs.c**：`v_id`/`v_key` 分配、`namei()` 查 name cache、目录 vnode 保留、
  `/dev/vmstat` 的文本渲染。
- **usermode.c**：`mmap/munmap/mprotect/msync`、VMA 表、退出/exec 时 teardown。

---

## 3. buffer cache（`kernel/fs/vfs_bio.c`）

| 项 | 值 |
|---|---|
| 块大小 | 2048 字节（一个 CD sector） |
| 缓冲数 | 64（启动时一次 `pmap_map` 到 `0xFE800000`，落在页面统计基线里） |
| 索引 | hash `(dev,blkno)` 32 桶 + 单一 LRU |
| 接口 | `bread` / `breada`（预读）/ `brelse` / `bwrite` / `bdwrite` / `buf_data` |
| 统计 | `bio_stats()`：total/used/hits/misses/reads/readahead/dirty |

要点：

1. **同步 I/O**：驱动是轮询 PIO，miss 时直接 `atapi_read()`，没有 sleep/wakeup。
2. **预读**：`iso_readraw_op()` 每读一个块就对该文件后续最多 4 个块调
   `breada()`；块号被 `vp->size` 换算的 sector 数钳住，不会读到文件外。
3. **写路径保留**：`bwrite/bdwrite` 与 dirty 计数都在，但唯一的设备是只读 CD，
   写返回 `EROFS`；将来接 ATA 盘时只需实现 `bio_read_block/bwrite` 的写侧。
4. **淘汰**：LRU 头部且 `b_refcnt == 0` 的块可复用；dirty 先回写（当前不会出现）。

ISO9660 的所有元数据都走它：PVD（`iso_mount`）、目录扫描
（`find_in_dir`）、`iso_readdir()`、`iso_read_file()`。

---

## 4. name cache（`kernel/fs/vfs_cache.c`）

| 项 | 值 |
|---|---|
| 容量 | 64 项，32 桶 hash |
| 键 | `(parent->v_id, name)` |
| 校验 | 命中后必须 `vp->v_id != 0 && vp->v_id == 记录的 v_id`，否则当作 miss 丢弃 |
| 引用 | 缓存项**不**持有 vnode 引用，命中时才 `vp->ref++`（返回已引用 vnode） |
| 失效 | `create`/`remove`/`rename` 显式 `namecache_purge`；`vn_free()` 清掉所有相关项 |

两个关键设计：

1. **v_id 复用防护**：vnode 池是 LIFO 复用，指针身份不稳定。
   `vn_alloc()` 分配新 `v_id`，`vn_free()` 把 `v_id` 清零，所以旧缓存项绝不会
   复活一个已释放的 slot。
2. **目录 vnode 保留**（`dir_cache_keep`，8 个槽）：普通文件每次 open/close
   都会释放，但 `root -> bin -> file` 的目录 vnode 保留下来，目录项缓存才能
   在多次 exec 之间命中。淘汰时调用 `vn_free()` 一并清 name/page cache。

统计：`namecache_stats()`：total/used/hits/misses。

---

## 5. page cache（`kernel/vm/pagecache.c`）

| 项 | 值 |
|---|---|
| 页大小 | 4096 字节 |
| 容量 | 128 页（启动时预分配并 `pmap_map` 到 `0xFE900000`） |
| 索引 | hash `(key, pindex)` 32 桶 + LRU |
| 引用 | `refs`（读入/映射中）；`refs == 0` 才在 LRU 上可淘汰 |
| 填充 | `vnodeops->readraw`（ISO：buffer cache；tmpfs：节点数据页） |
| 统计 | `pagecache_stats()`：total/used/hits/misses/fills/evicts |

**缓存键**（`pagecache_key()`）：

- ISO9660：`v_key = 0x80000000 | lba`。文件不可变，按 LBA 做键，所以
  close/reopen 后页还在 —— 第二次 `cat`/`exec` 不再读盘。
- tmpfs：`v_key == 0`，退化为 `v_id`；写/截断时 `pagecache_purge_vp()`，
  文件删除、vnode 释放时随之失效。

**EOF**：`pagecache_get()` 对文件尾页做零填充（`mmap` 需要）；
`pagecache_read()` 在入口按 `st_size` 钳一次长度，否则 `cat` 会在 EOF 后
一直读到零字节而死循环（这是移植时真实踩到的坑）。

**页回收**：页表池是启动时固定的，缓存淘汰只换内容不换物理页，因此
S10.1 的 `vstat` 里 `pages ... inuse` 的基线包含 192 页缓存，压力循环后
仍回到同一基线。

---

## 6. mmap / munmap / msync（`kernel/sys/usermode.c`）

本端口没有缺页处理（page fault 会停机），所以映射采取**立即填充**：

| 组合 | 行为 |
|---|---|
| `MAP_ANON` | 新分配零页，按 `prot` 映射 |
| `MAP_PRIVATE` + 文件 | 从 page cache 复制一页到私有页；写不影响文件 |
| `MAP_SHARED` + 文件 | 直接映射 page cache 的物理页；`msync()` 经 `vops->write` 回写 |
| `MAP_SHARED` + 只读后端 + `PROT_WRITE` | `EROFS` |

- **ABI**：4.5BSD `mmap` 有 7 个逻辑参数，本端口寄存器传前 5 个
  （addr/len/prot/flags/fd），**只支持 offset == 0**，libc 封装检测 `pos != 0`
  直接返回 `EINVAL`。
- **地址空间**：非 `MAP_FIXED` 从 `0x10000000` 向上分配，上限 `0x30000000`；
  每个进程 8 个 VMA，单次最大 4 MB。
- **`munmap`**：共享页清 PTE + `pagecache_unref_key`；私有/匿名页清 PTE 并
  `page_free`。
- **退出/exec**：`mmap_teardown()` 先解除共享页的 PTE 并放引用，避免
  `free_address_space()` 把 page cache 的物理页当私有页回收（双重释放）。
  私有页留给 `free_address_space()`。

**限制**（有意为之，文档记录）：

- 没有 demand paging / COW：`MAP_PRIVATE` 是 map 时复制；
- 不支持非零 offset、`mprotect` 不改变已建 PTE、没有 `madvise`；
- `MAP_SHARED` 写回只在 `msync`/显式路径发生，进程崩溃不会自动回写。

---

## 7. 观测接口

- `/dev/vmstat`（伪文件，每次 read 重新渲染）：

  ```
  vnode: total 96 used 2 free 94
  buf: total 64 used 64 hits 507 misses 11 reads 182 readahead 171 dirty 0
  name: total 64 used 2 hits 4 misses 11
  page: total 128 used 89 hits 45 misses 89 fills 89 evicts 0
  pages: alloc 550 freed 54 inuse 496
  ```

- `/bin/vmstat`：`cat /dev/vmstat` 的等价物。
- monitor `vstat`：原有 vnode/页计数之后追加同样的缓存行（不破坏 S10 的解析）。

**统计字段含义**：

| 字段 | 含义 |
|---|---|
| `buf hits/misses` | 块命中 / 未命中（miss 触发一次磁盘读） |
| `buf reads` | 实际 `atapi_read` 的块数（含预读） |
| `buf readahead` | 预读装入的块数 |
| `name hits/misses` | 目录项缓存命中率 |
| `page hits/fills` | 页缓存命中 / 从后端填充 |
| `page evicts` | 因容量淘汰的页数 |

---

## 8. 验收

新增两套，`tools/all_tests.sh` 从 15 套扩到 **17 套**：

- `tools/test_cache.ps1` / `.sh`：
  `cat /readme.txt` → `vmstat` → 再 `cat` → `vmstat`，断言
  `buf.hits` 增长、`buf.reads` 不变、`page.hits` 增长、`page.fills` 不变、
  `name.hits > 0`、无 TRAP、`[user exit 0]`。
- `tools/test_mmap.ps1` / `.sh`：`/bin/mmaptest`，断言
  `mmap: file ok match=1`、`mmap: anon ok`、`mmap: shared ok`、
  `mmap: ok`、无 TRAP。

实测（QEMU）：

```
S17 acceptance: buffer / name / page caches
  info buffer hits 413 -> 507, disk reads 182 -> 182
  info page hits 2 -> 45, fills 89 -> 89
  info name-cache hits 4
S17 caches: PASSED

S17 acceptance: mmap + page cache
  mmap: file ok match=1
  mmap: anon ok
  mmap: shared ok
  mmap: ok
S17 mmap: PASSED
```

---

## 9. 踩坑记录（S17）

1. **page cache 的 LRU 不能只放空闲项**：第一版只把“未使用”的页放 LRU，
   128 页填满后 `pc_take()` 永远返回空，`exec /bin/mmaptest` 直接 `EIO`。
   修正：所有 `refs == 0` 的项（无论 used）都在 LRU 上，`refs > 0` 时摘除，
   `pagecache_unref` 归零时重新挂回。
2. **`read()` 必须在 EOF 钳长度**：页缓存对尾页零填充是给 `mmap` 用的；
   但 `read` 若也无限返回零字节，`cat` 会死循环（第一次测试就是卡在这里，
   shell 再也不出提示符）。
3. **ISO 页缓存键不能用 v_id**：文件 vnode 每次 open 都新建，close 即释放；
   必须用不可变的 LBA（`v_key`）才能在第二次 `cat` 命中。
4. **name cache 需要目录 vnode 存活**：否则 `vn_free` 会把刚填的目录项一起
   清掉，命中率永远是 0。用 8 槽目录缓存平衡（且 S10 的 vnode 基线不受影响，
   因为 `/readme.txt` 是普通文件，用完即释放）。
5. **共享映射页不能交给 `free_address_space()`**：`MAP_SHARED` 的页属于
   page cache；`proc_exit` 前必须 `mmap_teardown()` 清 PTE + 放引用，
   否则物理页被双重释放。
6. **`pipe /dev/vmstat` 不要照抄 `/dev/ps` 的静态快照**：计数器是活的，
   每次 read 重新 `vfs_cache_report()`。

