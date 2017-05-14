
#include <new>
#include <malloc.h>
#include <assert.h>
#include <sys/types.h>

void* operator new(std::size_t size) _GLIBCXX_THROW (std::bad_alloc) {
  void *o = malloc(size);
  assert(o);
  while (!o) {}
  return o;
}

void* operator new[](std::size_t size) _GLIBCXX_THROW (std::bad_alloc) {
  void *o = malloc(size);
  assert(o);
  while (!o) {}
  return o;
}

void operator delete(void* ptr) _GLIBCXX_USE_NOEXCEPT {
  if (!ptr)
    return;
  free(ptr);
}

void operator delete(void* ptr, std::size_t /*sz*/) _GLIBCXX_USE_NOEXCEPT {
  if (!ptr)
    return;
  free(ptr);
}

void operator delete[](void* ptr) _GLIBCXX_USE_NOEXCEPT {
  if (!ptr)
    return;
  free(ptr);
}

void operator delete[](void* ptr, std::size_t /*sz*/) _GLIBCXX_USE_NOEXCEPT {
  if (!ptr)
    return;
  free(ptr);
}

/* Optionally you can override the 'nothrow' versions as well.
   This is useful if you want to catch failed allocs with your
   own debug code, or keep track of heap usage for example,
   rather than just eliminate exceptions.
*/

void* operator new(std::size_t size, const std::nothrow_t&) _GLIBCXX_USE_NOEXCEPT {
  void *o = malloc(size);
  assert(o);
  while (!o) {}
  return o;
}

void* operator new[](std::size_t size, const std::nothrow_t&) _GLIBCXX_USE_NOEXCEPT {
  void *o = malloc(size);
  assert(o);
  while (!o) {}
  return o;
}

void operator delete(void* ptr, const std::nothrow_t&) _GLIBCXX_USE_NOEXCEPT {
  if (!ptr)
    return;
  free(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) _GLIBCXX_USE_NOEXCEPT {
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
