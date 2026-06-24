/**
 * @file bptree.c
 * @brief Generic paginated B+ tree implementation (see bptree.h).
 *
 * Page layout and on-disk format: docs/data-structures.md.
 *
 * Deletion design note: bptree_delete uses tombstone-only logical deletion
 * and does NOT rebalance or merge nodes. Physical compaction is out of scope
 * for this phase. See docs/decisions.md for the rationale.
 */

#include "bptree.h"

#include "ioutils.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===================================================================== *
 *  Constants                                                            *
 * ===================================================================== */

#define HEADER_MAGIC "BPLI"
#define HEADER_VERSAO ((uint16_t)1)

#define NODE_INTERNAL ((uint8_t)0x01)
#define NODE_LEAF ((uint8_t)0x02)

#define TOMBSTONE_ACTIVE ((uint8_t)0x00)
#define TOMBSTONE_DELETED ((uint8_t)0x01)

/* Each value slot in a leaf: 1-byte tombstone + 8-byte offset (u64 LE). */
#define VAL_SLOT_SIZE ((size_t)9)

/* Maximum tree height supported by the fixed descent stack. */
#define BPTREE_MAX_HEIGHT 16

/* Page 0 header field offsets. */
#define HDR_OFF_MAGIC 0u
#define HDR_OFF_VERSAO 4u
#define HDR_OFF_ORDER 6u
#define HDR_OFF_ROOT 8u
#define HDR_OFF_NUMKEYS 16u

/* ===================================================================== *
 *  Opaque struct                                                        *
 * ===================================================================== */

struct bptree {
    FILE *fp;
    size_t key_size;
    bptree_cmp_fn cmp;
    uint16_t order; /* max keys per node */
    uint64_t root_page;
    uint64_t num_keys; /* active (non-tombstoned) entries */
};

/* ===================================================================== *
 *  Little-endian helpers (work directly on uint8_t page buffers)       *
 * ===================================================================== */

static inline uint16_t rd16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline uint64_t rd64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) {
        v = (v << 8) | p[i];
    }
    return v;
}

static inline void wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
}

static inline void wr64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) {
        p[i] = (uint8_t)(v >> ((unsigned)i * 8u));
    }
}

/* ===================================================================== *
 *  Page layout accessors                                                *
 * ===================================================================== */

/* Keys start at byte 8 (after the 8-byte node header). */
static inline uint8_t *key_ptr(uint8_t *page, int i, size_t ksz) {
    return page + 8u + (size_t)i * ksz;
}

static inline const uint8_t *key_ptr_c(const uint8_t *page, int i, size_t ksz) {
    return page + 8u + (size_t)i * ksz;
}

/*
 * Value slots section starts after ord * ksz bytes of keys.
 * Each slot is VAL_SLOT_SIZE (9) bytes: tombstone(u8) + offset(u64 LE).
 */
static inline uint8_t *val_slot(uint8_t *page, int i, size_t ksz, uint16_t ord) {
    return page + 8u + (size_t)ord * ksz + (size_t)i * VAL_SLOT_SIZE;
}

static inline const uint8_t *val_slot_c(const uint8_t *page, int i, size_t ksz, uint16_t ord) {
    return page + 8u + (size_t)ord * ksz + (size_t)i * VAL_SLOT_SIZE;
}

/* next_leaf pointer: 8 bytes immediately after all value slots. */
static inline uint64_t leaf_next_get(const uint8_t *page, size_t ksz, uint16_t ord) {
    return rd64(page + 8u + (size_t)ord * ksz + (size_t)ord * VAL_SLOT_SIZE);
}

static inline void leaf_next_set(uint8_t *page, size_t ksz, uint16_t ord, uint64_t pg) {
    wr64(page + 8u + (size_t)ord * ksz + (size_t)ord * VAL_SLOT_SIZE, pg);
}

/*
 * Internal node child pointers start after ord * ksz bytes of keys.
 * There are (ord + 1) child pointers, each 8 bytes.
 */
static inline uint64_t child_get(const uint8_t *page, int i, size_t ksz, uint16_t ord) {
    return rd64(page + 8u + (size_t)ord * ksz + (size_t)i * 8u);
}

static inline void child_set(uint8_t *page, int i, size_t ksz, uint16_t ord, uint64_t pg) {
    wr64(page + 8u + (size_t)ord * ksz + (size_t)i * 8u, pg);
}

/* Node header: type at [0], num_keys at [1..2]. */
static inline uint8_t node_type(const uint8_t *page) {
    return page[0];
}
static inline uint16_t node_nkeys(const uint8_t *page) {
    return rd16(page + 1);
}
static inline void node_set_type(uint8_t *page, uint8_t t) {
    page[0] = t;
}
static inline void node_set_nkeys(uint8_t *page, uint16_t n) {
    wr16(page + 1, n);
}

/* ===================================================================== *
 *  Binary search                                                        *
 * ===================================================================== */

/*
 * Returns the first index i in [0, nk] such that keys[i] >= key.
 * Used in leaf nodes for exact-match position and insertion position.
 */
static int lower_bound(const uint8_t *page, uint16_t nk, size_t ksz, bptree_cmp_fn cmp, const void *key) {
    int lo = 0, hi = (int)nk;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (cmp(key_ptr_c(page, mid, ksz), key) < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

/*
 * Returns the first index i in [0, nk] such that keys[i] > key.
 * Used in internal nodes for descent: separators are the minimum of the right
 * child, so we follow child[i] where i is the count of separators <= key.
 * This guarantees keys equal to a separator are routed to the right child.
 */
static int upper_bound(const uint8_t *page, uint16_t nk, size_t ksz, bptree_cmp_fn cmp, const void *key) {
    int lo = 0, hi = (int)nk;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (cmp(key_ptr_c(page, mid, ksz), key) <= 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

/* ===================================================================== *
 *  Order computation                                                    *
 * ===================================================================== */

static uint16_t compute_order(size_t key_size) {
    /* Leaf: 8 hdr + ord*(ksz + VAL_SLOT_SIZE) + 8 next_leaf <= IO_PAGE_SIZE */
    size_t leaf_cap = (IO_PAGE_SIZE - 16u) / (key_size + VAL_SLOT_SIZE);
    /* Internal: 8 hdr + ord*ksz + (ord+1)*8 <= IO_PAGE_SIZE
     *   => ord*(ksz+8) + 8 <= IO_PAGE_SIZE - 8
     *   => ord*(ksz+8) <= IO_PAGE_SIZE - 16                              */
    size_t int_cap = (IO_PAGE_SIZE - 16u) / (key_size + 8u);
    return (uint16_t)((leaf_cap < int_cap) ? leaf_cap : int_cap);
}

/* ===================================================================== *
 *  Header I/O                                                           *
 * ===================================================================== */

static bool write_header(bptree_t *bt) {
    uint8_t buf[IO_PAGE_SIZE];
    memset(buf, 0, sizeof buf);
    memcpy(buf + HDR_OFF_MAGIC, HEADER_MAGIC, 4);
    wr16(buf + HDR_OFF_VERSAO, HEADER_VERSAO);
    wr16(buf + HDR_OFF_ORDER, bt->order);
    wr64(buf + HDR_OFF_ROOT, bt->root_page);
    wr64(buf + HDR_OFF_NUMKEYS, bt->num_keys);
    return io_write_page(bt->fp, 0, buf);
}

static bool read_header(bptree_t *bt) {
    uint8_t buf[IO_PAGE_SIZE];
    if (!io_read_page(bt->fp, 0, buf)) {
        return false;
    }
    if (memcmp(buf + HDR_OFF_MAGIC, HEADER_MAGIC, 4) != 0) {
        errno = EPROTO;
        return false;
    }
    bt->order = rd16(buf + HDR_OFF_ORDER);
    bt->root_page = rd64(buf + HDR_OFF_ROOT);
    bt->num_keys = rd64(buf + HDR_OFF_NUMKEYS);
    return true;
}

/* ===================================================================== *
 *  Page I/O                                                             *
 * ===================================================================== */

static bool read_node(bptree_t *bt, uint64_t pg, uint8_t *buf) {
    return io_read_page(bt->fp, pg, buf);
}

static bool write_node(bptree_t *bt, uint64_t pg, const uint8_t *buf) {
    return io_write_page(bt->fp, pg, buf);
}

/* Appends a new page at end-of-file and returns its page number. */
static bool alloc_node(bptree_t *bt, uint64_t *out_pg, uint8_t *buf) {
    byte_offset_t sz;
    if (!io_file_size(bt->fp, &sz)) {
        return false;
    }
    *out_pg = sz / IO_PAGE_SIZE;
    return io_write_page(bt->fp, *out_pg, buf);
}

/* ===================================================================== *
 *  Concrete comparators                                                 *
 * ===================================================================== */

int bptree_cmp_u32(const void *a, const void *b) {
    uint32_t ka, kb;
    memcpy(&ka, a, sizeof ka);
    memcpy(&kb, b, sizeof kb);
    if (ka < kb)
        return -1;
    if (ka > kb)
        return 1;
    return 0;
}

int bptree_cmp_training(const void *a, const void *b) {
    TrainingKey ta, tb;
    memcpy(&ta, a, sizeof ta);
    memcpy(&tb, b, sizeof tb);

    if (ta.id_usuario != tb.id_usuario)
        return (ta.id_usuario < tb.id_usuario) ? -1 : 1;
    if (ta.id_exercicio != tb.id_exercicio)
        return (ta.id_exercicio < tb.id_exercicio) ? -1 : 1;
    if (ta.data != tb.data)
        return (ta.data < tb.data) ? -1 : 1;
    if (ta.offset != tb.offset)
        return (ta.offset < tb.offset) ? -1 : 1;
    return 0;
}

/* ===================================================================== *
 *  bptree_open / bptree_close                                          *
 * ===================================================================== */

bptree_t *bptree_open(const char *path, size_t key_size, bptree_cmp_fn cmp) {
    if (path == NULL || key_size == 0 || key_size > BPTREE_MAX_KEY_SIZE || cmp == NULL) {
        errno = EINVAL;
        return NULL;
    }

    bptree_t *bt = malloc(sizeof *bt);
    if (bt == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    bt->key_size = key_size;
    bt->cmp = cmp;
    bt->num_keys = 0;
    bt->order = 0;
    bt->root_page = 0;
    bt->fp = NULL;

    FILE *fp = io_open(path, "r+b");
    bool is_new = false;
    if (fp == NULL) {
        if (errno != ENOENT) {
            free(bt);
            return NULL;
        }
        fp = io_open(path, "w+b");
        if (fp == NULL) {
            free(bt);
            return NULL;
        }
        is_new = true;
    }
    bt->fp = fp;

    if (is_new) {
        bt->order = compute_order(key_size);
        bt->root_page = 1;
        bt->num_keys = 0;

        if (!write_header(bt)) {
            int saved = errno;
            io_close(fp);
            free(bt);
            errno = saved;
            return NULL;
        }

        /* Allocate root leaf (page 1): zeroed, type = NODE_LEAF */
        uint8_t root_buf[IO_PAGE_SIZE];
        memset(root_buf, 0, sizeof root_buf);
        node_set_type(root_buf, NODE_LEAF);
        node_set_nkeys(root_buf, 0);
        uint64_t new_pg;
        if (!alloc_node(bt, &new_pg, root_buf) || new_pg != 1) {
            int saved = errno;
            io_close(fp);
            free(bt);
            errno = saved;
            return NULL;
        }
    } else {
        if (!read_header(bt)) {
            int saved = errno;
            io_close(fp);
            free(bt);
            errno = saved;
            return NULL;
        }
        /* key_size and cmp come from the caller; order from file. */
    }

    return bt;
}

bool bptree_close(bptree_t *bt) {
    if (bt == NULL) {
        return true;
    }
    bool ok = write_header(bt);
    if (!io_close(bt->fp)) {
        ok = false;
    }
    free(bt);
    return ok;
}

/* ===================================================================== *
 *  bptree_search                                                        *
 * ===================================================================== */

bool bptree_search(bptree_t *bt, const void *key, bptree_val_t *out_val) {
    if (bt == NULL || key == NULL || out_val == NULL) {
        errno = EINVAL;
        return false;
    }

    uint8_t buf[IO_PAGE_SIZE];
    uint64_t cur = bt->root_page;

    for (;;) {
        if (!read_node(bt, cur, buf)) {
            return false;
        }
        uint16_t nk = node_nkeys(buf);

        if (node_type(buf) == NODE_INTERNAL) {
            int slot = upper_bound(buf, nk, bt->key_size, bt->cmp, key);
            cur = child_get(buf, slot, bt->key_size, bt->order);
        } else {
            int pos = lower_bound(buf, nk, bt->key_size, bt->cmp, key);
            if (pos >= (int)nk || bt->cmp(key_ptr_c(buf, pos, bt->key_size), key) != 0) {
                errno = ENOENT;
                return false;
            }
            const uint8_t *vs = val_slot_c(buf, pos, bt->key_size, bt->order);
            if (vs[0] == TOMBSTONE_DELETED) {
                errno = ENOENT;
                return false;
            }
            *out_val = rd64(vs + 1);
            return true;
        }
    }
}

/* ===================================================================== *
 *  bptree_insert — helpers                                              *
 * ===================================================================== */

/*
 * Copy a single leaf entry (key + val-slot) from src page index src_i
 * into dst page index dst_i.
 */
static void leaf_copy_entry(uint8_t *dst, int dst_i, const uint8_t *src, int src_i, size_t ksz, uint16_t ord) {
    memcpy(key_ptr(dst, dst_i, ksz), key_ptr_c(src, src_i, ksz), ksz);
    memcpy(val_slot(dst, dst_i, ksz, ord), val_slot_c(src, src_i, ksz, ord), VAL_SLOT_SIZE);
}

/*
 * Write a brand-new leaf entry (key from raw pointer, val, tombstone=active)
 * into dst page at index dst_i.
 */
static void leaf_write_entry(uint8_t *dst, int dst_i, const void *key, bptree_val_t val, size_t ksz, uint16_t ord) {
    memcpy(key_ptr(dst, dst_i, ksz), key, ksz);
    uint8_t *vs = val_slot(dst, dst_i, ksz, ord);
    vs[0] = TOMBSTONE_ACTIVE;
    wr64(vs + 1, val);
}

/* ===================================================================== *
 *  bptree_insert — split helpers                                        *
 * ===================================================================== */

typedef struct {
    uint64_t pg;
    int slot;
} Frame;

/*
 * Splits a full leaf (left_buf, order entries) by inserting (key, val) at
 * logical position pos (0..order).
 *
 * After the call:
 *   left_buf  has mid entries,  its next_leaf points to right_page.
 *   right_buf has the remaining entries, its next_leaf is the old left next.
 *   sep_key   receives the first key of the right node (a copy, B+ tree rule).
 */
static bool split_leaf(bptree_t *bt, uint8_t *left_buf, uint64_t left_pg, uint8_t *right_buf, uint64_t *right_pg, int pos, const void *key, bptree_val_t val, uint8_t *sep_key) {
    size_t ksz = bt->key_size;
    uint16_t ord = bt->order;
    int total = (int)ord + 1; /* entries after insertion */
    int mid = total / 2;      /* left keeps [0..mid-1] */

    /* Save old next_leaf before we zero right_buf. */
    uint64_t old_next = leaf_next_get(left_buf, ksz, ord);

    memset(right_buf, 0, IO_PAGE_SIZE);
    node_set_type(right_buf, NODE_LEAF);

    int right_count = total - mid;

    if (pos < mid) {
        /*
         * New entry goes into the left node.
         * Right node gets old left[mid-1 .. ord-1].
         */
        for (int j = 0; j < right_count; j++) {
            leaf_copy_entry(right_buf, j, left_buf, mid - 1 + j, ksz, ord);
        }
        /* Shift left[pos .. mid-2] right by 1 to make room. */
        for (int i = mid - 1; i > pos; i--) {
            leaf_copy_entry(left_buf, i, left_buf, i - 1, ksz, ord);
        }
        leaf_write_entry(left_buf, pos, key, val, ksz, ord);
    } else {
        /*
         * New entry goes into the right node.
         * Left node stays unchanged (first mid entries).
         */
        int adj = pos - mid;
        for (int j = 0; j < adj; j++) {
            leaf_copy_entry(right_buf, j, left_buf, mid + j, ksz, ord);
        }
        leaf_write_entry(right_buf, adj, key, val, ksz, ord);
        for (int j = adj + 1; j < right_count; j++) {
            leaf_copy_entry(right_buf, j, left_buf, mid + j - 1, ksz, ord);
        }
    }

    node_set_nkeys(left_buf, (uint16_t)mid);
    node_set_nkeys(right_buf, (uint16_t)right_count);

    /* Separator: first key of right node. */
    memcpy(sep_key, key_ptr(right_buf, 0, ksz), ksz);

    /* Fix next_leaf chain. */
    if (!alloc_node(bt, right_pg, right_buf)) {
        return false;
    }
    leaf_next_set(left_buf, ksz, ord, *right_pg);
    leaf_next_set(right_buf, ksz, ord, old_next);

    /* Write right page (alloc_node already wrote it; rewrite with chain set). */
    if (!write_node(bt, *right_pg, right_buf)) {
        return false;
    }
    return write_node(bt, left_pg, left_buf);
}

/*
 * Splits a full internal node (left_buf, order keys) by inserting
 * (sep_in, right_child) at position pos (0..order).
 *
 * The middle key is PROMOTED (not copied): it goes into sep_out for the
 * caller to push further up.  right_pg receives the new right internal page.
 */
static bool split_internal(bptree_t *bt, uint8_t *left_buf, uint64_t left_pg, uint8_t *right_buf, uint64_t *right_pg, int pos, const uint8_t *sep_in, uint64_t right_child_in, uint8_t *sep_out) {
    size_t ksz = bt->key_size;
    uint16_t ord = bt->order;
    int total = (int)ord + 1; /* keys after insertion */
    int mid = total / 2;      /* index of promoted key */

    /*
     * Build the virtual sorted sequence of (total) keys and (total+1) children
     * by conceptually inserting sep_in/right_child_in at position pos.
     * We use a temporary staging in right_buf, then fix up left_buf.
     */

    memset(right_buf, 0, IO_PAGE_SIZE);
    node_set_type(right_buf, NODE_INTERNAL);

    /*
     * The promoted key is the one at virtual index mid.
     * Left keeps virtual [0..mid-1], right keeps virtual [mid+1..total].
     */

    /* Helper lambda: read key at virtual index vi */
    /* (We implement this inline via the branch below.) */

    /* Write the promoted separator. */
    if (pos == mid) {
        memcpy(sep_out, sep_in, ksz);
        /* right child[0] = right_child_in; right keys = old[mid..ord-1] */
        child_set(right_buf, 0, ksz, ord, right_child_in);
        for (int j = 0; j < (int)ord - mid; j++) {
            memcpy(key_ptr(right_buf, j, ksz), key_ptr(left_buf, mid + j, ksz), ksz);
            child_set(right_buf, j + 1, ksz, ord, child_get(left_buf, mid + j + 1, ksz, ord));
        }
    } else if (pos < mid) {
        /* Virtual: [0..pos-1]=old[0..pos-1], [pos]=new, [pos+1..total]=old[pos..] */
        memcpy(sep_out, key_ptr(left_buf, mid - 1, ksz), ksz);

        /* right: virtual [mid..total] → old keys [mid-1..ord-1] shifted by insert */
        /* right child[0] = old child[mid] (after insert shift) = old child[mid] */
        child_set(right_buf, 0, ksz, ord, child_get(left_buf, mid, ksz, ord));
        for (int j = 0; j < (int)ord - mid; j++) {
            memcpy(key_ptr(right_buf, j, ksz), key_ptr(left_buf, mid + j, ksz), ksz);
            child_set(right_buf, j + 1, ksz, ord, child_get(left_buf, mid + j + 1, ksz, ord));
        }

        /* Fix left: insert sep_in at pos, shifting [pos..mid-2] right */
        for (int i = mid - 1; i > pos; i--) {
            memcpy(key_ptr(left_buf, i, ksz), key_ptr(left_buf, i - 1, ksz), ksz);
            child_set(left_buf, i + 1, ksz, ord, child_get(left_buf, i, ksz, ord));
        }
        memcpy(key_ptr(left_buf, pos, ksz), sep_in, ksz);
        child_set(left_buf, pos + 1, ksz, ord, right_child_in);
    } else {
        /* pos > mid */
        memcpy(sep_out, key_ptr(left_buf, mid, ksz), ksz);

        /* right: virtual [mid+1..total] */
        /* right_child[0] = old child[mid+1] */
        /* But we need to insert sep_in at pos > mid in the right side. */
        /* adj = pos - mid - 1 (position within right keys) */
        int adj = pos - mid - 1;
        int right_keys = (int)ord - mid - 1; /* before insertion = ord - (mid+1) */

        /* Build right keys/children from old left [mid+1..pos-1], new, old[pos..ord-1] */
        child_set(right_buf, 0, ksz, ord, child_get(left_buf, mid + 1, ksz, ord));
        for (int j = 0; j < adj; j++) {
            memcpy(key_ptr(right_buf, j, ksz), key_ptr(left_buf, mid + 1 + j, ksz), ksz);
            child_set(right_buf, j + 1, ksz, ord, child_get(left_buf, mid + 2 + j, ksz, ord));
        }
        memcpy(key_ptr(right_buf, adj, ksz), sep_in, ksz);
        child_set(right_buf, adj + 1, ksz, ord, right_child_in);
        for (int j = adj + 1; j <= right_keys; j++) {
            memcpy(key_ptr(right_buf, j, ksz), key_ptr(left_buf, mid + j, ksz), ksz);
            child_set(right_buf, j + 1, ksz, ord, child_get(left_buf, mid + 1 + j, ksz, ord));
        }
    }

    int right_keys_count = (int)ord - mid;
    node_set_nkeys(left_buf, (uint16_t)(mid));
    node_set_nkeys(right_buf, (uint16_t)right_keys_count);

    if (!alloc_node(bt, right_pg, right_buf)) {
        return false;
    }
    if (!write_node(bt, *right_pg, right_buf)) {
        return false;
    }
    return write_node(bt, left_pg, left_buf);
}

/* ===================================================================== *
 *  bptree_insert                                                        *
 * ===================================================================== */

bool bptree_insert(bptree_t *bt, const void *key, bptree_val_t val) {
    if (bt == NULL || key == NULL) {
        errno = EINVAL;
        return false;
    }

    uint8_t buf[IO_PAGE_SIZE];
    Frame stack[BPTREE_MAX_HEIGHT];
    int depth = 0;
    uint64_t cur = bt->root_page;

    /* ---- Descend to the target leaf ---- */
    for (;;) {
        if (!read_node(bt, cur, buf)) {
            return false;
        }
        if (node_type(buf) == NODE_LEAF) {
            break;
        }
        uint16_t nk = node_nkeys(buf);
        int slot = upper_bound(buf, nk, bt->key_size, bt->cmp, key);
        stack[depth].pg = cur;
        stack[depth].slot = slot;
        depth++;
        cur = child_get(buf, slot, bt->key_size, bt->order);
    }

    uint64_t leaf_pg = cur;
    /* buf holds the leaf. */
    uint16_t nk = node_nkeys(buf);
    int pos = lower_bound(buf, nk, bt->key_size, bt->cmp, key);

    if (nk < bt->order) {
        /* ---- Simple insert into leaf with room ---- */
        /* Shift keys right. */
        for (int i = (int)nk; i > pos; i--) {
            leaf_copy_entry(buf, i, buf, i - 1, bt->key_size, bt->order);
        }
        leaf_write_entry(buf, pos, key, val, bt->key_size, bt->order);
        node_set_nkeys(buf, (uint16_t)(nk + 1u));
        if (!write_node(bt, leaf_pg, buf)) {
            return false;
        }
        bt->num_keys++;
        return write_header(bt);
    }

    /* ---- Leaf is full: split ---- */
    uint8_t right_buf[IO_PAGE_SIZE];
    uint64_t right_pg;
    uint8_t sep[BPTREE_MAX_KEY_SIZE];

    if (!split_leaf(bt, buf, leaf_pg, right_buf, &right_pg, pos, key, val, sep)) {
        return false;
    }
    bt->num_keys++;

    /* ---- Push separator up through ancestor stack ---- */
    uint64_t new_right = right_pg;

    while (depth > 0) {
        depth--;
        uint64_t par_pg = stack[depth].pg;
        int par_slot = stack[depth].slot;

        uint8_t par_buf[IO_PAGE_SIZE];
        if (!read_node(bt, par_pg, par_buf)) {
            return false;
        }
        uint16_t par_nk = node_nkeys(par_buf);

        if (par_nk < bt->order) {
            /* Room in parent: insert sep at par_slot, shift right. */
            for (int i = (int)par_nk; i > par_slot; i--) {
                memcpy(key_ptr(par_buf, i, bt->key_size), key_ptr(par_buf, i - 1, bt->key_size), bt->key_size);
                child_set(par_buf, i + 1, bt->key_size, bt->order, child_get(par_buf, i, bt->key_size, bt->order));
            }
            memcpy(key_ptr(par_buf, par_slot, bt->key_size), sep, bt->key_size);
            child_set(par_buf, par_slot + 1, bt->key_size, bt->order, new_right);
            node_set_nkeys(par_buf, (uint16_t)(par_nk + 1u));
            if (!write_node(bt, par_pg, par_buf)) {
                return false;
            }
            return write_header(bt);
        }

        /* Parent also full: split internal node. */
        uint8_t par_right_buf[IO_PAGE_SIZE];
        uint64_t par_right_pg;
        uint8_t new_sep[BPTREE_MAX_KEY_SIZE];

        if (!split_internal(bt, par_buf, par_pg, par_right_buf, &par_right_pg, par_slot, sep, new_right, new_sep)) {
            return false;
        }
        memcpy(sep, new_sep, bt->key_size);
        new_right = par_right_pg;
    }

    /* ---- Root split: create new root ---- */
    uint8_t new_root_buf[IO_PAGE_SIZE];
    uint64_t new_root_pg;
    memset(new_root_buf, 0, sizeof new_root_buf);
    node_set_type(new_root_buf, NODE_INTERNAL);
    node_set_nkeys(new_root_buf, 1);
    memcpy(key_ptr(new_root_buf, 0, bt->key_size), sep, bt->key_size);
    child_set(new_root_buf, 0, bt->key_size, bt->order, bt->root_page);
    child_set(new_root_buf, 1, bt->key_size, bt->order, new_right);

    if (!alloc_node(bt, &new_root_pg, new_root_buf)) {
        return false;
    }
    /* alloc_node already wrote new_root_buf; rewrite to be safe. */
    if (!write_node(bt, new_root_pg, new_root_buf)) {
        return false;
    }
    bt->root_page = new_root_pg;
    return write_header(bt);
}

/* ===================================================================== *
 *  bptree_range                                                         *
 * ===================================================================== */

bool bptree_range(bptree_t *bt, const void *min, const void *max, bptree_scan_cb cb, void *ctx) {
    if (bt == NULL || cb == NULL) {
        errno = EINVAL;
        return false;
    }

    uint8_t buf[IO_PAGE_SIZE];
    uint64_t cur = bt->root_page;

    /* ---- Descend to the starting leaf ---- */
    for (;;) {
        if (!read_node(bt, cur, buf)) {
            return false;
        }
        if (node_type(buf) == NODE_LEAF) {
            break;
        }
        uint16_t nk = node_nkeys(buf);
        int slot;
        if (min == NULL) {
            slot = 0;
        } else {
            slot = upper_bound(buf, nk, bt->key_size, bt->cmp, min);
        }
        cur = child_get(buf, slot, bt->key_size, bt->order);
    }

    /* ---- Walk the leaf chain ---- */
    while (cur != 0) {
        if (!read_node(bt, cur, buf)) {
            return false;
        }
        uint16_t nk = node_nkeys(buf);

        for (int i = 0; i < (int)nk; i++) {
            const uint8_t *kp = key_ptr_c(buf, i, bt->key_size);

            if (min != NULL && bt->cmp(kp, min) < 0) {
                continue;
            }
            if (max != NULL && bt->cmp(kp, max) > 0) {
                return true; /* past upper bound, done */
            }

            const uint8_t *vs = val_slot_c(buf, i, bt->key_size, bt->order);
            if (vs[0] == TOMBSTONE_DELETED) {
                continue;
            }

            bptree_val_t v = rd64(vs + 1);
            if (!cb(kp, v, ctx)) {
                return true; /* early stop */
            }
        }

        cur = leaf_next_get(buf, bt->key_size, bt->order);
    }

    return true;
}

/* ===================================================================== *
 *  bptree_delete                                                        *
 * ===================================================================== */

bool bptree_delete(bptree_t *bt, const void *key) {
    if (bt == NULL || key == NULL) {
        errno = EINVAL;
        return false;
    }

    uint8_t buf[IO_PAGE_SIZE];
    uint64_t cur = bt->root_page;

    for (;;) {
        if (!read_node(bt, cur, buf)) {
            return false;
        }
        uint16_t nk = node_nkeys(buf);

        if (node_type(buf) == NODE_INTERNAL) {
            int slot = upper_bound(buf, nk, bt->key_size, bt->cmp, key);
            cur = child_get(buf, slot, bt->key_size, bt->order);
        } else {
            int pos = lower_bound(buf, nk, bt->key_size, bt->cmp, key);
            if (pos >= (int)nk || bt->cmp(key_ptr_c(buf, pos, bt->key_size), key) != 0) {
                errno = ENOENT;
                return false;
            }
            uint8_t *vs = val_slot(buf, pos, bt->key_size, bt->order);
            if (vs[0] == TOMBSTONE_DELETED) {
                errno = ENOENT;
                return false;
            }
            vs[0] = TOMBSTONE_DELETED;
            if (!write_node(bt, cur, buf)) {
                return false;
            }
            if (bt->num_keys > 0) {
                bt->num_keys--;
            }
            return write_header(bt);
        }
    }
}

/* ===================================================================== *
 *  bptree_print                                                         *
 * ===================================================================== */

/* Simple BFS queue entry. */
typedef struct {
    uint64_t pg;
    int level;
} PrintEntry;

void bptree_print(const bptree_t *bt) {
    if (bt == NULL) {
        return;
    }

    /* Fixed-size BFS queue (conservative upper bound). */
    PrintEntry queue[8192];
    int qhead = 0, qtail = 0;

    queue[qtail].pg = bt->root_page;
    queue[qtail].level = 0;
    qtail++;

    int cur_level = -1;

    while (qhead < qtail) {
        PrintEntry e = queue[qhead++];

        uint8_t buf[IO_PAGE_SIZE];
        /* bptree_print is const but io_read_page needs non-const FILE*;
         * the cast is intentional: we only read. */
        if (!io_read_page(bt->fp, e.pg, buf)) {
            printf("  [error reading page %" PRIu64 "]\n", e.pg);
            continue;
        }

        if (e.level != cur_level) {
            cur_level = e.level;
            printf("=== Level %d ===\n", cur_level);
        }

        uint8_t type = node_type(buf);
        uint16_t nk = node_nkeys(buf);
        const char *tname = (type == NODE_INTERNAL) ? "INTERNAL" : "LEAF";

        printf("  [%s page=%" PRIu64 " nkeys=%u]", tname, e.pg, (unsigned)nk);

        for (int i = 0; i < (int)nk; i++) {
            const uint8_t *kp = key_ptr_c(buf, i, bt->key_size);
            if (bt->key_size == sizeof(uint32_t)) {
                uint32_t k;
                memcpy(&k, kp, sizeof k);
                printf(" %u", k);
            } else {
                /* Print raw hex for composite keys. */
                printf(" [");
                for (size_t b = 0; b < bt->key_size; b++) {
                    printf("%02x", kp[b]);
                }
                printf("]");
            }
        }
        printf("\n");

        if (type == NODE_INTERNAL && qtail + (int)nk + 1 < 8192) {
            for (int i = 0; i <= (int)nk; i++) {
                queue[qtail].pg = child_get(buf, i, bt->key_size, bt->order);
                queue[qtail].level = e.level + 1;
                qtail++;
            }
        }
    }
}

/* ===================================================================== *
 *  bptree_verify                                                        *
 * ===================================================================== */

typedef struct {
    uint64_t pg;
    int depth;
    /* bounds propagated from parent: NULL = open */
    uint8_t lo[BPTREE_MAX_KEY_SIZE];
    bool has_lo;
    uint8_t hi[BPTREE_MAX_KEY_SIZE];
    bool has_hi;
} VerifyFrame;

bool bptree_verify(const bptree_t *bt) {
    if (bt == NULL) {
        return false;
    }

    uint64_t active_count = 0;
    uint64_t leaf_pages_dfs[65536]; /* pages visited by DFS in order */
    int leaf_dfs_count = 0;

    /* DFS stack */
    VerifyFrame dfs_stack[BPTREE_MAX_HEIGHT * 512];
    int dfs_top = 0;

    memset(&dfs_stack[0], 0, sizeof dfs_stack[0]);
    dfs_stack[0].pg = bt->root_page;
    dfs_stack[0].depth = 0;
    dfs_stack[0].has_lo = false;
    dfs_stack[0].has_hi = false;
    dfs_top = 1;

    int expected_leaf_depth = -1;

    while (dfs_top > 0) {
        VerifyFrame f = dfs_stack[--dfs_top];

        uint8_t buf[IO_PAGE_SIZE];
        if (!io_read_page(bt->fp, f.pg, buf)) {
            printf("verify: cannot read page %" PRIu64 "\n", f.pg);
            return false;
        }

        uint8_t type = node_type(buf);
        uint16_t nk = node_nkeys(buf);
        bool is_root = (f.pg == bt->root_page);

        /* Minimum occupancy: non-root must have >= order/2 keys. */
        if (!is_root && nk < (uint16_t)(bt->order / 2u)) {
            printf("verify: page %" PRIu64 " has %u keys < min %u\n", f.pg, (unsigned)nk, (unsigned)(bt->order / 2u));
            return false;
        }

        /* Keys must be strictly sorted within the node. */
        for (int i = 1; i < (int)nk; i++) {
            if (bt->cmp(key_ptr_c(buf, i - 1, bt->key_size), key_ptr_c(buf, i, bt->key_size)) >= 0) {
                printf("verify: page %" PRIu64 " keys not sorted at index %d\n", f.pg, i);
                return false;
            }
        }

        /* Bounds from parent. */
        if (f.has_lo && nk > 0 && bt->cmp(key_ptr_c(buf, 0, bt->key_size), f.lo) < 0) {
            printf("verify: page %" PRIu64 " first key below parent lower bound\n", f.pg);
            return false;
        }
        if (f.has_hi && nk > 0 && bt->cmp(key_ptr_c(buf, (int)nk - 1, bt->key_size), f.hi) > 0) {
            printf("verify: page %" PRIu64 " last key above parent upper bound\n", f.pg);
            return false;
        }

        if (type == NODE_LEAF) {
            if (expected_leaf_depth < 0) {
                expected_leaf_depth = f.depth;
            } else if (f.depth != expected_leaf_depth) {
                printf("verify: leaf page %" PRIu64 " at depth %d, expected %d\n", f.pg, f.depth, expected_leaf_depth);
                return false;
            }

            if (leaf_dfs_count < 65536) {
                leaf_pages_dfs[leaf_dfs_count++] = f.pg;
            }

            /* Count active entries. */
            for (int i = 0; i < (int)nk; i++) {
                if (val_slot_c(buf, i, bt->key_size, bt->order)[0] == TOMBSTONE_ACTIVE) {
                    active_count++;
                }
            }
        } else {
            /* Internal: push children with updated bounds. */
            for (int i = (int)nk; i >= 0; i--) {
                if (dfs_top >= (int)(sizeof dfs_stack / sizeof dfs_stack[0])) {
                    printf("verify: DFS stack overflow\n");
                    return false;
                }
                VerifyFrame cf;
                memset(&cf, 0, sizeof cf);
                cf.pg = child_get(buf, i, bt->key_size, bt->order);
                cf.depth = f.depth + 1;

                /* Lower bound for child[i]: key[i-1] of this node (if i>0). */
                if (i > 0) {
                    cf.has_lo = true;
                    memcpy(cf.lo, key_ptr_c(buf, i - 1, bt->key_size), bt->key_size);
                } else {
                    cf.has_lo = f.has_lo;
                    if (f.has_lo) {
                        memcpy(cf.lo, f.lo, bt->key_size);
                    }
                }

                /* Upper bound for child[i]: key[i] of this node (if i<nk). */
                if (i < (int)nk) {
                    cf.has_hi = true;
                    memcpy(cf.hi, key_ptr_c(buf, i, bt->key_size), bt->key_size);
                } else {
                    cf.has_hi = f.has_hi;
                    if (f.has_hi) {
                        memcpy(cf.hi, f.hi, bt->key_size);
                    }
                }

                dfs_stack[dfs_top++] = cf;
            }
        }
    }

    /* Verify active entry count matches header. */
    if (active_count != bt->num_keys) {
        printf("verify: header num_keys=%" PRIu64 " but counted %" PRIu64 " active entries\n", bt->num_keys, active_count);
        return false;
    }

    /* Verify leaf chain matches DFS order. */
    uint64_t chain_pg = (leaf_dfs_count > 0) ? leaf_pages_dfs[0] : 0;
    int chain_i = 0;

    while (chain_pg != 0) {
        if (chain_i >= leaf_dfs_count) {
            printf("verify: leaf chain longer than DFS leaf list\n");
            return false;
        }
        if (chain_pg != leaf_pages_dfs[chain_i]) {
            printf("verify: leaf chain page %" PRIu64 " != DFS leaf[%d]=%" PRIu64 "\n", chain_pg, chain_i, leaf_pages_dfs[chain_i]);
            return false;
        }
        chain_i++;

        uint8_t lbuf[IO_PAGE_SIZE];
        if (!io_read_page(bt->fp, chain_pg, lbuf)) {
            return false;
        }
        chain_pg = leaf_next_get(lbuf, bt->key_size, bt->order);
    }

    if (chain_i != leaf_dfs_count) {
        printf("verify: leaf chain has %d nodes, DFS found %d\n", chain_i, leaf_dfs_count);
        return false;
    }

    return true;
}
