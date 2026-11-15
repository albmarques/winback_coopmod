# Referência Completa de Hooks — `winback_coop`

Este documento cataloga todos os hooks (`RECOMP_HOOK` e `RECOMP_HOOK_RETURN`), funções nativas da ROM chamadas via relocations, símbolos de [winback.us.syms.toml](file:///C:/Users/Alberto/Documents/projetos/game/winback/generated/winback.us.syms.toml) e endereços de memória utilizados em [coop.c](file:///C:/Users/Alberto/Documents/projetos/game/winback/winback_coop/winback_coop/src/coop.c).

---

## 1. Como Funcionam os Hooks no `N64Recomp`

O `RecompModTool` lê a tabela de símbolos em [winback.us.syms.toml](file:///C:/Users/Alberto/Documents/projetos/game/winback/generated/winback.us.syms.toml) e o binário linkado (`mod.elf`) para injetar código C compilado para MIPS no endereço virtual `0x81000000` (definido em [mod.ld](file:///C:/Users/Alberto/Documents/projetos/game/winback/winback_coop/winback_coop/mod.ld)):

- **`RECOMP_HOOK("func_...")`**: executa **na entrada** da função original da ROM, recebendo os mesmos argumentos (`a0`, `a1`, `a2`, `a3`) antes de a função rodar.
- **`RECOMP_HOOK_RETURN("func_...")`**: executa **no retorno** da função original da ROM (`jr ra`), logo depois de ela terminar (ideal para restaurar estado temporário ou ajustar o resultado em memória).
- **Chamadas diretas (`extern ...`)**: declarações em `coop.c` resolvidas por `--emit-relocs` contra `winback.us.syms.toml`, permitindo chamar rotinas originais do motor (como alocação de objetos e descompressão de modelo).

---

## 2. Catálogo Geral de Hooks em `coop.c`

| Função da ROM | VRAM (`size`) | Tipo | Função no `coop.c` | Responsabilidade |
| :--- | :--- | :--- | :--- | :--- |
| `func_8004C438` | `0x8004C438` (`0x420`) | `HOOK` | `coop_stage_load` | Força `PLAYERS = 1` ao carregar fase Story e reseta estado de cutscene/buffers. |
| `func_8006B310` | `0x8006B310` (`0x24`) | `HOOK` | `coop_stage_players` | Copia spawn do P1 para P2 (`+spawn_offset`), ativa `PLAYERS = 2` e cria o objeto do HUD Versus. |
| `func_80034364` | `0x80034364` (`0x1A0`) | `HOOK_RETURN` | `coop_keep_slot` | Restaura `p2 + 0x92 = 1` (`in_use`) após o spawner de inimigos limpar slots `1..7`. |
| `func_800593EC` | `0x800593EC` (`0xA70`) | `HOOK_RETURN` | `coop_p2_model` | Define protagonista inicial (`0x18CC`), carrega modelo Engineer (`0x3A`), texturas e juntas do P2 na 2ª chamada. |
| `func_80025F10` | `0x80025F10` (`0x560`) | `HOOK` / `HOOK_RETURN` | `coop_draw_begin` / `coop_draw_end` | Preserva buffers do Engineer (inclusive em cutscenes), ativa `MODE = 2` (split-screen) e `0x8010F848 = 0x10` (HUD) no desenho, e restaura `MODE = 1`. |
| `func_800D1E24` | `0x800D1E24` (`0x2A8`) | `HOOK` / `HOOK_RETURN` | `coop_hud_update_begin` / `coop_hud_update_end` | Ativa `MODE = 2` durante o update da HUD para atualizar vida, arma, munição, reload e laser de P1 e P2. |
| `func_800337A8` | `0x800337A8` (`0xBC0`) | `HOOK` / `HOOK_RETURN` | `coop_enemy_begin` / `coop_enemy_end` | Troca ponteiros P1/P2 quando P2 está vivo e mais próximo (histerese `0.64`) e mantém inimigos visíveis (`~0x100`) nas salas de ambos. |
| `func_800B876C` | `0x800B876C` (`0x500`) | `HOOK` / `HOOK_RETURN` | `coop_bullet_begin` / `coop_bullet_end` | Ativa `MODE = 2` para projéteis de jogadores quando `friendly_fire` está ligado (`0 = On`). |
| `func_80057BB4` | `0x80057BB4` (`0x2C0`) | `HOOK` / `HOOK_RETURN` | `coop_damage_begin` / `coop_damage_end` | Alterna `MODE = 1` dentro de `func_80057BB4` durante `bullet_split` para aplicar dano direto de Story Mode sem depender da flag de HUD Versus. |
| `func_80058438` | `0x80058438` (`0x4C4`) | `HOOK` / `HOOK_RETURN` | `coop_lockon_begin` / `coop_lockon_end` | Zera temporariamente `in_use (+0x92)` do outro jogador durante a busca de alvo para remover o lock-on automático entre players. |
| `func_800ABC04` | `0x800ABC04` (`0x830`) | `HOOK` / `HOOK_RETURN` | `coop_wall_cover_begin` / `coop_wall_cover_end` | Ativa `MODE = 2` para pular o check de distância de slots (`0x1718 < 150.0f`) que bloqueava cover na parede entre P1 e P2. |
| `func_8003FCE4` | `0x8003FCE4` (`0x39C`) | `HOOK` / `HOOK_RETURN` | `coop_box_col_begin` / `coop_box_col_end` | Troca `ROOM_STATE` (`0x8016A410` ↔ `0x8016A414`) quando chamado para P2, habilitando cover em caixas/pilares e portas na sala do P2. |
| `func_800AB594` | `0x800AB594` (`0x3B8`) | `HOOK` / `HOOK_RETURN` | `coop_kick_begin` / `coop_kick_end` | Zera temporariamente `in_use (+0x92)` do parceiro quando `friendly_fire` está desligado (`!= 0`), evitando chute acidental ao tentar dar cover. |
| `func_800873A4` | `0x800873A4` (`0x1CC`) | `HOOK` / `HOOK_RETURN` | `coop_stage_item_begin` / `coop_stage_item_end` | Permite que o P2 pegue itens colocados na fase trocando `PLAYER_PTRS` e `ROOM_STATE` via `should_p2_pick_item()`. |
| `func_800C958C` | `0x800C958C` (`0x68`) | `HOOK` / `HOOK_RETURN` | `coop_drop_item_begin` / `coop_drop_item_end` | Permite que o P2 pegue itens soltos no chão via `should_p2_pick_item()`. |
| `func_800A8620` | `0x800A8620` (`0x1C0`) | `HOOK` / `HOOK_RETURN` | `coop_enemy_item_begin` / `coop_enemy_item_end` | Permite que o P2 pegue itens dropados por inimigos via `should_p2_pick_item()`. |
| `func_800A8B10` | `0x800A8B10` (`0x114`) | `HOOK` / `HOOK_RETURN` | `coop_a8b10_begin` / `coop_a8b10_end` | Salva o argumento `player` e restaura os buffers/modelo Engineer (`0x3A`) no `SLOT1_DATA` após chamadas internas do jogo (inclusive em cutscenes). |
| `func_8004DE48` | `0x8004DE48` (`0x10C`) | `HOOK` | `coop_event_push` | Detecta qual jogador (`SLOT0_DATA` ou `SLOT1_DATA`) cruzou uma linha de gatilho (`type == 1`) e atualiza `trigger_player`. |
| `func_800A8ED0` | `0x800A8ED0` (`0x1890`) | `HOOK` | `coop_action_btn` | Detecta qual jogador apertou o botão Ação (portas, terminais, switches) e atualiza `trigger_player`. |
| `func_800944E0` | `0x800944E0` (`0x248`) | `HOOK` / `HOOK_RETURN` | `coop_event_mgr_begin` / `coop_event_mgr_end` | Faz merge temporário dos botões do P2 (`0x80187FB2` / `0x80187FC2`) nos do P1 quando ambos têm `0x16EC < 2`, permitindo dispensar popups de itens e pular cenas. |
| `func_8009C2E4` | `0x8009C2E4` (`0x348`) | `HOOK` / `HOOK_RETURN` | `coop_event_dispatch_begin` / `coop_event_dispatch_end` | Define `0x18CC` do protagonista (`set_protagonist(trigger_player)`) e troca `PLAYER_PTRS`/`CAM_PTRS`/`ROOM_STATE` se `trigger_player == 1` antes de despachar eventos de fase. |
| `func_8009BE18` | `0x8009BE18` (`0x204`) | `HOOK` / `HOOK_RETURN` | `coop_script_run_begin` / `coop_script_run_end` | Garante que durante `in_cutscene` com `cutscene_protagonist == 1` os ponteiros continuem apontando para o P2 como protagonista do script. |
| `func_8009E1E0` | `0x8009E1E0` (`0x818`) | `HOOK` / `HOOK_RETURN` | `coop_cutscene_mode_begin` / `coop_cutscene_mode_end` | Na entrada da cutscene (`mode == 0`), fixa `cutscene_protagonist`, para o movimento do outro jogador e ativa `in_cutscene = 1`. Na saída (`mode == 1`), restaura ordem P1/P2 e reseta o estado. |
| `func_80098F20` | `0x80098F20` (`0x150`) | `HOOK` / `HOOK_RETURN` | `coop_cutscene_teleport_begin` / `coop_cutscene_teleport_end` | Quando uma cutscene reposiciona/teleporta um jogador no meio da fase, move o parceiro junto (`X + spawn_offset`, `Y`, `Z`, ângulo, polígono `0x8A` e sala `0x8E`). |

---

## 3. Detalhamento Técnico por Subsistemas

### 3.1. Ciclo de Vida da Fase e Spawn
1. **`coop_stage_load` (`func_8004C438`)**:
   - Condição: `U8(MODE) & 1`.
   - Ação: força `U8(PLAYERS) = 1`, zera `trigger_player`, `cutscene_protagonist`, `in_cutscene` e `U32(0x80138EEC) = 0`. Evita que o carregador de fase procure dados de Multiplayer Versus.
2. **`coop_stage_players` (`func_8006B310`)**:
   - Condição: `story_play()` (`(U8(MODE) & 1) && (U16(STATE) & 1)`).
   - Ação: copia `START_POS` (`+spawn_offset` em X), `START_ID`, `START_HEAD` e `START_CAM` do P1 para o P2; define `U8(PLAYERS) = 2` e `spawn_calls = 0`.
   - Se `recomp_get_config_u32("hud") == 0`, cria o objeto de tarefa `func_80027470(3, 4, 0x100)` com update `0x8007BAC4` (no-op) e draw `0x800B5400` (HUD de Death Match).
3. **`coop_keep_slot` (`func_80034364`)**:
   - Condição: `coop_active()`.
   - Ação: restaura `U16(p2 + 0x92) = 1` após `func_80034364` limpar `in_use` dos slots `1..7`.

### 3.2. Modelo Engineer (`0x3A`) e Renderização / HUD
1. **`coop_p2_model` (`func_800593EC` retorno)**:
   - Incrementa `spawn_calls`. Na 2ª chamada (`spawn_calls == 2`) em Story com 2 jogadores:
   - Chama `set_protagonist(0)` (`SLOT0_DATA->0x18CC = 0xFF`, `SLOT1_DATA->0x18CC = 0xFE`).
   - Se `p2_character == 0` (*Engineer*): define `p2 + 0x16F5 = 0x3A`, limpa bit `2` de `p2 + 0x18CD`, executa `func_800A8B10(p2)`, `func_8004D6E8(p2, 0x37)`, `func_8006B374(p2)`, `func_8006B3B8(p2)`, `func_8006B564(p2)` e salva os 3 ponteiros (`0x188C`, `0x1890`, `0x1894`) em `0x80138EEC..0x80138EF4`.
2. **`coop_draw_begin` / `coop_draw_end` (`func_80025F10`)**:
   - Em qualquer sessão Story (`coop_story_session()`), incluindo cutscenes, chama `restore_engineer_model(SLOT1_DATA)`.
   - Em gameplay ativo (`coop_active()`): se `in_cutscene` ainda estiver `1`, restaura a ordem normal P1/P2; roda `log_status()` se `log != 0`; ativa `MODE = 2`, `0x8016A495 = 1`, `0x8016A4C1 = 0`, `0x8016A4BD = 0xFF`, `0x8010F848 = 0x10`, `0x8010F844 = 0`, `0x8010F845 = 0` e restaura no retorno.
3. **`coop_hud_update_begin` / `coop_hud_update_end` (`func_800D1E24`)**:
   - Em `coop_active()`, alterna `MODE` para `2` durante `func_800D1E24` para atualizar barras de vida (`0x801B1168+`), arma/munição/reload/laser (`0x8011FB80+`) de ambos os jogadores.

### 3.3. IA de Inimigos e Friendly Fire
1. **`coop_enemy_begin` / `coop_enemy_end` (`func_800337A8`)**:
   - Se `enemies == 0`: calcula distância ao quadrado do inimigo até P1 (`d0`) e P2 (`d1`). Se P1 morreu (`hp0 <= 0 && hp1 > 0`) ou P2 está mais perto com histerese (`d1 < d0 * 0.64f`), troca `PLAYER_PTRS`, `CAM_PTRS` e `ROOM_STATE` durante a atualização da IA.
   - No retorno, desfaz a troca e limpa o bit de culling `0x100` em `U32(enemy + 0xF4)` se o inimigo estiver na sala/subsala de qualquer um dos dois jogadores.
2. **`coop_bullet_begin` / `coop_bullet_end` (`func_800B876C`)**:
   - Se `friendly_fire == 0` (*On*) e o dono do projétil for P1 ou P2, define `MODE = 2` durante o teste de colisão da bala.

### 3.4. Cover, Colisão de Caixas e Chute
1. **`coop_wall_cover_begin` / `coop_wall_cover_end` (`func_800ABC04`)**:
   - No endereço `0x800ABDC0` da ROM, `func_800ABC04` verifica `U8(MODE) & 1` e percorre os 8 slots (`0x80179350 + i*0x1980`), abortando cover se `F32(slot + 0x1718) < 150.0f` no mesmo andar (`abs(dy) < 10.0f`). Como `0x1718` fica `0.0f` nos jogadores, P1 e P2 bloqueavam um ao outro. Trocar `MODE = 2` durante `func_800ABC04` ignora essa checagem.
2. **`coop_box_col_begin` / `coop_box_col_end` (`func_8003FCE4`)**:
   - `func_8003FCE4(obj)` calcula colisão com caixas, pilares e portas (`player + 0x1814`), mas no modo Story lê apenas `0x8016A410` (P1). Quando `U32(obj + 0x2C) == PLAYER_PTRS[1]`, troca `ROOM_STATE` durante a execução.
3. **`coop_kick_begin` / `coop_kick_end` (`func_800AB594`)**:
   - Quando `friendly_fire != 0` (*Off*), salva e zera `other + 0x92` (`in_use`) durante `func_800AB594` para que apertar Ação perto do parceiro ao dar cover não acione chute nele.

### 3.5. Itens e Popups
1. **`coop_stage_item_*` (`func_800873A4`), `coop_drop_item_*` (`func_800C958C`), `coop_enemy_item_*` (`func_800A8620`)**:
   - Usam `item_pick_begin(obj)` / `item_pick_end()` com contador `item_swap_depth` e o filtro `should_p2_pick_item(item_data)`:
     - Exige P2 vivo (`S8(p2 + 0x176B) > 0`);
     - Se o item já está sendo pego (`S16(item_data + 0x132) != 0`), verifica se P2 está no estado de pickup (`U16(p2 + 0x16EC) == 17`);
     - Caso contrário, verifica se P2 está na mesma sala (`0x8E` / `ROOM_STATE + 4`) e se P1 está morto, fora da sala ou mais distante do item que P2.
2. **`coop_event_mgr_begin` / `coop_event_mgr_end` (`func_800944E0`)**:
   - Quando ambos têm `0x16EC < 2`, aplica `|=` dos botões do P2 (`BTN_NEW_P2`, `BTN_MASK_P2`) nos registradores do P1 (`BTN_NEW_P1`, `BTN_MASK_P1`) e restaura no retorno.

### 3.6. Cutscenes e Protagonista Dinâmico
1. **`coop_event_push` (`func_8004DE48`)** e **`coop_action_btn` (`func_800A8ED0`)**:
   - Em `func_8004DE48(type, sub, val, obj)`, se `type == 1 && obj != 0`, verifica `U32(obj + 0x2C)` para gravar `trigger_player = 0` (`SLOT0_DATA`) ou `1` (`SLOT1_DATA`).
   - Em `func_800A8ED0(obj)`, verifica `U32(obj + 0x2C)` pelo mesmo critério.
2. **`coop_event_dispatch_*` (`func_8009C2E4`)** e **`coop_script_run_*` (`func_8009BE18`)**:
   - Aplicam `set_protagonist(trigger_player)` (`0x18CC = 0xFF` no protagonista e `0xFE` no outro) e mantêm `PLAYER_PTRS`, `CAM_PTRS` e `ROOM_STATE` trocados enquanto `trigger_player == 1` ou `in_cutscene && cutscene_protagonist == 1`.
3. **`coop_cutscene_mode_*` (`func_8009E1E0`)**:
   - Em `mode == 0` (início): trava `cutscene_protagonist = trigger_player`, para o movimento do outro jogador (`0x0154 = 0`, `0x0158 = 0`, `0x16EC = 0`, `0x16F0 = 1`) e liga `in_cutscene = 1`.
   - Em `mode == 1` (fim): restaura `ensure_normal_player_order()`, `set_protagonist(0)`, `cutscene_protagonist = 0`, `trigger_player = 0`, `in_cutscene = 0`.
4. **`coop_cutscene_teleport_*` (`func_80098F20`)**:
   - Copia `X + spawn_offset`, `Y`, `Z`, ângulo (`0x64`), polígono (`0x8A`) e sala (`0x8E`) de `PLAYER_PTRS[tele_idx]` para `PLAYER_PTRS[1 - tele_idx]`.
5. **`coop_a8b10_*` (`func_800A8B10`)**:
   - Restaura os buffers do Engineer no `SLOT1_DATA` sempre que `func_800A8B10` rodar para o Player 2 (incluindo transições de cutscene).

---

## 4. Funções da ROM Chamadas Diretamente pelo `coop.c`

| Função | Assinatura usada no `coop.c` | Uso |
| :--- | :--- | :--- |
| `func_80027470` | `unsigned int func_80027470(int list, int group, int size)` | Aloca objeto de tarefa para o HUD Versus (`3, 4, 0x100`). |
| `func_800A8B10` | `void func_800A8B10(unsigned int player)` | Configura tabela de offsets de Display Lists (`+0x1744`) do modelo atual (`0x3A`). |
| `func_8004D6E8` | `void func_8004D6E8(unsigned int player, int slot)` | Descompacta DLs, texturas e vértices do Engineer no slot `0x37` (`0x806EE400`). |
| `func_8006B374` | `void func_8006B374(unsigned int player)` | Libera slot de juntas dinâmicas em `D_801DC710`. |
| `func_8006B3B8` | `void func_8006B3B8(unsigned int player)` | Faz DMA das juntas dinâmicas do modelo atual (`0x3A`) para `D_801DC710`. |
| `func_8006B564` | `void func_8006B564(unsigned int player)` | Calcula a deformação inicial das juntas dinâmicas. |

---

## 5. Símbolos Registrados em `winback.us.syms.toml`

Lista completa dos símbolos em [winback.us.syms.toml](file:///C:/Users/Alberto/Documents/projetos/game/winback/generated/winback.us.syms.toml) (`section "main"`, `rom = 0x1000`, `vram = 0x80025C00`, `size = 0xD8E60`):

| Símbolo | VRAM | Size | Papel |
| :--- | :--- | :--- | :--- |
| `func_80025F10` | `0x80025F10` | `0x560` | Loop de renderização do frame |
| `func_80027470` | `0x80027470` | `0x168` | Alocador de objetos/tarefas |
| `func_800337A8` | `0x800337A8` | `0xBC0` | Update de IA/visibilidade de inimigos |
| `func_80034364` | `0x80034364` | `0x1A0` | Setup do spawner de inimigos |
| `func_8003C58C` | `0x8003C58C` | `0x5F4` | Rastreamento de sala/subsala do jogador |
| `func_8003FCE4` | `0x8003FCE4` | `0x39C` | Colisão com caixas/pilares/portas (`0x1814`) |
| `func_8004C438` | `0x8004C438` | `0x420` | Carregamento principal da fase |
| `func_8004D144` | `0x8004D144` | `0x1B0` | Auxiliar de carga/modelos |
| `func_8004D39C` | `0x8004D39C` | `0x1D4` | Auxiliar de carga/modelos |
| `func_8004D570` | `0x8004D570` | `0x60` | Lookup de dados de modelo |
| `func_8004D5D0` | `0x8004D5D0` | `0x9C` | Busca de slot de modelo na fase |
| `func_8004D66C` | `0x8004D66C` | `0x50` | Auxiliar de carga de modelo |
| `func_8004D6E8` | `0x8004D6E8` | `0x17C` | Descompactação de modelo para RAM |
| `func_8004DE48` | `0x8004DE48` | `0x10C` | Push na fila de eventos (`a0=type, a3=obj`) |
| `func_80053BB4` | `0x80053BB4` | `0x200` | Auxiliar de personagens/objetos |
| `func_80057100` | `0x80057100` | `0xAB4` | Renderização de personagem humano |
| `func_80057BB4` | `0x80057BB4` | `0x2C0` | Aplicação de dano em personagem |
| `func_80058438` | `0x80058438` | `0x4C4` | Busca de alvo / lock-on automático do jogador |
| `func_800593EC` | `0x800593EC` | `0xA70` | Criação/inicialização de jogador |
| `func_80059E5C` | `0x80059E5C` | `0x6A0` | Update de jogador (inclui check `0x18CD & 2`) |
| `func_8006B298` | `0x8006B298` | `0x78` | Auxiliar de setup de jogadores |
| `func_8006B310` | `0x8006B310` | `0x24` | Inicialização de jogadores da fase |
| `func_8006B334` | `0x8006B334` | `0x40` | Auxiliar de juntas dinâmicas |
| `func_8006B374` | `0x8006B374` | `0x44` | Libera slot de juntas dinâmicas |
| `func_8006B3B8` | `0x8006B3B8` | `0x1AC` | DMA de juntas dinâmicas da ROM |
| `func_8006B564` | `0x8006B564` | `0x1128` | Skinning CPU das juntas dinâmicas |
| `func_8006C68C` | `0x8006C68C` | `0x2904` | Montagem de DLs dinâmicas (LOD 1) |
| `func_8006EF90` | `0x8006EF90` | `0x24E0` | Montagem de DLs dinâmicas (LOD 2/3) |
| `func_800873A4` | `0x800873A4` | `0x1CC` | Checagem de pickup de item da fase |
| `func_800937C4` | `0x800937C4` | `0x290` | Auxiliar de eventos/fase |
| `func_800944E0` | `0x800944E0` | `0x248` | Gerenciador de eventos e popups |
| `func_80098F20` | `0x80098F20` | `0x150` | Teleporte de jogador em cutscene |
| `func_8009BBB8` | `0x8009BBB8` | `0x260` | Avaliador de condições de script |
| `func_8009BE18` | `0x8009BE18` | `0x204` | Executor de script/cutscene |
| `func_8009C2E4` | `0x8009C2E4` | `0x348` | Dispatcher de eventos de fase |
| `func_8009E1E0` | `0x8009E1E0` | `0x818` | Transição entrada/saída de cutscene |
| `func_800A8620` | `0x800A8620` | `0x1C0` | Checagem de pickup de drop de inimigo |
| `func_800A8B10` | `0x800A8B10` | `0x114` | Setup de buffers/LOD do modelo |
| `func_800A8ED0` | `0x800A8ED0` | `0x1890` | Handler do botão Ação do jogador |
| `func_800AB594` | `0x800AB594` | `0x3B8` | Checagem de alvo do chute corpo a corpo |
| `func_800ABC04` | `0x800ABC04` | `0x830` | Checagem de cover na parede |
| `func_800B876C` | `0x800B876C` | `0x500` | Atualização e colisão de projéteis |
| `func_800C958C` | `0x800C958C` | `0x68` | Checagem de pickup de item solto |
| `func_800D1E24` | `0x800D1E24` | `0x2A8` | Atualização de dados da HUD |
