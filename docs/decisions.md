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

## ADR-002: Estrutura de índices e arquivos de dados

**Data:** 22/06/2026
**Status:** Aceito

**Contexto:** No início do projeto, a ideia era usar uma Árvore B para o
arquivo de usuários e outra Árvore B para os arquivos de treinos de cada
usuário. Depois da revisão da estrutura, vimos que o arquivo de usuários não
precisa dessa árvore, porque a consulta principal do projeto está mais ligada
aos treinos.

**Decisão:** A Árvore B do arquivo de usuários foi removida. O projeto vai usar
uma Árvore B+ para indexar o arquivo de treinos de todos os usuários. Também
serão usados arquivos de dados mais organizados, como `usuarios.txt` e
`treinos.txt`, com informações melhor definidas.

**Detalhes da estrutura:** O arquivo `usuarios.txt` vai guardar os dados dos
usuários. O arquivo `treinos.txt` vai guardar os dados dos treinos, incluindo
informações como seleção de exercícios, carga, repetições, número de séries,
número de exercícios, CPF do usuário, duração do treino e data do treino.

**Consequências:** A estrutura fica mais simples e mais direta para o objetivo
principal do projeto. Em vez de manter uma árvore para usuários e outra para
treinos separados, a busca principal passa a ser feita pela Árvore B+ no arquivo
de treinos. Isso facilita consultar os treinos registrados e evita manter uma
estrutura extra que não é tão necessária.
