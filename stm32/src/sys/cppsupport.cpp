
#include <new>
#include <malloc.h>
#include <assert.h>

void* operator new(std::size_t size) {
  void *o = malloc(size);
  assert(o);
  while (!o) {}
  return o;
}

void* operator new[](std::size_t size) {
  void *o = malloc(size);
  assert(o);
  while (!o) {}
  return o;
}

void operator delete(void* ptr) {
  if (!ptr)
    return;
  free(ptr);
}

void operator delete[](void* ptr) {
  if (!ptr)
    return;
  free(ptr);
}

/* Optionally you can override the 'nothrow' versions as well.
   This is useful if you want to catch failed allocs with your
   own debug code, or keep track of heap usage for example,
   rather than just eliminate exceptions.
*/

void* operator new(std::size_t size, const std::nothrow_t&) {
  void *o = malloc(size);
  assert(o);
  while (!o) {}
  return o;
}

void* operator new[](std::size_t size, const std::nothrow_t&) {
  void *o = malloc(size);
  assert(o);
  while (!o) {}
  return o;
}

void operator delete(void* ptr, const std::nothrow_t&) {
  if (!ptr)
    return;
  free(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) {
  if (!ptr)
    return;
  free(ptr);
}

namespace __gnu_cxx {

  void __verbose_terminate_handler()
  {
    assert(0);
    while(1) {}
  }

}
