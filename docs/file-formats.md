# Formatos de Arquivo

Especificação byte-a-byte de cada arquivo persistido pelo sistema.

## Convenções gerais

- Endianness: little-endian
- Strings: prefixadas pelo tamanho em `uint16_t`, sem terminador nulo
- Inteiros sem sinal usados como identificadores e tamanhos

## Estrutura comum de cabeçalho (32 bytes)

Todos os arquivos de dados do GymSocial compartilham o mesmo cabeçalho de 32 bytes:

| Offset | Tamanho | Tipo     | Campo          | Descrição                                    |
|--------|---------|----------|----------------|----------------------------------------------|
| 0      | 4       | char[4]  | magic          | Identificador do arquivo (ex.: "GYMS")       |
| 4      | 2       | uint16_t | versao         | Versão do formato (atualmente 1)             |
| 6      | 2       | uint16_t | reservado      | Zeros; reservado para uso futuro             |
| 8      | 4       | uint32_t | num_registros  | Quantidade de registros ativos               |
| 12     | 4       | uint32_t | num_deletados  | Quantidade de registros marcados como deletados |
| 16     | 4       | uint32_t | proximo_id     | Próximo ID livre (0 = não usado)             |
| 20     | 12      | uint8_t[]| reservado      | Zeros; padding para completar 32 bytes       |

## Envelope de registro

Cada registro é precedido por um envelope de 5 bytes:

| Offset | Tamanho | Tipo     | Campo      | Descrição                                      |
|--------|---------|----------|------------|------------------------------------------------|
| 0      | 1       | uint8_t  | status     | 0x00 = ativo, 0x01 = deletado logicamente      |
| 1      | 4       | uint32_t | tam_dados  | Tamanho do payload em bytes (sem o envelope)   |

O payload segue imediatamente após o envelope.

## Arquivos

[Seções 1–5 a preencher conforme cada arquivo for projetado]

---

## §6 — sessoes.dat (sessões de treino)

### Magic

`GYMS` (bytes: `47 59 4D 53`)

### Payload de sessão

Os campos são gravados nesta ordem, sem padding entre eles:

| Campo          | Tipo             | Bytes   | Descrição                               |
|----------------|------------------|---------|-----------------------------------------|
| id_usuario     | uint32_t         | 4       | ID do usuário dono da sessão            |
| data           | uint32_t         | 4       | Data no formato YYYYMMDD                |
| id_academia    | uint32_t         | 4       | ID da academia; 0 = não informado       |
| num_exercicios | uint16_t         | 2       | Quantidade de exercícios na sessão      |
| exercicios[]   | —                | variável| Sequência de `num_exercicios` blocos    |

Cada bloco de exercício:

| Campo          | Tipo             | Bytes   | Descrição                               |
|----------------|------------------|---------|-----------------------------------------|
| id_exercicio   | uint32_t         | 4       | ID do exercício                         |
| observacao     | string prefixada | 2+N     | uint16_t comprimento + N bytes de texto |
| num_series     | uint16_t         | 2       | Quantidade de séries                    |
| series[]       | —                | variável| Sequência de `num_series` blocos        |

Cada bloco de série:

| Campo      | Tipo     | Bytes | Descrição                      |
|------------|----------|-------|--------------------------------|
| carga_g    | uint32_t | 4     | Carga em gramas                |
| repeticoes | uint16_t | 2     | Número de repetições           |

### Nota sobre proximo_id

O campo `proximo_id` do cabeçalho permanece 0 e não é utilizado para sessões.
Sessões são localizadas pela chave composta `(id_usuario, data)` via índice B+.

### Exemplo canônico (test §6)

Sessão com 1 exercício, observação vazia, 1 série:

```
id_usuario  = 1
data        = 20240101
id_academia = 0
exercicio:
  id_exercicio = 1
  observacao   = "" (vazia)
  num_series   = 1
  serie: carga_g = 100000, repeticoes = 10
```

Hexdump do arquivo completo (65 bytes):

```
Offset  Hex                                      Descrição
------  ----                                     ---------
0x00    47 59 4D 53                              magic "GYMS"
0x04    01 00                                    versao = 1
0x06    00 00                                    reservado
0x08    01 00 00 00                              num_registros = 1
0x0C    00 00 00 00                              num_deletados = 0
0x10    00 00 00 00                              proximo_id = 0
0x14    00 00 00 00 00 00 00 00 00 00 00 00      reservado (12 bytes)
0x20    00                                       status = ativo
0x21    1C 00 00 00                              tam_dados = 28
0x25    01 00 00 00                              id_usuario = 1
0x29    E5 D6 34 01                              data = 20240101
0x2D    00 00 00 00                              id_academia = 0
0x31    01 00                                    num_exercicios = 1
0x33    01 00 00 00                              id_exercicio = 1
0x37    00 00                                    observacao len = 0
0x39    01 00                                    num_series = 1
0x3B    A0 86 01 00                              carga_g = 100000
0x3F    0A 00                                    repeticoes = 10
```

Tamanho total: 65 bytes  
Offset do registro: 32 (0x20)  
tam_dados: 28 (0x1C)
