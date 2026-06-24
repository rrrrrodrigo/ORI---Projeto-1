/**
 * @file bptree.h
 * @brief Generic paginated B+ tree index.
 *
 * Every node is exactly IO_PAGE_SIZE (4096) bytes on disk.
 * The tree is parameterised by key size and a comparator callback so the
 * same code handles both the 4-byte user index and the 20-byte training index.
 *
 * File format is specified in docs/data-structures.md.
 */

#ifndef GYMSOCIAL_BPTREE_H
#define GYMSOCIAL_BPTREE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ===================================================================== *
 *  Key abstraction                                                       *
 * ===================================================================== */

/**
 * Maximum supported key size (bytes). Larger keys require increasing this
 * constant and recompiling.
 */
#define BPTREE_MAX_KEY_SIZE 64u

/**
 * Comparator callback: same contract as memcmp.
 * a and b point to raw key bytes of the size passed to bptree_open.
 */
typedef int (*bptree_cmp_fn)(const void *a, const void *b);

/** Value stored per leaf entry: byte offset of the record in the .dat file. */
typedef uint64_t bptree_val_t;

/* ===================================================================== *
 *  Concrete key types used in this project                              *
 * ===================================================================== */

/**
 * Composite training-session key.
 * Ordered lexicographically (id_usuario primary) so all sessions of one user
 * are contiguous in the leaf level, making per-user range scans cheap.
 * The offset field acts as a tiebreaker to guarantee total order.
 */
typedef struct {
    uint32_t id_usuario;
    uint32_t id_exercicio;
    uint32_t data;   /* YYYYMMDD — numeric comparison == chronological */
    uint64_t offset; /* tiebreaker: guarantees total order */
} TrainingKey;

/** Comparator for the user index (4-byte uint32_t key). */
int bptree_cmp_u32(const void *a, const void *b);

/** Comparator for the training index (20-byte TrainingKey, lexicographic). */
int bptree_cmp_training(const void *a, const void *b);

/* ===================================================================== *
 *  Opaque tree handle                                                   *
 * ===================================================================== */

typedef struct bptree bptree_t;

/* ===================================================================== *
 *  API                                                                  *
 * ===================================================================== */

/**
 * Opens (or creates) a B+ tree index file at `path`.
 *
 * key_size  byte size of a single key (must be <= BPTREE_MAX_KEY_SIZE).
 * cmp       comparator for keys.
 *
 * On create: writes the file header and allocates an empty leaf root.
 * On open:   validates the magic bytes; fails with EPROTO if invalid.
 * Fails with EINVAL if path==NULL, key_size==0, or cmp==NULL.
 * Returns NULL on failure (errno set).
 */
bptree_t *bptree_open(const char *path, size_t key_size, bptree_cmp_fn cmp);

/**
 * Writes the updated header and closes the file.
 * NULL is a no-op (returns true).
 */
bool bptree_close(bptree_t *bt);

/**
 * Inserts (key, val) into the tree. Splits nodes as needed.
 * Fails with EINVAL if bt==NULL or key==NULL.
 */
bool bptree_insert(bptree_t *bt, const void *key, bptree_val_t val);

/**
 * Exact-match search. Writes the value to *out_val.
 * Returns false + ENOENT if the key is absent or tombstoned.
 * Fails with EINVAL if bt==NULL, key==NULL, or out_val==NULL.
 */
bool bptree_search(bptree_t *bt, const void *key, bptree_val_t *out_val);

/**
 * Callback type for bptree_range.
 * Return true to continue scanning, false to stop (early-stop).
 */
typedef bool (*bptree_scan_cb)(const void *key, bptree_val_t val, void *ctx);

/**
 * Range scan: calls cb for every active entry where
 *   cmp(entry_key, min) >= 0  AND  cmp(entry_key, max) <= 0,
 * in ascending key order. Stops when cb returns false.
 * min or max may be NULL (open-ended range).
 * Fails with EINVAL if bt==NULL or cb==NULL.
 */
bool bptree_range(bptree_t *bt, const void *min, const void *max, bptree_scan_cb cb, void *ctx);

/**
 * Logical deletion: marks the leaf entry as deleted (tombstone byte).
 * Does NOT rebalance or merge nodes — tombstone only.
 * Returns false + ENOENT if the key is absent or already tombstoned.
 * Fails with EINVAL if bt==NULL or key==NULL.
 */
bool bptree_delete(bptree_t *bt, const void *key);

/**
 * Prints the tree structure to stdout, level by level.
 * Used to visually confirm splits during debugging.
 */
void bptree_print(const bptree_t *bt);

/**
 * Integrity check: verifies sorted order within nodes, minimum occupancy
 * (>= order/2 keys for non-root nodes), and leaf-chain continuity.
 * Prints the first violated invariant and returns false if corrupt.
 */
bool bptree_verify(const bptree_t *bt);

#endif /* GYMSOCIAL_BPTREE_H */
