/**
 * @file io_utils.h
 * @brief Camada base de leitura e escrita binária do GymSocial.
 *
 * Tudo que vai para o disco passa por aqui. O módulo inteiro cabe em três
 * ideias:
 *
 *   1. ORDEM DE BYTES FIXA (little-endian). Um inteiro é gravado byte a byte,
 *      do menos para o mais significativo, na mão. Assim o arquivo fica igual
 *      em qualquer máquina, sem depender da arquitetura do processador.
 *
 *   2. STRINGS COM PREFIXO DE TAMANHO. Grava-se um uint16_t com o comprimento
 *      e, em seguida, os bytes da string (sem o '\0' em disco). Comprimento
 *      máximo: 65535 bytes.
 *
 *   3. WRAPPERS SEGUROS SOBRE stdio. Em vez de chamar fread/fwrite cru, usa-se
 *      io_read_exact/io_write_exact, que tratam leitura curta e fim de arquivo
 *      de forma consistente.
 *
 * CONVENÇÃO DE ERRO: funções que podem falhar retornam `bool` (true = sucesso).
 * Em falha, `errno` é setado — EINVAL (argumento inválido), EIO (leitura ou
 * escrita curta, ex.: arquivo truncado) ou o valor vindo da própria stdio.
 * Quando o retorno é false, NÃO confie no conteúdo do buffer de saída.
 *
 * ESCOPO ENXUTO: aqui só existem os tipos que os formatos do projeto usam (ver
 * file-formats.md): inteiros sem sinal de 8/16/32/64 bits, double, strings e
 * páginas. Inteiros com sinal e float de 32 bits ficaram de fora por não terem
 * uso hoje; quando um formato precisar, acrescenta-se o par read/write na hora.
 */

#ifndef GYMSOCIAL_IO_UTILS_H
#define GYMSOCIAL_IO_UTILS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/** Tamanho fixo de página, alinhado ao bloco típico de disco. */
#define IO_PAGE_SIZE 4096u

/** Comprimento máximo de string (limite do prefixo uint16_t). */
#define IO_MAX_STRING_LEN 0xFFFFu

/** Offset dentro de um arquivo. uint64_t por legibilidade e folga. */
typedef uint64_t byte_offset_t;

/* ===================================================================== *
 *  Inteiros sem sinal (little-endian)                                   *
 * ===================================================================== */

bool io_write_u8(FILE *f, uint8_t v);
bool io_write_u16(FILE *f, uint16_t v);
bool io_write_u32(FILE *f, uint32_t v);
bool io_write_u64(FILE *f, uint64_t v);

bool io_read_u8(FILE *f, uint8_t *out);
bool io_read_u16(FILE *f, uint16_t *out);
bool io_read_u32(FILE *f, uint32_t *out);
bool io_read_u64(FILE *f, uint64_t *out);

/* ===================================================================== *
 *  Double (IEEE-754)                                                    *
 *  Os 64 bits do double são reinterpretados como um uint64_t e gravados *
 *  pela função de u64 — então a ordem de bytes vive num lugar só.       *
 *  Usado pelas coordenadas (lat/lon) da quadtree de academias.          *
 * ===================================================================== */

bool io_write_f64(FILE *f, double v);
bool io_read_f64(FILE *f, double *out);

/* ===================================================================== *
 *  Strings com prefixo de tamanho (uint16_t)                            *
 * ===================================================================== */

/**
 * Grava `len` bytes de `s` precedidos por um prefixo uint16_t com `len`.
 * `s` pode ser NULL apenas se `len` for 0.
 * Falha com EINVAL se len > IO_MAX_STRING_LEN, ou se s == NULL com len > 0.
 */
bool io_write_string(FILE *f, const char *s, size_t len);

/**
 * Lê uma string com prefixo de tamanho. Aloca um buffer de (len + 1) bytes,
 * copia os `len` bytes e acrescenta um '\0' por conveniência (o '\0' NÃO está
 * no arquivo). O chamador é dono do buffer e deve liberá-lo com free().
 *
 * `*out_len` recebe o comprimento real (sem o '\0'); pode ser NULL.
 * A alocação é limitada a no máximo 65536 bytes (o prefixo é uint16_t). Se o
 * prefixo prometer mais bytes do que o arquivo tem, a leitura falha com EIO.
 * Em qualquer falha, *out_str fica NULL e nada precisa ser liberado.
 */
bool io_read_string(FILE *f, char **out_str, uint16_t *out_len);

/* ===================================================================== *
 *  Páginas de tamanho fixo (para as estruturas de índice: B+, quadtree) *
 * ===================================================================== */

/**
 * Lê a página `page_number` (offset = page_number * IO_PAGE_SIZE) em `buffer`,
 * que deve ter pelo menos IO_PAGE_SIZE bytes. Falha com EIO se a página estiver
 * além do fim do arquivo.
 */
bool io_read_page(FILE *f, uint64_t page_number, void *buffer);

/**
 * Grava `buffer` como a página `page_number`, sempre IO_PAGE_SIZE bytes
 * inteiros. O chamador deve preencher o espaço não usado da página antes de
 * chamar (o módulo não conhece o layout interno da página).
 */
bool io_write_page(FILE *f, uint64_t page_number, const void *buffer);

/* ===================================================================== *
 *  Wrappers seguros sobre stdio                                         *
 * ===================================================================== */

/** fopen com checagem; retorna NULL e preserva errno em falha. */
FILE *io_open(const char *path, const char *mode);

/** fclose com checagem; retorna true em sucesso. NULL é no-op (true). */
bool io_close(FILE *f);

/** Posiciona em `offset` a partir do início (SEEK_SET). */
bool io_seek(FILE *f, byte_offset_t offset);

/** Escreve a posição atual em *out_offset. */
bool io_tell(FILE *f, byte_offset_t *out_offset);

/** Escreve o tamanho atual do arquivo em *out_size (e preserva a posição). */
bool io_file_size(FILE *f, byte_offset_t *out_size);

/**
 * Lê exatamente `n` bytes. Distingue três casos:
 *   - leu n bytes         -> true
 *   - leu 0 e está em EOF  -> false, errno = 0  (fim limpo; o chamador decide)
 *   - leu menos que n      -> false, errno = EIO (truncamento/corrupção)
 */
bool io_read_exact(FILE *f, void *buffer, size_t n);

/** Escreve exatamente `n` bytes; false + EIO em escrita curta. */
bool io_write_exact(FILE *f, const void *buffer, size_t n);

#endif /* GYMSOCIAL_IO_UTILS_H */
