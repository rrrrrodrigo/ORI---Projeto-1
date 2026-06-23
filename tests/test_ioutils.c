/**
 * @file test_io_utils.c
 * @brief Testes unitários do módulo io_utils (framework Unity).
 *
 * Cada teste grava em um arquivo temporário, rebobina e lê de volta,
 * conferindo round-trip exato. Cobrimos valores extremos, ordem little-endian
 * no disco, strings (vazia, com nulos no meio, longa), páginas, double
 * (incluindo NaN/Inf), persistência ao reabrir, EOF limpo e argumentos
 * inválidos.
 */

#include "ioutils.h"
#include "unity/unity.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *TMP_PATH = "build/test_io_tmp.bin";
static FILE *g_file = NULL;

static void rewind_tmp(void) {
    TEST_ASSERT_TRUE(io_seek(g_file, 0));
}

void setUp(void) {
    g_file = io_open(TMP_PATH, "w+b");
    TEST_ASSERT_NOT_NULL_MESSAGE(g_file, "falha ao abrir arquivo temporario");
}

void tearDown(void) {
    if (g_file != NULL) {
        io_close(g_file);
        g_file = NULL;
    }
    remove(TMP_PATH);
}

/* ===================================================================== *
 *  Inteiros sem sinal — round-trip e extremos                           *
 * ===================================================================== */

void test_u8_roundtrip_extremes(void) {
    const uint8_t vals[] = {0u, 1u, 127u, 128u, 255u};
    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        TEST_ASSERT_TRUE(io_write_u8(g_file, vals[i]));
    }
    rewind_tmp();
    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        uint8_t got;
        TEST_ASSERT_TRUE(io_read_u8(g_file, &got));
        TEST_ASSERT_EQUAL_UINT8(vals[i], got);
    }
}

void test_u16_roundtrip_extremes(void) {
    const uint16_t vals[] = {0u, 1u, 0x00FFu, 0x0100u, 0x7FFFu, 0xFFFFu};
    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        TEST_ASSERT_TRUE(io_write_u16(g_file, vals[i]));
    }
    rewind_tmp();
    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        uint16_t got;
        TEST_ASSERT_TRUE(io_read_u16(g_file, &got));
        TEST_ASSERT_EQUAL_UINT16(vals[i], got);
    }
}

void test_u32_roundtrip_extremes(void) {
    const uint32_t vals[] = {0u, 1u, 0xFFu, 0x12345678u, 0x7FFFFFFFu, 0xFFFFFFFFu};
    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        TEST_ASSERT_TRUE(io_write_u32(g_file, vals[i]));
    }
    rewind_tmp();
    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        uint32_t got;
        TEST_ASSERT_TRUE(io_read_u32(g_file, &got));
        TEST_ASSERT_EQUAL_UINT32(vals[i], got);
    }
}

void test_u64_roundtrip_extremes(void) {
    const uint64_t vals[] = {0u, 1u, 0xFFFFFFFFu, 0x0123456789ABCDEFu, 0x7FFFFFFFFFFFFFFFu, 0xFFFFFFFFFFFFFFFFu};
    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        TEST_ASSERT_TRUE(io_write_u64(g_file, vals[i]));
    }
    rewind_tmp();
    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        uint64_t got;
        TEST_ASSERT_TRUE(io_read_u64(g_file, &got));
        TEST_ASSERT_EQUAL_UINT64(vals[i], got);
    }
}

/* Confirma explicitamente a ordem little-endian no disco. */
void test_u32_is_little_endian_on_disk(void) {
    TEST_ASSERT_TRUE(io_write_u32(g_file, 0x12345678u));
    rewind_tmp();
    uint8_t raw[4];
    TEST_ASSERT_TRUE(io_read_exact(g_file, raw, sizeof raw));
    TEST_ASSERT_EQUAL_UINT8(0x78u, raw[0]);
    TEST_ASSERT_EQUAL_UINT8(0x56u, raw[1]);
    TEST_ASSERT_EQUAL_UINT8(0x34u, raw[2]);
    TEST_ASSERT_EQUAL_UINT8(0x12u, raw[3]);
}

/* ===================================================================== *
 *  Double — round-trip exato, inclusive valores especiais               *
 * ===================================================================== */

void test_f64_roundtrip(void) {
    const double vals[] = {0.0, -0.0, 1.0, -2.5, 2.718281828459045, 1e300, -1e-300};
    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        TEST_ASSERT_TRUE(io_write_f64(g_file, vals[i]));
    }
    rewind_tmp();
    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        double got;
        TEST_ASSERT_TRUE(io_read_f64(g_file, &got));
        TEST_ASSERT_EQUAL_MEMORY(&vals[i], &got, sizeof(double));
    }
}

void test_f64_special_values(void) {
    const double vals[] = {INFINITY, -INFINITY, NAN};
    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        TEST_ASSERT_TRUE(io_write_f64(g_file, vals[i]));
    }
    rewind_tmp();
    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        double got;
        TEST_ASSERT_TRUE(io_read_f64(g_file, &got));
        /* NaN != NaN, então comparamos os bits, não os valores. */
        TEST_ASSERT_EQUAL_MEMORY(&vals[i], &got, sizeof(double));
    }
}

/* ===================================================================== *
 *  Strings                                                              *
 * ===================================================================== */

void test_string_roundtrip_basic(void) {
    const char *msg = "agachamento livre";
    size_t len = strlen(msg);
    TEST_ASSERT_TRUE(io_write_string(g_file, msg, len));
    rewind_tmp();

    char *out = NULL;
    uint16_t out_len = 0;
    TEST_ASSERT_TRUE(io_read_string(g_file, &out, &out_len));
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL_UINT16((uint16_t)len, out_len);
    TEST_ASSERT_EQUAL_STRING(msg, out);
    free(out);
}

void test_string_empty(void) {
    TEST_ASSERT_TRUE(io_write_string(g_file, "", 0));
    rewind_tmp();

    char *out = NULL;
    uint16_t out_len = 99;
    TEST_ASSERT_TRUE(io_read_string(g_file, &out, &out_len));
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL_UINT16(0, out_len);
    TEST_ASSERT_EQUAL_STRING("", out);
    free(out);
}

/* String com bytes nulos no meio: vale o comprimento, não strlen. */
void test_string_with_embedded_nul(void) {
    const char data[] = {'a', '\0', 'b', '\0', 'c'};
    TEST_ASSERT_TRUE(io_write_string(g_file, data, sizeof data));
    rewind_tmp();

    char *out = NULL;
    uint16_t out_len = 0;
    TEST_ASSERT_TRUE(io_read_string(g_file, &out, &out_len));
    TEST_ASSERT_EQUAL_UINT16((uint16_t)sizeof data, out_len);
    TEST_ASSERT_EQUAL_MEMORY(data, out, sizeof data);
    TEST_ASSERT_EQUAL_CHAR('\0', out[sizeof data]); /* terminador extra */
    free(out);
}

void test_string_long(void) {
    const size_t len = 50000; /* dentro do limite uint16_t */
    char *big = malloc(len);
    TEST_ASSERT_NOT_NULL(big);
    for (size_t i = 0; i < len; i++) {
        // cppcheck-suppress nullPointerRedundantCheck
        big[i] = (char)('A' + (i % 26));
    }
    TEST_ASSERT_TRUE(io_write_string(g_file, big, len));
    rewind_tmp();

    char *out = NULL;
    uint16_t out_len = 0;
    TEST_ASSERT_TRUE(io_read_string(g_file, &out, &out_len));
    TEST_ASSERT_EQUAL_UINT16((uint16_t)len, out_len);
    TEST_ASSERT_EQUAL_MEMORY(big, out, len);
    free(out);
    free(big);
}

void test_string_too_long_is_rejected(void) {
    errno = 0;
    bool ok = io_write_string(g_file, "x", (size_t)IO_MAX_STRING_LEN + 1);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);
}

/* Prefixo promete mais bytes do que existem: a leitura curta vira EIO. */
void test_string_truncated_prefix_is_rejected(void) {
    TEST_ASSERT_TRUE(io_write_u16(g_file, 1000u));
    TEST_ASSERT_TRUE(io_write_exact(g_file, "abc", 3));
    rewind_tmp();

    char *out = (char *)0x1; /* lixo proposital: deve virar NULL */
    errno = 0;
    bool ok = io_read_string(g_file, &out, NULL);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_INT(EIO, errno);
    TEST_ASSERT_NULL(out);
}

/* ===================================================================== *
 *  Páginas                                                              *
 * ===================================================================== */

void test_page_roundtrip(void) {
    uint8_t page_out[IO_PAGE_SIZE];
    for (size_t i = 0; i < IO_PAGE_SIZE; i++) {
        page_out[i] = (uint8_t)(i & 0xFFu);
    }
    TEST_ASSERT_TRUE(io_write_page(g_file, 0, page_out));

    uint8_t page_in[IO_PAGE_SIZE];
    TEST_ASSERT_TRUE(io_read_page(g_file, 0, page_in));
    TEST_ASSERT_EQUAL_MEMORY(page_out, page_in, IO_PAGE_SIZE);
}

/* Escrever a página 3 num arquivo vazio faz o arquivo crescer; o offset é
 * page_number * IO_PAGE_SIZE. */
void test_page_non_contiguous(void) {
    uint8_t buf[IO_PAGE_SIZE];
    memset(buf, 0xAB, sizeof buf);
    TEST_ASSERT_TRUE(io_write_page(g_file, 3, buf));

    byte_offset_t size;
    TEST_ASSERT_TRUE(io_file_size(g_file, &size));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)IO_PAGE_SIZE * 4, size);

    uint8_t in[IO_PAGE_SIZE];
    TEST_ASSERT_TRUE(io_read_page(g_file, 3, in));
    TEST_ASSERT_EQUAL_MEMORY(buf, in, IO_PAGE_SIZE);
}

/* Ler página além do fim do arquivo deve falhar (não retornar lixo). */
void test_page_read_beyond_eof_fails(void) {
    uint8_t buf[IO_PAGE_SIZE];
    memset(buf, 0x11, sizeof buf);
    TEST_ASSERT_TRUE(io_write_page(g_file, 0, buf));

    uint8_t in[IO_PAGE_SIZE];
    TEST_ASSERT_FALSE(io_read_page(g_file, 5, in));
}

/* ===================================================================== *
 *  Persistência, registro misto, EOF limpo e EINVAL                     *
 * ===================================================================== */

void test_persistence_close_reopen(void) {
    TEST_ASSERT_TRUE(io_write_u32(g_file, 0xDEADBEEFu));
    TEST_ASSERT_TRUE(io_write_string(g_file, "supino", 6));
    TEST_ASSERT_TRUE(io_close(g_file));

    g_file = io_open(TMP_PATH, "rb");
    TEST_ASSERT_NOT_NULL(g_file);

    uint32_t magic;
    TEST_ASSERT_TRUE(io_read_u32(g_file, &magic));
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, magic);

    char *s = NULL;
    TEST_ASSERT_TRUE(io_read_string(g_file, &s, NULL));
    TEST_ASSERT_EQUAL_STRING("supino", s);
    free(s);
}

/* Registro misto, simulando uso real: id, nome, coordenada, reps, academia. */
void test_mixed_record_roundtrip(void) {
    TEST_ASSERT_TRUE(io_write_u64(g_file, 832940u));
    TEST_ASSERT_TRUE(io_write_string(g_file, "Henrique", 8));
    TEST_ASSERT_TRUE(io_write_f64(g_file, -47.890833));
    TEST_ASSERT_TRUE(io_write_u16(g_file, 8u));
    TEST_ASSERT_TRUE(io_write_u32(g_file, 7u));
    rewind_tmp();

    uint64_t id;
    char *nome = NULL;
    double coord;
    uint16_t reps;
    uint32_t academia;
    TEST_ASSERT_TRUE(io_read_u64(g_file, &id));
    TEST_ASSERT_TRUE(io_read_string(g_file, &nome, NULL));
    TEST_ASSERT_TRUE(io_read_f64(g_file, &coord));
    TEST_ASSERT_TRUE(io_read_u16(g_file, &reps));
    TEST_ASSERT_TRUE(io_read_u32(g_file, &academia));

    TEST_ASSERT_EQUAL_UINT64(832940u, id);
    TEST_ASSERT_EQUAL_STRING("Henrique", nome);
    TEST_ASSERT_EQUAL_MEMORY(&(double){-47.890833}, &coord, sizeof(double));
    TEST_ASSERT_EQUAL_UINT16(8u, reps);
    TEST_ASSERT_EQUAL_UINT32(7u, academia);
    free(nome);
}

void test_read_at_eof_clean(void) {
    /* arquivo vazio: ler deve falhar com errno limpo (EOF), não EIO */
    rewind_tmp();
    uint32_t v;
    errno = 0;
    TEST_ASSERT_FALSE(io_read_u32(g_file, &v));
    TEST_ASSERT_EQUAL_INT(0, errno);
}

void test_einval_paths(void) {
    errno = 0;
    TEST_ASSERT_NULL(io_open(NULL, "rb"));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);

    errno = 0;
    TEST_ASSERT_FALSE(io_read_u32(g_file, NULL));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);

    errno = 0;
    TEST_ASSERT_FALSE(io_read_string(g_file, NULL, NULL));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);

    errno = 0;
    TEST_ASSERT_FALSE(io_write_string(g_file, NULL, 5));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);

    errno = 0;
    TEST_ASSERT_FALSE(io_seek(NULL, 0));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);
}

/* ===================================================================== *
 *  Runner                                                               *
 * ===================================================================== */

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_u8_roundtrip_extremes);
    RUN_TEST(test_u16_roundtrip_extremes);
    RUN_TEST(test_u32_roundtrip_extremes);
    RUN_TEST(test_u64_roundtrip_extremes);
    RUN_TEST(test_u32_is_little_endian_on_disk);

    RUN_TEST(test_f64_roundtrip);
    RUN_TEST(test_f64_special_values);

    RUN_TEST(test_string_roundtrip_basic);
    RUN_TEST(test_string_empty);
    RUN_TEST(test_string_with_embedded_nul);
    RUN_TEST(test_string_long);
    RUN_TEST(test_string_too_long_is_rejected);
    RUN_TEST(test_string_truncated_prefix_is_rejected);

    RUN_TEST(test_page_roundtrip);
    RUN_TEST(test_page_non_contiguous);
    RUN_TEST(test_page_read_beyond_eof_fails);

    RUN_TEST(test_persistence_close_reopen);
    RUN_TEST(test_mixed_record_roundtrip);
    RUN_TEST(test_read_at_eof_clean);
    RUN_TEST(test_einval_paths);

    return UNITY_END();
}
