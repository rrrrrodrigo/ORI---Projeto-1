Excelente evolução do escopo. O novo projeto é substancialmente mais rico em termos de Organização e Recuperação de Informação, pois agora envolve dados textuais variáveis, dados espaciais (localização de academias), dados relacionais (amizades, grupos) e séries temporais (histórico de treinos e ocupação). Vou esquematizar tudo com rigor e mapear cada necessidade à estrutura de baixo nível mais adequada.

# GymSocial — Esquematização e Projeto de Estruturas de Dados

## 1. Esquematização Funcional do Sistema

Antes de definir estruturas, é preciso ter clareza absoluta sobre o que o sistema faz. Reorganizei e refinei as ideias em seis domínios funcionais coesos.

### 1.1 Domínio de Usuários e Perfis
Cada usuário possui um perfil contendo dados pessoais (nome, data de nascimento, sexo, altura, peso atual e histórico de peso), nível de experiência, objetivos de treino (hipertrofia, força, resistência, perda de peso), academia principal vinculada e configurações de privacidade. O perfil é a entidade central do sistema, referenciada por praticamente todas as demais.

### 1.2 Domínio de Treinos e Progressão
Núcleo herdado da proposta original. Cada usuário registra sessões de treino contendo data, hora, academia onde foi realizado, lista de exercícios executados e, para cada exercício, suas séries com carga, repetições, RPE percebido e anotações textuais livres. A consulta crítica continua sendo a progressão histórica por exercício, agora também viável em escala social (comparar progressão entre usuários).

### 1.3 Domínio de Academias e Geolocalização
Cada academia possui identificador, nome, endereço, coordenadas geográficas (latitude, longitude), equipamentos disponíveis e horário de funcionamento. As consultas críticas aqui são duas: encontrar academias próximas a um ponto (consulta espacial por raio ou por k-vizinhos mais próximos) e consultar padrão de ocupação por horário.

### 1.4 Domínio de Ocupação e Horários de Pico
Para cada academia, registra-se ao longo do tempo o número de usuários presentes em cada faixa horária. Os dados podem vir de check-ins explícitos dos usuários ou de estimativas. A consulta crítica é "qual o horário menos cheio da academia X numa terça-feira?", que exige agregação por dia da semana e hora.

### 1.5 Domínio Social
Engloba amizades (relação simétrica entre usuários), grupos (conjunto fechado de usuários com nome e descrição), mensagens privadas entre usuários, mensagens em grupo, convites para parceria de treino, sugestões de treino entre amigos e descoberta de perfis similares (busca por usuários com objetivos, nível e padrão de treino parecidos).

### 1.6 Domínio de Gamificação e Rankings
Rankings por academia (quem treinou mais vezes no mês, quem teve maior progressão em determinado exercício, quem tem o maior PR em supino na academia X). Os rankings são derivados dos dados de treino e devem ser atualizáveis em tempo aceitável após cada novo registro.

---

## 2. Mapeamento de Consultas Críticas

A escolha de cada estrutura precisa partir das consultas que ela viabiliza. Listo aqui as consultas centrais que orientarão o projeto.

A primeira é a progressão histórica de um exercício para um usuário, herdada do projeto anterior. A segunda é a busca de academias dentro de um raio de N quilômetros de uma coordenada (ou as K academias mais próximas). A terceira é a recuperação do padrão de ocupação de uma academia agregado por dia da semana e hora. A quarta é a busca por usuários com perfil similar, que envolve múltiplos atributos. A quinta é a recuperação de mensagens de uma conversa em ordem cronológica. A sexta é a consulta de ranking de uma academia segundo algum critério. A sétima é a recuperação de amigos de um usuário e dos amigos dos amigos para sugestões.

---

## 3. Inventário das Estruturas de Armazenamento

Apresento abaixo cada estrutura que comporá o sistema, justificando rigorosamente a escolha, descrevendo sua construção e indicando suas interligações.

### 3.1 Arquivo de Dados de Usuários (registros de tamanho variável)

Os usuários têm campos de tamanho variável (nome, biografia, lista de objetivos textuais), justificando registros variáveis. Layout do registro: cabeçalho com tamanho total e flag de remoção lógica, campos de tamanho fixo (id_usuario, data_nascimento, sexo, altura, id_academia_principal, timestamp_criacao) seguidos dos campos variáveis precedidos cada um por seu indicador de tamanho.

Política de remoção: tombstone com posterior compactação. Inserção em append. O id_usuario é gerado sequencialmente e nunca reaproveitado, garantindo estabilidade das referências em todos os índices.

### 3.2 Árvore B+ primária sobre id_usuario

Esta árvore indexa o arquivo de usuários pelo identificador. A chave é id_usuario e o valor associado é o byte_offset do registro no arquivo de dados. Como id_usuario é gerado sequencialmente, as inserções tendem a ser nas folhas mais à direita, padrão favorável para a B+. Ordem sugerida entre 50 e 100 (a definir com base no tamanho da página de disco), buscando equilíbrio entre altura da árvore e custo de busca dentro da página.

Justificativa da escolha de B+ sobre B: a B+ mantém todos os dados nas folhas com encadeamento, viabilizando varreduras eficientes (útil para operações administrativas e dump completo de usuários). A B simples não tem essa propriedade.

### 3.3 Árvore B+ sobre arquivo de sessões de treino

Esta é a estrutura herdada e mantida do projeto anterior, agora com chave composta (id_usuario, id_exercicio, data, byte_offset). A inclusão do id_usuario no início da chave permite que todas as sessões de um mesmo usuário fiquem agrupadas nas folhas, e dentro de um usuário, todas as ocorrências de um exercício também ficam agrupadas e cronologicamente ordenadas.

Esta organização atende simultaneamente a três consultas distintas: progressão de um exercício para um usuário (faixa sobre id_usuario, id_exercicio), histórico completo de um usuário (faixa sobre id_usuario) e suporte à comparação entre usuários (duas faixas separadas e iteração paralela nas folhas).

### 3.4 Tabela Hash de Personal Records

Mantida do projeto original e expandida. Chave: (id_usuario, id_exercicio). Valor: (carga_maxima, data_recorde, offset_sessao). A hash é a escolha correta aqui porque a consulta é sempre por chave exata ("qual o PR do usuário U no exercício E?"), nunca por faixa. Função hash sugerida: combinação dos dois identificadores via XOR rotacionado ou multiplicação modular, com endereçamento aberto ou encadeamento externo.

Esta hash também alimenta diretamente os rankings de PR por exercício na academia.

### 3.5 Arquivo de Dados de Academias

Registros de tamanho variável (nome, endereço, lista de equipamentos). Cabeçalho com tamanho e flag de remoção, campos fixos (id_academia, latitude, longitude, hora_abertura, hora_fechamento) e campos variáveis.

### 3.6 PR-Quadtree para Academias (índice espacial)

Esta é a estrutura crítica nova do projeto. As academias têm coordenadas geográficas e precisam ser recuperadas por proximidade. A escolha entre Quadtree pontual (PR-Quadtree) e Region Quadtree depende da natureza dos dados, e como academias são pontos no espaço (não regiões), a PR-Quadtree é a estrutura correta.

Construção: o espaço é a região geográfica de cobertura (pode-se delimitar pelo retângulo envolvente das academias cadastradas ou usar limites geográficos amplos como o Brasil inteiro). Cada nó interno divide a região em quatro quadrantes iguais (NO, NE, SO, SE). Folhas armazenam no máximo um ponto. Inserção: desce-se até o quadrante apropriado, e em caso de colisão a folha é subdividida recursivamente. Cada folha aponta para o id_academia, que via outro índice leva ao registro completo.

Consultas suportadas: busca por ponto exato (academia em coordenada conhecida), busca por janela retangular (academias dentro de uma região geográfica do mapa) e busca por raio circular (academias até X km do usuário). A última requer interseção entre o círculo e os quadrantes durante a descida.

Justificativa contra alternativas: uma R-Tree também serviria, mas é mais complexa de implementar e seu ganho é maior para retângulos sobrepostos, não para pontos. Um KD-Tree também é viável, mas é menos didática no contexto da disciplina e tem desempenho pior em inserções dinâmicas se não for balanceada.

### 3.7 Árvore B+ secundária sobre id_academia

Pequena árvore B+ ou hash table mapeando id_academia para byte_offset no arquivo de academias. Necessária porque a PR-Quadtree devolve apenas o id_academia, e este precisa ser resolvido para o registro completo. A escolha entre hash e B+ aqui pende para hash pela simplicidade e por não haver consulta por faixa de id_academia.

### 3.8 Arquivo de Check-ins e Estrutura de Ocupação

Cada check-in é um registro com (id_usuario, id_academia, timestamp_entrada, timestamp_saida). São registros de tamanho fixo, simplificando seu armazenamento. Arquivo separado, append-only.

Sobre esse arquivo constrói-se uma Árvore B+ com chave (id_academia, timestamp) que permite recuperar todos os check-ins de uma academia em ordem cronológica em tempo logarítmico.

Para responder "qual o horário menos cheio" sem precisar varrer todo o histórico, mantém-se uma estrutura agregada por academia: uma matriz fixa de 7 dias por 24 horas (168 células) com a média de ocupação histórica. Esta matriz é armazenada em um arquivo dedicado ou anexada ao registro da academia (ela tem tamanho fixo). É atualizada incrementalmente a cada check-in.

### 3.9 Índice Multi-atributo para Perfis Similares

Para encontrar usuários com perfis parecidos, a consulta envolve múltiplos atributos (idade, sexo, nível, objetivo, peso, altura). Não existe estrutura única ideal para consulta multidimensional dinâmica em disco, mas há boas aproximações.

Recomendação: criar múltiplas Árvores B+ secundárias, uma por atributo discreto ou intervalável (faixa de idade, nível de experiência, objetivo principal), cada uma mapeando o valor do atributo para uma lista de id_usuario. A busca por perfil similar interseta os resultados dessas árvores. Esta é a estratégia clássica de índices secundários invertidos.

Para o atributo objetivo (que é categórico e pequeno), uma simples tabela hash já basta. Para idade (intervalável), a B+ é melhor pois permite consulta por faixa ("usuários entre 25 e 30 anos").

### 3.10 Grafo Social: Arquivo de Adjacências

A rede social de amizades é um grafo. Representação em arquivo: lista de adjacências em arquivo de registros variáveis, indexado por uma Árvore B+ ou hash sobre id_usuario que aponta para o início da lista de amigos daquele usuário no arquivo.

Cada entrada na lista de adjacências contém o id_amigo e o timestamp da amizade. Como amizades são adicionadas com frequência muito menor que treinos, a estrutura tolera reorganizações ocasionais. Para evitar fragmentação ao adicionar amigos, pode-se reservar espaço inicial sobredimensionado por usuário ou usar listas encadeadas em blocos.

Para grupos, abordagem análoga: arquivo de grupos (com nome e metadados, registros variáveis) indexado por id_grupo, e arquivo de membros (id_grupo, id_usuario) indexado por uma B+ com chave composta (id_grupo, id_usuario) que permite tanto listar membros de um grupo quanto verificar pertinência rapidamente.

### 3.11 Sistema de Mensagens

Mensagens são o caso mais óbvio de registros de tamanho variável (texto livre). Arquivo de mensagens em append puro com cabeçalho (id_mensagem, id_remetente, id_destinatario_ou_grupo, flag_individual_ou_grupo, timestamp) e corpo textual variável.

Índice crítico: Árvore B+ com chave composta (id_conversa, timestamp), onde id_conversa é um identificador derivado deterministicamente do par de usuários (para mensagens individuais) ou igual ao id_grupo (para mensagens de grupo). Esta árvore permite recuperar uma conversa inteira em ordem cronológica via varredura nas folhas encadeadas a partir do primeiro timestamp.

### 3.12 Estruturas de Ranking

Os rankings são derivados, não primários. Estratégia: pré-computar e materializar rankings em arquivos auxiliares atualizados incrementalmente.

Para cada academia, manter um pequeno arquivo com os top-N usuários por critério (frequência mensal, PR em exercícios populares, volume total). Atualizações disparadas por novas sessões verificam se o usuário entra no top-N e reorganizam o arquivo. Como N é pequeno (10 a 100), a manutenção é barata.

Para rankings dinâmicos com qualquer critério, fallback é varrer o índice apropriado. Por exemplo, "top 10 supinos da academia X" pode ser respondido varrendo a hash de PRs filtrando por id_academia (que precisa estar denormalizado no PR ou cruzado com o perfil do usuário).

---

## 4. Mapa de Interligações entre Estruturas

A coesão do sistema vem das interligações. Descrevo abaixo como os dados fluem entre as estruturas em cada operação principal.

**Cadastro de novo usuário:** registro escrito no arquivo de usuários produzindo byte_offset, inserção na B+ de usuários (id_usuario → offset), criação de entrada vazia na lista de adjacências sociais, inserção nos índices secundários multi-atributo.

**Registro de nova sessão de treino:** registro escrito no arquivo de sessões produzindo offset; para cada exercício, inserção na B+ primária de treinos com chave (id_usuario, id_exercicio, data); para cada série, verificação e possível atualização da hash de PRs; verificação se PRs atualizados disparam reordenação de rankings da academia do usuário.

**Check-in em academia:** registro escrito no arquivo de check-ins; inserção na B+ de check-ins com chave (id_academia, timestamp); atualização incremental da matriz de ocupação agregada da academia (incrementando a célula correspondente ao dia da semana e hora).

**Busca de academias próximas:** consulta na PR-Quadtree por janela ou raio sobre as coordenadas do usuário, devolvendo ids; cada id resolvido via hash de academias para obter o registro completo; opcionalmente, para cada academia retornada, consulta da matriz de ocupação para informar o status atual.

**Busca de perfis similares:** consulta em paralelo nos múltiplos índices secundários do domínio de usuários, interseção dos conjuntos de ids retornados, resolução de cada id via B+ de usuários para apresentar perfis.

**Consulta de progressão entre dois usuários:** duas consultas separadas na B+ primária de treinos (uma por usuário), iteração paralela nas folhas encadeadas filtrando pelo exercício de interesse, apresentação comparativa dos resultados.

**Envio de mensagem em grupo:** verificação de pertinência ao grupo via B+ de membros (id_grupo, id_usuario), escrita do registro no arquivo de mensagens, inserção na B+ de mensagens com chave (id_conversa, timestamp).

---

## 5. Sumário das Estruturas e Justificativas

Para consolidar, segue a lista final das estruturas que comporão o esqueleto da aplicação, cada uma com sua justificativa em uma linha.

Arquivo de usuários com registros variáveis, pela presença de campos textuais livres. Árvore B+ primária sobre id_usuario, para resolução rápida de identificadores e suporte a varredura. Arquivo de sessões com registros variáveis, mantido do projeto base. Árvore B+ sobre (id_usuario, id_exercicio, data), para a consulta crítica de progressão e suas extensões. Hash table de PRs, para consulta exata por chave composta em tempo constante. Arquivo de academias com registros variáveis. PR-Quadtree espacial sobre coordenadas, para a consulta espacial por proximidade. Hash table sobre id_academia, para resolução de identificadores devolvidos pela quadtree. Arquivo de check-ins com registros fixos. Árvore B+ sobre (id_academia, timestamp), para reconstrução de séries temporais. Matriz fixa de ocupação agregada por academia, para resposta imediata sobre horários de pico. Conjunto de Árvores B+ secundárias por atributo de perfil, para busca multi-atributo. Arquivo de adjacências do grafo social com lista por usuário. Arquivo de grupos com B+ sobre (id_grupo, id_usuario), para pertinência rápida. Arquivo de mensagens com registros variáveis. Árvore B+ sobre (id_conversa, timestamp), para reconstrução cronológica de conversas. Pequenos arquivos de ranking materializado por academia, para consultas instantâneas de top-N.

No total, o sistema integra duas árvores B+ primárias, cinco árvores B+ secundárias ou auxiliares, duas a três hash tables, uma PR-Quadtree, listas de adjacências em arquivo e estruturas agregadas materializadas. Esta diversidade cobre todos os padrões de consulta identificados e exercita praticamente todo o repertório da disciplina, sem extrapolar para estruturas fora do escopo.

---

Quer que eu desenhe um diagrama visual dessas interligações, detalhe a especificação binária dos registros de cada arquivo ou redija um cronograma de implementação ajustado para esse novo escopo expandido?