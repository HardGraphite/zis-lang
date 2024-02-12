#include "locals.h"

#include "attributes.h"
#include "context.h"
#include "ndefutil.h"
#include "objmem.h"

/// Locals root visitor. See `zis_objmem_object_visitor_t`.
static void locals_root_visitor(void *_lr, enum zis_objmem_obj_visit_op op) {
    struct zis_locals_root *lr = _lr;
    for (struct _zis_locals_head *h = lr->_list; h; h = h->_next) {
        void *begin = (char *)h + sizeof *h;
        void *end   = (char *)h + h->_size;
        assert(!(h->_size & (sizeof(void *) - 1)));
        assert(begin < end);
        zis_objmem_visit_object_vec(begin, end, op);
    }
}

void zis_locals_root_init(struct zis_locals_root *lr, struct zis_context *z) {
    lr->_list = NULL;
    if (z)
        zis_objmem_add_gc_root(z, lr, locals_root_visitor);
}

void zis_locals_root_fini(struct zis_locals_root *lr, struct zis_context *z) {
    assert(lr->_list == NULL);
    if (z)
        zis_objmem_remove_gc_root(z, lr);
}

void zis_locals_root_reset(struct zis_locals_root *lr) {
    lr->_list = NULL;
}

void _zis_locals_root_gc_visit(struct zis_locals_root *lr, int op) {
    locals_root_visitor(lr, (enum zis_objmem_obj_visit_op)op);
}

void _zis_locals_block_zero(struct _zis_locals_head *h, size_t n) {
    void *begin = (char *)h + sizeof *h;
    assert(h->_size - sizeof(struct _zis_locals_head) == n * sizeof(void *));
    zis_object_vec_zero(begin, n);
}
