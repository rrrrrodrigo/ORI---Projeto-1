# ORI - Projeto-1

Repositório referente ao projeto 1 de Organização e Recuperação da Informação

Sistema de diário de treinos com rede social, desenvolvido como projeto da
disciplina de Organização e Recuperação de Informação (UFSCar).

Implementação de baixo nível em C de estruturas de armazenamento em disco
(Árvore B, PR-Quadtree) integradas em um sistema coeso de registro
de treinos, perfis de usuários e busca de academias por geolocalização.

A ideia do projeto consiste fundamentalmente em um arquivo de dados contendo registros com informações (nome, gênero, carga máxima no supino, agachamento e puxada de frente, local onde mora, academias mais próximas) de vários usuários, com a presença de estruturas como uma Quadtree - dado o local onde um indivíduo mora, ela retornará as academias mais próximas em um raio específico -, além de árvores B - para realizar buscas referentes ao PR (Personal Record: carga máxima nos exercícios) das pessoas presentes nesse sistema de treinos. Assim, nosso código é capaz de aproximar os indivíduos que aguentam pesos parecidos nos exercícios e que moram relativamente perto uns dos outros, de modo que eles podem então ser parceiros de treino. O objetivo do projeto é desenvolver um sistema capaz de identificar possíveis parceiros de treino.

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
