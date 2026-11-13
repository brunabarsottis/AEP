#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#define MAX_ACOES 100
#define MAX_USUARIOS 20
#define TAM_TEXTO 100
#define TAM_DESCRICAO 150

#define SIT_PLANEJADA 1
#define SIT_REALIZADA 2
#define SIT_CANCELADA 3

#define NIVEL_BASICO 1
#define NIVEL_INTERMEDIARIO 2
#define NIVEL_SUPERIOR 3

#define NOME_ARQUIVO "dados_pse.bin"
#define NOME_ARQUIVO_USUARIOS "usuarios_pse.bin"

typedef struct {
    int codigo;
    char escola[TAM_TEXTO];
    char tema[TAM_TEXTO];
    char acao_pse[TAM_DESCRICAO];
    char responsavel[TAM_TEXTO];
    char data_prevista[TAM_TEXTO]; // Adicionado conforme RF5
    char publico_alvo[TAM_TEXTO];  // Adicionado conforme RF5
    int participantes_previstos;
    int participantes_reais;
    int situacao;
} AcaoPSE;

typedef struct {
    char usuario[TAM_TEXTO];
    char senha[TAM_TEXTO];
    int nivel; // 1: Basico, 2: Intermediario, 3: Superior
} Usuario;

AcaoPSE banco_acoes[MAX_ACOES];
int total_acoes = 0;

Usuario banco_usuarios[MAX_USUARIOS];
int total_usuarios = 0;
Usuario *usuario_logado = NULL;

void limpar_buffer(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

/* Lê um inteiro da linha inteira. Retorna 1 se válido, 0 se inválido, -1 em EOF. */
int ler_inteiro(int *valor) {
    char linha[64];
    char *fim;

    if (fgets(linha, sizeof(linha), stdin) == NULL) {
        return -1;
    }
    if (strchr(linha, '\n') == NULL) {
        limpar_buffer();
        return 0;
    }

    errno = 0;
    long numero = strtol(linha, &fim, 10);
    if (errno == ERANGE || numero < INT_MIN || numero > INT_MAX) return 0;
    if (fim == linha) {
        return 0;
    }
    while (*fim == ' ' || *fim == '\t' || *fim == '\n' || *fim == '\r') {
        fim++;
    }
    if (*fim != '\0') {
        return 0;
    }

    *valor = (int)numero;
    return 1;
}

/* Lê um texto não vazio; repete até ser válido. Retorna 0 em EOF. */
int ler_texto_obrigatorio(const char *rotulo, char *destino, int tamanho) {
    while (1) {
        printf("%s", rotulo);
        if (fgets(destino, tamanho, stdin) == NULL) {
            return 0;
        }

        if (strchr(destino, '\n') == NULL) {
            limpar_buffer();
            printf("[ERRO] Texto muito longo (maximo %d caracteres).\n", tamanho - 2);
            continue;
        }

        destino[strcspn(destino, "\r\n")] = '\0';

        int tem_conteudo = 0;
        for (int i = 0; destino[i] != '\0'; i++) {
            if (destino[i] != ' ' && destino[i] != '\t') {
                tem_conteudo = 1;
                break;
            }
        }

        if (tem_conteudo) {
            return 1;
        }
        printf("[ERRO] Este campo nao pode ficar vazio.\n");
    }
}

/* Lê um inteiro >= minimo; repete até ser válido. Retorna 0 em EOF. */
int ler_inteiro_minimo(const char *rotulo, int minimo, int *valor) {
    while (1) {
        printf("%s", rotulo);
        int resultado = ler_inteiro(valor);
        if (resultado == -1) {
            return 0;
        }
        if (resultado == 1 && *valor >= minimo) {
            return 1;
        }
        printf("[ERRO] Digite um numero inteiro maior ou igual a %d.\n", minimo);
    }
}

const char *nome_situacao(int situacao) {
    switch (situacao) {
        case SIT_PLANEJADA: return "Planejada";
        case SIT_REALIZADA: return "Realizada";
        case SIT_CANCELADA: return "Cancelada";
        default: return "Desconhecida";
    }
}

const char *nome_nivel(int nivel) {
    switch (nivel) {
        case NIVEL_BASICO: return "Basico";
        case NIVEL_INTERMEDIARIO: return "Intermediario";
        case NIVEL_SUPERIOR: return "Superior";
        default: return "Desconhecido";
    }
}

int buscar_acao_por_codigo(int codigo) {
    for (int i = 0; i < total_acoes; i++) {
        if (banco_acoes[i].codigo == codigo) {
            return i;
        }
    }
    return -1;
}

int solicitar_acao_existente(void) {
    int codigo;
    printf("Digite o codigo da acao: ");
    if (ler_inteiro(&codigo) != 1 || codigo <= 0) {
        printf("[ERRO] Codigo invalido! Use um numero inteiro positivo.\n");
        return -1;
    }

    int indice = buscar_acao_por_codigo(codigo);
    if (indice == -1) {
        printf("[ERRO] Registro inexistente para o codigo %d.\n", codigo);
    }
    return indice;
}

void exibir_acao(const AcaoPSE *acao) {
    printf("----------------------------------------\n");
    printf("Codigo: %d\n", acao->codigo);
    printf("Escola: %s\n", acao->escola);
    printf("Tema: %s\n", acao->tema);
    printf("Acao: %s\n", acao->acao_pse);
    printf("Responsavel: %s\n", acao->responsavel);
    printf("Data Prevista: %s\n", acao->data_prevista);
    printf("Publico-Alvo: %s\n", acao->publico_alvo);
    printf("Previstos: %d | Realizados: %d\n",
            acao->participantes_previstos, acao->participantes_reais);
    printf("Situacao: %s\n", nome_situacao(acao->situacao));
}

/* Permissoes verificadas tambem dentro das operacoes. */
int exigir_nivel(int minimo) {
    if (!usuario_logado || usuario_logado->nivel < minimo ||
        usuario_logado->nivel > NIVEL_SUPERIOR) {
        puts("[ERRO] Acesso negado.");
        return 0;
    }
    return 1;
}

/* Mesmo formato legado: int quantidade, seguido das estruturas originais.
 * Primeiro grava temporario; preserva a versao anterior em .bak.
 * Em erro, o chamador desfaz a alteracao em memoria. */
int salvar_registros(const char *nome, const void *dados, size_t tamanho, int total) {
    char temporario[128], backup[128];
    FILE *arquivo, *existente;
    int ok, havia_original = 0;
    snprintf(temporario, sizeof(temporario), "%s.tmp", nome);
    snprintf(backup, sizeof(backup), "%s.bak", nome);
    arquivo = fopen(temporario, "wb");
    if (!arquivo) goto erro;
    ok = fwrite(&total, sizeof(total), 1, arquivo) == 1;
    if (ok && total > 0)
        ok = fwrite(dados, tamanho, (size_t)total, arquivo) == (size_t)total;
    if (fflush(arquivo) != 0) ok = 0;
    if (fclose(arquivo) != 0) ok = 0;
    if (!ok) { remove(temporario); goto erro; }
    errno = 0;
    existente = fopen(nome, "rb");
    if (existente) {
        fclose(existente);
        havia_original = 1;
        if (remove(backup) != 0 && errno != ENOENT) goto erro;
        if (rename(nome, backup) != 0) goto erro;
    } else if (errno != ENOENT) goto erro;
    if (rename(temporario, nome) != 0) {
        if (havia_original && rename(backup, nome) != 0)
            printf("[ERRO] Restaure manualmente %s para %s.\n", backup, nome);
        goto erro;
    }
    return 1;
erro:
    printf("[ERRO] Nao foi possivel salvar %s. Alteracao cancelada.\n", nome);
    return 0;
}

/* Retornos: 1 lido, 0 ausente, -1 erro. Nunca regrava arquivo invalido. */
int carregar_registros(const char *nome, void *dados, size_t tamanho, int limite, int *total) {
    FILE *arquivo;
    int n, ok;
    char backup[128];
    errno = 0;
    arquivo = fopen(nome, "rb");
    if (!arquivo) {
        if (errno != ENOENT) return -1;
        snprintf(backup, sizeof(backup), "%s.bak", nome);
        errno = 0;
        arquivo = fopen(backup, "rb");
        if (arquivo) {
            fclose(arquivo);
            printf("[ERRO] Arquivo principal ausente. Restaure %s para %s.\n", backup, nome);
            return -1;
        }
        if (errno != ENOENT) return -1;
        *total = 0;
        return 0;
    }
    ok = fread(&n, sizeof(n), 1, arquivo) == 1;
    if (ok) ok = n >= 0 && n <= limite;
    if (ok && n > 0) ok = fread(dados, tamanho, (size_t)n, arquivo) == (size_t)n;
    if (ok) ok = fgetc(arquivo) == EOF && !ferror(arquivo);
    if (fclose(arquivo) != 0) ok = 0;
    if (!ok) {
        printf("[ERRO] Arquivo %s incompleto ou incompativel; preservado.\n", nome);
        return -1;
    }
    *total = n;
    return 1;
}

int texto_valido(const char *texto, size_t tamanho) {
    return texto[0] != '\0' && memchr(texto, '\0', tamanho) != NULL;
}

int salvar_usuarios(void) {
    return salvar_registros(NOME_ARQUIVO_USUARIOS, banco_usuarios, sizeof(Usuario), total_usuarios);
}

int buscar_usuario(const char *nome) {
    for (int i = 0; i < total_usuarios; ++i)
        if (strcmp(banco_usuarios[i].usuario, nome) == 0) return i;
    return -1;
}

int contar_superiores(void) {
    int total = 0;
    for (int i = 0; i < total_usuarios; ++i)
        if (banco_usuarios[i].nivel == NIVEL_SUPERIOR) ++total;
    return total;
}

int carregar_usuarios(void) {
    int resultado = carregar_registros(NOME_ARQUIVO_USUARIOS, banco_usuarios,
                                      sizeof(Usuario), MAX_USUARIOS, &total_usuarios);
    if (resultado < 0) return 0;
    if (resultado == 0) {
        Usuario admin;
        memset(&admin, 0, sizeof(admin));
        puts("Primeira execucao: configure o administrador Superior.");
        if (!ler_texto_obrigatorio("Usuario administrador: ", admin.usuario, sizeof(admin.usuario)) ||
            !ler_texto_obrigatorio("Senha do administrador: ", admin.senha, sizeof(admin.senha))) return 0;
        admin.nivel = NIVEL_SUPERIOR;
        banco_usuarios[0] = admin;
        total_usuarios = 1;
        return salvar_usuarios();
    }
    for (int i = 0; i < total_usuarios; ++i) {
        Usuario *u = &banco_usuarios[i];
        if (!texto_valido(u->usuario, sizeof(u->usuario)) ||
            !texto_valido(u->senha, sizeof(u->senha)) ||
            u->nivel < NIVEL_BASICO || u->nivel > NIVEL_SUPERIOR) {
            puts("[ERRO] Cadastro de usuarios invalido; arquivo preservado.");
            return 0;
        }
        for (int j = 0; j < i; ++j)
            if (strcmp(u->usuario, banco_usuarios[j].usuario) == 0) {
                puts("[ERRO] Usuarios duplicados no arquivo legado; corrija uma copia antes de continuar.");
                return 0;
            }
    }
    if (!contar_superiores())
        puts("[AVISO] Base sem Superior. Administracao indisponivel; restaure uma copia valida de usuarios.");
    return 1;
}

int ler_nivel(int *nivel) {
    puts("1 - Basico | 2 - Intermediario | 3 - Superior");
    printf("Nivel: ");
    if (ler_inteiro(nivel) != 1 || *nivel < NIVEL_BASICO || *nivel > NIVEL_SUPERIOR) {
        puts("[ERRO] Nivel invalido.");
        return 0;
    }
    return 1;
}

void cadastrar_usuario_impl(int avancado) {
    Usuario novo;
    memset(&novo, 0, sizeof(novo));
    if (avancado && !exigir_nivel(NIVEL_SUPERIOR)) return;
    if (total_usuarios >= MAX_USUARIOS) { puts("[ERRO] Limite de usuarios atingido."); return; }
    puts(avancado ? "--- CADASTRO AVANCADO ---" : "--- CADASTRO PUBLICO: SOMENTE BASICO ---");
    if (!ler_texto_obrigatorio("Nome de usuario: ", novo.usuario, sizeof(novo.usuario))) return;
    if (buscar_usuario(novo.usuario) >= 0) { puts("[ERRO] Usuario ja cadastrado."); return; }
    if (!ler_texto_obrigatorio("Senha: ", novo.senha, sizeof(novo.senha))) return;
    novo.nivel = NIVEL_BASICO;
    if (avancado && !ler_nivel(&novo.nivel)) return;
    banco_usuarios[total_usuarios++] = novo;
    if (!salvar_usuarios()) { --total_usuarios; memset(&banco_usuarios[total_usuarios], 0, sizeof(Usuario)); return; }
    printf("[SUCESSO] Usuario cadastrado: %s (%s).\n", novo.usuario, nome_nivel(novo.nivel));
}

void cadastrar_usuario(void) { cadastrar_usuario_impl(0); }
void cadastrar_usuario_avancado(void) { cadastrar_usuario_impl(1); }

void gerenciar_niveis(void) {
    char nome[TAM_TEXTO];
    int indice, nivel, anterior;
    if (!exigir_nivel(NIVEL_SUPERIOR)) return;
    puts("--- USUARIOS E NIVEIS ---");
    for (int i = 0; i < total_usuarios; ++i)
        printf("%s - %s\n", banco_usuarios[i].usuario, nome_nivel(banco_usuarios[i].nivel));
    if (!ler_texto_obrigatorio("Usuario a alterar: ", nome, sizeof(nome))) return;
    indice = buscar_usuario(nome);
    if (indice < 0) { puts("[ERRO] Usuario inexistente."); return; }
    if (!ler_nivel(&nivel)) return;
    anterior = banco_usuarios[indice].nivel;
    if (anterior == NIVEL_SUPERIOR && nivel != NIVEL_SUPERIOR && contar_superiores() == 1) {
        puts("[ERRO] Nao e permitido rebaixar o ultimo Superior."); return;
    }
    banco_usuarios[indice].nivel = nivel;
    if (!salvar_usuarios()) { banco_usuarios[indice].nivel = anterior; return; }
    puts("[SUCESSO] Nivel salvo. Permissoes atualizadas imediatamente.");
}

int realizar_login(void) {
    char user[TAM_TEXTO];
    char senha[TAM_TEXTO];

    printf("\n=== LOGIN NO SISTEMA PSE ===\n");
    if (!ler_texto_obrigatorio("Usuario: ", user, sizeof(user)) ||
        !ler_texto_obrigatorio("Senha: ", senha, sizeof(senha))) {
        return 0;
    }

    for (int i = 0; i < total_usuarios; i++) {
        if (strcmp(banco_usuarios[i].usuario, user) == 0 && strcmp(banco_usuarios[i].senha, senha) == 0) {
            usuario_logado = &banco_usuarios[i];
            printf("[SUCESSO] Bem-vindo, %s! (Perfil: %s)\n", usuario_logado->usuario, nome_nivel(usuario_logado->nivel));
            return 1;
        }
    }
    printf("[ERRO] Usuario ou senha incorretos.\n");
    return 0;
}

/* Funções do Sistema PSE */
void cadastrar_acao(void) {
    if (!exigir_nivel(NIVEL_BASICO)) return;

    printf("\n--- CADASTRO DE ACAO PSE ---\n");
    if (total_acoes >= MAX_ACOES) {
        printf("[ERRO] Limite maximo de acoes atingido (%d)!\n", MAX_ACOES);
        return;
    }

    int codigo;
    printf("Digite o codigo da acao: ");
    if (ler_inteiro(&codigo) != 1 || codigo <= 0) {
        printf("[ERRO] Codigo invalido! Use um numero inteiro positivo.\n");
        return;
    }
    if (buscar_acao_por_codigo(codigo) != -1) {
        printf("[ERRO] Ja existe uma acao cadastrada com o codigo %d!\n", codigo);
        return;
    }

    AcaoPSE nova = {0};
    nova.codigo = codigo;

    if (!ler_texto_obrigatorio("Nome da Escola: ", nova.escola, sizeof(nova.escola)) ||
        !ler_texto_obrigatorio("Tema (ex: Saude Mental): ", nova.tema, sizeof(nova.tema)) ||
        !ler_texto_obrigatorio("Descricao da Acao PSE: ", nova.acao_pse, sizeof(nova.acao_pse)) ||
        !ler_texto_obrigatorio("Responsavel: ", nova.responsavel, sizeof(nova.responsavel)) ||
        !ler_texto_obrigatorio("Data Prevista (ex: 20/10/2026): ", nova.data_prevista, sizeof(nova.data_prevista)) ||
        !ler_texto_obrigatorio("Publico-Alvo (ex: Alunos do Ensino Medio): ", nova.publico_alvo, sizeof(nova.publico_alvo)) ||
        !ler_inteiro_minimo("Quantidade de participantes previstos: ", 0, &nova.participantes_previstos)) {
        printf("\n[ERRO] Entrada encerrada. Cadastro cancelado.\n");
        return;
    }

    nova.participantes_reais = 0;
    nova.situacao = SIT_PLANEJADA;

    banco_acoes[total_acoes] = nova;
    total_acoes++;

    printf("[INFO] Nova acao preparada.\n");
}

void listar_acoes(void) {
    if (!exigir_nivel(NIVEL_BASICO)) return;
    printf("\n--- LISTA DE ACOES PSE CADASTRADAS ---\n");
    if (total_acoes == 0) {
        printf("Nenhuma acao cadastrada no sistema.\n");
        return;
    }

    for (int i = 0; i < total_acoes; i++) {
        exibir_acao(&banco_acoes[i]);
    }
}

/* Pesquisa expandida por Código, Escola ou Tema (Conforme RF4) */
void pesquisar_acao(void) {
    if (!exigir_nivel(NIVEL_BASICO)) return;
    printf("\n--- PESQUISAR ACAO ---\n");
    printf("Escolher criterio de busca:\n");
    printf("1 - Por Codigo\n");
    printf("2 - Por Escola\n");
    printf("3 - Por Tema\n");
    printf("Opcao: ");

    int opcao_busca;
    if (ler_inteiro(&opcao_busca) != 1) return;

    if (opcao_busca == 1) {
        int indice = solicitar_acao_existente();
        if (indice != -1) {
            printf("\n[REGISTRO ENCONTRADO]\n");
            exibir_acao(&banco_acoes[indice]);
        }
    } else if (opcao_busca == 2 || opcao_busca == 3) {
        char termo[TAM_TEXTO];
        if (!ler_texto_obrigatorio(opcao_busca == 2 ? "Digite o nome da escola: " : "Digite o tema: ", termo, sizeof(termo))) {
            return;
        }

        int encontrados = 0;
        for (int i = 0; i < total_acoes; i++) {
            int match = 0;
            if (opcao_busca == 2 && strstr(banco_acoes[i].escola, termo) != NULL) match = 1;
            if (opcao_busca == 3 && strstr(banco_acoes[i].tema, termo) != NULL) match = 1;

            if (match) {
                exibir_acao(&banco_acoes[i]);
                encontrados++;
            }
        }
        if (encontrados == 0) {
            printf("[INFO] Nenhuma acao encontrada para o termo informado.\n");
        } else {
            printf("[INFO] Total de registros encontrados: %d\n", encontrados);
        }
    } else {
        printf("[ERRO] Opcao invalida.\n");
    }
}

void atualizar_situacao(void) {
    if (!exigir_nivel(NIVEL_INTERMEDIARIO)) return;

    printf("\n--- ATUALIZAR SITUACAO DA ACAO ---\n");
    int indice = solicitar_acao_existente();
    if (indice == -1) return;

    printf("Situacao atual: %s\n", nome_situacao(banco_acoes[indice].situacao));
    printf("Escolha a nova situacao:\n1 - Planejada\n2 - Realizada\n3 - Cancelada\nOpcao: ");

    int nova_situacao;
    if (ler_inteiro(&nova_situacao) != 1 || nova_situacao < SIT_PLANEJADA || nova_situacao > SIT_CANCELADA) {
        printf("[ERRO] Opcao invalida!\n");
        return;
    }

    banco_acoes[indice].situacao = nova_situacao;
    if (nova_situacao != SIT_REALIZADA) {
        banco_acoes[indice].participantes_reais = 0;
    }
    printf("[INFO] Nova situacao: %s\n", nome_situacao(nova_situacao));
}

void registrar_participantes(void) {
    if (!exigir_nivel(NIVEL_INTERMEDIARIO)) return;

    printf("\n--- REGISTRAR PARTICIPANTES REALIZADOS ---\n");
    int indice = solicitar_acao_existente();
    if (indice == -1) return;

    AcaoPSE *acao = &banco_acoes[indice];
    if (acao->situacao == SIT_CANCELADA) {
        printf("[ERRO] Nao e possivel registrar participantes em acao cancelada.\n");
        return;
    }

    printf("Digite a quantidade de participantes reais: ");
    int quantidade;
    if (ler_inteiro(&quantidade) != 1 || quantidade < 0) {
        printf("[ERRO] Quantidade invalida!\n");
        return;
    }

    acao->participantes_reais = quantidade;
    if (acao->situacao == SIT_PLANEJADA) {
        acao->situacao = SIT_REALIZADA;
        printf("[INFO] Situacao alterada automaticamente para Realizada.\n");
    }
    printf("[INFO] Novos participantes: %d\n", quantidade);
}

void gerar_resumo(void) {
    if (!exigir_nivel(NIVEL_BASICO)) return;
    printf("\n========================================\n");
    printf("        RELATORIO E RESUMO GERAL PSE     \n");
    printf("========================================\n");

    int planejadas = 0, realizadas = 0, canceladas = 0;
    double total_previsto = 0, total_realizado = 0;

    for (int i = 0; i < total_acoes; i++) {
        if (banco_acoes[i].situacao == SIT_PLANEJADA) planejadas++;
        else if (banco_acoes[i].situacao == SIT_REALIZADA) realizadas++;
        else if (banco_acoes[i].situacao == SIT_CANCELADA) canceladas++;

        if (banco_acoes[i].situacao != SIT_CANCELADA) {
            total_previsto += banco_acoes[i].participantes_previstos;
            total_realizado += banco_acoes[i].participantes_reais;
        }
    }

    float percentual = 0.0f;
    if (total_previsto > 0) {
        percentual = ((float)total_realizado / total_previsto) * 100.0f;
    }

    printf("Total de Acoes Cadastradas: %d\n", total_acoes);
    printf(" - Planejadas: %d\n", planejadas);
    printf(" - Realizadas: %d\n", realizadas);
    printf(" - Canceladas: %d\n", canceladas);
    printf("----------------------------------------\n");
    printf("Participantes Previstos (sem canceladas): %.0f\n", total_previsto);
    printf("Participantes Reais:                      %.0f\n", total_realizado);
    printf("Percentual de Atingimento:                %.2f%%\n", percentual);
    printf("========================================\n");
}

int salvar_dados(void) {
    return salvar_registros(NOME_ARQUIVO, banco_acoes, sizeof(AcaoPSE), total_acoes);
}

int carregar_dados(void) {
    int r = carregar_registros(NOME_ARQUIVO, banco_acoes, sizeof(AcaoPSE), MAX_ACOES, &total_acoes);
    if (r < 0) return 0;
    for (int i = 0; i < total_acoes; ++i) {
        AcaoPSE *a = &banco_acoes[i];
        if (a->codigo <= 0 || !texto_valido(a->escola, sizeof(a->escola)) ||
            !texto_valido(a->tema, sizeof(a->tema)) || !texto_valido(a->acao_pse, sizeof(a->acao_pse)) ||
            !texto_valido(a->responsavel, sizeof(a->responsavel)) ||
            !texto_valido(a->data_prevista, sizeof(a->data_prevista)) ||
            !texto_valido(a->publico_alvo, sizeof(a->publico_alvo)) ||
            a->participantes_previstos < 0 || a->participantes_reais < 0 ||
            a->situacao < SIT_PLANEJADA || a->situacao > SIT_CANCELADA) {
            puts("[ERRO] Dados de acoes invalidos; arquivo preservado."); return 0;
        }
        for (int j = 0; j < i; ++j)
            if (a->codigo == banco_acoes[j].codigo) {
                puts("[ERRO] Codigos duplicados; arquivo preservado."); return 0;
            }
    }
    return 1;
}

void editar_acao(void) {
    int indice, campo, numero;
    AcaoPSE nova;
    if (!exigir_nivel(NIVEL_INTERMEDIARIO)) return;
    indice = solicitar_acao_existente();
    if (indice < 0) return;
    nova = banco_acoes[indice];
    exibir_acao(&nova);
    puts("Editar campo: 1 Codigo | 2 Escola | 3 Tema | 4 Descricao | 5 Responsavel");
    puts("6 Data prevista | 7 Publico-alvo | 8 Participantes previstos");
    puts("9 Participantes reais | 10 Situacao | 0 Cancelar");
    printf("Campo: ");
    if (ler_inteiro(&campo) != 1 || campo == 0) return;
    switch (campo) {
        case 1:
            if (!ler_inteiro_minimo("Novo codigo: ", 1, &numero)) return;
            if (numero != nova.codigo && buscar_acao_por_codigo(numero) >= 0) {
                puts("[ERRO] Codigo ja cadastrado."); return;
            }
            nova.codigo = numero; break;
        case 2: if (!ler_texto_obrigatorio("Escola: ", nova.escola, sizeof(nova.escola))) return; break;
        case 3: if (!ler_texto_obrigatorio("Tema: ", nova.tema, sizeof(nova.tema))) return; break;
        case 4: if (!ler_texto_obrigatorio("Descricao: ", nova.acao_pse, sizeof(nova.acao_pse))) return; break;
        case 5: if (!ler_texto_obrigatorio("Responsavel: ", nova.responsavel, sizeof(nova.responsavel))) return; break;
        case 6: if (!ler_texto_obrigatorio("Data prevista: ", nova.data_prevista, sizeof(nova.data_prevista))) return; break;
        case 7: if (!ler_texto_obrigatorio("Publico-alvo: ", nova.publico_alvo, sizeof(nova.publico_alvo))) return; break;
        case 8: if (!ler_inteiro_minimo("Participantes previstos: ", 0, &nova.participantes_previstos)) return; break;
        case 9:
            if (nova.situacao == SIT_CANCELADA) { puts("[ERRO] Acao cancelada."); return; }
            if (!ler_inteiro_minimo("Participantes reais: ", 0, &nova.participantes_reais)) return;
            nova.situacao = SIT_REALIZADA; break;
        case 10:
            printf("1 Planejada | 2 Realizada | 3 Cancelada: ");
            if (ler_inteiro(&numero) != 1 || numero < SIT_PLANEJADA || numero > SIT_CANCELADA) {
                puts("[ERRO] Situacao invalida."); return;
            }
            nova.situacao = numero;
            if (numero != SIT_REALIZADA) nova.participantes_reais = 0;
            break;
        default: puts("[ERRO] Campo invalido."); return;
    }
    banco_acoes[indice] = nova;
}

void excluir_acao(void) {
    int indice, confirmar;
    if (!exigir_nivel(NIVEL_SUPERIOR)) return;
    indice = solicitar_acao_existente();
    if (indice < 0) return;
    exibir_acao(&banco_acoes[indice]);
    printf("Excluir esta acao? 1 = Sim, 0 = Cancelar: ");
    if (ler_inteiro(&confirmar) != 1 || confirmar != 1) return;
    for (int i = indice; i < total_acoes - 1; ++i) banco_acoes[i] = banco_acoes[i + 1];
    --total_acoes;
    memset(&banco_acoes[total_acoes], 0, sizeof(AcaoPSE));
}

void exibir_menu(void) {
    if (!exigir_nivel(NIVEL_BASICO)) return;
    puts("\n=== SISTEMA PROGRAMA SAUDE NA ESCOLA (PSE) ===");
    printf("Logado como: %s [Nivel: %s]\n", usuario_logado->usuario, nome_nivel(usuario_logado->nivel));
    puts("1. Cadastrar Acao PSE\n2. Listar Acoes\n3. Pesquisar Acoes\n6. Gerar Relatorio");
    if (usuario_logado->nivel >= NIVEL_INTERMEDIARIO)
        puts("4. Atualizar Situacao\n5. Registrar Participantes\n7. Editar qualquer campo da Acao");
    if (usuario_logado->nivel == NIVEL_SUPERIOR)
        puts("8. Excluir Acao\n9. Cadastrar Usuario Avancado\n10. Listar Usuarios e Alterar Niveis");
    puts("11. Trocar Usuario\n0. Sair");
    printf("Escolha: ");
}

/* Centraliza a transacao das acoes: salva a cada mudanca, desfaz em erro. */
void executar_opcao(int opcao) {
    AcaoPSE anterior[MAX_ACOES];
    int quantidade = total_acoes;
    memcpy(anterior, banco_acoes, sizeof(anterior));
    switch (opcao) {
        case 1: cadastrar_acao(); break;
        case 2: listar_acoes(); break;
        case 3: pesquisar_acao(); break;
        case 4: atualizar_situacao(); break;
        case 5: registrar_participantes(); break;
        case 6: gerar_resumo(); break;
        case 7: editar_acao(); break;
        case 8: excluir_acao(); break;
        case 9: cadastrar_usuario_avancado(); break;
        case 10: gerenciar_niveis(); break;
        case 11: usuario_logado = NULL; break;
        default: puts("[ERRO] Opcao invalida."); break;
    }
    if (quantidade != total_acoes || memcmp(anterior, banco_acoes, sizeof(anterior)) != 0) {
        if (salvar_dados()) puts("[SUCESSO] Alteracao salva.");
        else { memcpy(banco_acoes, anterior, sizeof(anterior)); total_acoes = quantidade; }
    }
}

int main(void) {
    int opcao, resultado;
    if (!carregar_dados() || !carregar_usuarios()) {
        puts("[ERRO] Inicializacao interrompida. Verifique os arquivos de dados.");
        return EXIT_FAILURE;
    }
    for (;;) {
        if (!usuario_logado) {
            puts("\n=== AUTENTICACAO ===\n1. Fazer Login\n2. Cadastro Publico (Basico)\n0. Sair");
            printf("Escolha: ");
        } else exibir_menu();
        resultado = ler_inteiro(&opcao);
        if (resultado == -1) break;
        if (!resultado) { puts("[ERRO] Entrada invalida."); continue; }
        if (opcao == 0) break;
        if (!usuario_logado) {
            if (opcao == 1) realizar_login();
            else if (opcao == 2) cadastrar_usuario();
            else puts("[ERRO] Opcao invalida.");
        } else executar_opcao(opcao);
    }
    puts("Sistema encerrado.");
    return EXIT_SUCCESS;
}
