#include "BTreeUsers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
   FUNÇÕES DE CABEÇALHO
   ============================================================ */

void LerCabecalho(FILE *bTreeFile, BTreeHeader *header)
{
    fseek(bTreeFile, 0, SEEK_SET);
    fread(header, sizeof(BTreeHeader), 1, bTreeFile);
}

void EscreverCabecalho(FILE *bTreeFile, BTreeHeader *header)
{
    fseek(bTreeFile, 0, SEEK_SET);
    fwrite(header, sizeof(BTreeHeader), 1, bTreeFile);
    fflush(bTreeFile);
}

/* ============================================================
   FUNÇÕES DE PÁGINA
   ============================================================ */

void LerPagina(FILE *bTreeFile, int rrn, BTreePage *page)
{
    int offset;

    /* Cálculo do offset == Tamannho do cabeçalho + qtde páginas * tamanho página */
    offset = sizeof(BTreeHeader) + rrn * sizeof(BTreePage);

    fseek(bTreeFile, offset, SEEK_SET);

    fread(page, sizeof(BTreePage), 1, bTreeFile);
}

void EscreverPagina(FILE *bTreeFile, int rrn, BTreePage *page)
{
    long offset;

    /* Cálculo do offset */
    offset = sizeof(BTreeHeader) + rrn * sizeof(BTreePage);

    fseek(bTreeFile, offset, SEEK_SET);

    fwrite(page, sizeof(BTreePage), 1, bTreeFile);

    fflush(bTreeFile);
}

/* ============================================================
   UTILITÁRIOS
   ============================================================ */

long long CPFToLongLong(const char *cpf)
{
    return atoll(cpf);
}

/* ============================================================
   INICIALIZAÇÃO
   ============================================================ */

void InicializarB(FILE *bTreeFile)
{
    BTreeHeader header;

    header.rootRRN = -1;
    header.nextRRN = 0;

    EscreverCabecalho(bTreeFile, &header);
}

/* ============================================================
   BUSCA
   ============================================================ */

void BuscaB(FILE *bTreeFile, long long cpf, int *rrnOut)
{
    BTreeHeader header;
    BTreePage page;

    int currentRRN;
    int i;

    LerCabecalho(bTreeFile, &header);

    if(header.rootRRN == -1) {
        *rrnOut = -1;
        return;
    }

    currentRRN = header.rootRRN;

    while(currentRRN != -1) {
        LerPagina(bTreeFile, currentRRN, &page);

        i = 0;

        /* encontra a posição da chave */
        while(i < page.keyCount && cpf > page.key[i]) {
            i++;
        }

        /* chave encontrada */
        if(i < page.keyCount && cpf == page.key[i]) {
            *rrnOut = page.dataPointers[i];
            return;
        }

        /* chegou numa folha e não encontrou */
        if(page.leaf) {
            *rrnOut = -1;
            return;
        }

        /* desce para o filho apropriado */
        currentRRN = page.child[i];
    }

    *rrnOut = -1;
}

/* ============================================================
   FALTANDO DIVISÃO, INSERÇÃO (AUXILIAR E PRINCIPAL) E REMOÇÃO
   ============================================================ */

