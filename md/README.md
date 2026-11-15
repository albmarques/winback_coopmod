# Análise do Workspace: WinBack: Covert Operations (Recomp & Co-op Mod)

Este diretório reúne os componentes para a **recompilação estática nativa para PC** do jogo *WinBack: Covert Operations* (Nintendo 64, versão USA) e o desenvolvimento de um **mod cooperativo (Split-Screen Story Co-op)**.

---

## 1. Estrutura da Raiz (`game/winback/`)

| Item | Tipo | Descrição Resumida |
| :--- | :--- | :--- |
| `baserom.us.z64` | Arquivo ROM (16 MB) | ROM original de *WinBack: Covert Operations (USA)* no formato big-endian `.z64`. Hash SHA-1 verificado: `FA04477B321A95E1F7A2EDCAC5C09BFA6EF72041` (exatamente o hash exigido pelo projeto). |
| `WinBackRecomp-main/` | Diretório | Projeto principal **WinBack Recompiled (v0.3)**: código-fonte do port nativo para PC (Windows x64) via recompilação estática MIPS $\rightarrow$ C/C++. |
| `WinBackRecompiled-v0.3.0-Windows-x64/` | Diretório (Build Pronto) | Instalação portátil pronta para jogar do **WinBack Recompiled v0.3.0**, já configurada com a ROM (`winback.us.z64`), saves e o mod `winback_coop.nrm` (`v0.2.0`) habilitado em `mods/winback_coop.nrm`. |
| `winback_coop/` | Diretório | Código-fonte do mod **Story Co-op (`winback_coop` v0.1.0 - Etapa 1)** que adiciona um segundo jogador em tela dividida (split-screen) ao modo História. |
| `conversa.txt` | Texto (Log) | Histórico da conversa de desenvolvimento onde o mod `winback_coop` foi concebido, planejado em 5 etapas, compilado para `.nrm` (v0.1.0) e posteriormente expandido para a **v0.2.0** (HUD, Engineer, IA de inimigos e Fogo Amigo). |

> **Observação sobre aninhamento de pastas:** Ambos os diretórios possuem um nível extra de subpasta com o mesmo nome (`WinBackRecomp-main/WinBackRecomp-main/` e `winback_coop/winback_coop/`), típico de arquivos extraídos de pacotes `.zip`.

---

## 2. `WinBackRecomp-main` — Port Nativo para PC

Trata-se de uma recompilação estática completa da ROM americana de Nintendo 64 para executável nativo x64, sem uso de emulador tradicional.

### Arquitetura e Pipeline de Build
1. **Extração e Disassembly (`splat64`):** Divide a ROM (`rom/baserom.us.z64`) usando `config/winback.us.yaml` e desmonta o código MIPS.
2. **Identificação de Símbolos (`tools/sigmatch.py` & `gen_recomp_syms.py`):** Identifica funções de bibliotecas padrão (`libultra`, `libc`, `n_audio`, `libmus`) via assinaturas `n64sym` combinadas com `config/symbol_addrs.txt`.
3. **Tradução Estática (`N64Recomp` & `RSPRecomp`):** Converte todas as funções MIPS do jogo para código C em `generated/RecompiledFuncs/` e recompila o microcódigo de áudio (`aspMain` em `0xDAD50`) para `generated/rsp/aspMain.cpp`.
4. **Runtime e Renderização (`N64ModernRuntime` + `RT64`):** Compila o código gerado junto ao runtime moderno (ultramodern/librecomp) com renderização gráfica em **Vulkan** (ou Direct3D 12) via **RT64**, interface de menu em **RmlUi + FreeType** e entrada/janela via **SDL2**.

### Principais Diretórios e Arquivos Internos
- **`config/`**: Configurações do `splat` (`winback.us.yaml`), mapeamento manual de símbolos (`symbol_addrs.txt`), hooks/patches do N64Recomp (`winback.us.toml`) e configuração de áudio RSP (`aspMain.us.toml`).
- **`src/main/`**: Aplicação host — inicialização (`main.cpp`), janela e configurações de vídeo (`display.cpp`), contexto RT64 (`rt64_context.cpp`), áudio (`audio.cpp`), mapeamento de controles (`input.cpp`) e carregador/gerenciador de mods (`mods.cpp`).
- **`src/game/`**: Integração e melhorias específicas do jogo:
  - `hud.cpp`: Ancoragem dinâmica do HUD nas bordas em modo Widescreen (16:9) usando comandos estendidos GBI do RT64 (`gEXSetRectAlign`, `gEXPushScissor`).
  - `level_select.cpp`: Seleção de fases (Stages 00 a 30) e sistema de checkpoints (*Stage Start*, *Basic Checkpoint* e *Recorded Checkpoint* salvos em `saves/checkpoints/`).
  - `controller_pak.cpp`: Emulação nativa de Controller Pak persistida na pasta `saves/`.
  - `patches.c`: Hooks de correção de spinloops (inserindo `yield_self_1ms` para permitir troca cooperativa de threads no ultramodern) e câmera analógica no analógico direito.
- **`src/ui/`**: Menu overlay in-game (`menu.cpp`, `menu.rml`, `ui_renderer.cpp`) acionado por `Select` ou `Esc`, permitindo configurar Display, Controles (presets *PS2 Style*, *N64 Style* ou *Custom*), Level Select e Mods em tempo real.
- **`patches/`**: Patches locais aplicados sobre as dependências:
  - `N64ModernRuntime`: Preservação de erros de carregamento de mods (`0001`) e sincronização da thread do jogo durante leitura de display lists pelo RT64 para evitar corrupção de matrizes de personagens em memória viva (`0002`).
  - `rt64`: 9 patches específicos para o motor gráfico do WinBack, incluindo implementação do microcódigo `L3DEX2 G_LINE3D` (usado pela mira laser), correção de seed de dither (`G_AC_DITHER`) da mira laser, suporte a `G_TEXRECT` stateful para nuvens do céu, corte de overscan, escala inteira e ajustes de projeção/recorte em Widescreen.
- **`mods/include/winback_mod.h`**: Header da API pública para criação de mods (`RECOMP_HOOK`, `RECOMP_HOOK_RETURN`, `RECOMP_PATCH`, leitura de configuração, leitura de arquivos do mod, log e interceptação de controles via `winback_mod_pad_read`/`winback_mod_pad_set`).
- **`tools/`**: Scripts de automação do ciclo de desenvolvimento (`fetch_deps.sh`, `regen.sh`, `build_mod.sh`, `capture_test.ps1`, `package.ps1`, `find_spinloops.py`).

---

## 3. `winback_coop` — Mod Cooperativo para o Modo História

O diretório `winback_coop/winback_coop/` contém um mod em C compilado para MIPS ELF (`.elf` $\rightarrow$ `.nrm`) que habilita um **segundo jogador no modo História com tela dividida (split-screen)**.

### Arquivos do Mod
- **`mod.toml`**: Manifesto do mod (`id = "winback_coop"`, versão `0.1.0`, requer `minimum_recomp_version = "0.3.0"`). Define duas opções configuráveis no menu de Mods do jogo:
  - `spawn_offset` (0 a 200, padrão `40`): Distância no eixo X em que o Jogador 2 nasce em relação ao Jogador 1.
  - `log` (`On` / `Off`): Imprime a cada 1 segundo no console o estado dos dois jogadores (ponteiros, HP, flag `in_use` do slot e mapa atual).
- **`mod.ld`**: Script de linker para memória extra (`ORIGIN = 0x81000000`, `LENGTH = 64M`).
- **`src/coop.c`**: Implementação da lógica do Estágio 1 do Co-op usando hooks da API do N64Recomp.

### Como `src/coop.c` Funciona
O motor original do WinBack já possui suporte nativo a múltiplos jogadores e câmeras (do modo Versus) quando `D_8016A4EE` (*player count*) é `2` e desenha a tela dividida de duas visões quando `D_8016A40E` (*mode*) é `2`. Porém, o modo História verifica se há apenas 1 jogador em cerca de 140 pontos do código. Para contornar isso com impacto mínimo, o mod mantém o jogo no estado de História (1 jogador / modo 1) quase o tempo todo e injeta 4 hooks cirúrgicos:

1. **`coop_stage_load` (`RECOMP_HOOK("func_8004C438")`)**: Durante o carregamento do estágio (antes do init), força `PLAYERS = 1` para que o elenco de personagens e os pontos de spawn carregados por `func_8004C108` sejam os do modo História.
2. **`coop_stage_players` (`RECOMP_HOOK("func_8006B310")`)**: Na inicialização do estágio (após carregar os pontos de partida, mas antes de instanciar os jogadores), copia a posição inicial, ID de personagem, orientação (`START_HEAD`) e câmera inicial do Jogador 1 para o Jogador 2 (deslocando o eixo X por `spawn_offset`) e altera `PLAYERS = 2`.
3. **`coop_keep_slot` (`RECOMP_HOOK_RETURN("func_80034364")`)**: A rotina de configuração do gerador de inimigos limpa os slots de personagens `1` a `7`, o que apagaria o Jogador 2 (alocado no slot 1). Ao retornar de `func_80034364`, o hook restaura a flag de uso do slot do Jogador 2 (`U16(p2 + 0x92) = 1`), fazendo com que os inimigos sejam alocados apenas nos slots `2` a `7`.
4. **`coop_draw_begin` / `coop_draw_end` (`func_80025F10`)**: Ao entrar na função que constrói as tarefas gráficas do frame, altera temporariamente `MODE = 2` (ativando a renderização split-screen para os dois jogadores) e, ao sair da função (`RECOMP_HOOK_RETURN`), restaura `MODE = 1` (História) para o próximo ciclo de lógica.

### Limitações Atuais do Mod (Próximas Etapas Planejadas)
Conforme documentado no próprio código (`coop.c` e `mod.toml`), este é o "Stage 1" do mod (o Jogador 2 aparece, anda e atira em tela dividida). Ainda faltam implementar em etapas futuras:
- HUD específico para o modo cooperativo;
- Mira e lock-on entre jogadores / inimigos vistos pelo Jogador 2;
- Sistema de cobertura (*cover*) para o Jogador 2;
- Tratamento de morte, revive e game over compartilhado;
- Comportamento em cutscenes/cenas roteirizadas e câmeras fixas.

---

## 4. Fluxo de Integração (Como os Componentes se Conectam)

Para compilar o projeto base e o mod a partir deste workspace:

1. **Posicionar a ROM no projeto principal:**
   Copiar `baserom.us.z64` da raiz para `WinBackRecomp-main/WinBackRecomp-main/rom/baserom.us.z64`.
2. **Gerar código e compilar o Recomp:**
   Dentro de `WinBackRecomp-main/WinBackRecomp-main/`, configurar o `.venv` com `splat64[mips]`, buscar dependências (`sh tools/fetch_deps.sh`), gerar as fontes (`sh tools/regen.sh`) e compilar via CMake + Ninja + `clang-cl`.
3. **Integrar e compilar o Mod Co-op:**
   Copiar a pasta `winback_coop/winback_coop` para `WinBackRecomp-main/WinBackRecomp-main/mods/winback_coop` e executar:
   ```bash
   sh tools/build_mod.sh mods/winback_coop
   ```
   Isso usa o `generated/winback.us.syms.toml` e o compilador LLVM 18 (com suporte a target MIPS) para gerar o pacote do mod em `generated/mods/winback_coop/`, pronto para ser colocado em `game/mods/`.

---

## 5. Análise do Histórico em `conversa.txt`

O arquivo `conversa.txt` documenta toda a sessão anterior de engenharia reversa e desenvolvimento do mod cooperativo (em paralelo a um trabalho prévio de mod/patches de **60 FPS**, referenciados como patches `0012` e `0013` de interpolação de vértices).

### 5.1. Diagnóstico do Motor do Jogo (Por que o Co-op no Modo História é Complexo)
Embora o *WinBack* já suporte até 4 jogadores, câmeras independentes e tela dividida em 2 ou 4 vistas (usados no modo Versus), o modo História testa o bit 0 de `D_8016A40E` em cerca de **140 funções**, assumindo apenas 1 jogador. Os problemas catalogados ao forçar 2 jogadores na História são:
1. **Sobrescrita de Slot (`func_80034364`):** O spawner de inimigos limpa os slots de personagens `1` a `7`, apagando o Jogador 2 (slot 1). *(Resolvido na v0.1.0)*
2. **Mira e Lock-on:** Os jogadores travam a mira um no outro.
3. **Cobertura (*Cover*):** Nenhum dos dois jogadores consegue entrar em cobertura quando o modo não está configurado corretamente.
4. **Morte do Jogador 2:** O Jogador 2 fica congelado ao morrer.
5. **Projéteis:** Os tiros desaparecem se o Jogador 1 morrer.
6. **IA e Visibilidade dos Inimigos:** Os inimigos só olham e calculam visibilidade de sala para o Jogador 1, podendo ficar invisíveis na tela do Jogador 2.
7. **HUD:** A HUD da História só acompanha o Jogador 1.
8. **Cutscenes e Câmeras de Evento:** Seguem apenas o Jogador 1.
9. **Modelo de Personagem:** O Jogador 2 usa por padrão o modelo do Jean-Luc (protagonista), a menos que outro seja carregado.

### 5.2. Roteiro Planejado em 5 Etapas
- **Etapa 1 (Código atual em `winback_coop/` — v0.1.0):** Fazer o Jogador 2 aparecer na Fase 1, andar e atirar em tela dividida, preservando o slot 1 contra o spawner de inimigos. **Testado em jogo com sucesso ("funcionou muito bem até").**
- **Etapa 2:** Mira/lock-on, visibilidade/IA de inimigos nas duas vistas e funcionamento da cobertura (*cover*).
- **Etapa 3:** Morte e reviver do Jogador 2, independência dos tiros em relação ao Jogador 1 e HUD para ambos.
- **Etapa 4:** Cenas de script (cutscenes), câmera de evento, portas e checkpoints.
- **Etapa 5:** Modelo próprio para o Jogador 2.

### 5.3. O que foi implementado na `v0.2.0` (Descrita no final de `conversa.txt`)
Após o sucesso do teste da `v0.1.0` (entregue compilada como `.nrm`), foram solicitadas e implementadas 4 novas funcionalidades na versão **v0.2.0** (cada uma com chave liga/desliga no menu de Mods):
1. **Personagem "Engineer" como Jogador 2:**
   - Identificado como o **modelo `58` (`0x3A`)**, que compartilha o mesmo esqueleto do Jean-Luc (permitindo reaproveitar todas as animações do modo História).
   - O mod substitui o modelo após a criação do Jogador 2 e carrega seus arquivos em memória expandida acima dos 4 MB originais do N64, no endereço `0x806EE400`.
2. **HUD para Ambos os Jogadores:**
   - Como a HUD da História só desenha para o Jogador 1, o mod instancia o objeto de HUD do **modo Versus** (`func_800B4FD4`), que desenha uma vez por vista, mas com sua rotina de atualização desligada (para não encerrar ou arbitrar a partida como Versus).
   - Durante o desenho, aplica regras de *Death Match* sem limite de tempo, exibindo barra de vida e munição no pente em cada metade da tela.
3. **Inimigos Reconhecendo Ambos os Jogadores:**
   - Na função de atualização de cada inimigo, o mod compara a distância e troca temporariamente as referências de Jogador 1 e Jogador 2 quando o Jogador 2 está vivo e mais próximo (com margem de histerese para não oscilar a cada frame).
   - Força o inimigo a ficar visível se estiver na sala de qualquer um dos dois jogadores.
4. **Fogo Amigo (*Friendly Fire*):**
   - Projéteis disparados por jogadores passam a usar as regras do modo Versus: colidem com ambos os jogadores e não desaparecem caso o Jogador 1 esteja caído (mantendo os tiros dos inimigos inalterados).

> **Atenção — Estado dos Arquivos no Workspace:**
> O diretório `winback_coop/winback_coop/` contém o código-fonte original da **v0.1.0 (Etapa 1)**, enquanto o arquivo `winback_coop.nrm` na raiz é o pacote compilado da **v0.2.0**. Abaixo está a engenharia reversa completa do `winback_coop.nrm` (`v0.2.0`), incluindo o código C integralmente reconstruído.

---

## 6. Engenharia Reversa Completa do `winback_coop.nrm` (`v0.2.0`)

O arquivo `winback_coop.nrm` (3.687 bytes) é um pacote ZIP gerado pelo `RecompModTool` contendo 3 arquivos internos:

| Arquivo Interno | Tamanho | Função |
| :--- | :--- | :--- |
| `mod.json` | 2.984 bytes | Manifesto JSON da versão `0.2.0` com 6 opções de configuração (`spawn_offset`, `log`, `p2_character`, `hud`, `enemies`, `friendly_fire`). |
| `mod_syms.bin` | 1.996 bytes | Tabela binária `N64RSYMS` contendo 4 imports do runtime, 10 definições de funções em `0x81000000`, tabela de realocações MIPS (`R_MIPS_26`, `R_MIPS_HI16`, `R_MIPS_LO16`) e 10 registros de hooks. |
| `mod_binary.bin` | 3.104 bytes | Binário MIPS big-endian compilado (`0x81000000` a `0x81000C20`), além de `0x208` bytes de `.bss` (`0x81000C20` a `0x81000E28`). |

### 6.1. Tabela dos 10 Hooks e Chamadas ao Jogo no Binário `v0.2.0`

| # | Offset no Mod | Tipo de Hook | Função Alvo no Jogo | Descrição |
| :--- | :--- | :--- | :--- | :--- |
| 0 | `0x00F0` (`0x02C` B) | `RECOMP_HOOK` | `func_8004C438` | Volta `PLAYERS` (`0x8016A4EE`) para `1` antes de carregar a fase no modo História. |
| 1 | `0x011C` (`0x15C` B) | `RECOMP_HOOK` | `func_8006B310` | Configura o spawn do Jogador 2, define `PLAYERS = 2`, zera `spawn_calls` e, se `hud == On`, instancia o objeto de HUD do Versus via `func_80027470(3, 4, 0x100)`. |
| 2 | `0x0278` (`0x068` B) | `RECOMP_HOOK_RETURN` | `func_80034364` | Preserva o slot 1 do Jogador 2 (`U16(p2 + 0x92) = 1`) após o setup do spawner de inimigos. |
| 3 | `0x02E0` (`0x418` B) | `RECOMP_HOOK` | `func_80025F10` | No início do desenho do frame: emite o log a cada 1s, ativa `MODE = 2` (split-screen) e salva/força as flags de regras de *Death Match* sem tempo (`0x8016A495 = 1`, `0x8016A4C1 = 0`, `0x8016A4BD = 0xFF`). |
| 4 | `0x06F8` (`0x05C` B) | `RECOMP_HOOK_RETURN` | `func_80025F10` | Ao término do desenho do frame: restaura `MODE = 1` e os bytes originais em `0x8016A495`, `0x8016A4C1` e `0x8016A4BD`. |
| 5 | `0x0754` (`0x0C4` B) | `RECOMP_HOOK_RETURN` | `func_800593EC` | Conta chamadas de criação de jogador; na 2ª chamada (Jogador 2), se `p2_character == Engineer`, define `U8(p2 + 0x16F5) = 0x3A` (modelo 58), chama `func_800A8B10(p2)` e carrega os assets com `func_8004D6E8(p2, 0x37)`. |
| 6 | `0x0818` (`0x1E0` B) | `RECOMP_HOOK` | `func_800337A8` | Antes da atualização de um inimigo (`obj`): se `enemies == On` e o Jogador 2 está vivo e mais perto (`dist_p2² < dist_p1² * 0.64f`, ou Jogador 1 morto), troca temporariamente os ponteiros e salas de P1 e P2 (`0x801390C0/C4`, `0x80179280/84`, `0x8016A410/14`). |
| 7 | `0x09F8` (`0x128` B) | `RECOMP_HOOK_RETURN` | `func_800337A8` | Após atualizar o inimigo: desfaz a troca de ponteiros P1/P2 e, se a sala do inimigo (`e + 0x8E`) coincidir com a sala de qualquer um dos dois jogadores, limpa o bit de ocultação `0x100` em `U32(e + 0xF4)`. |
| 8 | `0x0B20` (`0x0D4` B) | `RECOMP_HOOK` | `func_800B876C` | Antes de atualizar um projétil (`obj`): se `friendly_fire == On` e o dono do tiro (`U32(U32(obj + 0x20) + 0x2C)`) for P1 ou P2, muda temporariamente `MODE = 2` (regras de colisão do Versus). |
| 9 | `0x0BF4` (`0x02C` B) | `RECOMP_HOOK_RETURN` | `func_800B876C` | Após atualizar o projétil: restaura `MODE = 1`. |

### 6.2. Código-Fonte C Reconstruído da `v0.2.0` (`coop.c`)

A descompilação instrução por instrução de `mod_binary.bin` + `mod_syms.bin` resulta exatamente no seguinte código C:

```c
// Story co-op v0.2.0 (descompilado de winback_coop.nrm)
#include "winback_mod.h"

#define U8(a)  (*(volatile unsigned char *)(a))
#define S8(a)  (*(volatile signed char *)(a))
#define U16(a) (*(volatile unsigned short *)(a))
#define U32(a) (*(volatile unsigned int *)(a))
#define F32(a) (*(volatile float *)(a))

#define MODE        0x8016A40E  // u8: bit 0 Story, bit 1 two-view split
#define STATE       0x8016A41C  // u16: bit 0 set during normal play
#define PLAYERS     0x8016A4EE  // u8
#define START_POS   0x8016A424  // 12 bytes per player
#define START_ID    0x8016A39A  // u16 per player
#define START_HEAD  0x8016A454  // float per player
#define START_CAM   0x8016A464  // 12 bytes per player
#define PLAYER_PTRS 0x801390C0  // object pointer per player

// Funções internas do jogo referenciadas via relocations:
extern unsigned int func_80027470(int list, int group, int size);
extern void func_800A8B10(unsigned int player);
extern void func_8004D6E8(unsigned int player, int slot);

static int spawn_calls;              // 0x81000C20
static unsigned char drawing_split;  // 0x81000C24
static unsigned char saved_a495;     // 0x81000C28
static unsigned char saved_a4c1;     // 0x81000C2C
static unsigned char saved_a4bd;     // 0x81000C30
static unsigned char swapped_players;// 0x81000C34
static unsigned int cur_enemy;       // 0x81000C38
static unsigned char bullet_split;   // 0x81000C3C
static unsigned int last_log;        // 0x81000C40

static int story_play(void) {
    return (U8(MODE) & 1) && (U16(STATE) & 1);
}

static int coop_active(void) {
    return story_play() && U8(PLAYERS) == 2;
}

// 1. Story's own player count while the stage loads.
RECOMP_HOOK("func_8004C438") void coop_stage_load(void) {
    if (U8(MODE) & 1) {
        U8(PLAYERS) = 1;
    }
}

// 2. Player 2's start point, player count, and Versus HUD object creation.
RECOMP_HOOK("func_8006B310") void coop_stage_players(void) {
    if (!story_play()) {
        return;
    }
    float off = (float)recomp_get_config_double("spawn_offset");
    F32(START_POS + 12 + 0) = F32(START_POS + 0) + off;
    F32(START_POS + 12 + 4) = F32(START_POS + 4);
    F32(START_POS + 12 + 8) = F32(START_POS + 8);
    U16(START_ID + 2) = U16(START_ID);
    F32(START_HEAD + 4) = F32(START_HEAD);
    F32(START_CAM + 12 + 0) = F32(START_CAM + 0) + off;
    F32(START_CAM + 12 + 4) = F32(START_CAM + 4);
    F32(START_CAM + 12 + 8) = F32(START_CAM + 8);
    U8(PLAYERS) = 2;
    spawn_calls = 0;

    // "hud" option: 0 = "On", 1 = "Off"
    if (recomp_get_config_u32("hud") == 0) {
        unsigned int obj = func_80027470(3, 4, 0x100);
        if (obj != 0) {
            unsigned int data = U32(obj + 0x2C);
            U8(obj + 0x34) = 0x80;
            U16(data + 0x0A) = 0x10;
            U32(obj + 0x24) = 0x8007BAC4; // No-op update callback
            U32(obj + 0x28) = 0x800B5400; // Versus HUD draw callback
            U8(obj + 0x35) = 1;
            U16(obj + 0x36) |= 0x20;
        }
    }
}

// 3. Keep player 2's slot after enemy spawner setup.
RECOMP_HOOK_RETURN("func_80034364") void coop_keep_slot(void) {
    if (coop_active()) {
        unsigned int p2 = U32(PLAYER_PTRS + 4);
        if (p2 != 0) {
            U16(p2 + 0x92) = 1;
        }
    }
}

// --- Status log ----------------------------------------------------------------
static void put_hex(char *s, unsigned int v, int digits) {
    int i;
    for (i = digits - 1; i >= 0; i--) {
        s[i] = "0123456789ABCDEF"[v & 15];
        v >>= 4;
    }
}

static void log_status(void) {
    if (recomp_get_config_u32("log") == 0) {
        return;
    }
    unsigned int now = winback_mod_time_us();
    if ((unsigned int)(now - last_log) < 1000000u) {
        return;
    }
    last_log = now;
    unsigned int p0 = U32(PLAYER_PTRS);
    unsigned int p1 = U32(PLAYER_PTRS + 4);
    static const char tmpl[] = "coop: p0 0x........ hp 00 slot1 0000 | p1 0x........ hp 00 in_use 0000 map 0000";
    static char line[sizeof(tmpl)];
    unsigned int i;
    for (i = 0; i < sizeof(tmpl); i++) {
        line[i] = tmpl[i];
    }
    put_hex(line + 11, p0, 8);
    put_hex(line + 23, p0 ? (unsigned int)(unsigned char)S8(p0 + 0x176B) : 0, 2);
    put_hex(line + 32, U16(0x8017ACD0 + 0x92), 4);
    put_hex(line + 44, p1, 8);
    put_hex(line + 56, p1 ? (unsigned int)(unsigned char)S8(p1 + 0x176B) : 0, 2);
    put_hex(line + 66, p1 ? U16(p1 + 0x92) : 0, 4);
    put_hex(line + 75, U16(0x8016A41A), 4);
    winback_mod_log(line);
}

// 4. Two-view split and Death Match HUD rules only while drawing.
RECOMP_HOOK("func_80025F10") void coop_draw_begin(void) {
    drawing_split = 0;
    if (coop_active()) {
        log_status();
        U8(MODE) = 2;
        saved_a495 = U8(0x8016A495);
        saved_a4c1 = U8(0x8016A4C1);
        saved_a4bd = U8(0x8016A4BD);
        U8(0x8016A495) = 1;
        U8(0x8016A4C1) = 0;
        U8(0x8016A4BD) = 0xFF;
        drawing_split = 1;
    }
}

RECOMP_HOOK_RETURN("func_80025F10") void coop_draw_end(void) {
    if (drawing_split) {
        U8(MODE) = 1;
        U8(0x8016A495) = saved_a495;
        U8(0x8016A4C1) = saved_a4c1;
        U8(0x8016A4BD) = saved_a4bd;
        drawing_split = 0;
    }
}

// 5. Engineer model (0x3A = 58) for Player 2 after second player creation (FIXED).
RECOMP_HOOK_RETURN("func_800593EC") void coop_p2_model(void) {
    spawn_calls++;
    if (spawn_calls == 2) {
        unsigned int p2 = U32(PLAYER_PTRS + 4);
        if (p2 != 0) {
            U8(p2 + 0x16F5) = 0x3A;              // Engineer model ID
            U8(p2 + 0x1758) = 0x03;              // Engineer character property index
            U8(p2 + 0x18CD) &= (unsigned char)~2;// Prevent frame-1 func_800A8B10 buffer reset
            func_800A8B10(p2);
            func_8004D6E8(p2, 0x37);
            func_8006B374(p2);                   // Free Jean-Luc's dynamic joint slot in D_801DC710
            func_8006B3B8(p2);                   // DMA Engineer's dynamic joint vertices into D_801DC710
            func_8006B564(p2);                   // Skin Engineer's initial dynamic joint vertices
            U32(0x80138EEC) = U32(p2 + 0x188C);  // Save Engineer DL buffer pointer
            U32(0x80138EF0) = U32(p2 + 0x1890);  // Save Engineer texture buffer pointer
            U32(0x80138EF4) = U32(p2 + 0x1894);  // Save Engineer vertex buffer pointer
        }
    }
}

// 6. Enemies target nearer living player and stay visible in either player's room.
RECOMP_HOOK("func_800337A8") void coop_enemy_begin(unsigned int obj) {
    swapped_players = 0;
    cur_enemy = 0;
    if (!coop_active() || recomp_get_config_u32("enemies") != 0) {
        return;
    }
    unsigned int e = U32(obj + 0x2C);
    cur_enemy = e;
    unsigned int p0 = U32(PLAYER_PTRS);
    unsigned int p1 = U32(PLAYER_PTRS + 4);
    if (e == 0 || p0 == 0 || p1 == 0) {
        return;
    }
    int hp0 = S8(p0 + 0x176B);
    int hp1 = S8(p1 + 0x176B);
    if (hp0 <= 0) {
        if (hp1 <= 0) {
            return;
        }
    } else {
        if (hp1 <= 0) {
            return;
        }
        float dx1 = F32(e + 0) - F32(p1 + 0);
        float dy1 = F32(e + 4) - F32(p1 + 4);
        float dz1 = F32(e + 8) - F32(p1 + 8);
        float d1 = dx1 * dx1 + dy1 * dy1 + dz1 * dz1;

        float dx0 = F32(e + 0) - F32(p0 + 0);
        float dy0 = F32(e + 4) - F32(p0 + 4);
        float dz0 = F32(e + 8) - F32(p0 + 8);
        float d0 = dx0 * dx0 + dy0 * dy0 + dz0 * dz0;

        if (!(d1 < d0 * 0.64f)) {
            return;
        }
    }
    U32(PLAYER_PTRS) = p1;
    U32(PLAYER_PTRS + 4) = p0;
    unsigned int c0 = U32(0x80179280);
    unsigned int c1 = U32(0x80179284);
    U32(0x80179280) = c1;
    U32(0x80179284) = c0;
    unsigned int r0 = U32(0x8016A410);
    unsigned int r1 = U32(0x8016A414);
    U32(0x8016A410) = r1;
    U32(0x8016A414) = r0;
    swapped_players = 1;
}

RECOMP_HOOK_RETURN("func_800337A8") void coop_enemy_end(void) {
    if (swapped_players) {
        unsigned int p0 = U32(PLAYER_PTRS);
        unsigned int p1 = U32(PLAYER_PTRS + 4);
        U32(PLAYER_PTRS) = p1;
        U32(PLAYER_PTRS + 4) = p0;
        unsigned int c0 = U32(0x80179280);
        unsigned int c1 = U32(0x80179284);
        U32(0x80179280) = c1;
        U32(0x80179284) = c0;
        unsigned int r0 = U32(0x8016A410);
        unsigned int r1 = U32(0x8016A414);
        U32(0x8016A410) = r1;
        U32(0x8016A414) = r0;
        swapped_players = 0;
    }
    unsigned int e = cur_enemy;
    if (e != 0) {
        unsigned short eroom = U16(e + 0x8E);
        unsigned int p0 = U32(PLAYER_PTRS);
        unsigned int p1 = U32(PLAYER_PTRS + 4);
        int vis = 0;
        if (p0 != 0 && (U16(p0 + 0x8E) == eroom || U16(0x8016A410) == eroom || U16(0x8016A412) == eroom)) {
            vis = 1;
        } else if (p1 != 0 && (U16(p1 + 0x8E) == eroom || U16(0x8016A414) == eroom || U16(0x8016A416) == eroom)) {
            vis = 1;
        }
        if (vis) {
            U32(e + 0xF4) &= ~0x100u;
        }
        cur_enemy = 0;
    }
}

// 7. Friendly fire: player-fired bullets use Versus collision rules.
RECOMP_HOOK("func_800B876C") void coop_bullet_begin(unsigned int obj) {
    bullet_split = 0;
    if (!coop_active() || recomp_get_config_u32("friendly_fire") != 0) {
        return;
    }
    unsigned int owner_obj = U32(obj + 0x20);
    unsigned int owner = U32(owner_obj + 0x2C);
    if (owner != 0 && (owner == U32(PLAYER_PTRS) || owner == U32(PLAYER_PTRS + 4))) {
        U8(MODE) = 2;
        bullet_split = 1;
    }
}

RECOMP_HOOK_RETURN("func_800B876C") void coop_bullet_end(void) {
    if (bullet_split) {
        U8(MODE) = 1;
        bullet_split = 0;
    }
}
```


