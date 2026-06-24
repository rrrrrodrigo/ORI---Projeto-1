/**
 * @file test_sessao_file.c
 * @brief Testes unitários do módulo sessao_file (framework Unity).
 *
 * Cada teste cria/remove um arquivo temporário via setUp/tearDown.
 * Testa: cabeçalho, persistência, inserção, round-trip, deleção lógica,
 * scan, early-stop, integridade de bytes no disco e caminhos de erro.
 */

#include "sessao_file.h"
#include "unity/unity.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TMP_PATH = "build/test_sessao_tmp.dat";

/* ===================================================================== *
 *  setUp / tearDown                                                     *
 * ===================================================================== */

void setUp(void) {
    remove(TMP_PATH);
}

void tearDown(void) {
    remove(TMP_PATH);
}

/* ===================================================================== *
 *  Helpers                                                              *
 * ===================================================================== */

/* Sessão mínima: 1 exercício, 2 séries, observação não vazia. */
static void fill_simple_sessao(Sessao *s, Serie *series_buf, Exercicio *ex_buf) {
    series_buf[0].carga_g = 80000u;
    series_buf[0].repeticoes = 10u;
    series_buf[1].carga_g = 100000u;
    series_buf[1].repeticoes = 8u;

    ex_buf[0].id_exercicio = 7u;
    ex_buf[0].observacao = "pegada larga";
    ex_buf[0].num_series = 2u;
    ex_buf[0].series = series_buf;

    s->id_usuario = 42u;
    s->data = 20240315u;
    s->id_academia = 3u;
    s->num_exercicios = 1u;
    s->exercicios = ex_buf;
}

/* Lê num_registros e num_deletados do cabeçalho diretamente do arquivo raw. */
static void read_header_raw(const char *path, uint32_t *num_reg, uint32_t *num_del) {
    FILE *f = io_open(path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_TRUE(io_seek(f, 8)); /* offset de num_registros */
    TEST_ASSERT_TRUE(io_read_u32(f, num_reg));
    TEST_ASSERT_TRUE(io_read_u32(f, num_del));
    TEST_ASSERT_TRUE(io_close(f));
}

/* ===================================================================== *
 *  Callbacks de scan                                                    *
 * ===================================================================== */

typedef struct {
    int count;
    byte_offset_t offsets[16];
} scan_count_ctx_t;

static bool scan_count_cb(byte_offset_t offset, const Sessao *s, void *ctx) {
    (void)s;
    scan_count_ctx_t *c = (scan_count_ctx_t *)ctx;
    if (c->count < 16) {
        c->offsets[c->count] = offset;
    }
    c->count++;
    return true;
}

static bool scan_stop_after_one_cb(byte_offset_t offset, const Sessao *s, void *ctx) {
    (void)offset;
    (void)s;
    int *calls = (int *)ctx;
    (*calls)++;
    return false; /* para após o primeiro */
}

/* ===================================================================== *
 *  Teste 1 — open cria cabeçalho válido (magic "GYMS", contadores zero) *
 * ===================================================================== */

void test_open_creates_valid_header(void) {
    sessao_file_t *sf = sessao_file_open(TMP_PATH);
    TEST_ASSERT_NOT_NULL(sf);
    TEST_ASSERT_TRUE(sessao_file_close(sf));

    FILE *raw = io_open(TMP_PATH, "rb");
    TEST_ASSERT_NOT_NULL(raw);

    uint8_t magic[4];
    TEST_ASSERT_TRUE(io_read_exact(raw, magic, 4));
    TEST_ASSERT_EQUAL_UINT8('G', magic[0]);
    TEST_ASSERT_EQUAL_UINT8('Y', magic[1]);
    TEST_ASSERT_EQUAL_UINT8('M', magic[2]);
    TEST_ASSERT_EQUAL_UINT8('S', magic[3]);

    uint16_t versao;
    TEST_ASSERT_TRUE(io_read_u16(raw, &versao));
    TEST_ASSERT_EQUAL_UINT16(1u, versao);

    TEST_ASSERT_TRUE(io_seek(raw, 8));
    uint32_t num_reg, num_del;
    TEST_ASSERT_TRUE(io_read_u32(raw, &num_reg));
    TEST_ASSERT_TRUE(io_read_u32(raw, &num_del));
    TEST_ASSERT_EQUAL_UINT32(0u, num_reg);
    TEST_ASSERT_EQUAL_UINT32(0u, num_del);

    TEST_ASSERT_TRUE(io_close(raw));
}

/* ===================================================================== *
 *  Teste 2 — cabeçalho persiste após close + reopen                    *
 * ===================================================================== */

void test_header_persists_after_reopen(void) {
    Serie series[2];
    Exercicio ex[1];
    Sessao s;
    fill_simple_sessao(&s, series, ex);

    sessao_file_t *sf = sessao_file_open(TMP_PATH);
    TEST_ASSERT_NOT_NULL(sf);
    byte_offset_t off;
    TEST_ASSERT_TRUE(sessao_file_insert(sf, &s, &off));
    TEST_ASSERT_TRUE(sessao_file_close(sf));

    /* reabre e verifica contadores */
    sf = sessao_file_open(TMP_PATH);
    TEST_ASSERT_NOT_NULL(sf);
    TEST_ASSERT_TRUE(sessao_file_close(sf));

    uint32_t num_reg, num_del;
    read_header_raw(TMP_PATH, &num_reg, &num_del);
    TEST_ASSERT_EQUAL_UINT32(1u, num_reg);
    TEST_ASSERT_EQUAL_UINT32(0u, num_del);
}

/* ===================================================================== *
 *  Teste 3 — open rejeita magic errado → errno == EPROTO               *
 * ===================================================================== */

void test_open_rejects_wrong_magic(void) {
    /* cria arquivo com magic inválido */
    FILE *f = io_open(TMP_PATH, "wb");
    TEST_ASSERT_NOT_NULL(f);
    uint8_t bad[32];
    memset(bad, 0, sizeof bad);
    bad[0] = 'B';
    bad[1] = 'A';
    bad[2] = 'D';
    bad[3] = '!';
    TEST_ASSERT_TRUE(io_write_exact(f, bad, sizeof bad));
    TEST_ASSERT_TRUE(io_close(f));

    errno = 0;
    // cppcheck-suppress constVariablePointer
    sessao_file_t *const sf = sessao_file_open(TMP_PATH);
    TEST_ASSERT_NULL(sf);
    TEST_ASSERT_EQUAL_INT(EPROTO, errno);
}

/* ===================================================================== *
 *  Teste 4 — insert + read_at round-trip: 1 exercício, 2 séries        *
 * ===================================================================== */

void test_insert_read_at_roundtrip(void) {
    Serie series[2];
    Exercicio ex[1];
    Sessao s_in;
    fill_simple_sessao(&s_in, series, ex);

    sessao_file_t *sf = sessao_file_open(TMP_PATH);
    TEST_ASSERT_NOT_NULL(sf);

    byte_offset_t off;
    TEST_ASSERT_TRUE(sessao_file_insert(sf, &s_in, &off));

    Sessao s_out;
    TEST_ASSERT_TRUE(sessao_file_read_at(sf, off, &s_out));

    TEST_ASSERT_EQUAL_UINT32(42u, s_out.id_usuario);
    TEST_ASSERT_EQUAL_UINT32(20240315u, s_out.data);
    TEST_ASSERT_EQUAL_UINT32(3u, s_out.id_academia);
    TEST_ASSERT_EQUAL_UINT16(1u, s_out.num_exercicios);

    TEST_ASSERT_NOT_NULL(s_out.exercicios);
    // cppcheck-suppress nullPointerRedundantCheck
    TEST_ASSERT_EQUAL_UINT32(7u, s_out.exercicios[0].id_exercicio);
    // cppcheck-suppress nullPointerRedundantCheck
    TEST_ASSERT_EQUAL_STRING("pegada larga", s_out.exercicios[0].observacao);
    // cppcheck-suppress nullPointerRedundantCheck
    TEST_ASSERT_EQUAL_UINT16(2u, s_out.exercicios[0].num_series);

    // cppcheck-suppress nullPointerRedundantCheck
    TEST_ASSERT_NOT_NULL(s_out.exercicios[0].series);
    // cppcheck-suppress nullPointerRedundantCheck
    TEST_ASSERT_EQUAL_UINT32(80000u, s_out.exercicios[0].series[0].carga_g);
    // cppcheck-suppress nullPointerRedundantCheck
    TEST_ASSERT_EQUAL_UINT16(10u, s_out.exercicios[0].series[0].repeticoes);
    // cppcheck-suppress nullPointerRedundantCheck
    TEST_ASSERT_EQUAL_UINT32(100000u, s_out.exercicios[0].series[1].carga_g);
    // cppcheck-suppress nullPointerRedundantCheck
    TEST_ASSERT_EQUAL_UINT16(8u, s_out.exercicios[0].series[1].repeticoes);

    sessao_free(&s_out);
    TEST_ASSERT_TRUE(sessao_file_close(sf));
}

/* ===================================================================== *
 *  Teste 5 — múltiplos exercícios, observações variadas                 *
 * ===================================================================== */

void test_insert_read_at_multiple_exercises(void) {
    Serie s1[1] = {{50000u, 12u}};
    Serie s2[3] = {{70000u, 8u}, {70000u, 7u}, {70000u, 6u}};

    char long_obs[101];
    memset(long_obs, 'A', 100);
    long_obs[100] = '\0';

    Exercicio exs[3];
    exs[0].id_exercicio = 1u;
    exs[0].observacao = ""; /* vazia */
    exs[0].num_series = 1u;
    exs[0].series = s1;

    exs[1].id_exercicio = 2u;
    exs[1].observacao = "observacao com espacos";
    exs[1].num_series = 3u;
    exs[1].series = s2;

    exs[2].id_exercicio = 3u;
    exs[2].observacao = long_obs;
    exs[2].num_series = 0u;
    exs[2].series = NULL;

    Sessao s_in = {10u, 20240401u, 0u, 3u, exs};

    sessao_file_t *sf = sessao_file_open(TMP_PATH);
    TEST_ASSERT_NOT_NULL(sf);

    byte_offset_t off;
    TEST_ASSERT_TRUE(sessao_file_insert(sf, &s_in, &off));

    Sessao s_out;
    TEST_ASSERT_TRUE(sessao_file_read_at(sf, off, &s_out));

    TEST_ASSERT_EQUAL_UINT16(3u, s_out.num_exercicios);
    TEST_ASSERT_NOT_NULL(s_out.exercicios);

    /* exercício 0: observação vazia */
    // cppcheck-suppress nullPointerRedundantCheck
    TEST_ASSERT_EQUAL_UINT32(1u, s_out.exercicios[0].id_exercicio);
    // cppcheck-suppress nullPointerRedundantCheck
    TEST_ASSERT_NOT_NULL(s_out.exercicios[0].observacao);
    TEST_ASSERT_EQUAL_STRING("", s_out.exercicios[0].observacao);
    TEST_ASSERT_EQUAL_UINT16(1u, s_out.exercicios[0].num_series);
    TEST_ASSERT_EQUAL_UINT32(50000u, s_out.exercicios[0].series[0].carga_g);

    /* exercício 1: observação com espaços */
    TEST_ASSERT_EQUAL_UINT32(2u, s_out.exercicios[1].id_exercicio);
    TEST_ASSERT_EQUAL_STRING("observacao com espacos", s_out.exercicios[1].observacao);
    TEST_ASSERT_EQUAL_UINT16(3u, s_out.exercicios[1].num_series);

    /* exercício 2: observação longa, 0 séries */
    TEST_ASSERT_EQUAL_UINT32(3u, s_out.exercicios[2].id_exercicio);
    TEST_ASSERT_EQUAL_UINT16(100u, (uint16_t)strlen(s_out.exercicios[2].observacao));
    TEST_ASSERT_EQUAL_UINT16(0u, s_out.exercicios[2].num_series);
    TEST_ASSERT_NULL(s_out.exercicios[2].series);

    sessao_free(&s_out);
    TEST_ASSERT_TRUE(sessao_file_close(sf));
}

/* ===================================================================== *
 *  Teste 6 — múltiplas inserções produzem offsets estritamente crescentes *
 * ===================================================================== */

void test_multiple_inserts_increasing_offsets(void) {
    Serie series[1] = {{60000u, 10u}};
    Exercicio ex[1];
    ex[0].id_exercicio = 1u;
    ex[0].observacao = "x";
    ex[0].num_series = 1u;
    ex[0].series = series;

    Sessao s = {1u, 20240101u, 0u, 1u, ex};

    sessao_file_t *sf = sessao_file_open(TMP_PATH);
    TEST_ASSERT_NOT_NULL(sf);

    byte_offset_t off[3];
    for (int i = 0; i < 3; i++) {
        s.data = (uint32_t)(20240101 + i);
        TEST_ASSERT_TRUE(sessao_file_insert(sf, &s, &off[i]));
    }

    TEST_ASSERT_GREATER_THAN_UINT64(off[0], off[1]);
    TEST_ASSERT_GREATER_THAN_UINT64(off[1], off[2]);

    TEST_ASSERT_TRUE(sessao_file_close(sf));
}

/* ===================================================================== *
 *  Teste 7 — mark_deleted: read_at retorna false + ENOENT              *
 * ===================================================================== */

void test_mark_deleted_read_at_returns_enoent(void) {
    Serie series[1] = {{60000u, 5u}};
    Exercicio ex[1];
    ex[0].id_exercicio = 1u;
    ex[0].observacao = "";
    ex[0].num_series = 1u;
    ex[0].series = series;
    Sessao s = {1u, 20240101u, 0u, 1u, ex};

    sessao_file_t *sf = sessao_file_open(TMP_PATH);
    TEST_ASSERT_NOT_NULL(sf);

    byte_offset_t off;
    TEST_ASSERT_TRUE(sessao_file_insert(sf, &s, &off));
    TEST_ASSERT_TRUE(sessao_file_mark_deleted(sf, off));

    Sessao s_out;
    errno = 0;
    TEST_ASSERT_FALSE(sessao_file_read_at(sf, off, &s_out));
    TEST_ASSERT_EQUAL_INT(ENOENT, errno);

    TEST_ASSERT_TRUE(sessao_file_close(sf));
}

/* ===================================================================== *
 *  Teste 8 — mark_deleted duas vezes: segunda retorna ENOENT,           *
 *            contadores inalterados                                     *
 * ===================================================================== */

void test_mark_deleted_twice_counters_unchanged(void) {
    Serie series[1] = {{60000u, 5u}};
    Exercicio ex[1];
    ex[0].id_exercicio = 1u;
    ex[0].observacao = "";
    ex[0].num_series = 1u;
    ex[0].series = series;
    Sessao s = {1u, 20240101u, 0u, 1u, ex};

    sessao_file_t *sf = sessao_file_open(TMP_PATH);
    TEST_ASSERT_NOT_NULL(sf);

    byte_offset_t off;
    TEST_ASSERT_TRUE(sessao_file_insert(sf, &s, &off));
    TEST_ASSERT_TRUE(sessao_file_mark_deleted(sf, off));

    /* segunda deleção deve falhar */
    errno = 0;
    TEST_ASSERT_FALSE(sessao_file_mark_deleted(sf, off));
    TEST_ASSERT_EQUAL_INT(ENOENT, errno);

    TEST_ASSERT_TRUE(sessao_file_close(sf));

    /* verifica contadores: num_reg=0, num_del=1 (não incrementou para 2) */
    uint32_t num_reg, num_del;
    read_header_raw(TMP_PATH, &num_reg, &num_del);
    TEST_ASSERT_EQUAL_UINT32(0u, num_reg);
    TEST_ASSERT_EQUAL_UINT32(1u, num_del);
}

/* ===================================================================== *
 *  Teste 9 — scan pula registros deletados                             *
 * ===================================================================== */

void test_scan_skips_deleted(void) {
    Serie series[1] = {{60000u, 10u}};
    Exercicio ex[1];
    ex[0].id_exercicio = 1u;
    ex[0].observacao = "";
    ex[0].num_series = 1u;
    ex[0].series = series;
    Sessao s = {1u, 20240101u, 0u, 1u, ex};

    sessao_file_t *sf = sessao_file_open(TMP_PATH);
    TEST_ASSERT_NOT_NULL(sf);

    byte_offset_t off[3];
    for (int i = 0; i < 3; i++) {
        s.data = (uint32_t)(20240101 + i);
        TEST_ASSERT_TRUE(sessao_file_insert(sf, &s, &off[i]));
    }
    /* deleta o do meio */
    TEST_ASSERT_TRUE(sessao_file_mark_deleted(sf, off[1]));

    scan_count_ctx_t ctx = {0, {0}};
    TEST_ASSERT_TRUE(sessao_file_scan(sf, scan_count_cb, &ctx));

    /* deve ter visitado 2 registros (o primeiro e o último) */
    TEST_ASSERT_EQUAL_INT(2, ctx.count);
    TEST_ASSERT_EQUAL_UINT64(off[0], ctx.offsets[0]);
    TEST_ASSERT_EQUAL_UINT64(off[2], ctx.offsets[1]);

    TEST_ASSERT_TRUE(sessao_file_close(sf));
}

/* ===================================================================== *
 *  Teste 10 — scan early-stop (callback retorna false)                 *
 * ===================================================================== */

void test_scan_early_stop(void) {
    Serie series[1] = {{60000u, 10u}};
    Exercicio ex[1];
    ex[0].id_exercicio = 1u;
    ex[0].observacao = "";
    ex[0].num_series = 1u;
    ex[0].series = series;
    Sessao s = {1u, 20240101u, 0u, 1u, ex};

    sessao_file_t *sf = sessao_file_open(TMP_PATH);
    TEST_ASSERT_NOT_NULL(sf);

    for (int i = 0; i < 3; i++) {
        s.data = (uint32_t)(20240101 + i);
        TEST_ASSERT_TRUE(sessao_file_insert(sf, &s, NULL));
    }

    int calls = 0;
    /* scan deve retornar true mesmo com early-stop */
    TEST_ASSERT_TRUE(sessao_file_scan(sf, scan_stop_after_one_cb, &calls));
    TEST_ASSERT_EQUAL_INT(1, calls);

    TEST_ASSERT_TRUE(sessao_file_close(sf));
}

/* ===================================================================== *
 *  Teste 11 — scan em arquivo vazio (só cabeçalho) completa sem erro   *
 * ===================================================================== */

void test_scan_empty_file(void) {
    sessao_file_t *sf = sessao_file_open(TMP_PATH);
    TEST_ASSERT_NOT_NULL(sf);

    scan_count_ctx_t ctx = {0, {0}};
    TEST_ASSERT_TRUE(sessao_file_scan(sf, scan_count_cb, &ctx));
    TEST_ASSERT_EQUAL_INT(0, ctx.count);

    TEST_ASSERT_TRUE(sessao_file_close(sf));
}

/* ===================================================================== *
 *  Teste 12 — bytes em disco batem com o hexdump do §6                 *
 * ===================================================================== */

void test_disk_bytes_match_spec(void) {
    /* Sessão canônica do §6:
     *   id_usuario=1, data=20240101, id_academia=0, num_exercicios=1
     *   exercicio: id=1, obs="", num_series=1, serie: carga=100000, reps=10 */
    Serie serie_spec[1] = {{100000u, 10u}};
    Exercicio ex_spec[1];
    ex_spec[0].id_exercicio = 1u;
    ex_spec[0].observacao = "";
    ex_spec[0].num_series = 1u;
    ex_spec[0].series = serie_spec;

    Sessao s_spec = {1u, 20240101u, 0u, 1u, ex_spec};

    sessao_file_t *sf = sessao_file_open(TMP_PATH);
    TEST_ASSERT_NOT_NULL(sf);
    TEST_ASSERT_TRUE(sessao_file_insert(sf, &s_spec, NULL));
    TEST_ASSERT_TRUE(sessao_file_close(sf));

    /* abre o arquivo raw para verificação de bytes */
    FILE *raw = io_open(TMP_PATH, "rb");
    TEST_ASSERT_NOT_NULL(raw);

    /* tamanho total deve ser 65 bytes (32 header + 1 status + 4 tam + 28 payload) */
    byte_offset_t file_size;
    TEST_ASSERT_TRUE(io_file_size(raw, &file_size));
    TEST_ASSERT_EQUAL_UINT64(65u, file_size);

    /* magic em 0x00 */
    uint8_t magic[4];
    TEST_ASSERT_TRUE(io_seek(raw, 0));
    TEST_ASSERT_TRUE(io_read_exact(raw, magic, 4));
    TEST_ASSERT_EQUAL_UINT8('G', magic[0]);
    TEST_ASSERT_EQUAL_UINT8('Y', magic[1]);
    TEST_ASSERT_EQUAL_UINT8('M', magic[2]);
    TEST_ASSERT_EQUAL_UINT8('S', magic[3]);

    /* num_registros em 0x08 = 1 */
    uint32_t num_reg;
    TEST_ASSERT_TRUE(io_seek(raw, 8));
    TEST_ASSERT_TRUE(io_read_u32(raw, &num_reg));
    TEST_ASSERT_EQUAL_UINT32(1u, num_reg);

    /* status em 0x20 = 0x00 (ativo) */
    uint8_t status;
    TEST_ASSERT_TRUE(io_seek(raw, 32));
    TEST_ASSERT_TRUE(io_read_u8(raw, &status));
    TEST_ASSERT_EQUAL_UINT8(0x00u, status);

    /* tam_dados em 0x21 = 28 (0x1C) */
    uint32_t tam_dados;
    TEST_ASSERT_TRUE(io_read_u32(raw, &tam_dados));
    TEST_ASSERT_EQUAL_UINT32(28u, tam_dados);

    /* id_usuario em 0x25 = 1 */
    uint32_t id_usuario;
    TEST_ASSERT_TRUE(io_read_u32(raw, &id_usuario));
    TEST_ASSERT_EQUAL_UINT32(1u, id_usuario);

    /* data em 0x29 = 20240101 (bytes: E5 D6 34 01) */
    uint32_t data;
    TEST_ASSERT_TRUE(io_read_u32(raw, &data));
    TEST_ASSERT_EQUAL_UINT32(20240101u, data);
    /* verifica os bytes individuais no disco */
    uint8_t data_bytes[4];
    TEST_ASSERT_TRUE(io_seek(raw, 0x29));
    TEST_ASSERT_TRUE(io_read_exact(raw, data_bytes, 4));
    TEST_ASSERT_EQUAL_UINT8(0xE5u, data_bytes[0]);
    TEST_ASSERT_EQUAL_UINT8(0xD6u, data_bytes[1]);
    TEST_ASSERT_EQUAL_UINT8(0x34u, data_bytes[2]);
    TEST_ASSERT_EQUAL_UINT8(0x01u, data_bytes[3]);

    /* carga_g em 0x3B = 100000 (bytes: A0 86 01 00) */
    uint8_t carga_bytes[4];
    TEST_ASSERT_TRUE(io_seek(raw, 0x3B));
    TEST_ASSERT_TRUE(io_read_exact(raw, carga_bytes, 4));
    TEST_ASSERT_EQUAL_UINT8(0xA0u, carga_bytes[0]);
    TEST_ASSERT_EQUAL_UINT8(0x86u, carga_bytes[1]);
    TEST_ASSERT_EQUAL_UINT8(0x01u, carga_bytes[2]);
    TEST_ASSERT_EQUAL_UINT8(0x00u, carga_bytes[3]);

    /* repeticoes em 0x3F = 10 (bytes: 0A 00) */
    uint8_t reps_bytes[2];
    TEST_ASSERT_TRUE(io_seek(raw, 0x3F));
    TEST_ASSERT_TRUE(io_read_exact(raw, reps_bytes, 2));
    TEST_ASSERT_EQUAL_UINT8(0x0Au, reps_bytes[0]);
    TEST_ASSERT_EQUAL_UINT8(0x00u, reps_bytes[1]);

    TEST_ASSERT_TRUE(io_close(raw));
}

/* ===================================================================== *
 *  Teste 13 — sessao_free(NULL) é seguro                               *
 * ===================================================================== */

void test_sessao_free_null_is_safe(void) {
    sessao_free(NULL); /* não deve crashar */
}

/* ===================================================================== *
 *  Teste 14 — caminhos de EINVAL: argumentos NULL                      *
 * ===================================================================== */

void test_einval_paths(void) {
    Serie series[1] = {{60000u, 10u}};
    Exercicio ex[1];
    ex[0].id_exercicio = 1u;
    ex[0].observacao = "";
    ex[0].num_series = 1u;
    ex[0].series = series;
    Sessao s = {1u, 20240101u, 0u, 1u, ex};

    sessao_file_t *sf = sessao_file_open(TMP_PATH);
    TEST_ASSERT_NOT_NULL(sf);
    byte_offset_t off;
    TEST_ASSERT_TRUE(sessao_file_insert(sf, &s, &off));

    /* sessao_file_open(NULL) */
    errno = 0;
    TEST_ASSERT_NULL(sessao_file_open(NULL));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);

    /* sessao_file_insert: sf == NULL */
    errno = 0;
    TEST_ASSERT_FALSE(sessao_file_insert(NULL, &s, NULL));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);

    /* sessao_file_insert: s == NULL */
    errno = 0;
    TEST_ASSERT_FALSE(sessao_file_insert(sf, NULL, NULL));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);

    /* sessao_file_read_at: sf == NULL */
    Sessao s_out;
    errno = 0;
    TEST_ASSERT_FALSE(sessao_file_read_at(NULL, off, &s_out));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);

    /* sessao_file_read_at: out == NULL */
    errno = 0;
    TEST_ASSERT_FALSE(sessao_file_read_at(sf, off, NULL));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);

    TEST_ASSERT_TRUE(sessao_file_close(sf));
}

/* ===================================================================== *
 *  Runner                                                               *
 * ===================================================================== */

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_open_creates_valid_header);
    RUN_TEST(test_header_persists_after_reopen);
    RUN_TEST(test_open_rejects_wrong_magic);
    RUN_TEST(test_insert_read_at_roundtrip);
    RUN_TEST(test_insert_read_at_multiple_exercises);
    RUN_TEST(test_multiple_inserts_increasing_offsets);
    RUN_TEST(test_mark_deleted_read_at_returns_enoent);
    RUN_TEST(test_mark_deleted_twice_counters_unchanged);
    RUN_TEST(test_scan_skips_deleted);
    RUN_TEST(test_scan_early_stop);
    RUN_TEST(test_scan_empty_file);
    RUN_TEST(test_disk_bytes_match_spec);
    RUN_TEST(test_sessao_free_null_is_safe);
    RUN_TEST(test_einval_paths);

    return UNITY_END();
}
