#ifndef BTREEUSERS_H
#define BTREEUSERS_H

#include <stdio.h>

/* Grau mínimo da B-Tree */
#define ORDER       5

/* Máximo de chaves por página */
#define MAX_KEYS    (2*ORDER - 1)

/* Máximo de filhos por página*/
#define MAX_CHILD   (2*ORDER)

/* ============================================================
   REGISTRO DE USUÁRIO
   ============================================================ */

typedef struct {
    char  nome[70];
    char  sexo;
    char  CPF[12];               /* 11 dígitos + '\0' */
    float peso;
    float altura;
    int   idade;
    int   tempoTreino;           /* Tempo de treino em meses */
    char  localizacao[50];
    char  arquivoTreino[30];     /* Arquivo contendo os treinos do usuário */
} Usuario;

/* ============================================================
   PÁGINA (NÓ) DA B-TREE
   ============================================================ */

typedef struct {
    int leaf;                    /* 1 = folha, 0 = nó interno */
    int keyCount;                /* Quantidade atual de chaves */
    long long key[MAX_KEYS];     /* Chaves armazenadas */
    int dataPointers[MAX_KEYS];  /* RRNs dos registros de usuário */
    int child[MAX_CHILD];        /* RRNs das páginas-filhas */
} BTreePage;

/* ============================================================
   CABEÇALHO DO ARQUIVO DE ÍNDICE
   ============================================================ */

typedef struct {
    int rootRRN; /* RRN da raiz da árvore */
    int nextRRN; /* Próximo RRN livre para alocação de páginas */
} BTreeHeader;

/* ============================================================
   FUNÇÕES DO CABEÇALHO
   ============================================================ */

void LerCabecalho(FILE *bTreeFile, BTreeHeader *header);
void EscreverCabecalho(FILE *bTreeFile, BTreeHeader *header);

/* ============================================================
   FUNÇÕES MANIPULAÇÃO DE PÁGINAS
   ============================================================ */

void LerPagina(FILE *bTreeFile, int rrn, BTreePage *page);
void EscreverPagina(FILE *bTreeFile, int rrn, BTreePage *page);

/* ============================================================
   FUNÇÕES PRINCIPAIS DA B-TREE
   ============================================================ */

void InicializarB(FILE *bTreeFile);                         /* Cria e inicializa uma B-Tree vazia */
void BuscaB(FILE *bTreeFile, long long cpf, int *rrnOut);   /* Busca um CPF na árvore pelo cpf*/
void InsertB(FILE *bTreeFile, long long cpf, int rrnData);
void RemoveB(FILE *bTreeFile, long long cpf);

/* ============================================================
   FUNÇÕES AUXILIARES DE INSERÇÃO
   ============================================================ */

void InserirNaoCheio(FILE *bTreeFile, int pageRRN, long long cpf, int rrnData);  /* Insere uma chave em uma página não cheia. */
void DividirFilho(FILE *bTreeFile, int parentRRN, int childIndex, int childRRN); /* Divide um filho cheio durante a inserção. */

/* ============================================================
   UTILITÁRIOS
   ============================================================ */

long long CPFToLongLong(const char *cpf); /* Converte CPF de string para long long */

#endif
