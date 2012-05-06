//
//  Kmem.c
//  Kmem
//
//  Created by comex on 5/6/12.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//

#include <mach/mach_types.h>
#include <mach/vm_param.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/filio.h>
#include <sys/errno.h>
#include <sys/conf.h>
#include <sys/systm.h>
#include <miscfs/devfs/devfs.h>
#include <libkern/libkern.h>

#define VM_MIN_KERNEL_ADDRESS       ((vm_offset_t) 0xFFFFFF8000000000UL)
#define VM_MIN_KERNEL_AND_KEXT_ADDRESS  (VM_MIN_KERNEL_ADDRESS - 0x80000000ULL)
#define VM_MAX_KERNEL_ADDRESS       ((vm_offset_t) 0xFFFFFFFFFFFFEFFFUL)

typedef struct pmap     *pmap_t;

extern vm_map_t kernel_map;
extern pmap_t kernel_pmap;

extern kern_return_t    kmem_alloc(
                                   vm_map_t    map,
                                   vm_offset_t *addrp,
                                   vm_size_t   size);

extern void     kmem_free(
                          vm_map_t    map,
                          vm_offset_t addr,
                          vm_size_t   size);


extern ppnum_t          pmap_find_phys(pmap_t map, addr64_t va);


kern_return_t Kmem_start(kmod_info_t * ki, void *d);
kern_return_t Kmem_stop(kmod_info_t *ki, void *d);

static int mmread(dev_t dev, struct uio *uio, int ioflag);
static int mmwrite(dev_t dev, struct uio *uio, int ioflag);
static int mmioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p);
static int mmrw(dev_t dev, struct uio *uio, enum uio_rw rw);
static boolean_t kernacc(off_t start, size_t len);

int
mmread(dev_t dev, struct uio *uio, int ioflag)
{
    
	return (mmrw(dev, uio, UIO_READ));
}

int
mmwrite(dev_t dev, struct uio *uio, int ioflag)
{
    
	return (mmrw(dev, uio, UIO_WRITE));
}

int
mmioctl(dev_t dev, u_long cmd, __unused caddr_t data, 
		__unused int flag, __unused struct proc *p)
{
	switch (cmd) {
        case FIONBIO:
        case FIOASYNC:
            /* OK to do nothing: we always return immediately */
            break;
        default:
            return ENODEV;
	}
    
	return (0);
}

int
mmrw(dev_t dev, struct uio *uio, enum uio_rw rw)
{
	register int o, c;
    register addr64_t v;
	int error = 0;
	vm_offset_t	where;
	vm_size_t size;
    
    
	while (uio_resid(uio) > 0 && error == 0) {
		uio_update(uio, 0);
        addr64_t offset = (addr64_t) uio_offset(uio) | (VM_MAX_KERNEL_ADDRESS & (1ull << 63));
        
		switch (minor(dev)) {
                
                /* minor device 0 is physical memory */
            case 0:
                
                v = trunc_page(offset);
                if (offset >= mem_size)
                    goto fault;
                
                size = PAGE_SIZE;
                if (kmem_alloc(kernel_map, &where, size) 
                    != KERN_SUCCESS) {
                    goto fault;
                }
                o = (int) (offset - v);
                c = min(PAGE_SIZE - o, (int) uio_curriovlen(uio));
                error = uiomove((caddr_t) (where + o), c, uio);
                kmem_free(kernel_map, where, PAGE_SIZE);
                continue;
                
                /* minor device 1 is kernel memory */
            case 1:
                /* Do some sanity checking */
                if ((offset >= VM_MAX_KERNEL_ADDRESS) ||
                    (offset <= VM_MIN_KERNEL_AND_KEXT_ADDRESS))
                    goto fault;
                c = (int) uio_curriovlen(uio);
                if (!kernacc(offset, c))
                    goto fault;
                error = uiomove((caddr_t)(uintptr_t)offset,
                                (int)c, uio);
                continue;
                
            default:
                goto fault;
                break;
		}
        
		if (error)
			break;
		uio_update(uio, c);
	}
	return (error);
fault:
	return (EFAULT);
}


boolean_t
kernacc(
        off_t 	start,
        size_t	len
        )
{
	off_t base;
	off_t end;
    
	base = trunc_page(start);
	end = start + len;
	
	while (base < end) {
        if(!pmap_find_phys(kernel_pmap, base))
			return(FALSE);
		base += page_size;
	}   
    
	return (TRUE);
}

#define nullopen    (d_open_t *)&nulldev
#define nullclose   (d_close_t *)&nulldev
#define nullstop    (d_stop_t *)&nulldev
#define nullreset   (d_reset_t *)&nulldev
#define mmselect    (select_fcn_t *)seltrue

static int
seltrue(__unused dev_t dev, __unused int flag, __unused struct proc *p) 
{    
    return (1);
}

static struct cdevsw csw[] = {
    nullopen,   nullclose,  mmread,     mmwrite,    /* 3*/
    mmioctl,    nullstop,   nullreset,  0,          mmselect,
    eno_mmap,   eno_strat,  eno_getc,   eno_putc,   D_DISK
};

static void *mem, *kmem;
static int major;


kern_return_t Kmem_start(kmod_info_t * ki, void *d)
{
    major = cdevsw_add(-24, csw);
    if(!(mem = devfs_make_node(makedev(major, 0), DEVFS_CHAR, UID_ROOT, GID_KMEM, 0640, "mem")) ||
       !(kmem = devfs_make_node(makedev(major, 1), DEVFS_CHAR, UID_ROOT, GID_KMEM, 0640, "kmem")))
        return KERN_FAILURE;
    
    return KERN_SUCCESS;

}

kern_return_t Kmem_stop(kmod_info_t *ki, void *d)
{
    devfs_remove(mem);
    devfs_remove(kmem);
    cdevsw_remove(major, csw);
    return KERN_SUCCESS;
}
