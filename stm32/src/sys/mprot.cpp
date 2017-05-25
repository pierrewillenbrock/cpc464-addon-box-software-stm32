
#include <malloc.h>
#include <irq.h>
#include <string.h>
#include <assert.h>
#include <bsp/core_cm4.h>
#include <sys/mprot.h>

extern "C" {
  void MPROT_Setup();
}

#ifndef MPROT_ENABLE
void MPROT_Setup() {
}
#endif


#define RAM_BASE 0x20000000

#ifdef MPROT_ENABLE
/*
(Idea of reusing the size information from the malloc implementation does not
 really help a lot since it is only 8byte accurate.
 anyway, (*(((size_t*)ptr)-1) & ~7) (including the chunk size field itself,
 i.E. allocation was at least 4 smaller)
 the mpu has only 32B accuracy, so we would only make the initial access more
 accurate, and our structures are sized multiples of 4 anyway.)
*/
#ifdef MPROT_LINEAR_LIST

#define ALLOC_LIST_SIZE 1024
struct Alloc {
  uint32_t addr:17;
  uint32_t next_low:11; //also used for the freelist
  uint32_t unused1:4;
  uint32_t size:17;
  uint32_t next_high:11;
  uint32_t unused2:3;
  uint8_t inuse:1;
} __attribute__((packed));

static struct Alloc alloc_list[ALLOC_LIST_SIZE] = { { 0 } };

static uint32_t freeAllocList = 0;//chained through next_low

static struct Alloc *findAllocation(void *ptr) {
  unsigned i;
  uint32_t addr = ((uint32_t)ptr) - RAM_BASE;
  for(i = 0; i < ALLOC_LIST_SIZE; i++) {
    if (alloc_list[i].addr == addr &&
	alloc_list[i].inuse) {
      return &alloc_list[i];
    }
  }
  return NULL;
}

static struct Alloc *findAllocationContaining(void *ptr) {
  unsigned i;
  uint32_t addr = ((uint32_t)ptr) - RAM_BASE;
  for(i = 0; i < ALLOC_LIST_SIZE; i++) {
    if (alloc_list[i].addr <= addr &&
      alloc_list[i].addr + alloc_list[i].size > addr &&
      alloc_list[i].inuse)
      return &alloc_list[i];
  }
  return NULL;
}

static Alloc *addAllocation(void *ptr, size_t size) {
  unsigned i = freeAllocList;
  if (i >= ALLOC_LIST_SIZE) {
    assert(0);
    return NULL;
  }
  freeAllocList = alloc_list[i].next_low;
  assert(!alloc_list[i].inuse);
  alloc_list[i].addr = ((uint32_t)ptr) - RAM_BASE;
  alloc_list[i].size = size;
  alloc_list[i].inuse = 1;
  return &alloc_list[i];
}

static void removeAllocation(struct Alloc *alloc) {
  assert(alloc->inuse);
  unsigned i = alloc - alloc_list;
  alloc->next_low = freeAllocList;
  freeAllocList = i;
  alloc->inuse = 0;
}
#endif

#ifdef MPROT_HASH

#define ALLOC_LIST_SIZE 1024
#define NEXT_BUCKET_INC 251
/*
 * Each Alloc contains only information for the adresses it corresponds to.
 * If the addr does not match, there is no chain starting at that bucket.
 * If inuse is 0, that bucket is not used.
 * Independent of inuse, extend points to the bucket that points to the
 * bucket containing the info block for the allocation extending into the
 * beginning of this block.
 *
 * Example:
 * Allocation#1: addr: 0 size: 10
 * Allocation#2: addr: 32 size: 128
 * Allocation#3: addr: 192 size: 10
 * Allocation#4: addr: 224 size: 128
 * Allocation#5: addr: 224 size: 128
 *
 * Bucket#0: addr: 0, next: 251, size: 10, extend: 1024, inuse: 1
 * Bucket#1: addr: 192, next: 252, size: 128, extend: 251, inuse: 1
 * Bucket#2: addr: undef, next: undef, size: undef, extend: 252, inuse: 0
 * Bucket#251: addr: 32, next: 1024, size: 128, extend: 1024, inuse: 1
 * Bucket#252: addr: 224, next: 1024, size: 128, extend: 1024, inuse: 1
 *
 */
struct Alloc {
  uint32_t addr:17;
  uint32_t next:11;
  uint32_t unused1:4;
  uint32_t size:17;
  uint32_t extend:11;
  uint32_t unused2:3;
  uint8_t inuse:1;
} __attribute__((packed));

/* yes, i want to have it all-zero initialized, and yes,
 * i know that's the default. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
static struct Alloc alloc_list[ALLOC_LIST_SIZE] = { { 0 } };
#pragma GCC diagnostic pop

static uint16_t addr2bucket(uint32_t addr) {
	return addr >> 7;
}

static struct Alloc *findAllocation(void *ptr) {
  unsigned i;
  uint32_t addr = ((uint32_t)ptr) - RAM_BASE;
  i = addr2bucket(addr);
  if (addr2bucket(alloc_list[i].addr) != i ||
    alloc_list[i].inuse == 0)
    //does not belong to the right bucket, so terminate already.
    return NULL;
  //every other Alloc we encounter will have
  //addr2bucket(alloc_list[i].addr) == /*this*/i
  //and inuse == 1
  while(i != ALLOC_LIST_SIZE) {
    if (alloc_list[i].addr == addr) {
      return &alloc_list[i];
    }
    i = alloc_list[i].next;
  }
  return NULL;
}

static struct Alloc *findAllocationContaining(void *ptr) {
  unsigned i;
  uint32_t addr = ((uint32_t)ptr) - RAM_BASE;
  i = addr2bucket(addr);
  //first, check the block leaking into this bucket(via extend)
  //no need to check for inuse, extend must be cleared when the allocation
  //is freed.
  assert(alloc_list[i].extend <= ALLOC_LIST_SIZE);
  if (alloc_list[i].extend != ALLOC_LIST_SIZE) {
    unsigned j = alloc_list[i].extend;
    if (alloc_list[j].addr <= addr &&
      (unsigned)(alloc_list[j].addr + alloc_list[j].size) > addr)
      return &alloc_list[j];
  }
  if (addr2bucket(alloc_list[i].addr) != i ||
    alloc_list[i].inuse == 0)
    //does not belong to the right bucket, so terminate already.
    return NULL;
  //every other Alloc we encounter will have
  //addr2bucket(alloc_list[i].addr) == /*this*/i
  //and inuse == 1
  while(i != ALLOC_LIST_SIZE) {
    assert(i <= ALLOC_LIST_SIZE);
    if (alloc_list[i].addr <= addr &&
      (unsigned)(alloc_list[i].addr + alloc_list[i].size) > addr)
      return &alloc_list[i];
    i = alloc_list[i].next;
  }
  return NULL;
}

static void removeAllocation(struct Alloc *alloc);

static void fixExtends(Alloc *alloc, unsigned infoblock) {
  unsigned baseblock = addr2bucket(alloc->addr);
  unsigned j = addr2bucket(alloc->addr + alloc->size -1);
  for(;j > baseblock; j--) {
    alloc_list[j].extend = infoblock;
  }
}

static unsigned findFreeBlock(unsigned start) {
  unsigned b = (start + NEXT_BUCKET_INC) % ALLOC_LIST_SIZE;
  while(b != start && alloc_list[b].inuse) {
    b = (b + NEXT_BUCKET_INC) % ALLOC_LIST_SIZE;
  }
  assert(b != start);
  return b;
}

static Alloc *findParent(unsigned infoblock) {
  unsigned baseblock = addr2bucket(alloc_list[infoblock].addr);
  unsigned parentblock = baseblock;
  while(alloc_list[parentblock].next != infoblock &&
    parentblock != ALLOC_LIST_SIZE) {
    parentblock = alloc_list[parentblock].next;
  }
  assert(parentblock != ALLOC_LIST_SIZE);
  return &alloc_list[parentblock];
}

static Alloc *addAllocation(void *ptr, size_t size) {
  unsigned infoblock;
  unsigned baseblock;
  uint32_t addr = ((uint32_t)ptr) - RAM_BASE;
  baseblock = addr2bucket(addr);
  //check if the bucket is free
  if (alloc_list[baseblock].inuse == 0) {
    //block is unused, just occupy it.
    infoblock = baseblock;
    alloc_list[infoblock].next = ALLOC_LIST_SIZE;
  } else if (addr2bucket(alloc_list[baseblock].addr) != baseblock) {
    //the block currently in there does not belong here. move it to a new
    //place and update its parent and possible extends.
    unsigned displacedblock = findFreeBlock(baseblock);
    Alloc *displacedparent = findParent(baseblock);
    displacedparent->next = displacedblock;
    alloc_list[displacedblock].addr = alloc_list[baseblock].addr;
    alloc_list[displacedblock].next = alloc_list[baseblock].next;
    alloc_list[displacedblock].size = alloc_list[baseblock].size;
    alloc_list[displacedblock].inuse = 1;
    fixExtends(&alloc_list[displacedblock],displacedblock);

    infoblock = baseblock;
    alloc_list[infoblock].next = ALLOC_LIST_SIZE;
  } else {
    //okay, the allocation does indeed start here, so we need to add ourselves
    //into its list.
    //first, find an empty entry.
    infoblock = findFreeBlock(baseblock);
    //then, add it to the list.
    alloc_list[infoblock].next = alloc_list[baseblock].next;
    alloc_list[baseblock].next = infoblock;
  }

  //now, fill in our own info
  alloc_list[infoblock].addr = addr;
  alloc_list[infoblock].size = size;
  //do not touch extend in this block.
  alloc_list[infoblock].inuse = 1;

  //update the extend information
  fixExtends(&alloc_list[infoblock], infoblock);
  return &alloc_list[infoblock];
}

static void removeAllocation(struct Alloc *alloc) {
  assert(alloc->inuse);
  unsigned infoblock = alloc - alloc_list;
  unsigned baseblock = addr2bucket(alloc->addr);
  //drop the extends(if any)
  fixExtends(alloc, ALLOC_LIST_SIZE);
  if(baseblock == infoblock) {
    //this is the base.
    if (alloc->next != ALLOC_LIST_SIZE) {
      //got another node in the list, move it to the front.
      unsigned n = alloc->next;
      alloc->addr = alloc_list[n].addr;
      alloc->size = alloc_list[n].size;
      alloc->next = alloc_list[n].next;
      alloc_list[n].inuse = 0;
      alloc_list[n].next = ALLOC_LIST_SIZE;
      fixExtends(alloc, infoblock);
    } else {
      //no other node, mark unused.
      alloc->next = ALLOC_LIST_SIZE;
      alloc->inuse = 0;
    }
    return;
  } else {
    //okay, we are some way inside the list. find the parent.
    Alloc *parentblock = findParent(infoblock);
    parentblock->next = alloc->next;
    alloc->next = ALLOC_LIST_SIZE;
    alloc->inuse = 0;
  }
}
#endif

extern char _bheap[];//top of data section
extern char _eheap[];//bottom of stack section

/* we only override the default memory map as needed, that is: for the
   section between _bheap and _eheap.
 */
static uint8_t region_min = 0;
static uint8_t region_count = 0;

static uint32_t region_gen = 0;
static struct {
  uint32_t base;
  uint32_t gen;
} regions[16];

void MPROT_Setup() {
  //setup memory protection unit
  SCB_Type *scb = SCB;
  MPU_Type *mpu = MPU;

  mpu->CTRL = 0;
  region_count = mpu->TYPE >> 8;

  //region#0: 0x20000000 + 0x20000 all of SRAM, NO ACCESS
  mpu->RBAR = 0x20000010;
  mpu->RASR = 0x10000021; // 1<<((0x20 >> 1)+1) == 0x20000

  //region#1: 0x2001e000 + 0x2000 for stack, No execute, R/W, write back,
  //read and write allocate
  mpu->RBAR = 0x2001e011;
  mpu->RASR = 0x13290019; // 1<<((0x18 >> 1)+1) == 0x2000

  uint32_t base = 0x20000000;
  unsigned region_no = 2;
  while(base < ((uint32_t)&_bheap)) {
    //region for static data:
    unsigned size = ((uint32_t)&_bheap) - base;
    //find the next higher 1<<n
    unsigned n = 0;
    if (size < 32)
      size = 32;
    while((1U<<n) < size)
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
  //bheap = 0x5520                              101010100100000
  //#2: 0 - 0x5000 (0x8000, 5 subregions),      101
  //#3: 0x5000-0x5500 (0x800, 5 subregions),        101
  //#4: 0x5500-0x5520 (0x100, 1 subregions),           001

  //bheap = 0x9600                                1001011000000000
  //#2: 0 - 0x8000 (0x10000, 4 subregions)        100
  //#3: 0x8000 - 0x9400 (0x2000, 5 subregions)       101
  //#4: 0x9400 - 0x9600 (0x200, 8 subregions)           1000

  scb->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk;
  scb->SHCSR |= SCB_SHCSR_USGFAULTENA_Msk;//enable the other two as well.
  scb->SHCSR |= SCB_SHCSR_BUSFAULTENA_Msk;
  __ISB();
  __DSB();
  mpu->CTRL = 5;
  __ISB();
  __DSB();

  //setup our data structures
#ifdef MPROT_LINEAR_LIST
  freeAllocList = 0;
  for(unsigned i = 0; i < ALLOC_LIST_SIZE; i++) {
    alloc_list[i].next_low = i+1;
  }
#endif
#ifdef MPROT_HASH
  for(unsigned i = 0; i < ALLOC_LIST_SIZE; i++) {
    alloc_list[i].next = ALLOC_LIST_SIZE;
    alloc_list[i].extend = ALLOC_LIST_SIZE;
  }
#endif
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
    assert(addr > 0x20000000 && addr < 0x20020000);
    struct Alloc *alloc = findAllocationContaining((void*)addr);
    assert(alloc != NULL);
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
      if (((uint32_t)alloc->addr + RAM_BASE)+alloc->size <= base + j*32 ||
	  ((uint32_t)alloc->addr + RAM_BASE) >= base + 32 + j*32)
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
	if (((uint32_t)alloc->addr + RAM_BASE)+alloc->size <= base + j*32 ||
	    ((uint32_t)alloc->addr + RAM_BASE) >= base + 32 + j*32)
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
  if ((uint32_t)ptr < RAM_BASE ||
	  (uint32_t)ptr >= RAM_BASE + 0x20000)
	  return;
  addAllocation(ptr, size);
  memset(ptr,0xfe,size);
}

static void free_hook(void *ptr) {
  ISR_Guard g;
  if (!ptr)
    return;
  if ((uint32_t)ptr < RAM_BASE ||
	  (uint32_t)ptr >= RAM_BASE + 0x20000)
	  return;
  struct Alloc *alloc = findAllocation(ptr);
  assert(alloc != NULL);
  memset(ptr,0xfd,alloc->size);
  //see if any region crosses this and disable its access
  for(unsigned region_no = region_min; region_no < region_count; region_no++) {
    uint32_t base = regions[region_no].base;
    if (base+0x100 <= (uint32_t)ptr &&
	((uint32_t)ptr) + alloc->size <= base)
      continue;
    regions[region_no].gen = 0;
    MPU_Type *mpu = MPU;
    mpu->RBAR = 0 | 0x10 | region_no;
    mpu->RASR = 0;
    __ISB();
    __DSB();
  }
  removeAllocation(alloc);
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
  struct Alloc *alloc = findAllocation(oldptr);
  removeAllocation(alloc);
  addAllocation(newptr, newsize);
  //cannot invalidate memory since it may be reused by the allocator already.
}

static void calloc_hook(void *ptr, size_t nmemb, size_t size) {
  ISR_Guard g;
  size *= nmemb;
  if (size == 0)
    return;
  while (!ptr) {}
  addAllocation(ptr, size);
  //calloc automatically zeros its allocation
}

static size_t findsize(void *ptr) {
  while (!ptr) {}
  struct Alloc *alloc = findAllocation(ptr);
  assert(alloc != NULL);
  return alloc->size;
}
#endif

extern "C" {
  void *__real__calloc_r(struct _reent *r, size_t nmemb, size_t size);
  void *__wrap__calloc_r(struct _reent *r, size_t nmemb, size_t size) {
#ifdef MPROT_ENABLE
    mprot_unprotect();
#endif
    void *p = __real__calloc_r(r, nmemb, size);
#ifdef MPROT_ENABLE
    mprot_protect();
    calloc_hook(p, nmemb, size);
#endif
    return p;
  }

  void *__real__malloc_r(struct _reent *r, size_t size);
  void *__wrap__malloc_r(struct _reent *r, size_t size) {
#ifdef MPROT_ENABLE
    mprot_unprotect();
#endif
    void *p = __real__malloc_r(r,size);
#ifdef MPROT_ENABLE
    mprot_protect();
    malloc_hook(p, size);
#endif
    return p;
  }

  void __real__free_r(struct _reent *r, void *ptr);
  void __wrap__free_r(struct _reent *r, void *ptr) {
#ifdef MPROT_ENABLE
    free_hook(ptr);
    mprot_unprotect();
#endif
    __real__free_r(r,ptr);
#ifdef MPROT_ENABLE
    mprot_protect();
#endif
  }

  void *__real__realloc_r(struct _reent *r, void *ptr, size_t size);
  void *__wrap__realloc_r(struct _reent *r, void *ptr, size_t size) {
#ifdef MPROT_ENABLE
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
#else
    return __real__realloc_r(r,ptr,size);
#endif
  }

  void __real__malloc_stats_r(struct _reent *r);
  void __wrap__malloc_stats_r(struct _reent *r) {
#ifdef MPROT_ENABLE
    mprot_unprotect();
#endif
    __real__malloc_stats_r(r);
#ifdef MPROT_ENABLE
    mprot_protect();
#endif
  }

  struct mallinfo __real__mallinfo_r(struct _reent *r);
  struct mallinfo __wrap__mallinfo_r(struct _reent *r) {
#ifdef MPROT_ENABLE
    mprot_unprotect();
#endif
    struct mallinfo info = __real__mallinfo_r(r);
#ifdef MPROT_ENABLE
    mprot_protect();
#endif
    return info;
  }

}

struct MProtInfo mprot_info() {
  struct MProtInfo info;
  #ifdef MPROT_ENABLE
  #ifdef MPROT_LINEAR_LIST
  #endif
  #ifdef MPROT_HASH
  info.buckets_used = 0;
  info.buckets_used_by_owner = 0;
  info.max_list_depth = 0;
  unsigned depth_sum = 0;
  for(unsigned int i = 0; i < ALLOC_LIST_SIZE; i++) {
    if (!alloc_list[i].inuse)
      continue;
    info.buckets_used++;
    //does it start here?
    if (addr2bucket(alloc_list[i].addr) != i)
      continue;
    info.buckets_used_by_owner++;
    unsigned depth = 0;
    unsigned j = i;
    while(alloc_list[j].next != ALLOC_LIST_SIZE) {
      depth++;
      j = alloc_list[j].next;
    }
    if (info.max_list_depth < depth)
      info.max_list_depth = depth;
    depth_sum += depth;
  }
  info.average_list_depth = (depth_sum + info.buckets_used_by_owner / 2) / info.buckets_used_by_owner;
  #endif
  #endif
  return info;
}

// kate: indent-width 2; indent-mode cstyle;
