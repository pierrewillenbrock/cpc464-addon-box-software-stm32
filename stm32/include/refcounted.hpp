
#pragma once

#include "irq.h"
#include "assert.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <string>

template<typename R> class RefPtr;

class Refcounted {
private:
	unsigned refcount;
	template<typename R>
	friend class RefPtr;
public:
	Refcounted() : refcount(0) {}
	~Refcounted() { assert(refcount == 0); }
};

template<typename R> class RefPtr {
private:
	R *ptr;
	friend struct std::hash<RefPtr<R> >;
public:
	RefPtr() : ptr(NULL) {}
	RefPtr(R *ptr) : ptr(ptr) {
		//stop the user from moving objects into a RefPtr multiple times
		if (ptr) {
			ISR_Guard g;
			ptr->refcount++;
		}
	}
	RefPtr(RefPtr<R> const &ptr) : ptr(ptr.ptr) {
		if (this->ptr) {
			ISR_Guard g;
			assert(ptr->refcount != 0);
			ptr->refcount++;
		}
	}
	~RefPtr() {
		if (!ptr) return;
		ISR_Guard g;
		ptr->refcount--;
		if (ptr->refcount) return;
		delete ptr;
	}
	RefPtr<R> &operator= (R *ptr) {
		if (this->ptr) {
			ISR_Guard g;
			this->ptr->refcount--;
			if (!this->ptr->refcount)
				delete this->ptr;
		}
		this->ptr = ptr;
		if (this->ptr) {
			ISR_Guard g;
			this->ptr->refcount++;
		}
		return *this;
	}
	RefPtr<R> &operator= (RefPtr<R> const &ptr) {
		if (this->ptr) {
			ISR_Guard g;
			this->ptr->refcount--;
			if (!this->ptr->refcount)
				delete this->ptr;
		}
		this->ptr = ptr.ptr;
		if (this->ptr) {
			ISR_Guard g;
			assert(this->ptr->refcount != 0);
			this->ptr->refcount++;
		}
		return *this;
	}
	operator bool() const { return ptr != NULL; }
	R *operator->() const { return ptr; }
	static R *get(R *ptr) {
		if (ptr) {
			ISR_Guard g;
			ptr->refcount++;
		}
		return ptr;
	}
	static void put(R *ptr) {
		if (ptr) {
			ISR_Guard g;
			ptr->refcount--;
			if (!ptr->refcount)
				delete ptr;
		}
		return ptr;
	}
};

namespace std {
  template<typename R>
  struct hash<RefPtr<R> > {
    size_t operator()(RefPtr<R> const &r) const {
      return std::hash<R*>()(r.ptr);
    }
  };
}

