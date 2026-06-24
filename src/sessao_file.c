/**
 * @file sessao_file.c
 * @brief Implementação do handler de arquivo de sessões (ver sessao_file.h).
 *
 * Formato binário: docs/file-formats.md §6.
 * Cabeçalho de 32 bytes (magic "GYMS") + registros com envelope de 5 bytes
 * (status u8 + tam_dados u32) + payload de comprimento variável.
 *
 * A inserção usa a técnica de backpatch: grava um placeholder 0 para
 * tam_dados, escreve o payload e retrocede para preencher o valor real.
 * Nunca usa buffer temporário nem arquivo auxiliar.
 */

#include "sessao_file.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* ===================================================================== *
 *  Constantes internas                                                  *
 * ===================================================================== */

#define HEADER_SIZE 32u
#define HEADER_MAGIC "GYMS"
#define HEADER_VERSAO 1u
#define STATUS_ACTIVE ((uint8_t)0x00)
#define STATUS_DELETED ((uint8_t)0x01)

/* Offsets dentro do cabeçalho (todos em bytes a partir do início do arquivo). */
#define HDR_OFF_NUM_REGISTROS 8u
#define HDR_OFF_NUM_DELETADOS 12u

/* ===================================================================== *
 *  Estrutura opaca                                                      *
 * ===================================================================== */

struct sessao_file {
    FILE *fp;
    uint32_t num_registros; /* registros ativos */
    uint32_t num_deletados; /* registros logicamente deletados */
};

/* ===================================================================== *
 *  Helpers de cabeçalho                                                 *
 * ===================================================================== */

static bool write_header(sessao_file_t *sf) {
    if (!io_seek(sf->fp, 0)) {
        return false;
    }
    if (!io_write_exact(sf->fp, HEADER_MAGIC, 4)) {
        return false;
    }
    if (!io_write_u16(sf->fp, HEADER_VERSAO)) {
        return false;
    }
    if (!io_write_u16(sf->fp, 0)) { /* reservado */
        return false;
    }
    if (!io_write_u32(sf->fp, sf->num_registros)) {
        return false;
    }
    if (!io_write_u32(sf->fp, sf->num_deletados)) {
        return false;
    }
    if (!io_write_u32(sf->fp, 0)) { /* proximo_id — sempre 0 para sessões */
        return false;
    }
    uint8_t pad[12];
    memset(pad, 0, sizeof pad);
    if (!io_write_exact(sf->fp, pad, sizeof pad)) {
        return false;
    }
    return fflush(sf->fp) == 0;
}

static bool read_header(sessao_file_t *sf) {
    if (!io_seek(sf->fp, 0)) {
        return false;
    }
    uint8_t magic[4];
    if (!io_read_exact(sf->fp, magic, 4)) {
        return false;
    }
    if (magic[0] != 'G' || magic[1] != 'Y' || magic[2] != 'M' || magic[3] != 'S') {
        errno = EPROTO;
        return false;
    }
    /* pula versao (u16) + reservado (u16) = 4 bytes — offset 4→8 */
    if (!io_seek(sf->fp, 8)) {
        return false;
    }
    if (!io_read_u32(sf->fp, &sf->num_registros)) {
        return false;
    }
    if (!io_read_u32(sf->fp, &sf->num_deletados)) {
        return false;
    }
    /* posiciona logo após o cabeçalho para operações subsequentes */
    return io_seek(sf->fp, HEADER_SIZE);
}

/* ===================================================================== *
 *  sessao_free                                                          *
 * ===================================================================== */

void sessao_free(Sessao *s) {
    if (s == NULL) {
        return;
    }
    if (s->exercicios != NULL) {
        uint32_t n = s->num_exercicios;
        for (uint32_t i = 0; i < n; i++) {
            free(s->exercicios[i].observacao);
            free(s->exercicios[i].series);
        }
        free(s->exercicios);
        s->exercicios = NULL;
    }
}

/* ===================================================================== *
 *  Helpers de payload                                                   *
 * ===================================================================== */

/* Lê o payload de uma sessão de fp para *out.
 * Em falha, chama sessao_free(out) internamente e retorna false. */
static bool read_payload(FILE *fp, Sessao *out) {
    out->exercicios = NULL;
    out->num_exercicios = 0;

    if (!io_read_u32(fp, &out->id_usuario) || !io_read_u32(fp, &out->data) || !io_read_u32(fp, &out->id_academia) || !io_read_u16(fp, &out->num_exercicios)) {
        return false;
    }

    if (out->num_exercicios == 0) {
        return true;
    }

    out->exercicios = calloc((size_t)out->num_exercicios, sizeof(Exercicio));
    if (out->exercicios == NULL) {
        errno = ENOMEM;
        return false;
    }

    uint32_t num_ex = out->num_exercicios;
    for (uint32_t i = 0; i < num_ex; i++) {
        Exercicio *ex = &out->exercicios[i];

        if (!io_read_u32(fp, &ex->id_exercicio) || !io_read_string(fp, &ex->observacao, NULL) || !io_read_u16(fp, &ex->num_series)) {
            goto fail;
        }

        if (ex->num_series == 0) {
            continue;
        }

        ex->series = malloc((size_t)ex->num_series * sizeof(Serie));
        if (ex->series == NULL) {
            errno = ENOMEM;
            goto fail;
        }

        uint32_t num_ser = ex->num_series;
        for (uint32_t j = 0; j < num_ser; j++) {
            if (!io_read_u32(fp, &ex->series[j].carga_g) || !io_read_u16(fp, &ex->series[j].repeticoes)) {
                goto fail;
            }
        }
    }
    return true;

fail: {
    int saved = errno;
    sessao_free(out);
    errno = saved;
    return false;
}
}

/* Grava o payload de *s em fp.
 * Não aloca memória; só lê os campos de s. */
static bool write_payload(FILE *fp, const Sessao *s) {
    if (!io_write_u32(fp, s->id_usuario) || !io_write_u32(fp, s->data) || !io_write_u32(fp, s->id_academia) || !io_write_u16(fp, s->num_exercicios)) {
        return false;
    }

    uint32_t num_ex = s->num_exercicios;
    for (uint32_t i = 0; i < num_ex; i++) {
        const Exercicio *ex = &s->exercicios[i];
        size_t obs_len = (ex->observacao != NULL) ? strlen(ex->observacao) : 0;

        if (!io_write_u32(fp, ex->id_exercicio) || !io_write_string(fp, ex->observacao, obs_len) || !io_write_u16(fp, ex->num_series)) {
            return false;
        }

        uint32_t num_ser = ex->num_series;
        for (uint32_t j = 0; j < num_ser; j++) {
            if (!io_write_u32(fp, ex->series[j].carga_g) || !io_write_u16(fp, ex->series[j].repeticoes)) {
                return false;
            }
        }
    }
    return true;
}

/* ===================================================================== *
 *  API pública                                                          *
 * ===================================================================== */

sessao_file_t *sessao_file_open(const char *path) {
    if (path == NULL) {
        errno = EINVAL;
        return NULL;
    }

    sessao_file_t *sf = malloc(sizeof *sf);
    if (sf == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    sf->num_registros = 0;
    sf->num_deletados = 0;

    /* tenta abrir existente; se não existir, cria */
    FILE *fp = io_open(path, "r+b");
    bool is_new = false;
    if (fp == NULL) {
        if (errno != ENOENT) {
            free(sf);
            return NULL;
        }
        fp = io_open(path, "w+b");
        if (fp == NULL) {
            free(sf);
            return NULL;
        }
        is_new = true;
    }
    sf->fp = fp;

    if (is_new) {
        if (!write_header(sf)) {
            int saved = errno;
            fclose(fp);
            free(sf);
            errno = saved;
            return NULL;
        }
    } else {
        if (!read_header(sf)) {
            int saved = errno;
            fclose(fp);
            free(sf);
            errno = saved;
            return NULL;
        }
    }

    return sf;
}

bool sessao_file_close(sessao_file_t *sf) {
    if (sf == NULL) {
        return true;
    }
    bool ok = write_header(sf);
    if (!io_close(sf->fp)) {
        ok = false;
    }
    free(sf);
    return ok;
}

bool sessao_file_insert(sessao_file_t *sf, const Sessao *s, byte_offset_t *out_offset) {
    if (sf == NULL || s == NULL) {
        errno = EINVAL;
        return false;
    }

    /* posiciona no final do arquivo */
    byte_offset_t file_size;
    if (!io_file_size(sf->fp, &file_size)) {
        return false;
    }
    if (!io_seek(sf->fp, file_size)) {
        return false;
    }

    byte_offset_t record_offset = file_size;

    /* escreve status ativo */
    if (!io_write_u8(sf->fp, STATUS_ACTIVE)) {
        return false;
    }

    /* backpatch: placeholder 0 para tam_dados */
    byte_offset_t tam_dados_offset = record_offset + 1u;
    if (!io_write_u32(sf->fp, 0)) {
        return false;
    }

    /* início do payload */
    byte_offset_t payload_start;
    if (!io_tell(sf->fp, &payload_start)) {
        return false;
    }

    if (!write_payload(sf->fp, s)) {
        return false;
    }

    /* fim do payload → calcula tam_dados */
    byte_offset_t payload_end;
    if (!io_tell(sf->fp, &payload_end)) {
        return false;
    }

    byte_offset_t payload_size = payload_end - payload_start;
    if (payload_size > (byte_offset_t)UINT32_MAX) {
        errno = EOVERFLOW;
        return false;
    }
    uint32_t tam_dados = (uint32_t)payload_size;

    /* retrocede e preenche tam_dados real */
    if (!io_seek(sf->fp, tam_dados_offset)) {
        return false;
    }
    if (!io_write_u32(sf->fp, tam_dados)) {
        return false;
    }

    sf->num_registros++;
    if (!write_header(sf)) {
        return false;
    }

    if (out_offset != NULL) {
        *out_offset = record_offset;
    }
    return true;
}

bool sessao_file_read_at(sessao_file_t *sf, byte_offset_t offset, Sessao *out) {
    if (sf == NULL || out == NULL) {
        errno = EINVAL;
        return false;
    }

    if (!io_seek(sf->fp, offset)) {
        return false;
    }

    uint8_t status;
    if (!io_read_u8(sf->fp, &status)) {
        return false;
    }

    if (status != STATUS_ACTIVE) {
        errno = ENOENT;
        return false;
    }

    uint32_t tam_dados;
    if (!io_read_u32(sf->fp, &tam_dados)) {
        return false;
    }

    byte_offset_t payload_start;
    if (!io_tell(sf->fp, &payload_start)) {
        return false;
    }

    if (!read_payload(sf->fp, out)) {
        return false;
    }

    byte_offset_t payload_end;
    if (!io_tell(sf->fp, &payload_end)) {
        sessao_free(out);
        return false;
    }

    if ((payload_end - payload_start) != (byte_offset_t)tam_dados) {
        sessao_free(out);
        errno = EPROTO;
        return false;
    }

    return true;
}

bool sessao_file_mark_deleted(sessao_file_t *sf, byte_offset_t offset) {
    if (sf == NULL) {
        errno = EINVAL;
        return false;
    }

    if (!io_seek(sf->fp, offset)) {
        return false;
    }

    uint8_t status;
    if (!io_read_u8(sf->fp, &status)) {
        return false;
    }

    if (status != STATUS_ACTIVE) {
        errno = ENOENT;
        return false;
    }

    if (!io_seek(sf->fp, offset)) {
        return false;
    }
    if (!io_write_u8(sf->fp, STATUS_DELETED)) {
        return false;
    }

    if (sf->num_registros > 0) {
        sf->num_registros--;
    }
    sf->num_deletados++;

    return write_header(sf);
}

bool sessao_file_scan(sessao_file_t *sf, sessao_scan_cb cb, void *ctx) {
    if (sf == NULL || cb == NULL) {
        errno = EINVAL;
        return false;
    }

    if (!io_seek(sf->fp, HEADER_SIZE)) {
        return false;
    }

    while (true) {
        byte_offset_t record_offset;
        if (!io_tell(sf->fp, &record_offset)) {
            return false;
        }

        uint8_t status;
        if (!io_read_u8(sf->fp, &status)) {
            if (errno == 0) {
                break; /* EOF limpo */
            }
            return false;
        }

        uint32_t tam_dados;
        if (!io_read_u32(sf->fp, &tam_dados)) {
            return false;
        }

        if (status != STATUS_ACTIVE) {
            /* deletado: pula o payload */
            byte_offset_t skip_to = record_offset + 1u + 4u + (byte_offset_t)tam_dados;
            if (!io_seek(sf->fp, skip_to)) {
                return false;
            }
            continue;
        }

        byte_offset_t payload_start;
        if (!io_tell(sf->fp, &payload_start)) {
            return false;
        }

        Sessao s;
        if (!read_payload(sf->fp, &s)) {
            return false;
        }

        byte_offset_t payload_end;
        if (!io_tell(sf->fp, &payload_end)) {
            sessao_free(&s);
            return false;
        }

        if ((payload_end - payload_start) != (byte_offset_t)tam_dados) {
            sessao_free(&s);
            errno = EPROTO;
            return false;
        }

        bool cont = cb(record_offset, &s, ctx);
        sessao_free(&s);

        if (!cont) {
            break;
        }
    }

    return true;
}
