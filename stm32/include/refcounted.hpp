
#pragma once

#include "irq.h"
#include "assert.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <string>

template<typename R> class RefPtr;

class RefcountedBase {
private:
	unsigned refcount;
	template<typename R>
	friend class RefPtr;
	template<typename R>
	friend class RefcountProxy;
	template<typename R>
	friend class Refcounted;
	void refcountinc() { refcount++; }
	void refcountdec() {
		refcount--;
		if (!refcount)
			delete this;//Possible trouble here: maybe we don't call the correct destructor, even though it is virtual and abstract.
	}
	void refcountnonzero() { assert(refcount != 0); }
public:
	RefcountedBase() : refcount(0) {}
	virtual ~RefcountedBase() = 0;
	void refIsStatic() { refcount = 0x80000000UL; }
};
template<typename K>
class Refcounted : public virtual RefcountedBase {
public:
	virtual ~Refcounted() { assert(refcount == 0); }
};

template <class RO>
class RefcountProxy {
private:
	RO &obj;
	template<typename R>
	friend class RefPtr;
	void refcountinc() { obj.refcountinc(); }
	void refcountdec() { obj.refcountdec(); }
	void refcountnonzero() { obj.refcountnonzero(); }
public:
	RefcountProxy(RO &obj) : obj(obj) { }
	~RefcountProxy() { }
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
			ptr->refcountinc();
		}
	}
	RefPtr(RefPtr<R> const &ptr) : ptr(ptr.ptr) {
		if (this->ptr) {
			ISR_Guard g;
			ptr->refcountnonzero();
			ptr->refcountinc();
		}
	}
	~RefPtr() {
		if (!ptr) return;
		ISR_Guard g;
		ptr->refcountdec();
	}
	RefPtr<R> &operator= (R *ptr) {
		if (this->ptr) {
			ISR_Guard g;
			this->ptr->refcountdec();
		}
		this->ptr = ptr;
		if (this->ptr) {
			ISR_Guard g;
			this->ptr->refcountinc();
		}
		return *this;
	}
	RefPtr<R> &operator= (RefPtr<R> const &ptr) {
		if (this->ptr) {
			ISR_Guard g;
			this->ptr->refcountdec();
		}
		this->ptr = ptr.ptr;
		if (this->ptr) {
			ISR_Guard g;
			this->ptr->refcountnonzero();
			this->ptr->refcountinc();
		}
		return *this;
	}
	operator bool() const { return ptr != NULL; }
	R *operator->() const { return ptr; }
	static R *get(R *ptr) {
		if (ptr) {
			ISR_Guard g;
			ptr->refcountinc();
		}
		return ptr;
	}
	static void put(R *ptr) {
		if (ptr) {
			ISR_Guard g;
			ptr->refcountdec();
		}
		return ptr;
	}
	bool operator== (R *ptr) {
		return this->ptr == ptr;
	}
	template<typename T>
	operator RefPtr<T>() {
		return RefPtr<T>(ptr);
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

