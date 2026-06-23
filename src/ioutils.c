/**
 * @file io_utils.c
 * @brief Implementação da camada base de I/O binário (ver io_utils.h).
 *
 * A ordem little-endian é montada explicitamente, byte a byte, com
 * deslocamentos de bits. Isso não depende da arquitetura da máquina: o mesmo
 * arquivo é lido igual em qualquer host. O double reaproveita as funções de
 * uint64_t, então a lógica de ordem de bytes está num único lugar.
 */

#include "ioutils.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* ===================================================================== *
 *  Wrappers seguros sobre stdio                                         *
 * ===================================================================== */

FILE *io_open(const char *path, const char *mode) {
    if (path == NULL || mode == NULL) {
        errno = EINVAL;
        return NULL;
    }
    return fopen(path, mode); /* fopen já seta errno em falha */
}

bool io_close(FILE *f) {
    if (f == NULL) {
        return true; /* fechar NULL é no-op idempotente */
    }
    return fclose(f) == 0;
}

bool io_seek(FILE *f, byte_offset_t offset) {
    if (f == NULL) {
        errno = EINVAL;
        return false;
    }
    /* fseek trabalha com long. Para os tamanhos deste projeto (muito abaixo de
     * 2 GB) isso sobra. Se algum dia o offset passar de LONG_MAX, falhamos de
     * forma explícita em vez de truncar silenciosamente. */
    if (offset > (byte_offset_t)LONG_MAX) {
        errno = EOVERFLOW;
        return false;
    }
    return fseek(f, (long)offset, SEEK_SET) == 0;
}

bool io_tell(FILE *f, byte_offset_t *out_offset) {
    if (f == NULL || out_offset == NULL) {
        errno = EINVAL;
        return false;
    }
    long pos = ftell(f);
    if (pos < 0) {
        return false;
    }
    *out_offset = (byte_offset_t)pos;
    return true;
}

bool io_file_size(FILE *f, byte_offset_t *out_size) {
    if (f == NULL || out_size == NULL) {
        errno = EINVAL;
        return false;
    }
    long saved = ftell(f);
    if (saved < 0) {
        return false;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        return false;
    }
    long end = ftell(f);
    if (end < 0) {
        return false;
    }
    if (fseek(f, saved, SEEK_SET) != 0) { /* restaura a posição original */
        return false;
    }
    *out_size = (byte_offset_t)end;
    return true;
}

bool io_read_exact(FILE *f, void *buffer, size_t n) {
    if (f == NULL || (buffer == NULL && n > 0)) {
        errno = EINVAL;
        return false;
    }
    if (n == 0) {
        return true;
    }
    size_t got = fread(buffer, 1, n, f);
    if (got == n) {
        return true;
    }
    if (got == 0 && feof(f)) {
        errno = 0; /* fim de arquivo limpo: não é erro de I/O */
        return false;
    }
    if (!ferror(f)) { /* leu menos que n: truncamento no meio de um campo */
        errno = EIO;
    }
    return false;
}

bool io_write_exact(FILE *f, const void *buffer, size_t n) {
    if (f == NULL || (buffer == NULL && n > 0)) {
        errno = EINVAL;
        return false;
    }
    if (n == 0) {
        return true;
    }
    if (fwrite(buffer, 1, n, f) != n) {
        if (!ferror(f)) {
            errno = EIO;
        }
        return false;
    }
    return true;
}

/* ===================================================================== *
 *  Inteiros sem sinal — little-endian byte a byte                       *
 *                                                                       *
 *  Escrita: o byte i recebe o i-ésimo octeto, do menos significativo    *
 *  (i=0) para o mais. Leitura: o processo inverso, recompondo o valor   *
 *  com deslocamentos. É o conceito de little-endian escrito como código.*
 * ===================================================================== */

bool io_write_u8(FILE *f, uint8_t v) {
    return io_write_exact(f, &v, 1);
}

bool io_write_u16(FILE *f, uint16_t v) {
    uint8_t b[2];
    for (unsigned i = 0; i < 2; i++) {
        b[i] = (uint8_t)(v >> (8u * i));
    }
    return io_write_exact(f, b, sizeof b);
}

bool io_write_u32(FILE *f, uint32_t v) {
    uint8_t b[4];
    for (unsigned i = 0; i < 4; i++) {
        b[i] = (uint8_t)(v >> (8u * i));
    }
    return io_write_exact(f, b, sizeof b);
}

bool io_write_u64(FILE *f, uint64_t v) {
    uint8_t b[8];
    for (unsigned i = 0; i < 8; i++) {
        b[i] = (uint8_t)(v >> (8u * i));
    }
    return io_write_exact(f, b, sizeof b);
}

bool io_read_u8(FILE *f, uint8_t *out) {
    if (out == NULL) {
        errno = EINVAL;
        return false;
    }
    return io_read_exact(f, out, 1);
}

bool io_read_u16(FILE *f, uint16_t *out) {
    if (out == NULL) {
        errno = EINVAL;
        return false;
    }
    uint8_t b[2];
    if (!io_read_exact(f, b, sizeof b)) {
        return false;
    }
    uint16_t v = 0;
    for (unsigned i = 0; i < 2; i++) {
        v = (uint16_t)(v | ((uint16_t)b[i] << (8u * i)));
    }
    *out = v;
    return true;
}

bool io_read_u32(FILE *f, uint32_t *out) {
    if (out == NULL) {
        errno = EINVAL;
        return false;
    }
    uint8_t b[4];
    if (!io_read_exact(f, b, sizeof b)) {
        return false;
    }
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; i++) {
        v |= (uint32_t)b[i] << (8u * i);
    }
    *out = v;
    return true;
}

bool io_read_u64(FILE *f, uint64_t *out) {
    if (out == NULL) {
        errno = EINVAL;
        return false;
    }
    uint8_t b[8];
    if (!io_read_exact(f, b, sizeof b)) {
        return false;
    }
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; i++) {
        v |= (uint64_t)b[i] << (8u * i);
    }
    *out = v;
    return true;
}

/* ===================================================================== *
 *  Double — reaproveita u64 para a ordem de bytes                       *
 *                                                                       *
 *  memcpy copia os 64 bits do double para um uint64_t sem conversão     *
 *  numérica (preserva o padrão de bits IEEE-754 exatamente). Daí em      *
 *  diante é só um u64, gravado/lido pelas funções acima.                *
 * ===================================================================== */

bool io_write_f64(FILE *f, double v) {
    uint64_t bits;
    memcpy(&bits, &v, sizeof bits);
    return io_write_u64(f, bits);
}

bool io_read_f64(FILE *f, double *out) {
    if (out == NULL) {
        errno = EINVAL;
        return false;
    }
    uint64_t bits;
    if (!io_read_u64(f, &bits)) {
        return false;
    }
    memcpy(out, &bits, sizeof *out);
    return true;
}

/* ===================================================================== *
 *  Strings com prefixo uint16_t                                         *
 * ===================================================================== */

bool io_write_string(FILE *f, const char *s, size_t len) {
    if (len > IO_MAX_STRING_LEN || (s == NULL && len > 0)) {
        errno = EINVAL;
        return false;
    }
    if (!io_write_u16(f, (uint16_t)len)) {
        return false;
    }
    return io_write_exact(f, s, len);
}

bool io_read_string(FILE *f, char **out_str, uint16_t *out_len) {
    if (out_str == NULL) {
        errno = EINVAL;
        return false;
    }
    *out_str = NULL;

    uint16_t len;
    if (!io_read_u16(f, &len)) {
        return false;
    }

    /* O prefixo é uint16_t, então a alocação é no máximo 65536 bytes — não há
     * como um prefixo corrompido pedir uma alocação gigante. Se o prefixo
     * prometer mais bytes do que o arquivo tem, io_read_exact falha com EIO. */
    char *buf = malloc((size_t)len + 1);
    if (buf == NULL) {
        errno = ENOMEM;
        return false;
    }

    if (len > 0 && !io_read_exact(f, buf, len)) {
        int saved = errno;
        free(buf);
        errno = saved;
        return false;
    }

    buf[len] = '\0';
    *out_str = buf;
    if (out_len != NULL) {
        *out_len = len;
    }
    return true;
}

/* ===================================================================== *
 *  Páginas de tamanho fixo                                              *
 * ===================================================================== */

bool io_read_page(FILE *f, uint64_t page_number, void *buffer) {
    if (f == NULL || buffer == NULL) {
        errno = EINVAL;
        return false;
    }
    if (!io_seek(f, (byte_offset_t)page_number * IO_PAGE_SIZE)) {
        return false;
    }
    return io_read_exact(f, buffer, IO_PAGE_SIZE);
}

bool io_write_page(FILE *f, uint64_t page_number, const void *buffer) {
    if (f == NULL || buffer == NULL) {
        errno = EINVAL;
        return false;
    }
    if (!io_seek(f, (byte_offset_t)page_number * IO_PAGE_SIZE)) {
        return false;
    }
    return io_write_exact(f, buffer, IO_PAGE_SIZE);
}
