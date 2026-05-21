# Registro de Decisões de Projeto

Decisões arquiteturais relevantes, no estilo ADR (Architecture Decision Records).

## ADR-001: Linguagem de implementação

**Data:** [preencher]
**Status:** Aceito

**Contexto:** O projeto exige manipulação de baixo nível de arquivos binários
e implementação de estruturas de dados persistentes.

**Decisão:** Linguagem C (C11).

**Consequências:** Aderência total aos objetivos pedagógicos da disciplina,
controle preciso sobre layout em disco, ausência de abstrações implícitas.
Custo: implementação manual de utilitários que linguagens de alto nível
oferecem prontos.
