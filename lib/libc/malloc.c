#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <kernel/syscall.h>

typedef long Align; /* for alignment */

union header {
	struct {
		union header *next;
		size_t size; /* in units of header */
	} s;
	Align x;
};

typedef union header Header;

static Header base_;        /* empty list to get started */
static Header *freep = NULL; /* start of free list */

static Header *morecore(size_t nu) {
	MESSAGE msg;
	if (nu < 1024) nu = 1024;
	msg.num[0] = (uint64_t)(nu * sizeof(Header));
	msgSend(SYS_MM, SYS_MM_SBRK, &msg);
	msgRecv(NULL, SYS_MM_SBRK, &msg);
	void *cp = (void *)(uintptr_t)msg.num[0];

	if (cp == (void *)-1 || cp == NULL) return NULL;
	Header *up = (Header *)cp;
	up->s.size = nu;
	free((void *)(up + 1));
	return freep;
}

void malloc_init() {
	freep = NULL;
}

void *malloc(size_t nbytes) {
	Header *p, *prevp;
	size_t nunits;

	if (nbytes == 0) return NULL;
	nunits = (nbytes + sizeof(Header) - 1) / sizeof(Header) + 1;
	if ((prevp = freep) == NULL) {
		base_.s.next = freep = prevp = &base_;
		base_.s.size = 0;
	}
	for (p = prevp->s.next; ; prevp = p, p = p->s.next) {
		if (p->s.size >= nunits) {
			if (p->s.size == nunits) {
				prevp->s.next = p->s.next;
			} else {
				p->s.size -= nunits;
				p += p->s.size;
				p->s.size = nunits;
			}
			freep = prevp;
			return (void *)(p + 1);
		}
		if (p == freep) {
			p = morecore(nunits);
			if (p == NULL) return NULL;
		}
	}
}

void free(void *ap) {
	Header *bp, *p;
	if (ap == NULL) return;
	bp = (Header *)ap - 1;
	for (p = freep; !(bp > p && bp < p->s.next); p = p->s.next) {
		if (p >= p->s.next && (bp > p || bp < p->s.next)) break;
	}
	if (bp + bp->s.size == p->s.next) {
		bp->s.size += p->s.next->s.size;
		bp->s.next = p->s.next->s.next;
	} else {
		bp->s.next = p->s.next;
	}
	if (p + p->s.size == bp) {
		p->s.size += bp->s.size;
		p->s.next = bp->s.next;
	} else {
		p->s.next = bp;
	}
	freep = p;
}

void *calloc(size_t nmemb, size_t size) {
	size_t total = nmemb * size;
	void *p = malloc(total);
	if (p) memset(p, 0, total);
	return p;
}

void *realloc(void *ptr, size_t size) {
	if (!ptr) return malloc(size);
	if (size == 0) { free(ptr); return NULL; }
	Header *h = (Header *)ptr - 1;
	size_t old = (h->s.size - 1) * sizeof(Header);
	void *newp = malloc(size);
	if (!newp) return NULL;
	size_t copy = old < size ? old : size;
	memcpy(newp, ptr, copy);
	free(ptr);
	return newp;
}

/* brk/sbrk wrappers for libc (use IPC to mm service) */
void *sbrk(intptr_t increment) {
	MESSAGE msg;
	msg.num[0] = (uint64_t)increment;
	msgSend(SYS_MM, SYS_MM_SBRK, &msg);
	msgRecv(NULL, SYS_MM_SBRK, &msg);
	void *ret = (void *)(uintptr_t)msg.num[0];
	if (ret == (void *)0 || ret == (void *)-1) return (void *)-1;
	return ret;
}

void *brk(void *addr) {
	MESSAGE msg;
	msg.num[0] = (uint64_t)(uintptr_t)addr;
	msgSend(SYS_MM, SYS_MM_BRK, &msg);
	msgRecv(NULL, SYS_MM_BRK, &msg);
	void *ret = (void *)(uintptr_t)msg.num[0];
	if (ret == (void *)0) return (void *)-1;
	return ret;
}