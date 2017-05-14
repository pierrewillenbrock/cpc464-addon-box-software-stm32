
#pragma once

#include "irq.h"
#include "assert.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <string>

template<typename R> class RefPtr;

template<typename K>
class Refcounted {
private:
	unsigned refcount;
	template<typename R>
	friend class RefPtr;
	template<typename R>
	friend class RefcountProxy;
	void refcountinc() { refcount++; }
	void refcountdec() {
		refcount--;
		if (!refcount) {
			K *_this = dynamic_cast<K*>(this);
			delete _this;
		}
	}
	void refcountnonzero() { assert(refcount != 0); }
public:
	Refcounted() : refcount(0) {}
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
};

namespace std {
  template<typename R>
  struct hash<RefPtr<R> > {
    size_t operator()(RefPtr<R> const &r) const {
      return std::hash<R*>()(r.ptr);
    }
  };
}

