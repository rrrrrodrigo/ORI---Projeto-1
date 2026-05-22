# ORI---Projeto-1

Repositório referente ao projeto 1 de Organização e Recuperação da Informação

Sistema de diário de treinos com rede social, desenvolvido como projeto da
disciplina de Organização e Recuperação de Informação (UFSCar).

Implementação de baixo nível em C de estruturas de armazenamento em disco
(Árvore B+, Hash, PR-Quadtree) integradas em um sistema coeso de registro
de treinos, perfis de usuários e busca de academias por geolocalização.


## Status

Em desenvolvimento. Fase 1 — estruturas até a indexação espacial de academias.

## Requisitos

- GCC 11+ ou Clang 13+
- GNU Make 4+
- Sistema Linux ou macOS (Windows via WSL2)

## Compilação

\`\`\`bash
make            # compila o projeto em modo debug
make release    # compila otimizado
make test       # roda a suite de testes
make clean      # remove artefatos
\`\`\`

## Estrutura do repositório

\`\`\`
.
├── src/          # implementação dos módulos
├── include/      # cabeçalhos públicos
├── tests/        # testes unitários e de integração
├── tools/        # utilitários (geradores, inspetores)
├── docs/         # documentação técnica
├── data/         # arquivos de dados (não versionados)
└── build/        # artefatos de compilação (não versionados)
\`\`\`

## Documentação

- [Arquitetura](docs/architecture.md)
- [Formatos de Arquivo](docs/file-formats.md)
- [Estruturas de Dados](docs/data-structures.md)
- [Decisões de Projeto](docs/decisions.md)

## Autores

- Henrique Schinor Perassoli — RA 832940
- Lucas Sufredini — RA 830113
- Rodrigo Garcia de Gáspari Valdejão — RA 831409
- David Achiles Merlin — RA 845573

## Licença

Projeto acadêmico, sem licença de distribuição definida.
