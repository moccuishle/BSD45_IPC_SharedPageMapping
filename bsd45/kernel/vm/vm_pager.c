/* 
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * The Mach Operating System project at Carnegie-Mellon University.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)vm_pager.c	8.7 (Berkeley) 7/7/94
 *
 *
 * Copyright (c) 1987, 1990 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Authors: Avadis Tevanian, Jr., Michael Wayne Young
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

/*
 *	Paging space routine stubs.  Emulates a matchmaker-like interface
 *	for builtin pagers.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_kern.h>

#ifdef SWAPPAGER
extern struct pagerops swappagerops;
#endif

#ifdef VNODEPAGER
extern struct pagerops vnodepagerops;
#endif

#ifdef DEVPAGER
extern struct pagerops devicepagerops;
#endif

struct pagerops *pagertab[] = {
#ifdef SWAPPAGER
	&swappagerops,		/* PG_SWAP */
#else
	NULL,
#endif
#ifdef VNODEPAGER
	&vnodepagerops,		/* PG_VNODE */
#else
	NULL,
#endif
#ifdef DEVPAGER
	&devicepagerops,	/* PG_DEV */
#else
	NULL,
#endif
};
int npagers = sizeof (pagertab) / sizeof (pagertab[0]);

struct pagerops *dfltpagerops = NULL;	/* default pager */

/*
 * Kernel address space for mapping pages.
 * Used by pagers where KVAs are needed for IO.
 *
 * XXX needs to be large enough to support the number of pending async
 * cleaning requests (NPENDINGIO == 64) * the maximum swap cluster size
 * (MAXPHYS == 64k) if you want to get the most efficiency.
 */
#define PAGER_MAP_SIZE	(4 * 1024 * 1024)

vm_map_t pager_map;
boolean_t pager_map_wanted;
vm_offset_t pager_sva, pager_eva;

/* 中文：初始化分页器子系统：为页面映射创建内核子映射，并逐个调用所有已注册分页器的初始化例程 */
/* EN: Initialize the pager subsystem: create a kernel submap for page mappings and call the init routine of every registered pager */
void
vm_pager_init()
{
	struct pagerops **pgops;

	/*
	 * Allocate a kernel submap for tracking get/put page mappings
	 */
	pager_map = kmem_suballoc(kernel_map, &pager_sva, &pager_eva,
				  PAGER_MAP_SIZE, FALSE);
	/*
	 * Initialize known pagers
	 */
	for (pgops = pagertab; pgops < &pagertab[npagers]; pgops++)
		if (pgops)
			(*(*pgops)->pgo_init)();
	if (dfltpagerops == NULL)
		panic("no default pager");
}

/*
 * Allocate an instance of a pager of the given type.
 * Size, protection and offset parameters are passed in for pagers that
 * need to perform page-level validation (e.g. the device pager).
 */
/* 中文：按给定类型创建一个分页器实例：把 size、保护模式和偏移传给该类型分页器的分配例程 */
/* EN: Allocate a pager instance of the given type: pass size, protection and offset to that pager's allocation routine */
vm_pager_t
vm_pager_allocate(type, handle, size, prot, off)
	int type;
	caddr_t handle;
	vm_size_t size;
	vm_prot_t prot;
	vm_offset_t off;
{
	struct pagerops *ops;

	ops = (type == PG_DFLT) ? dfltpagerops : pagertab[type];
	if (ops)
		return ((*ops->pgo_alloc)(handle, size, prot, off));
	return (NULL);
}

/* 中文：释放一个分页器实例（传入空指针时触发 panic） */
/* EN: Deallocate a pager instance (panics on a null pager) */
void
vm_pager_deallocate(pager)
	vm_pager_t	pager;
{
	if (pager == NULL)
		panic("vm_pager_deallocate: null pager");

	(*pager->pg_ops->pgo_dealloc)(pager);
}

/* 中文：从分页器读取 npages 个页面；若分页器为空则直接用零填充这些页面 */
/* EN: Read npages pages from a pager; when the pager is null, zero-fill the pages instead */
int
vm_pager_get_pages(pager, mlist, npages, sync)
	vm_pager_t	pager;
	vm_page_t	*mlist;
	int		npages;
	boolean_t	sync;
{
	int rv;

	if (pager == NULL) {
		rv = VM_PAGER_OK;
		while (npages--)
			if (!vm_page_zero_fill(*mlist)) {
				rv = VM_PAGER_FAIL;
				break;
			} else
				mlist++;
		return (rv);
	}
	return ((*pager->pg_ops->pgo_getpages)(pager, mlist, npages, sync));
}

/* 中文：把 npages 个页面写回分页器（调用其 pgo_putpages 例程） */
/* EN: Write npages pages back to a pager by calling its pgo_putpages routine */
int
vm_pager_put_pages(pager, mlist, npages, sync)
	vm_pager_t	pager;
	vm_page_t	*mlist;
	int		npages;
	boolean_t	sync;
{
	if (pager == NULL)
		panic("vm_pager_put_pages: null pager");
	return ((*pager->pg_ops->pgo_putpages)(pager, mlist, npages, sync));
}

/* XXX compatibility*/
/* 中文：兼容接口：从分页器读取单个页面 */
/* EN: Compatibility wrapper: read a single page from a pager */
int
vm_pager_get(pager, m, sync)
	vm_pager_t	pager;
	vm_page_t	m;
	boolean_t	sync;
{
	return vm_pager_get_pages(pager, &m, 1, sync);
}

/* XXX compatibility*/
/* 中文：兼容接口：向分页器写回单个页面 */
/* EN: Compatibility wrapper: write a single page back to a pager */
int
vm_pager_put(pager, m, sync)
	vm_pager_t	pager;
	vm_page_t	m;
	boolean_t	sync;
{
	return vm_pager_put_pages(pager, &m, 1, sync);
}

/* 中文：查询分页器在指定偏移处是否持有页面 */
/* EN: Ask a pager whether it holds a page at the given offset */
boolean_t
vm_pager_has_page(pager, offset)
	vm_pager_t	pager;
	vm_offset_t	offset;
{
	if (pager == NULL)
		panic("vm_pager_has_page: null pager");
	return ((*pager->pg_ops->pgo_haspage)(pager, offset));
}

/*
 * Called by pageout daemon before going back to sleep.
 * Gives pagers a chance to clean up any completed async pageing operations.
 */
/* 中文：在 pageout 守护进程再次睡眠前调用，给各分页器清理已完成的异步分页操作的机会 */
/* EN: Called by the pageout daemon before sleeping again, giving each pager a chance to clean up completed asynchronous paging operations */
void
vm_pager_sync()
{
	struct pagerops **pgops;

	for (pgops = pagertab; pgops < &pagertab[npagers]; pgops++)
		if (pgops)
			(*(*pgops)->pgo_putpages)(NULL, NULL, 0, FALSE);
}

/* 中文：调用分页器的聚类例程，求出给定偏移附近连续页面的起止范围 */
/* EN: Call the pager's clustering routine to find the range of pages surrounding a given offset */
void
vm_pager_cluster(pager, offset, loff, hoff)
	vm_pager_t	pager;
	vm_offset_t	offset;
	vm_offset_t	*loff;
	vm_offset_t	*hoff;
{
	if (pager == NULL)
		panic("vm_pager_cluster: null pager");
	((*pager->pg_ops->pgo_cluster)(pager, offset, loff, hoff));
}

/* 中文：不支持聚类的分页器所使用的占位例程，被调用时直接 panic */
/* EN: Placeholder clustering routine for pagers without clustering support; panics when called */
void
vm_pager_clusternull(pager, offset, loff, hoff)
	vm_pager_t	pager;
	vm_offset_t	offset;
	vm_offset_t	*loff;
	vm_offset_t	*hoff;
{
	panic("vm_pager_nullcluster called");
}

/* 中文：把 npages 个物理页面映射到 pager_map 内核子映射中，返回映射的起始虚拟地址 */
/* EN: Map npages physical pages into the pager_map kernel submap and return the starting virtual address */
vm_offset_t
vm_pager_map_pages(mlist, npages, canwait)
	vm_page_t	*mlist;
	int		npages;
	boolean_t	canwait;
{
	vm_offset_t kva, va;
	vm_size_t size;
	vm_page_t m;

	/*
	 * Allocate space in the pager map, if none available return 0.
	 * This is basically an expansion of kmem_alloc_wait with optional
	 * blocking on no space.
	 */
	size = npages * PAGE_SIZE;
	vm_map_lock(pager_map);
	while (vm_map_findspace(pager_map, 0, size, &kva)) {
		if (!canwait) {
			vm_map_unlock(pager_map);
			return (0);
		}
		pager_map_wanted = TRUE;
		vm_map_unlock(pager_map);
		(void) tsleep(pager_map, PVM, "pager_map", 0);
		vm_map_lock(pager_map);
	}
	vm_map_insert(pager_map, NULL, 0, kva, kva + size);
	vm_map_unlock(pager_map);

	for (va = kva; npages--; va += PAGE_SIZE) {
		m = *mlist++;
#ifdef DEBUG
		if ((m->flags & PG_BUSY) == 0)
			panic("vm_pager_map_pages: page not busy");
		if (m->flags & PG_PAGEROWNED)
			panic("vm_pager_map_pages: page already in pager");
#endif
#ifdef DEBUG
		m->flags |= PG_PAGEROWNED;
#endif
		pmap_enter(vm_map_pmap(pager_map), va, VM_PAGE_TO_PHYS(m),
			   VM_PROT_DEFAULT, TRUE);
	}
	return (kva);
}

/* 中文：从 pager_map 中取消映射 npages 个页面，并清除对应页面的 PG_PAGEROWNED 标志 */
/* EN: Unmap npages pages from pager_map and clear the PG_PAGEROWNED flag on the corresponding pages */
void
vm_pager_unmap_pages(kva, npages)
	vm_offset_t	kva;
	int		npages;
{
	vm_size_t size = npages * PAGE_SIZE;

#ifdef DEBUG
	vm_offset_t va;
	vm_page_t m;
	int np = npages;

	for (va = kva; np--; va += PAGE_SIZE) {
		m = vm_pager_atop(va);
		if (m->flags & PG_PAGEROWNED)
			m->flags &= ~PG_PAGEROWNED;
		else
			printf("vm_pager_unmap_pages: %x(%x/%x) not owned\n",
			       m, va, VM_PAGE_TO_PHYS(m));
	}
#endif
	pmap_remove(vm_map_pmap(pager_map), kva, kva + size);
	vm_map_lock(pager_map);
	(void) vm_map_delete(pager_map, kva, kva + size);
	if (pager_map_wanted)
		wakeup(pager_map);
	vm_map_unlock(pager_map);
}

/* 中文：将 pager_map 中的内核虚拟地址转换为对应的 vm_page 结构 */
/* EN: Translate a kernel virtual address in pager_map into its corresponding vm_page */
vm_page_t
vm_pager_atop(kva)
	vm_offset_t	kva;
{
	vm_offset_t pa;

	pa = pmap_extract(vm_map_pmap(pager_map), kva);
	if (pa == 0)
		panic("vm_pager_atop");
	return (PHYS_TO_VM_PAGE(pa));
}

/* 中文：在分页器链表中按 handle 查找分页器实例，找不到返回 NULL */
/* EN: Look up a pager instance by handle in a pager list, returning NULL if absent */
vm_pager_t
vm_pager_lookup(pglist, handle)
	register struct pagerlst *pglist;
	caddr_t handle;
{
	register vm_pager_t pager;

	for (pager = pglist->tqh_first; pager; pager = pager->pg_list.tqe_next)
		if (pager->pg_handle == handle)
			return (pager);
	return (NULL);
}

/*
 * This routine gains a reference to the object.
 * Explicit deallocation is necessary.
 */
/* 中文：设置或清除 vm_object 的 OBJ_CANPERSIST 标志以控制其是否可持久缓存，并释放对象引用 */
/* EN: Set or clear OBJ_CANPERSIST on a vm_object to control whether it may persist in the cache, then release the object reference */
int
pager_cache(object, should_cache)
	vm_object_t	object;
	boolean_t	should_cache;
{
	if (object == NULL)
		return (KERN_INVALID_ARGUMENT);

	vm_object_cache_lock();
	vm_object_lock(object);
	if (should_cache)
		object->flags |= OBJ_CANPERSIST;
	else
		object->flags &= ~OBJ_CANPERSIST;
	vm_object_unlock(object);
	vm_object_cache_unlock();

	vm_object_deallocate(object);

	return (KERN_SUCCESS);
}
