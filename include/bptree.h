/**
 * @file bptree.h
 * @brief Índice B+ tree paginado genérico.
 *
 * Cada nó ocupa exatamente IO_PAGE_SIZE (4096) bytes em disco.
 * A árvore é parametrizada pelo tamanho da chave e uma callback de comparação,
 * de modo que o mesmo código serve tanto para o índice de usuário (chave de 4 bytes)
 * quanto para o índice de sessão de treino (chave de 20 bytes).
 *
 * O formato do arquivo está especificado em docs/data-structures.md.
 */

#ifndef GYMSOCIAL_BPTREE_H
#define GYMSOCIAL_BPTREE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ===================================================================== *
 *  Abstração de chave                                                    *
 * ===================================================================== */

/**
 * Tamanho máximo de chave suportado (bytes). Chaves maiores exigem
 * aumentar esta constante e recompilar.
 */
#define BPTREE_MAX_KEY_SIZE 64u

/**
 * Callback de comparação: mesmo contrato que memcmp.
 * a e b apontam para os bytes brutos da chave cujo tamanho foi passado
 * para bptree_open.
 */
typedef int (*bptree_cmp_fn)(const void *a, const void *b);

/** Valor armazenado por entrada de folha: deslocamento em bytes do registro no arquivo .dat. */
typedef uint64_t bptree_val_t;

/* ===================================================================== *
 *  Tipos concretos de chave usados neste projeto                        *
 * ===================================================================== */

/**
 * Chave composta de sessão de treino.
 * Ordenada lexicograficamente (id_usuario é o campo primário) para que todas
 * as sessões de um usuário fiquem contíguas no nível de folha, tornando
 * varreduras de intervalo por usuário eficientes.
 * O campo offset é o desempatador para garantir ordem total.
 */
typedef struct {
    uint32_t id_usuario;
    uint32_t id_exercicio;
    uint32_t data;   /* YYYYMMDD — comparação numérica equivale a cronológica */
    uint64_t offset; /* desempatador: garante ordem total */
} TrainingKey;

/** Comparador para o índice de usuário (chave uint32_t de 4 bytes). */
int bptree_cmp_u32(const void *a, const void *b);

/** Comparador para o índice de treino (TrainingKey de 20 bytes, lexicográfico). */
int bptree_cmp_training(const void *a, const void *b);

/* ===================================================================== *
 *  Handle opaco da árvore                                               *
 * ===================================================================== */

typedef struct bptree bptree_t;

/* ===================================================================== *
 *  API                                                                  *
 * ===================================================================== */

/**
 * Abre (ou cria) um arquivo de índice B+ tree em `path`.
 *
 * key_size  tamanho em bytes de uma chave (deve ser <= BPTREE_MAX_KEY_SIZE).
 * cmp       comparador de chaves.
 *
 * Na criação: grava o cabeçalho do arquivo e aloca uma raiz folha vazia.
 * Na abertura: valida os bytes mágicos; falha com EPROTO se inválido.
 * Falha com EINVAL se path==NULL, key_size==0 ou cmp==NULL.
 * Retorna NULL em caso de falha (errno definido).
 */
bptree_t *bptree_open(const char *path, size_t key_size, bptree_cmp_fn cmp);

/**
 * Grava o cabeçalho atualizado e fecha o arquivo.
 * NULL é operação nula (retorna true).
 */
bool bptree_close(bptree_t *bt);

/**
 * Insere (chave, val) na árvore. Divide nós conforme necessário.
 * Falha com EINVAL se bt==NULL ou key==NULL.
 */
bool bptree_insert(bptree_t *bt, const void *key, bptree_val_t val);

/**
 * Busca por correspondência exata. Grava o valor em *out_val.
 * Retorna false + ENOENT se a chave está ausente ou marcada como excluída.
 * Falha com EINVAL se bt==NULL, key==NULL ou out_val==NULL.
 */
bool bptree_search(bptree_t *bt, const void *key, bptree_val_t *out_val);

/**
 * Tipo de callback para bptree_range.
 * Retorne true para continuar a varredura, false para interromper (parada antecipada).
 */
typedef bool (*bptree_scan_cb)(const void *key, bptree_val_t val, void *ctx);

/**
 * Varredura de intervalo: chama cb para cada entrada ativa onde
 *   cmp(chave_entrada, min) >= 0  E  cmp(chave_entrada, max) <= 0,
 * em ordem crescente de chave. Para quando cb retornar false.
 * min ou max podem ser NULL (intervalo aberto).
 * Falha com EINVAL se bt==NULL ou cb==NULL.
 */
bool bptree_range(bptree_t *bt, const void *min, const void *max, bptree_scan_cb cb, void *ctx);

/**
 * Exclusão lógica: marca a entrada da folha como excluída (byte tombstone).
 * NÃO reequilibra nem mescla nós — apenas tombstone.
 * Retorna false + ENOENT se a chave está ausente ou já marcada.
 * Falha com EINVAL se bt==NULL ou key==NULL.
 */
bool bptree_delete(bptree_t *bt, const void *key);

/**
 * Imprime a estrutura da árvore na saída padrão, nível por nível.
 * Usado para confirmar visualmente as divisões durante depuração.
 */
void bptree_print(const bptree_t *bt);

/**
 * Verificação de integridade: checa a ordenação dentro de cada nó, a ocupação
 * mínima (>= order/2 chaves para nós não-raiz) e a continuidade da cadeia de
 * folhas. Imprime o primeiro invariante violado e retorna false se corrompido.
 */
bool bptree_verify(const bptree_t *bt);

#endif /* GYMSOCIAL_BPTREE_H */
