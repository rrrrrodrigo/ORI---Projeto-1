/**
 * @file test_bptree.c
 * @brief Suite de testes Unity para a árvore B+ genérica (bptree).
 *
 * Todos os testes usam chaves uint32_t e bptree_cmp_u32.
 * Nenhum tipo Sessao ou Usuario está envolvido aqui.
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
 *  Infraestrutura de testes                                             *
 * ===================================================================== */

static char tmp_path[256];
static bptree_t *bt = NULL;

// cppcheck-suppress unusedFunction
void setUp(void) {
    /* Caminho único por processo (testes executam em série sob o Unity). */
    snprintf(tmp_path, sizeof tmp_path, "/tmp/test_bptree_%d.idx", (int)getpid());
    remove(tmp_path); /* estado limpo */
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

/* Auxiliar: insere uma chave uint32_t simples com ela mesma como valor. */
static void insert_u32(uint32_t k) {
    bptree_val_t val = (bptree_val_t)k * 100u;
    TEST_ASSERT_TRUE(bptree_insert(bt, &k, val));
}

/* Auxiliar: verifica que uma chave é encontrada com o valor esperado. */
static void assert_found(uint32_t k) {
    bptree_val_t out = 0;
    TEST_ASSERT_TRUE_MESSAGE(bptree_search(bt, &k, &out), "chave nao encontrada");
    TEST_ASSERT_EQUAL_UINT64((bptree_val_t)k * 100u, out);
}

/* Auxiliar: verifica que uma chave está ausente (ENOENT). */
static void assert_absent(uint32_t k) {
    bptree_val_t out = 0;
    bool found = bptree_search(bt, &k, &out);
    TEST_ASSERT_FALSE(found);
    TEST_ASSERT_EQUAL_INT(ENOENT, errno);
}

/* ===================================================================== *
 *  Teste 1: abertura cria cabeçalho válido                             *
 * ===================================================================== */

void test_open_creates_valid_header(void) {
    /* Uma árvore recém-criada deve passar em bptree_verify. */
    TEST_ASSERT_TRUE(bptree_verify(bt));
}

/* ===================================================================== *
 *  Teste 2: inserir 1 chave e a busca a encontra                       *
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
 *  Teste 3: inserir 10 chaves em ordem embaralhada, buscar todas       *
 * ===================================================================== */

void test_insert_10_random_search_all(void) {
    /* Embaralhado manualmente para que as inserções atinjam posições diferentes. */
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
 *  Teste 4: busca por chave ausente retorna false + ENOENT             *
 * ===================================================================== */

void test_search_absent_returns_enoent(void) {
    insert_u32(1u);
    insert_u32(2u);
    assert_absent(99u);
}

/* ===================================================================== *
 *  Teste 5: inserir exatamente order chaves (folha raiz cheia), depois *
 *           mais 1 para forçar divisão de folha; verify passa nas duas. *
 * ===================================================================== */

void test_leaf_split_verify_passes(void) {
    /*
     * A ordem para chaves u32 é 313.
     * Inserir 313 chaves para encher a folha raiz, verificar.
     * Inserir mais 1 para forçar a divisão, verificar.
     */
    for (uint32_t i = 1u; i <= 313u; i++) {
        insert_u32(i);
    }
    TEST_ASSERT_TRUE(bptree_verify(bt));

    insert_u32(314u);
    TEST_ASSERT_TRUE(bptree_verify(bt));

    /* Todas as chaves ainda pesquisáveis. */
    for (uint32_t i = 1u; i <= 314u; i++) {
        assert_found(i);
    }
}

/* ===================================================================== *
 *  Teste 6: inserir order+1 chaves → raiz divide → altura se torna 2  *
 * ===================================================================== */

void test_root_split_height_2_verify(void) {
    /* 314 inserções fazem a raiz folha única dividir:
     * uma nova raiz interna é criada, altura = 2. */
    for (uint32_t i = 1u; i <= 314u; i++) {
        insert_u32(i);
    }
    TEST_ASSERT_TRUE(bptree_verify(bt));

    /* Verificação pontual de algumas chaves. */
    assert_found(1u);
    assert_found(157u);
    assert_found(314u);
}

/* ===================================================================== *
 *  Teste 7: estresse com 10 000 chaves em ordem crescente             *
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
 *  Teste 8: estresse com 10 000 chaves em ordem decrescente           *
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
 *  Teste 9: estresse com 10 000 chaves aleatórias (LCG)              *
 * ===================================================================== */

void test_stress_10k_random_verify(void) {
    /* LCG para obter sequência determinística mas não-monotônica. */
    uint32_t seen[10000];
    uint32_t state = 0xDEADBEEFu;
    int n = 0;

    while (n < 10000) {
        state = state * 1664525u + 1013904223u;
        uint32_t k = (state % 100000u) + 1u;
        /* Evita duplicatas (varredura linear simples para n pequeno). */
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
 *  Teste 10: varredura [20..40] sobre chaves 1..100 → exatos 21 result *
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

    /* Verifica ordem crescente e valores corretos. */
    for (int i = 0; i < 21; i++) {
        TEST_ASSERT_EQUAL_UINT32((uint32_t)(20 + i), sc.results[i]);
    }
}

/* ===================================================================== *
 *  Teste 11: varredura com min NULL → retorna todas as chaves <= max   *
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
 *  Teste 12: varredura com max NULL → retorna todas as chaves >= min   *
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
 *  Teste 13: varredura com parada antecipada após 5 resultados        *
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
 *  Teste 14: excluir chave existente → busca retorna false + ENOENT   *
 * ===================================================================== */

void test_delete_existing_search_enoent(void) {
    insert_u32(10u);
    insert_u32(20u);
    insert_u32(30u);

    uint32_t k = 20u;
    TEST_ASSERT_TRUE(bptree_delete(bt, &k));
    assert_absent(20u);

    /* Vizinhos ainda presentes. */
    assert_found(10u);
    assert_found(30u);
}

/* ===================================================================== *
 *  Teste 15: excluir chave ausente retorna false + ENOENT             *
 * ===================================================================== */

void test_delete_absent_returns_enoent(void) {
    insert_u32(1u);
    uint32_t missing = 999u;
    bool ok = bptree_delete(bt, &missing);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_INT(ENOENT, errno);
}

/* ===================================================================== *
 *  Teste 16: persistência — inserir 500, fechar, reabrir, buscar todas *
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
 *  Teste 17: teste unitário do comparador TrainingKey                 *
 * ===================================================================== */

void test_training_key_comparator(void) {
    TrainingKey a = {1u, 1u, 20240101u, 0u};
    TrainingKey b = {1u, 1u, 20240101u, 0u};
    TEST_ASSERT_EQUAL_INT(0, bptree_cmp_training(&a, &b));

    /* id_usuario é o campo primário. */
    b.id_usuario = 2u;
    TEST_ASSERT_TRUE(bptree_cmp_training(&a, &b) < 0);
    TEST_ASSERT_TRUE(bptree_cmp_training(&b, &a) > 0);

    /* id_exercicio é o campo secundário. */
    b = a;
    b.id_exercicio = 2u;
    TEST_ASSERT_TRUE(bptree_cmp_training(&a, &b) < 0);

    /* data é o campo terciário. */
    b = a;
    b.data = 20240102u;
    TEST_ASSERT_TRUE(bptree_cmp_training(&a, &b) < 0);

    /* offset é o desempatador. */
    b = a;
    b.offset = 1u;
    TEST_ASSERT_TRUE(bptree_cmp_training(&a, &b) < 0);

    /* O campo primário domina mesmo que todos os outros difiram. */
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
