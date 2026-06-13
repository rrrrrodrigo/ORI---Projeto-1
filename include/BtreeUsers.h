#ifndef BTREEUSERS_H
#define BTREEUSERS_H

#include <stdio.h>

#define ORDER       5                 // Grau mínimo da B-Tree
#define MAX_KEYS    (2*ORDER - 1)     // Máximo de chaves por página
#define MAX_CHILD   (2*ORDER)         // Máximo de filhos por página

/* ============================================================
   REGISTRO DE USUÁRIO
   Dados armazenados no arquivo principal de usuários.
   A B-Tree indexa os registros através do CPF.
   ============================================================ */

typedef struct {
    char  nome[70];
    char  sexo;
    char  CPF[12];               // 11 dígitos + '\0'
    float peso;
    float altura;
    int   idade;
    int   tempoTreino;           // Tempo de treino em meses
    char  localizacao[50];
    char  arquivoTreino[30];     // Arquivo contendo os treinos do usuário
} Usuario;

/* ============================================================
   PÁGINA (NÓ) DA B-TREE

   key[]           -> CPF convertido para long long
   dataPointers[]  -> RRN do registro no arquivo de usuários
   child[]         -> RRNs das páginas-filhas

   Uma página pode conter até MAX_KEYS chaves e
   MAX_CHILD ponteiros para filhos.
   ============================================================ */

typedef struct {
    int leaf;                    // 1 = folha, 0 = nó interno
    int keyCount;                // Quantidade atual de chaves
    long long key[MAX_KEYS];     // Chaves armazenadas
    int dataPointers[MAX_KEYS];  // RRNs dos registros de usuário
    int child[MAX_CHILD];        // RRNs das páginas-filhas
} BTreePage;

/* ============================================================
   CABEÇALHO DO ARQUIVO DE ÍNDICE

   rootRRN -> RRN da raiz da árvore
   nextRRN -> Próximo RRN livre para alocação de páginas
   ============================================================ */

typedef struct {
    int rootRRN;
    int nextRRN;
} BTreeHeader;

/* ============================================================
   FUNÇÕES PRINCIPAIS DA B-TREE
   ============================================================ */
   
/*
 * Cria e inicializa uma B-Tree vazia.
 */
void InicializarB(FILE *bTreeFile);

/*
 * Busca um CPF na árvore.
 * cpf     -> chave procurada
 * rrnOut  -> recebe o RRN do registro encontrado
 */
void BuscaB(FILE *bTreeFile, long long cpf, int *rrnOut);

/*
 * Insere uma nova chave na árvore.
 * cpf      -> chave de indexação
 * rrnData  -> RRN do registro no arquivo de usuários
 */
void InsertB(FILE *bTreeFile, long long cpf, int rrnData);

#endif