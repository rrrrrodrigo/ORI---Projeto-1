/**
 * @file sessao_file.h
 * @brief Modelo em memória e handler de arquivo para sessões de treino.
 *
 * Estrutura de dados complexa: número variável de exercícios por sessão,
 * string de observação de comprimento variável por exercício, número variável
 * de séries por exercício.
 *
 * REGRA DE PROPRIEDADE (assimetria leitura/escrita):
 *   - Na LEITURA (read_at, scan): este módulo aloca todas as strings e arrays.
 *     O chamador libera com sessao_free().
 *   - Na ESCRITA (insert): o chamador é dono da Sessao; o módulo só a lê
 *     (const Sessao *). Nunca toca na memória do chamador.
 *
 * O formato binário está especificado em docs/file-formats.md §6.
 */

#ifndef GYMSOCIAL_SESSAO_FILE_H
#define GYMSOCIAL_SESSAO_FILE_H

#include "ioutils.h"

#include <stdbool.h>
#include <stdint.h>

/* ===================================================================== *
 *  Modelo em memória                                                    *
 * ===================================================================== */

typedef struct {
    uint32_t carga_g;
    uint16_t repeticoes;
} Serie;

typedef struct {
    uint32_t id_exercicio;
    char *observacao; /* alocado no heap; liberado por sessao_free */
    uint16_t num_series;
    Serie *series; /* array alocado no heap; liberado por sessao_free */
} Exercicio;

typedef struct {
    uint32_t id_usuario;
    uint32_t data;        /* YYYYMMDD */
    uint32_t id_academia; /* 0 = não informado */
    uint16_t num_exercicios;
    Exercicio *exercicios; /* array alocado no heap; liberado por sessao_free */
} Sessao;

/**
 * Libera todos os recursos alocados pelo módulo dentro de *s.
 * Libera recursivamente: observacao e series de cada Exercicio, depois
 * o array exercicios. NÃO libera o próprio ponteiro s.
 * Seguro chamar com s == NULL (no-op).
 */
void sessao_free(Sessao *s);

/* ===================================================================== *
 *  Handler de arquivo                                                   *
 * ===================================================================== */

typedef struct sessao_file sessao_file_t;

/**
 * Tipo do callback de scan.
 * Retorne true para continuar, false para parar o scan (early-stop).
 * O módulo libera *s após o retorno do callback.
 */
typedef bool (*sessao_scan_cb)(byte_offset_t offset, const Sessao *s, void *ctx);

/**
 * Abre (ou cria) o arquivo de sessões em `path`.
 * Se o arquivo não existir, cria e grava o cabeçalho inicial.
 * Se existir, verifica o magic; falha com EPROTO se inválido.
 * Falha com EINVAL se path == NULL.
 * Retorna NULL em falha (errno setado).
 */
sessao_file_t *sessao_file_open(const char *path);

/**
 * Grava o cabeçalho atualizado e fecha o arquivo.
 * NULL é no-op (retorna true).
 */
bool sessao_file_close(sessao_file_t *sf);

/**
 * Insere uma sessão no final do arquivo usando a técnica de backpatch
 * (placeholder 0 para tam_dados → escreve payload → retrocede e preenche).
 * *out_offset recebe o offset do início do registro (pode ser NULL).
 * Falha com EINVAL se sf == NULL ou s == NULL.
 */
bool sessao_file_insert(sessao_file_t *sf, const Sessao *s, byte_offset_t *out_offset);

/**
 * Lê a sessão no offset dado e preenche *out.
 * Falha com ENOENT se o registro estiver deletado.
 * Falha com EPROTO se os bytes consumidos != tam_dados (arquivo corrompido).
 * Falha com EINVAL se sf == NULL ou out == NULL.
 * Em falha, *out não precisa ser liberado (já limpo internamente).
 */
bool sessao_file_read_at(sessao_file_t *sf, byte_offset_t offset, Sessao *out);

/**
 * Marca o registro no offset como deletado (status = 0x01).
 * Falha com ENOENT se já estiver deletado.
 * Falha com EINVAL se sf == NULL.
 */
bool sessao_file_mark_deleted(sessao_file_t *sf, byte_offset_t offset);

/**
 * Percorre todos os registros ativos em ordem e chama cb para cada um.
 * Registros deletados são pulados.
 * Se cb retornar false, interrompe (early-stop) e retorna true.
 * Falha com EINVAL se sf == NULL ou cb == NULL.
 */
bool sessao_file_scan(sessao_file_t *sf, sessao_scan_cb cb, void *ctx);

#endif /* GYMSOCIAL_SESSAO_FILE_H */
