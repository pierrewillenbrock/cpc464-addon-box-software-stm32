
#include <malloc.h>
#include <irq.h>
#include <string.h>
#include <assert.h>
#include <bsp/core_cm4.h>

#define ALLOC_LIST_SIZE 1024
static struct Alloc {
  void *ptr;
  uint16_t size;
  uint8_t inuse;
} alloc_list[ALLOC_LIST_SIZE] = { { 0 } };

extern char _bheap[];//top of data section
extern char _eheap[];//bottom of stack section

/* we only override the default memory map as needed, that is: for the
   section between _bheap and _eheap.
 */
extern "C" {
  void MPROT_Setup();
}

static uint8_t region_min = 0;
static uint8_t region_count = 0;

static uint32_t region_gen = 0;
static struct {
  uint32_t base;
  uint32_t gen;
} regions[16];

void MPROT_Setup() {
  SCB_Type *scb = SCB;
  MPU_Type *mpu = MPU;

  mpu->CTRL = 0;
  region_count = mpu->TYPE >> 8;

  mpu->RBAR = 0x20000010;
  mpu->RASR = 0x10000021;

  //region for stack:
  mpu->RBAR = 0x2001f011;
  mpu->RASR = 0x13290017;

  uint32_t base = 0x20000000;
  unsigned region_no = 2;
  while(base < ((uint32_t)&_bheap)) {
    //region for static data:
    unsigned size = ((uint32_t)&_bheap) - base;
    //find the next higher 1<<n
    unsigned n = 0;
    if (size < 32)
      size = 32;
    while((1<<n) < size)
      n++;
    unsigned subreg_cnt = size / (1 << (n-3));
    if (n < 8)
      subreg_cnt = 8;
    mpu->RBAR = base | 0x10 | region_no;
    mpu->RASR = 0x13290001 | ((n-1) << 1) |
      ((0xff0000 >> (8-subreg_cnt)) & 0xff00);
    base += subreg_cnt * (1 << (n-3));
    region_no++;
  }
  region_min = region_no;
  //0 - 0x5000 (0x10000, 5 subregions),
  //0x5000-0x5500 (0x800, 5 subregions),
  //0x5500-0x5520 (0x100, 1 subregions),
  scb->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk;
  scb->SHCSR |= SCB_SHCSR_USGFAULTENA_Msk;//enable the other two as well.
  scb->SHCSR |= SCB_SHCSR_BUSFAULTENA_Msk;
  __ISB();
  __DSB();
  mpu->CTRL = 5;
  __ISB();
  __DSB();
}

static void mprot_unprotect() {
  MPU_Type *mpu = MPU;
  mpu->CTRL = 0;
  __ISB();
  __DSB();
}

static void mprot_protect() {
  MPU_Type *mpu = MPU;
  mpu->CTRL = 5;
  __ISB();
  __DSB();
}

void MemManage_Handler(void)
{
  //get the address of the failure
  SCB_Type *scb = SCB;
  MPU_Type *mpu = MPU;
  if (scb->CFSR & 0x01) {
    //IACCVIOL
    assert(0);
  }
  if (scb->CFSR & 0x08) {
    //MUNSTKERR
    assert(0);
  }
  if (scb->CFSR & 0x10) {
    //MSTKERR
    assert(0);
  }
  if (scb->CFSR & 0x20) {
    //MLSPERR
    assert(0);
  }
  if (scb->CFSR & 0x02) {
    //DACCVIOL
    //we can handle that.
    if (!(scb->CFSR & 0x80)) {
      //but we need the address...
      assert(0);
    }
    uint32_t addr = scb->MMFAR;
    //find the allocation
    unsigned i;
    for(i = 0; i < ALLOC_LIST_SIZE; i++) {
      if ((uint32_t)alloc_list[i].ptr <= addr &&
	  ((uint32_t)alloc_list[i].ptr) + alloc_list[i].size > addr &&
	  alloc_list[i].inuse)
	break;
    }
    assert(i<ALLOC_LIST_SIZE);
    //found it. now find a region to unprotect.
    unsigned region_no = region_min;
    for(unsigned i = region_min; i < region_count; i++) {
      if (regions[i].gen < regions[region_no].gen)
	region_no = i;
    }
    uint32_t base = addr & ~0xff;
    regions[region_no].gen = ++region_gen;
    regions[region_no].base = base;
    unsigned subregions = 0xff00;
    for(unsigned int j = 0; j < 8; j++) {
      if (((uint32_t)alloc_list[i].ptr)+alloc_list[i].size <= base + j*32 ||
	  ((uint32_t)alloc_list[i].ptr) >= base + 32 + j*32)
	continue;
      subregions &= ~(0x100 << j);
    }
    mpu->RBAR = base | 0x10 | region_no;
    mpu->RASR = 0x1329000f | subregions;
    //need to protect the next 256 byte block as well
    if ((addr & 0xff) > 0xfc) {
      base += 0x100;
      unsigned region_no = region_min;
      for(unsigned i = region_min; i < region_count; i++) {
	if (regions[i].gen < regions[region_no].gen)
	  region_no = i;
      }
      regions[region_no].gen = ++region_gen;
      regions[region_no].base = base;
      unsigned subregions = 0xff00;
      for(unsigned int j = 0; j < 8; j++) {
	if (((uint32_t)alloc_list[i].ptr)+alloc_list[i].size <= base + j*32 ||
	    ((uint32_t)alloc_list[i].ptr) >= base + 32 + j*32)
	  continue;
	subregions &= ~(0x100 << j);
      }
      mpu->RBAR = base | 0x10 | region_no;
      mpu->RASR = 0x1329000f | subregions;
    }
    __ISB();
    __DSB();
    scb->CFSR = 0x82;
  }
}

static void malloc_hook(void *ptr, size_t size) {
  ISR_Guard g;
  if (size == 0)
    return;
  while (!ptr) {}
  unsigned i;
  for(i = 0; i < ALLOC_LIST_SIZE; i++) {
    if (!alloc_list[i].inuse) {
      alloc_list[i].ptr = ptr;
      alloc_list[i].size = size;
      alloc_list[i].inuse = 1;
      break;
    }
  }
  assert(i<ALLOC_LIST_SIZE);
  memset(ptr,0xfe,size);
}

static void free_hook(void *ptr) {
  ISR_Guard g;
  if (!ptr)
    return;
  unsigned i;
  for(i = 0; i < ALLOC_LIST_SIZE; i++) {
    if (alloc_list[i].ptr == ptr &&
	alloc_list[i].inuse) {
      break;
    }
  }
  assert(i<ALLOC_LIST_SIZE);
  memset(ptr,0xfd,alloc_list[i].size);
  //see if any region crosses this and disable its access
  for(unsigned region_no = region_min; region_no < region_count; region_no++) {
    uint32_t base = regions[region_no].base;
    if (base+0x100 <= (uint32_t)ptr &&
	((uint32_t)ptr) + alloc_list[i].size <= base)
      continue;
    regions[region_no].gen = 0;
    MPU_Type *mpu = MPU;
    mpu->RBAR = 0 | 0x10 | region_no;
    mpu->RASR = 0;
    __ISB();
    __DSB();
  }
  alloc_list[i].inuse = 0;
}

static void realloc_hook(void *newptr, void *oldptr, size_t newsize) {
  ISR_Guard g;
  if (newsize == 0) {
    free_hook(oldptr);
    return;
  }
  if (!oldptr) {
    malloc_hook(newptr, newsize);
    return;
  }
  unsigned i;
  for(i = 0; i < ALLOC_LIST_SIZE; i++) {
    if (alloc_list[i].ptr == oldptr &&
	alloc_list[i].inuse) {
      alloc_list[i].ptr = newptr;
      alloc_list[i].size = newsize;
      break;
    }
  }
  assert(i<ALLOC_LIST_SIZE);
  //cannot invalidate memory since it may be reused by the allocator already.
}

static void calloc_hook(void *ptr, size_t nmemb, size_t size) {
  ISR_Guard g;
  size *= nmemb;
  if (size == 0)
    return;
  while (!ptr) {}
  unsigned i;
  for(i = 0; i < ALLOC_LIST_SIZE; i++) {
    if (!alloc_list[i].inuse) {
      alloc_list[i].ptr = ptr;
      alloc_list[i].size = size;
      alloc_list[i].inuse = 1;
      break;
    }
  }
  assert(i<ALLOC_LIST_SIZE);
  //calloc automatically zeros its allocation
}

static size_t findsize(void *ptr) {
  while (!ptr) {}
  unsigned i;
  for(i = 0; i < ALLOC_LIST_SIZE; i++) {
    if (alloc_list[i].ptr == ptr &&
	alloc_list[i].inuse) {
      return alloc_list[i].size;
    }
  }
  assert(i<ALLOC_LIST_SIZE);
  return 0;
}

extern "C" {
  void *__real__calloc_r(struct _reent *r, size_t nmemb, size_t size);
  void *__wrap__calloc_r(struct _reent *r, size_t nmemb, size_t size) {
    mprot_unprotect();
    void *p = __real__calloc_r(r, nmemb, size);
    mprot_protect();
    calloc_hook(p, nmemb, size);
    return p;
  }

  void *__real__malloc_r(struct _reent *r, size_t size);
  void *__wrap__malloc_r(struct _reent *r, size_t size) {
    mprot_unprotect();
    void *p = __real__malloc_r(r,size);
    mprot_protect();
    malloc_hook(p, size);
    return p;
  }

  void __real__free_r(struct _reent *r, void *ptr);
  void __wrap__free_r(struct _reent *r, void *ptr) {
    free_hook(ptr);
    mprot_unprotect();
    __real__free_r(r,ptr);
    mprot_protect();
  }

  void *__real__realloc_r(struct _reent *r, void *ptr, size_t size);
  void *__wrap__realloc_r(struct _reent *r, void *ptr, size_t size) {
#if 1
    ISR_Guard g;
    size_t oldsize = findsize(ptr);
    void *p = __wrap__malloc_r(r, size);
    if (oldsize > size)
      memcpy(p, ptr, size);
    else
      memcpy(p, ptr, oldsize);
    __wrap__free_r(r, ptr);
    return p;
#else
    mprot_unprotect();
    void *p = __real__realloc_r(r,ptr,size);
    mprot_protect();
    realloc_hook(p, ptr, size);
    return p;
#endif
  }

  void __real__malloc_stats_r(struct _reent *r);
  void __wrap__malloc_stats_r(struct _reent *r) {
    mprot_unprotect();
    __real__malloc_stats_r(r);
    mprot_protect();
  }
}

