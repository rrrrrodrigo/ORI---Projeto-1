# ORI - Projeto-1

Repositório referente ao projeto 1 de Organização e Recuperação da Informação

Sistema de diário de treinos com rede social, desenvolvido como projeto da
disciplina de Organização e Recuperação de Informação (UFSCar).

Implementação de baixo nível em C de estruturas de armazenamento em disco
(Árvore B, PR-Quadtree) integradas em um sistema coeso de registro
de treinos, perfis de usuários.

A ideia do projeto consiste fundamentalmente em um arquivo contendo registros com informações de vários usuários, além de outros arquivos com os dados dos treinos de cada um deles, com a presença de árvores B - para realizar buscas referentes aos indivíduos presentes nesse sistema e buscar informações de seus treinos. Assim, nosso código se baseia em um mecanismo de armazenamento que permite acesso a um treino especifico que um dos usuários fez, numa data especifica, por exemplo. O objetivo do projeto é desenvolver um diário de treinamento para diversos indivíduos simultaneamente.

## Estrutura do repositório

```
.
├── src/          # implementação dos módulos
├── include/      # cabeçalhos públicos
├── tests/        # testes unitários e de integração
├── tools/        # utilitários (geradores, inspetores)
├── docs/         # documentação técnica
├── data/         # arquivos de dados (não versionados)
└── build/        # artefatos de compilação (não versionados)
```

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
