/**
 * @file test_bptree.c
 * @brief Unity test suite for the generic B+ tree (bptree).
 *
 * All tests use uint32_t keys and bptree_cmp_u32.
 * No Sessao or Usuario types are involved here.
 */

#include "bptree.h"
#include "ioutils.h"
#include "unity/unity.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ===================================================================== *
 *  Test infrastructure                                                  *
 * ===================================================================== */

static char tmp_path[256];
static bptree_t *bt = NULL;

// cppcheck-suppress unusedFunction
void setUp(void) {
    /* Unique path per process (tests run serially under Unity). */
    snprintf(tmp_path, sizeof tmp_path, "/tmp/test_bptree_%d.idx", (int)getpid());
    remove(tmp_path); /* clean slate */
    bt = bptree_open(tmp_path, sizeof(uint32_t), bptree_cmp_u32);
    TEST_ASSERT_NOT_NULL(bt);
}

// cppcheck-suppress unusedFunction
void tearDown(void) {
    if (bt != NULL) {
        bptree_close(bt);
        bt = NULL;
    }
    remove(tmp_path);
}

/* Convenience: insert a plain uint32_t key with itself as value. */
static void insert_u32(uint32_t k) {
    bptree_val_t val = (bptree_val_t)k * 100u;
    TEST_ASSERT_TRUE(bptree_insert(bt, &k, val));
}

/* Convenience: assert a key is found with the expected value. */
static void assert_found(uint32_t k) {
    bptree_val_t out = 0;
    TEST_ASSERT_TRUE_MESSAGE(bptree_search(bt, &k, &out), "key not found");
    TEST_ASSERT_EQUAL_UINT64((bptree_val_t)k * 100u, out);
}

/* Convenience: assert a key is absent (ENOENT). */
static void assert_absent(uint32_t k) {
    bptree_val_t out = 0;
    bool found = bptree_search(bt, &k, &out);
    TEST_ASSERT_FALSE(found);
    TEST_ASSERT_EQUAL_INT(ENOENT, errno);
}

/* ===================================================================== *
 *  Test 1: open creates a valid header                                  *
 * ===================================================================== */

void test_open_creates_valid_header(void) {
    /* A fresh tree must survive bptree_verify. */
    TEST_ASSERT_TRUE(bptree_verify(bt));
}

/* ===================================================================== *
 *  Test 2: insert 1 key and search finds it                            *
 * ===================================================================== */

void test_insert_one_search_finds_it(void) {
    uint32_t k = 42u;
    bptree_val_t val = 9999u;
    TEST_ASSERT_TRUE(bptree_insert(bt, &k, val));

    bptree_val_t out = 0;
    TEST_ASSERT_TRUE(bptree_search(bt, &k, &out));
    TEST_ASSERT_EQUAL_UINT64(val, out);
}

/* ===================================================================== *
 *  Test 3: insert 10 keys in a scrambled order, search all             *
 * ===================================================================== */

void test_insert_10_random_search_all(void) {
    /* Scrambled manually so inserts hit different positions. */
    const uint32_t keys[10] = {7, 3, 9, 1, 5, 0, 8, 4, 6, 2};
    for (int i = 0; i < 10; i++) {
        insert_u32(keys[i]);
    }
    for (int i = 0; i < 10; i++) {
        assert_found(keys[i]);
    }
    TEST_ASSERT_TRUE(bptree_verify(bt));
}

/* ===================================================================== *
 *  Test 4: search for absent key returns false + ENOENT                *
 * ===================================================================== */

void test_search_absent_returns_enoent(void) {
    insert_u32(1u);
    insert_u32(2u);
    assert_absent(99u);
}

/* ===================================================================== *
 *  Test 5: insert exactly order keys (full root leaf), then 1 more to  *
 *          force a leaf split; verify passes both times.                *
 * ===================================================================== */

void test_leaf_split_verify_passes(void) {
    /*
     * The order for u32 keys is 313.
     * Insert 313 keys to fill the root leaf, verify.
     * Insert 1 more to force the split, verify.
     */
    for (uint32_t i = 1u; i <= 313u; i++) {
        insert_u32(i);
    }
    TEST_ASSERT_TRUE(bptree_verify(bt));

    insert_u32(314u);
    TEST_ASSERT_TRUE(bptree_verify(bt));

    /* All keys still searchable. */
    for (uint32_t i = 1u; i <= 314u; i++) {
        assert_found(i);
    }
}

/* ===================================================================== *
 *  Test 6: insert order+1 keys → root splits → height becomes 2        *
 * ===================================================================== */

void test_root_split_height_2_verify(void) {
    /* 314 inserts causes the single-leaf root to split:
     * a new internal root is created, height = 2. */
    for (uint32_t i = 1u; i <= 314u; i++) {
        insert_u32(i);
    }
    TEST_ASSERT_TRUE(bptree_verify(bt));

    /* Spot-check a few keys. */
    assert_found(1u);
    assert_found(157u);
    assert_found(314u);
}

/* ===================================================================== *
 *  Test 7: stress 10 000 ascending                                     *
 * ===================================================================== */

void test_stress_10k_ascending_verify(void) {
    for (uint32_t i = 1u; i <= 10000u; i++) {
        insert_u32(i);
    }
    TEST_ASSERT_TRUE(bptree_verify(bt));
    for (uint32_t i = 1u; i <= 10000u; i++) {
        assert_found(i);
    }
}

/* ===================================================================== *
 *  Test 8: stress 10 000 descending                                    *
 * ===================================================================== */

void test_stress_10k_descending_verify(void) {
    for (uint32_t i = 10000u; i >= 1u; i--) {
        insert_u32(i);
    }
    TEST_ASSERT_TRUE(bptree_verify(bt));
    for (uint32_t i = 1u; i <= 10000u; i++) {
        assert_found(i);
    }
}

/* ===================================================================== *
 *  Test 9: stress 10 000 random (LCG)                                  *
 * ===================================================================== */

void test_stress_10k_random_verify(void) {
    /* LCG to get a deterministic but non-monotonic sequence. */
    uint32_t seen[10000];
    uint32_t state = 0xDEADBEEFu;
    int n = 0;

    while (n < 10000) {
        state = state * 1664525u + 1013904223u;
        uint32_t k = (state % 100000u) + 1u;
        /* Avoid duplicates (simple linear scan for small n). */
        bool dup = false;
        for (int j = 0; j < n; j++) {
            if (seen[j] == k) {
                dup = true;
                break;
            }
        }
        if (dup)
            continue;
        seen[n++] = k;
        insert_u32(k);
    }

    TEST_ASSERT_TRUE(bptree_verify(bt));

    for (int i = 0; i < 10000; i++) {
        assert_found(seen[i]);
    }
}

/* ===================================================================== *
 *  Test 10: range scan [20..40] over keys 1..100 → exactly 21 results  *
 * ===================================================================== */

typedef struct {
    uint32_t results[128];
    int count;
} ScanCtx;

static bool scan_collect(const void *key, bptree_val_t val, void *ctx) {
    ScanCtx *sc = (ScanCtx *)ctx;
    uint32_t k;
    memcpy(&k, key, sizeof k);
    sc->results[sc->count++] = k;
    (void)val;
    return true;
}

void test_range_scan_20_to_40(void) {
    for (uint32_t i = 1u; i <= 100u; i++) {
        insert_u32(i);
    }

    uint32_t lo = 20u, hi = 40u;
    ScanCtx sc = {.count = 0};
    TEST_ASSERT_TRUE(bptree_range(bt, &lo, &hi, scan_collect, &sc));
    TEST_ASSERT_EQUAL_INT(21, sc.count);

    /* Verify ascending order and correct values. */
    for (int i = 0; i < 21; i++) {
        TEST_ASSERT_EQUAL_UINT32((uint32_t)(20 + i), sc.results[i]);
    }
}

/* ===================================================================== *
 *  Test 11: range with NULL min → returns all keys ≤ max               *
 * ===================================================================== */

void test_range_open_min_null(void) {
    for (uint32_t i = 1u; i <= 10u; i++) {
        insert_u32(i);
    }

    uint32_t hi = 5u;
    ScanCtx sc = {.count = 0};
    TEST_ASSERT_TRUE(bptree_range(bt, NULL, &hi, scan_collect, &sc));
    TEST_ASSERT_EQUAL_INT(5, sc.count);
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_UINT32((uint32_t)(i + 1), sc.results[i]);
    }
}

/* ===================================================================== *
 *  Test 12: range with NULL max → returns all keys ≥ min               *
 * ===================================================================== */

void test_range_open_max_null(void) {
    for (uint32_t i = 1u; i <= 10u; i++) {
        insert_u32(i);
    }

    uint32_t lo = 7u;
    ScanCtx sc = {.count = 0};
    TEST_ASSERT_TRUE(bptree_range(bt, &lo, NULL, scan_collect, &sc));
    TEST_ASSERT_EQUAL_INT(4, sc.count);
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_UINT32((uint32_t)(7 + i), sc.results[i]);
    }
}

/* ===================================================================== *
 *  Test 13: range early stop after 5 results                           *
 * ===================================================================== */

typedef struct {
    int limit;
    int count;
} EarlyStopCtx;

static bool scan_early_stop(const void *key, bptree_val_t val, void *ctx) {
    EarlyStopCtx *ec = (EarlyStopCtx *)ctx;
    (void)key;
    (void)val;
    ec->count++;
    return ec->count < ec->limit;
}

void test_range_early_stop(void) {
    for (uint32_t i = 1u; i <= 100u; i++) {
        insert_u32(i);
    }

    EarlyStopCtx ec = {.limit = 5, .count = 0};
    TEST_ASSERT_TRUE(bptree_range(bt, NULL, NULL, scan_early_stop, &ec));
    TEST_ASSERT_EQUAL_INT(5, ec.count);
}

/* ===================================================================== *
 *  Test 14: delete existing key → search returns false + ENOENT        *
 * ===================================================================== */

void test_delete_existing_search_enoent(void) {
    insert_u32(10u);
    insert_u32(20u);
    insert_u32(30u);

    uint32_t k = 20u;
    TEST_ASSERT_TRUE(bptree_delete(bt, &k));
    assert_absent(20u);

    /* Neighbours still present. */
    assert_found(10u);
    assert_found(30u);
}

/* ===================================================================== *
 *  Test 15: delete absent key returns false + ENOENT                   *
 * ===================================================================== */

void test_delete_absent_returns_enoent(void) {
    insert_u32(1u);
    uint32_t missing = 999u;
    bool ok = bptree_delete(bt, &missing);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_INT(ENOENT, errno);
}

/* ===================================================================== *
 *  Test 16: persistence — insert 500, close, reopen, search all        *
 * ===================================================================== */

void test_persistence_500_keys(void) {
    for (uint32_t i = 1u; i <= 500u; i++) {
        insert_u32(i);
    }
    TEST_ASSERT_TRUE(bptree_close(bt));
    bt = NULL;

    bt = bptree_open(tmp_path, sizeof(uint32_t), bptree_cmp_u32);
    TEST_ASSERT_NOT_NULL(bt);

    for (uint32_t i = 1u; i <= 500u; i++) {
        assert_found(i);
    }
    TEST_ASSERT_TRUE(bptree_verify(bt));
}

/* ===================================================================== *
 *  Test 17: TrainingKey comparator unit test                           *
 * ===================================================================== */

void test_training_key_comparator(void) {
    TrainingKey a = {1u, 1u, 20240101u, 0u};
    TrainingKey b = {1u, 1u, 20240101u, 0u};
    TEST_ASSERT_EQUAL_INT(0, bptree_cmp_training(&a, &b));

    /* id_usuario is primary. */
    b.id_usuario = 2u;
    TEST_ASSERT_TRUE(bptree_cmp_training(&a, &b) < 0);
    TEST_ASSERT_TRUE(bptree_cmp_training(&b, &a) > 0);

    /* id_exercicio is secondary. */
    b = a;
    b.id_exercicio = 2u;
    TEST_ASSERT_TRUE(bptree_cmp_training(&a, &b) < 0);

    /* data is tertiary. */
    b = a;
    b.data = 20240102u;
    TEST_ASSERT_TRUE(bptree_cmp_training(&a, &b) < 0);

    /* offset is the tiebreaker. */
    b = a;
    b.offset = 1u;
    TEST_ASSERT_TRUE(bptree_cmp_training(&a, &b) < 0);

    /* Primary field dominates even if all others differ. */
    TrainingKey lo = {1u, 9u, 99999999u, 999u};
    TrainingKey hi = {2u, 0u, 0u, 0u};
    TEST_ASSERT_TRUE(bptree_cmp_training(&lo, &hi) < 0);
}

/* ===================================================================== *
 *  main                                                                 *
 * ===================================================================== */

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_open_creates_valid_header);
    RUN_TEST(test_insert_one_search_finds_it);
    RUN_TEST(test_insert_10_random_search_all);
    RUN_TEST(test_search_absent_returns_enoent);
    RUN_TEST(test_leaf_split_verify_passes);
    RUN_TEST(test_root_split_height_2_verify);
    RUN_TEST(test_stress_10k_ascending_verify);
    RUN_TEST(test_stress_10k_descending_verify);
    RUN_TEST(test_stress_10k_random_verify);
    RUN_TEST(test_range_scan_20_to_40);
    RUN_TEST(test_range_open_min_null);
    RUN_TEST(test_range_open_max_null);
    RUN_TEST(test_range_early_stop);
    RUN_TEST(test_delete_existing_search_enoent);
    RUN_TEST(test_delete_absent_returns_enoent);
    RUN_TEST(test_persistence_500_keys);
    RUN_TEST(test_training_key_comparator);

    return UNITY_END();
}
