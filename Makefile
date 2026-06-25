# Compilador e flags
CC := gcc
CSTD := -std=c11
WARNINGS := -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Wconversion
DEBUG_FLAGS := -g -O0 -DDEBUG
RELEASE_FLAGS := -O2 -DNDEBUG
SANITIZE_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

# Diretórios
SRC_DIR := src
INC_DIR := include
TEST_DIR := tests
TOOLS_DIR := tools
BUILD_DIR := build

# Flags de inclusão
INCLUDES := -I$(INC_DIR)

# Modo padrão: debug
MODE ?= debug

# Validação do modo
VALID_MODES := debug release asan
ifeq ($(filter $(MODE),$(VALID_MODES)),)
    $(error MODE invalido: '$(MODE)'. Use um de: $(VALID_MODES))
endif

# Diretórios específicos do modo
MODE_DIR := $(BUILD_DIR)/$(MODE)
OBJ_DIR := $(MODE_DIR)/obj
BIN_DIR := $(MODE_DIR)/bin

# Flags por modo
ifeq ($(MODE),release)
    CFLAGS := $(CSTD) $(WARNINGS) $(RELEASE_FLAGS) $(INCLUDES)
    LDFLAGS :=
else ifeq ($(MODE),asan)
    CFLAGS := $(CSTD) $(WARNINGS) $(DEBUG_FLAGS) $(SANITIZE_FLAGS) $(INCLUDES)
    LDFLAGS := $(SANITIZE_FLAGS)
else
    CFLAGS := $(CSTD) $(WARNINGS) $(DEBUG_FLAGS) $(INCLUDES)
    LDFLAGS :=
endif

# Descoberta automática de fontes
SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

UNITY_TEST_SRCS := $(shell grep -rl '"unity/unity\.h"' $(wildcard $(TEST_DIR)/*.c) 2>/dev/null)
UNITY_TEST_BINS := $(patsubst $(TEST_DIR)/%.c,$(BIN_DIR)/test_%,$(UNITY_TEST_SRCS))

SIMPLE_TEST_SRCS := $(filter-out $(UNITY_TEST_SRCS),$(wildcard $(TEST_DIR)/*.c))
SIMPLE_TEST_BINS := $(patsubst $(TEST_DIR)/%.c,$(BIN_DIR)/test_%,$(SIMPLE_TEST_SRCS))

TEST_BINS := $(UNITY_TEST_BINS) $(SIMPLE_TEST_BINS)

MAIN_BIN := $(BIN_DIR)/gymsocial

# Configuração da ferramenta de formatação
CLANG_FORMAT ?= clang-format

# Alvo padrão
.PHONY: all
all: $(MAIN_BIN)

# Compilação do executável principal
$(MAIN_BIN): $(OBJS) | $(BIN_DIR)
	@echo "  LD    $@"
	@$(CC) $(LDFLAGS) -o $@ $^

# Compilação de cada objeto
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compilação de testes com Unity
$(UNITY_TEST_BINS): $(BIN_DIR)/test_%: $(TEST_DIR)/%.c $(TEST_DIR)/unity/unity.c $(filter-out $(OBJ_DIR)/main.o,$(OBJS)) | $(BIN_DIR)
	@echo "  LD    $@"
	@$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

# Compilação de testes simples (sem Unity)
$(SIMPLE_TEST_BINS): $(BIN_DIR)/test_%: $(TEST_DIR)/%.c $(filter-out $(OBJ_DIR)/main.o,$(OBJS)) | $(BIN_DIR)
	@echo "  LD    $@"
	@$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

# Criação de diretórios
$(BIN_DIR) $(OBJ_DIR):
	@mkdir -p $@

# Alvos auxiliares
.PHONY: test
test: $(TEST_BINS)
	@echo "Executando testes (modo $(MODE))..."
	@failed=0; \
	for t in $(TEST_BINS); do \
		echo "--- $$t ---"; \
		if ! $$t; then failed=1; fi; \
	done; \
	if [ $$failed -eq 0 ]; then \
		echo "Todos os testes passaram."; \
	else \
		echo "Falhas em pelo menos um teste."; \
		exit 1; \
	fi

.PHONY: test-asan
test-asan:
	@$(MAKE) --no-print-directory MODE=asan test

.PHONY: release
release:
	@$(MAKE) --no-print-directory MODE=release all

.PHONY: format
format:
	@command -v $(CLANG_FORMAT) >/dev/null 2>&1 || { \
		echo "Erro: $(CLANG_FORMAT) nao encontrado. Instale com: sudo apt install clang-format"; \
		exit 1; \
	}
	@echo "Formatando codigo..."
	@find $(SRC_DIR) $(INC_DIR) $(TEST_DIR) $(TOOLS_DIR) \
		\( -name '*.c' -o -name '*.h' \) -print0 | \
		xargs -0 -r $(CLANG_FORMAT) -i

.PHONY: format-check
format-check:
	@command -v $(CLANG_FORMAT) >/dev/null 2>&1 || { \
		echo "Erro: $(CLANG_FORMAT) nao encontrado. Instale com: sudo apt install clang-format"; \
		exit 1; \
	}
	@echo "Verificando formatacao..."
	@find $(SRC_DIR) $(INC_DIR) $(TEST_DIR) $(TOOLS_DIR) \
		\( -name '*.c' -o -name '*.h' \) -print0 | \
		xargs -0 -r $(CLANG_FORMAT) --dry-run --Werror

.PHONY: lint
lint:
	@command -v cppcheck >/dev/null 2>&1 || { \
		echo "Erro: cppcheck nao encontrado. Instale com: sudo apt install cppcheck"; \
		exit 1; \
	}
	@echo "Executando analise estatica..."
	@cppcheck --enable=all --suppress=missingIncludeSystem \
		--suppress=toomanyconfigs --suppress=checkersReport \
		--suppress=unmatchedSuppression \
		--inline-suppr --error-exitcode=1 \
		-I$(INC_DIR) $(SRC_DIR) $(wildcard $(TEST_DIR)/*.c)

.PHONY: compile-db
compile-db:
	@command -v bear >/dev/null 2>&1 || { \
		echo "Erro: bear nao encontrado. Instale com: sudo apt install bear"; \
		exit 1; \
	}
	@$(MAKE) clean
	@bear -- $(MAKE)
	@echo "compile_commands.json gerado."

.PHONY: clean
clean:
	@echo "Removendo artefatos de build..."
	@rm -rf $(BUILD_DIR)

.PHONY: help
help:
	@echo "Alvos disponiveis:"
	@echo "  make              - compila em modo debug (padrao)"
	@echo "  make release      - compila otimizado"
	@echo "  make test         - roda os testes em modo debug"
	@echo "  make test-asan    - roda os testes com sanitizers"
	@echo "  make format       - formata todo o codigo com clang-format"
	@echo "  make format-check - verifica formatacao sem alterar"
	@echo "  make lint         - analise estatica com cppcheck"
	@echo "  make compile-db   - gera compile_commands.json para o IntelliSense"
	@echo "  make clean        - remove artefatos de build"
	@echo "  make help         - mostra esta mensagem"
	@echo ""
	@echo "Variaveis configuraveis:"
	@echo "  MODE              - debug | release | asan (atual: $(MODE))"
	@echo "  CLANG_FORMAT      - caminho para o clang-format (atual: $(CLANG_FORMAT))"
