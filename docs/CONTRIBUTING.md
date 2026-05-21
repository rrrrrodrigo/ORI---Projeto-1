# Manual de Contribuição — GymSocial

Guia prático para o dia a dia. Mantenha aberto enquanto trabalha.

---

## Fluxo geral em uma linha

**Atualizar main → criar branch → codar → testar → commitar → push → abrir PR → revisar → merge.**

Nunca commite direto na `main`. A branch é protegida e o GitHub vai recusar.

---

## 1. Antes de começar a trabalhar

Sempre sincronize sua main com o remoto antes de criar uma branch nova:

```bash
git checkout main
git pull
```

Se você esquecer disso, sua branch pode nascer desatualizada e gerar conflitos depois.

---

## 2. Criar uma branch

Toda contribuição vive em uma branch própria. Nomeie com prefixo + descrição curta em minúsculas com hifens:

```bash
git checkout -b feat/bptree-insert
```

**Prefixos:**

| Prefixo | Quando usar |
|---|---|
| `feat/` | Funcionalidade nova |
| `fix/` | Correção de bug |
| `refactor/` | Reorganização sem mudar comportamento |
| `test/` | Apenas testes |
| `docs/` | Apenas documentação |
| `chore/` | Configuração, build, CI, etc. |

Uma branch = uma tarefa coesa. Se você está fazendo duas coisas diferentes, são duas branches.

---

## 3. Trabalhar no código

Edite, salve, edite. O VS Code formata automaticamente ao salvar (`format on save` ativado).

**Comandos do make que você vai usar o tempo todo:**

| Comando | O que faz |
|---|---|
| `make` | Compila em modo debug |
| `make test` | Compila e roda os testes em modo debug |
| `make test-asan` | Roda os testes com AddressSanitizer (detecta bugs de memória) |
| `make format` | Formata todo o código |
| `make format-check` | Verifica formatação sem alterar |
| `make lint` | Análise estática com cppcheck |
| `make clean` | Remove artefatos de compilação |
| `make compile-db` | Regenera `compile_commands.json` (rode após adicionar/remover arquivos `.c`) |

**Regra prática:** antes de cada commit, rode no mínimo `make test`. Antes de abrir o PR, rode `make test-asan` também — se algo passar no debug mas falhar no asan, é bug de memória e precisa ser corrigido antes do PR.

---

## 4. Verificar o que você mudou

Antes de commitar, sempre olhe o que está prestes a ser registrado:

```bash
git status        # lista arquivos modificados
git diff          # mostra as mudanças linha a linha
```

Se algum arquivo apareceu modificado sem você ter mexido nele intencionalmente, investigue antes de adicionar.

---

## 5. Commitar

**Adicione apenas o que você quer commitar.** Não use `git add .` por preguiça se houver arquivos não relacionados:

```bash
git add src/bptree.c include/bptree.h tests/test_bptree.c
```

Se realmente todos os arquivos modificados pertencem ao mesmo commit:

```bash
git add .
```

**Escreva a mensagem no padrão Conventional Commits:**

```bash
git commit -m "feat(bptree): implementa busca por chave exata

Adiciona a funcao bptree_search que percorre a arvore da raiz
ate a folha, retornando o valor associado a chave ou NULL se
nao existir. Cobertura de testes inclui chaves presentes,
ausentes e casos de fronteira."
```

**Formato:**

```
<tipo>(<escopo>): <resumo curto, imperativo, sem ponto final>

<corpo explicando o quê e o porquê, não o como>
```

Escopo (entre parênteses) ajuda a identificar a área: `bptree`, `quadtree`, `usuario`, `build`, `ci`, etc.

**Mensagens ruins** (evite):

- `"alteracoes"`
- `"fix"`
- `"funcionou"`
- `"final"`

**Mensagens boas:**

- `feat(quadtree): implementa busca por raio circular`
- `fix(bptree): corrige split de nó folha cheio`
- `refactor(io_utils): extrai leitura de string para funcao dedicada`
- `docs(architecture): adiciona diagrama de dependencias entre modulos`

Faça commits **pequenos e atômicos**. Cada commit deve fazer uma coisa bem feita. Se você está prestes a commitar 600 linhas em arquivos diferentes, provavelmente são vários commits.

---

## 6. Subir para o GitHub

Primeira vez empurrando a branch:

```bash
git push -u origin nome-da-branch
```

A flag `-u` configura o tracking, então a partir daí basta:

```bash
git push
```

---

## 7. Abrir o Pull Request

No GitHub, vá ao repositório. Aparece uma faixa amarela com **Compare & pull request**. Clique.

**Preencha o PR assim:**

**Título:** mesmo formato do commit principal. Exemplo: `feat(bptree): implementa busca por chave exata`.

**Descrição:** use este template (copie e adapte):

```markdown
## O que este PR faz

Breve descrição em 2-3 linhas do que muda no sistema.

## Por que

Contexto da mudança. Qual problema resolve, qual funcionalidade adiciona.

## Como testar

- [ ] `make test` passa
- [ ] `make test-asan` passa
- [ ] `make lint` sem warnings novos
- [ ] (Outros passos manuais, se houver)

## Notas adicionais

Decisões de projeto não óbvias, limitações conhecidas, próximos passos.
```

**Marque um reviewer** (um dos colegas) no painel lateral direito.

**Aguarde o CI rodar.** Aparecem checks na parte inferior do PR. Todos precisam ficar verdes antes do merge ser permitido.

---

## 8. Revisar o PR de um colega

Quando alguém te marcar como reviewer:

Abra o PR, vá na aba **Files changed**.

Leia o código com atenção. Comente em linhas específicas clicando no `+` ao lado do número da linha.

**O que olhar:**

- O código faz o que o PR diz que faz?
- Os testes cobrem os casos importantes?
- Há nomes confusos, funções gigantes, código duplicado?
- Falta documentação em alguma função pública?
- Algum `TODO` ou `printf` de debug esquecido?

**Tipos de comentário:**

- **Sugestão:** "que tal renomear isso para X?" — não bloqueia o merge.
- **Pedido de mudança:** "isso precisa ser corrigido antes de mergear" — use o botão **Request changes** na revisão.
- **Pergunta:** "por que essa escolha aqui?" — para entender, não para bloquear.

Quando estiver satisfeito, clique em **Review changes → Approve**.

Seja crítico mas educado. Revisão é sobre o código, não sobre a pessoa.

---

## 9. Responder a uma revisão

Se um reviewer pediu mudanças:

Faça as alterações na sua máquina. Commite normalmente na **mesma branch**:

```bash
git add .
git commit -m "fix(bptree): corrige edge case apontado na revisao"
git push
```

O PR atualiza automaticamente. Marque os comentários respondidos como **Resolved** quando aplicáveis. Peça nova revisão ao colega.

---

## 10. Fazer o merge

Quando o PR tem aprovação e CI verde:

Clique em **Merge pull request** → **Confirm merge**.

Use a opção padrão **Create a merge commit** (preserva o histórico da branch). 

Depois do merge, **delete a branch remota** clicando no botão que aparece. Limpa o repositório.

Na sua máquina, sincronize e apague a branch local:

```bash
git checkout main
git pull
git branch -d feat/bptree-insert
```

---

## 11. Lidando com situações comuns

### Esqueci de criar uma branch e commitei na main local

Não fez push ainda? Recupere com:

```bash
git branch feat/minha-coisa     # cria branch a partir do estado atual
git reset --hard origin/main    # restaura a main ao estado do remoto
git checkout feat/minha-coisa   # vai para a branch nova
```

Pronto, seus commits estão salvos na branch nova e a main local está limpa.

### Minha branch está desatualizada em relação à main

Aconteceu de outro PR ser mergeado depois que você criou sua branch. Para incorporar:

```bash
git checkout main
git pull
git checkout sua-branch
git merge main
```

Resolva conflitos se houver (o git vai indicar quais arquivos), commite a resolução, push.

### Estou no meio de algo e preciso trocar de branch

Salve seu trabalho temporariamente:

```bash
git stash                  # guarda mudanças não commitadas
git checkout outra-branch  # troca
# (faz o que precisa)
git checkout sua-branch    # volta
git stash pop              # restaura o trabalho
```

### Quero desfazer o último commit (sem ter dado push)

```bash
git reset --soft HEAD~1    # desfaz o commit, mantém as mudanças
```

Edite o que precisar e commite de novo.

### Quero descartar mudanças locais não commitadas

```bash
git checkout -- arquivo.c     # descarta mudanças em um arquivo
git checkout -- .             # descarta tudo (cuidado!)
```

### Errei a mensagem do último commit (e ainda não dei push)

```bash
git commit --amend -m "nova mensagem"
```

Se já deu push, **não use amend** — abra um commit novo de correção em vez disso.

---

## 12. Regras rápidas

Sempre `git pull` na main antes de criar branch.

Uma tarefa = uma branch = um PR.

Commits pequenos e descritivos.

`make test` antes de commitar, `make test-asan` antes do PR.

Nunca force push (`git push --force`) em branch compartilhada.

Não commite arquivos gerados (`build/`, `compile_commands.json`, etc.) — o `.gitignore` cobre, mas confira.

Não commite arquivos com dados grandes em `data/`.

Em caso de dúvida sobre comando do git que você não usa há tempos: pergunte no grupo ou consulte este manual antes de improvisar.

---

## 13. Cheatsheet visual

```
┌─────────────────────────────────────────────────────────┐
│ git checkout main && git pull                           │
│ git checkout -b <tipo>/<descricao>                      │
│                                                         │
│  ... codar, salvar (formato automatico) ...             │
│                                                         │
│ make test                                               │
│ make test-asan                                          │
│                                                         │
│ git status                                              │
│ git diff                                                │
│ git add <arquivos>                                      │
│ git commit -m "<tipo>(<escopo>): <resumo>"              │
│ git push -u origin <branch>                             │
│                                                         │
│  ... abrir PR no GitHub, marcar reviewer ...            │
│  ... aguardar CI verde + aprovacao ...                  │
│  ... Merge pull request ...                             │
│                                                         │
│ git checkout main && git pull                           │
│ git branch -d <branch>                                  │
└─────────────────────────────────────────────────────────┘
```

---

