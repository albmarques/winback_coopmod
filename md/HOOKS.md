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
| `func_80026EB8` | `0x80026EB8` (`0x288`) | `HOOK` / `HOOK_RETURN` | `coop_fb_clear_begin` / `coop_fb_clear_end` | Define `0x8016A40F` na entrada do clear do framebuffer (após `func_80026A10` sobrescrevê-lo em `0x80025F38`) e emite comandos RDP de fill preto (`0x00010001`) apenas na metade da tela do jogador que estiver em sala interna fechada. |
| `func_800C4EE0` | `0x800C4EE0` (`0xD0`) | `HOOK` / `HOOK_RETURN` | `coop_sky_update_begin` / `coop_sky_update_end` | Redireciona temporariamente `obj + 0x20` e `player + 0x8E` para o jogador externo (`get_outdoor_room`) durante o update da matriz do Sky Dome (`0x01006640`). |
| `func_800C4FB0` | `0x800C4FB0` (`0x30C`) | `HOOK` / `HOOK_RETURN` | `coop_sky_draw_begin` / `coop_sky_draw_end` | Redireciona temporariamente `obj + 0x20`, `PLAYER_PTRS[0]` e `player + 0x8E` de acordo com o viewport atual (`0x8016A40F`), desenhando nuvens 2D e Sky Dome 3D apenas nos viewports que enxergam área externa. |
| `func_800D1E24` | `0x800D1E24` (`0x2A8`) | `HOOK` / `HOOK_RETURN` | `coop_hud_update_begin` / `coop_hud_update_end` | Ativa `MODE = 2` durante o update da HUD para atualizar vida, arma, munição, reload e laser de P1 e P2. |
| `func_800337A8` | `0x800337A8` (`0xBC0`) | `HOOK` / `HOOK_RETURN` | `coop_enemy_begin` / `coop_enemy_end` | Troca ponteiros P1/P2 quando P2 está vivo e mais próximo (histerese `0.64`) e mantém inimigos visíveis (`~0x100`) nas salas de ambos. |
| `func_800B876C` | `0x800B876C` (`0x500`) | `HOOK` / `HOOK_RETURN` | `coop_bullet_begin` / `coop_bullet_end` | Ativa `MODE = 2` para projéteis de jogadores quando `friendly_fire` está ligado (`0 = On`). |
| `func_80057BB4` | `0x80057BB4` (`0x2C0`) | `HOOK` / `HOOK_RETURN` | `coop_damage_begin` / `coop_damage_end` | Alterna `MODE = 1` dentro de `func_80057BB4` durante `bullet_split` para aplicar dano direto de Story Mode sem depender da flag de HUD Versus. |
| `func_80058438` | `0x80058438` (`0x4C4`) | `HOOK` / `HOOK_RETURN` | `coop_lockon_begin` / `coop_lockon_end` | Zera temporariamente `in_use (+0x92)` do outro jogador durante a busca de alvo para remover o lock-on automático entre players. |
| `func_800ABC04` | `0x800ABC04` (`0x830`) | `HOOK` / `HOOK_RETURN` | `coop_wall_cover_begin` / `coop_wall_cover_end` | Ativa `MODE = 2` para pular o check de distância de slots (`0x1718 < 150.0f`) que bloqueava cover na parede entre P1 e P2. |
| `func_8003C58C` | `0x8003C58C` (`0x5F4`) | `HOOK` / `HOOK_RETURN` | `coop_room_begin` / `coop_room_end` | Força `MODE = 2` em `coop_story_session()` (inclusive durante cutscenes/escadas) e desinverte temporariamente `ROOM_STATE` caso `PLAYER_PTRS` esteja trocado, alinhando com `S16(player + 0x90)`. |
| `func_8003FCE4` | `0x8003FCE4` (`0x39C`) | `HOOK` / `HOOK_RETURN` | `coop_box_col_begin` / `coop_box_col_end` | Troca `ROOM_STATE` (`0x8016A410` ↔ `0x8016A414`) quando chamado para P2, habilitando cover em caixas/pilares e portas na sala do P2. |
| `func_800AB594` | `0x800AB594` (`0x3B8`) | `HOOK` / `HOOK_RETURN` | `coop_kick_begin` / `coop_kick_end` | Zera temporariamente `in_use (+0x92)` do parceiro quando `friendly_fire` está desligado (`!= 0`), evitando chute acidental ao tentar dar cover. |
| `func_800873A4` | `0x800873A4` (`0x1CC`) | `HOOK` / `HOOK_RETURN` | `coop_stage_item_begin` / `coop_stage_item_end` | Permite que o P2 pegue itens colocados na fase trocando `PLAYER_PTRS` e `ROOM_STATE` via `should_p2_pick_item()`. |
| `func_800C958C` | `0x800C958C` (`0x68`) | `HOOK` / `HOOK_RETURN` | `coop_drop_item_begin` / `coop_drop_item_end` | Permite que o P2 pegue itens soltos no chão via `should_p2_pick_item()`. |
| `func_800A8620` | `0x800A8620` (`0x1C0`) | `HOOK` / `HOOK_RETURN` | `coop_enemy_item_begin` / `coop_enemy_item_end` | Permite que o P2 pegue itens dropados por inimigos via `should_p2_pick_item()`. |
| `func_800A8B10` | `0x800A8B10` (`0x114`) | `HOOK` / `HOOK_RETURN` | `coop_a8b10_begin` / `coop_a8b10_end` | Salva o argumento `player` e restaura os buffers/modelo Engineer (`0x3A`) no `SLOT1_DATA` após chamadas internas do jogo (inclusive em cutscenes). |
| `func_8004DE48` | `0x8004DE48` (`0x10C`) | `HOOK` | `coop_event_push` | Detecta qual jogador (`SLOT0_DATA` ou `SLOT1_DATA`) disparou um gatilho/interação (`type == 1`) e atualiza `trigger_player` + `trigger_ttl`. |
| `func_800944E0` | `0x800944E0` (`0x248`) | `HOOK` / `HOOK_RETURN` | `coop_event_mgr_begin` / `coop_event_mgr_end` | Detecta a flag de morte `DEATH_FLAG` (`0x8010E0FA`) antes de `func_8009CBB0` para definir o jogador morto como protagonista da cutscene de morte; faz merge temporário dos botões do P2 nos do P1 quando ambos têm `0x16EC < 2` (popups/skip) e decrementa `trigger_ttl`. |
| `func_8009C2E4` | `0x8009C2E4` (`0x348`) | `HOOK` / `HOOK_RETURN` | `coop_event_dispatch_begin` / `coop_event_dispatch_end` | Define `0x18CC` do protagonista (`set_protagonist(trigger_player)`) e troca `PLAYER_PTRS`/`CAM_PTRS`/`ROOM_STATE` + floats de câmera se `trigger_player == 1` antes de despachar eventos de fase. |
| `func_8009BE18` | `0x8009BE18` (`0x204`) | `HOOK` / `HOOK_RETURN` | `coop_script_run_begin` / `coop_script_run_end` | Garante que durante `in_cutscene` com `cutscene_protagonist == 1` os ponteiros continuem apontando para o P2 como protagonista do script. |
| `func_8009E1E0` | `0x8009E1E0` (`0x818`) | `HOOK` / `HOOK_RETURN` | `coop_cutscene_mode_begin` / `coop_cutscene_mode_end` | Na entrada da cutscene (`mode == 1` ou `mode == 3`), detecta ações diretas (`0x16EC == 23/25/12/13`), fixa `cutscene_protagonist`, para o movimento do outro jogador e ativa `in_cutscene = 1`. Na saída (`mode == 0` ou `mode == 2`), restaura a ordem P1/P2 e limpa a flag `0x20000200` em `SLOT1_DATA + 0x00F4`. |
| `func_80098F20` | `0x80098F20` (`0x150`) | `HOOK` / `HOOK_RETURN` | `coop_cutscene_teleport_begin` / `coop_cutscene_teleport_end` | Mantém transições intra-fase (escadas/mudança de sala) independentes por jogador; quando o P2 é o protagonista da transição (`PLAYER_PTRS[0] == SLOT1_DATA`), move `START_POS`/`START_ID`/`START_HEAD`/`START_CAM` para o slot 1 e preserva os do P1 no slot 0. |
| `func_800CF6A8` | `0x800CF6A8` (`0xC4`) | `PATCH` | `func_800CF6A8` | Substitui a barra de vida padrão pela HUD sci-fi customizada (`224x48` em escala `0.5f`) com retratos 3D do Jean-Luc (P1) e Engenheiro (P2). |

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

### 3.2. Modelo Engineer (`0x3A`), Renderização Split-Screen, HUD e Céu/Salas Internas-Externas
1. **`coop_p2_model` (`func_800593EC` retorno)**:
   - Incrementa `spawn_calls`. Na 2ª chamada (`spawn_calls == 2`) em Story com 2 jogadores:
   - Chama `set_protagonist(0)` (`SLOT0_DATA->0x18CC = 0xFF`, `SLOT1_DATA->0x18CC = 0xFE`).
   - Se `p2_character == 0` (*Engineer*): define `p2 + 0x16F5 = 0x3A`, limpa bit `2` de `p2 + 0x18CD`, executa `func_800A8B10(p2)`, `func_8004D6E8(p2, 0x37)`, `func_8006B374(p2)`, `func_8006B3B8(p2)`, `func_8006B564(p2)` e salva os 3 ponteiros (`0x188C`, `0x1890`, `0x1894`) em `0x80138EEC..0x80138EF4`.
2. **`coop_draw_begin` / `coop_draw_end` (`func_80025F10`)**:
   - Em qualquer sessão Story (`coop_story_session()`), incluindo cutscenes, chama `restore_engineer_model(SLOT1_DATA)`.
   - Em gameplay ativo (`coop_active()`): se `in_cutscene` ainda estiver `1`, restaura a ordem normal P1/P2; roda `log_status()` se `log != 0`; ativa `MODE = 2`, `0x8016A495 = 1`, `0x8016A4C1 = 0`, `0x8016A4BD = 0xFF`, `0x8010F848 = 0x10`, `0x8010F844 = 0`, `0x8010F845 = 0` e restaura no retorno.
3. **`coop_hud_update_begin` / `coop_hud_update_end` (`func_800D1E24`)**:
   - Em `coop_active()`, alterna `MODE` para `2` durante `func_800D1E24` para atualizar barras de vida (`0x801B1168+`), arma/munição/reload/laser (`0x8011FB80+`) de ambos os jogadores.
4. **`coop_fb_clear_begin` / `coop_fb_clear_end` (`func_80026EB8`)**:
   - Na entrada de `func_80026EB8` (logo após `func_80026A10(dl, 0, 0)` em `0x80025F38` ter sobrescrito `0x8016A40F = 0`), calcula `fb_p0_out` e `fb_p1_out` via `room_is_outdoor(room, subroom)` e define `U8(0x8016A40F) = (!fb_p0_out && fb_p1_out) ? 1 : 0`.
   - No retorno (`coop_fb_clear_end`), restaura `0x8016A40F` e, caso `fb_p0_out != fb_p1_out` (um jogador em área externa e o outro em sala interna fechada), emite 24 bytes de comandos RDP (`gDPSetFillColor(0x00010001)`, `gDPFillRectangle(0, uly, 319, lry)` e `gDPPipeSync()`) em `U32(0x801D2C80)` para preencher de preto somente a metade da tela (`Y=0..119` se P1 interno, `Y=120..239` se P2 interno) do jogador interno.
5. **`coop_sky_update_begin` / `coop_sky_update_end` (`func_800C4EE0`)** e **`coop_sky_draw_begin` / `coop_sky_draw_end` (`func_800C4FB0`)**:
   - Em `func_800C4EE0` (update do objeto de Céu/Nuvens criado por `func_800C52BC`): se o P1 estiver em sala interna (`out0 < 0`) e o P2 em área externa (`out1 >= 0`), redireciona temporariamente `obj + 0x20` para o slot do P2 (`U32(p1 + 0x1740)`) e `S16(p1 + 0x8E) = out1`.
   - Em `func_800C4FB0` (draw chamado uma vez por viewport): lê o viewport atual `vp = U8(0x8016A40F) ? 1 : 0`, redireciona temporariamente `obj + 0x20` e `PLAYER_PTRS[0]` para `PLAYER_PTRS[vp]`, e ajusta `S16(p_vp + 0x8E)` para `get_outdoor_room(room, subroom)` (se `>= 0`) ou para a sala interna `room` (se `< 0`, fazendo `0x800C5008` abortar o desenho das nuvens e do Sky Dome 3D naquele viewport).

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
1. **`coop_event_push` (`func_8004DE48`)**:
   - Em `func_8004DE48(type, sub, val, obj)`, se `type == 1 && obj != 0`, verifica `U32(obj + 0x2C)` para gravar `trigger_player = 0` (`SLOT0_DATA`) ou `1` (`SLOT1_DATA`, com `trigger_ttl = 180` frames decrementado em `coop_event_mgr_end`). Isso cobre tanto linhas de gatilho quanto interações de botão Ação (que chamam `func_8004DE48(1, ..., s1)` em `0x800A9868`).
2. **`coop_event_mgr_begin` / `coop_event_mgr_end` (`func_800944E0`)**:
   - Verifica `U8(DEATH_FLAG) != 0` (`0x8010E0FA`) logo na entrada, antes de `func_800944E0` limpar a flag e chamar `func_8009CBB0()`. Se `S8(SLOT1_DATA + 0x176B) <= 0 && S8(SLOT0_DATA + 0x176B) > 0`, define `cutscene_protagonist = 1`, `trigger_player = 1`, `set_protagonist(1)`, chama `swap_all_player_state()` e liga `in_cutscene = 1`.
3. **`coop_event_dispatch_*` (`func_8009C2E4`)** e **`coop_script_run_*` (`func_8009BE18`)**:
   - Aplicam `set_protagonist(trigger_player)` (`0x18CC = 0xFF` no protagonista e `0xFE` no outro) e mantêm `PLAYER_PTRS`, `CAM_PTRS`, `ROOM_STATE` e os 6 pares de floats de câmera (`0x8016D6D8`, `0x801B0CE8`, `0x80188FB0`, `0x80168748`, `0x8016D268`, `0x801C3808`) trocados enquanto `trigger_player == 1` ou `in_cutscene && cutscene_protagonist == 1`.
4. **`coop_cutscene_mode_*` (`func_8009E1E0`)**:
   - Em `mode == 1 || mode == 3` (início de cutscene normal, escada/porta ou cutscene de morte): verifica também diretamente `U16(SLOT0_DATA/SLOT1_DATA + 0x16EC)` (`23` = escada, `25` = switch/painel, `12`/`13` = porta) e `S8(SLOT1_DATA + 0x176B) <= 0` para definir `trigger_player`, trava `cutscene_protagonist = trigger_player`, para o movimento do outro jogador (`0x0154 = 0`, `0x0158 = 0`, `U16(0x16EC) = 0`, `U8(0x16F0) = 1`), executa `swap_all_player_state()` se `cutscene_protagonist == 1` e liga `in_cutscene = 1`.
   - Em `mode == 0 || mode == 2` (fim de cutscene normal ou fim da cutscene de morte): restaura `ensure_normal_player_order()`, limpa a flag de bloqueio de cutscene em `SLOT1_DATA` (`0x16EC = 0`, `0x16F0 = 1`, `0x0088 = 0x1000`, `0x00F4 = (0x00F4 & ~0x20000200u) | 2`), `set_protagonist(0)`, `cutscene_protagonist = 0`, `trigger_player = 0`, `trigger_ttl = 0` e `in_cutscene = 0`.
   - Em `coop_draw_begin` (`func_80025F10`), antes de `if (coop_active())`, há também um fallback defensivo que limpa `in_cutscene` e restaura a ordem normal se `(U16(STATE) & 1)` estiver ativo e `CUTSCENE_FLAG == 0`, `CUTSCENE_CAM == 0` e `(U16(SCRIPT_FLAGS) & 0x0010) == 0`.
5. **`coop_cutscene_teleport_*` (`func_80098F20`)**:
   - Mantém o teleporte intra-fase (escadas e transições de área dentro do mesmo estágio) restrito apenas ao jogador que acionou a transição (`PLAYER_PTRS[tele_idx]`), permitindo que o outro jogador permaneça em sua área atual.
   - Quando `player_idx == 0` e `U32(PLAYER_PTRS) == SLOT1_DATA` (Player 2 executando a transição), salva `START_POS[0]`, `START_ID[0]`, `START_HEAD[0]` e `START_CAM[0]` na entrada e, no retorno, copia os novos valores gravados para o slot 1 (`START_POS + 12`, `START_ID + 2`, `START_HEAD + 4`, `START_CAM + 12`) e restaura os dados do Player 1 no slot 0.
6. **`coop_room_begin` / `coop_room_end` (`func_8003C58C`)**:
   - Ativa `U8(MODE) = 2` sempre que `coop_story_session() && U8(MODE) == 1` (incluindo chamadas de `func_8003C58C` feitas por `func_80098F20` durante `in_cutscene == 1`).
   - Como `func_8003C58C` indexa `ROOM_STATE` (`0x8016A410`) pelo campo fixo `S16(player + 0x90)` (`0` para `SLOT0_DATA`, `1` para `SLOT1_DATA`), desinverte temporariamente `ROOM_STATE[0]` ↔ `ROOM_STATE[1]` caso `U32(PLAYER_PTRS) == SLOT1_DATA` e restaura no retorno.
7. **`coop_a8b10_*` (`func_800A8B10`)**:
   - Restaura os buffers do Engineer no `SLOT1_DATA` sempre que `func_800A8B10` rodar para o Player 2 (incluindo transições de cutscene).

---

### 3.7. Arquitetura Interna do Motor de Scripts, Cutscenes e Sequência de Morte (`ROM 0x9315CC`)

Esta seção documenta o mapeamento completo de engenharia reversa do motor de scripts e cutscenes obtido direto da ROM (`baserom.us.z64`, onde `rom_offset = vram - 0x80024C00`):

#### 1. Detecção de Morte e Armadilha de Sign-Extension em MIPS (`0x8010E0FA`)
- Na função de update do jogador `func_80059E5C` (`0x8005A0D0..0x8005A170`):
  - Quando `U8(0x8016A40E) & 1` (modo Story) e `S8(slot + 0x176B) <= 0` (HP zerado):
    - `0x8005A154`: chama `func_8006208C(slot)` (verifica se há respawn de modo especial em `func_800DD8CC`; no modo Story normal retorna `0`).
    - `0x8005A160`: grava `U16(slot + 0x16EC) = 22` (`0x16`, estado de morte).
    - `0x8005A164`: grava `U8(slot + 0x176B) = 0`.
    - `0x8005A168`: chama `func_8009CBA0` (`0x8009CBA0`), que executa:
      ```mips
      0x8009CBA0: addiu $v0, $zero, 1
      0x8009CBA4: lui   $at, 0x8011
      0x8009CBA8: jr    $ra
      0x8009CBAC: sb    $v0, 0xE0FA($at)
      ```
  - **Atenção ao cálculo de endereço MIPS:** instruções de load/store tratam o offset imediato de 16 bits como **signed** (`int16_t`). Como `0xE0FA` tem o bit 15 ligado (`0xE0FA = -7942 = -0x1F06`), o endereço efetivo é `0x80110000 - 0x1F06 = 0x8010E0FA` (e **não** `0x8011E0FA`). O mesmo vale para:
    - `CUTSCENE_FLAG`: `lui 0x8011` + `0xEDE4` (`-4636`) = **`0x8010EDE4`**
    - `SCRIPT_FLAGS`: `lui 0x8011` + `0xE0F4` (`-7948`) = **`0x8010E0F4`**
    - `DEATH_FLAG`: `lui 0x8011` + `0xE0FA` (`-7942`) = **`0x8010E0FA`**
    - `PLAYER_PTRS`: `lui 0x8014` + `0x90C0` (`-28480`) = **`0x801390C0`**
    - `CAM_PTRS`: `lui 0x8018` + `0x9280` (`-28032`) = **`0x80179280`**
    - `SLOT0_DATA`: `lui 0x8018` + `0x9350` (`-27824`) = **`0x80179350`**

#### 2. Carregamento e Disparo do Script Global de Morte (`func_800944E0` $\rightarrow$ `func_8009CBB0`)
- Em `func_800944E0` (`0x80094534..0x80094568`), quando `(U16(STATE) & 9) != 0` e `U8(0x8010E0FA) != 0`:
  - Se `U16(STATE) & 1`, zera `U8(0x8010E0FA) = 0` e chama `func_8009CBB0()` (`0x8009CBB0`).
- `func_8009CBB0()` executa 4 chamadas em sequência:
  1. `func_800941C8(8)`: toca a música (BGM `8`) de morte / Game Over.
  2. `func_800996B0(0x7F)`: como o argumento é `0x7F` (`127`), carrega o **arquivo de script global `0`** a partir da tabela de arquivos em ROM `0x931470` (`offset = *(u32*)(0x931470 + 8) = 0x15C` $\rightarrow$ **`ROM 0x9315CC`**) para o buffer de scripts em RAM `0x8020A800` (gravando o ponteiro em `*(u32*)0x8010E0F0`).
  3. `s = func_8009C050(33, 0)`: busca na tabela carregada o script correspondente ao evento `33` (`0x21`, morte).
  4. `func_8009BE18(s, 0)`: inicia a execução do bytecode do script de morte.

#### 3. Estrutura da VM de Bytecode de Scripts (`func_8009C62C` e Tabela `0x80119630`)
- O interpretador de bytecode `func_8009C62C` (`0x8009C62C..0x8009C700`) lê comandos de tamanho variável a partir do ponteiro atual de script (`*(u32*)0x8010E0FC`), onde o 1º byte é o `opcode` (`0x00..0x2B`), e despacha via tabela de 44 entradas (8 bytes por entrada: `{ u32 handler_vram, u8 num_args, ... }`) em **`0x80119630`** (`ROM 0xF4A30`).
- Os argumentos de cada instrução são lidos como `s16` big-endian desalinhados (`lbu hi, lbu lo`) e empilhados nos registradores `$a0..$a3` e stack `0x10($sp)..0x20($sp)`.
- **Bytecode decodificado do Script Global de Morte (`ROM 0x9315CC`, Script 0 / Script 3):**
  1. `op=0x08 (handler 0x8009AF00) args=[3]` $\rightarrow$ chama **`func_8009E1E0(3)`** (entra em modo cutscene in-game `3`).
  2. `op=0x24 (handler 0x8009B990) args=[0, 0, 999, 6, 1072]` $\rightarrow$ chama **`func_8009CC10(0.0f, 0.0f, 999.0f, 6, 1072)`** (agenda a animação de morte `1072` = `0x430` no jogador `PLAYER_PTRS[0]`).
  3. `op=0x12 (handler 0x8009B30C) args=[1]` $\rightarrow$ chama **`func_800A0DC8(1)`** (inicializa a câmera de cutscene `1` copiando os 6 floats de `0x8016D6D8`, `0x801B0CE8`, `0x80188FB0`, `0x80168748`, `0x8016D268`, `0x801C3808`).
  4. `op=0x18 (handler 0x8009B4BC) args=[0, 1, 6, 255, 200, 180, 200, 0]` $\rightarrow$ chama **`func_8009D7F8`** (posiciona o Eye da câmera `1` em offset `(200, 180, 200)` relativo ao ator `255` = `0xFF`).
  5. `op=0x18 (handler 0x8009B4BC) args=[1, 1, 24, 255, 0, 150, 0, 0]` $\rightarrow$ chama **`func_8009DAD8`** (posiciona o LookAt da câmera `1` em offset `(0, 150, 0)` relativo ao ator `255` = `0xFF`).
  6. `op=0x16 (handler 0x8009B3E8) args=[0]` $\rightarrow$ configuração de câmera/transição.
  7. `op=0x11 (handler 0x8009B2FC) args=[1]` $\rightarrow$ chama **`func_8009E9F8(1)`** (ativa a câmera de cutscene `1`, gravando `U8(0x8010EDE5) = 1`).
  8. `op=0x18 (handler 0x8009B4BC) args=[2, 1, 0, 255, -150, 240, 80, 3000]` $\rightarrow$ chama **`func_8009DE54`** (orbita a câmera `1` ao redor do ator `0xFF` durante `3000` ms).
  9. `op=0x05 (handler 0x8009AE18) args=[2900]` $\rightarrow$ aguarda `2900` ms (~96 frames), cedendo execução frame a frame para `func_800944E0` continuar via `func_8009BE18(0, 1)`.
  10. `op=0x06 (handler 0x8009AE40) args=[0, 1, 1000]` e `op=0x05 args=[1000]` $\rightarrow$ aplica fade-out de `1000` ms e aguarda concluir.
  11. `op=0x13 (handler 0x8009B31C) args=[1]` $\rightarrow$ encerra a câmera de cutscene `1`.
  12. `op=0x08 (handler 0x8009AF00) args=[5]` $\rightarrow$ chama **`func_800DD388(0)`** (prepara estado de fim de vida / Continue).
  13. `op=0x08 (handler 0x8009AF00) args=[2]` $\rightarrow$ chama **`func_8009E1E0(2)`** (transição final para tela de Game Over / Continue via `func_8008EF18`).

#### 4. Os 4 Modos de `func_8009E1E0(int mode)` (`0x8009E1E0..0x8009E9F0`)
- **`mode == 0` (`0x8009E230`)**: Sai de uma cutscene normal de volta para o gameplay (`U16(STATE) |= 1`, `U16(STATE) &= ~8`, `U8(0x8010EDE4) = 0`).
- **`mode == 1` (`0x8009E388`)**: Entra em cutscene de história padrão (`U16(STATE) &= ~1`, `U16(STATE) |= 8`, copia os 6 floats de câmera de `0x8016D6D8+` e prepara `PLAYER_PTRS[0]`).
- **`mode == 2` (`0x8009E344` / `0x8009AF38`)**: Encerra a sequência de morte / fim de partida chamando `func_8008EF18`.
- **`mode == 3` (`0x8009E72C`)**: Entra em cutscene in-game mantendo `STATE & 1` ativo, mas gravando **`U8(0x8010EDE4) = 1`** (`CUTSCENE_FLAG`) e **`U8(0x8011F784) = 1`** (`CUTSCENE_CAM`), copiando os 6 floats de câmera (`0x8016D6D8+`) e resetando `PLAYER_PTRS[0]->0x16EC = 0`, `0x18D1 = 0` e `0x18D2 = 0`.

#### 5. Como o Jogador Executa a Animação de Morte `1072` (`0x430`) Durante `CUTSCENE_FLAG == 1`
- Na função de tick do objeto do jogador `func_80058360` (`0x800583C8..0x80058424`):
  - Se `U8(0x8010EDE4) != 0` (`CUTSCENE_FLAG == 1`) e `U8(MODE) & 1 != 0`, o jogo **não** chama `func_80059E5C`; em vez disso, chama **`func_800CB660(obj)`** (`0x800CB660`).
- Dentro de `func_800CB660(obj)`:
  - Lê `slot = U32(obj + 0x2C)` e `cmd = U8(slot + 0x18CE)`.
  - Quando `func_8009CC10` (`0x8009CC10`) é chamada pelo `op=0x24 args=[0, 0, 999, 6, 1072]`, ela verifica se `U8(PLAYER_PTRS[0] + 0x18CD) == 0` e grava em `PLAYER_PTRS[0]`:
    - `U8(slot + 0x18CE) = 6`
    - `F32(slot + 0x18DC) = 999.0f`
    - `F32(slot + 0x18E0) = 1072.0f` (`0x430`)
  - No dispatch de `func_800CB660` (`tabela 0x8011B420`), o caso `6` (`0x800CBA08`) chama **`func_800CC57C(slot)`** (`0x800CC57C`), que converte `F32(slot + 0x18E0)` para inteiro (`1072 = 0x430`) e grava `U16(slot + 0x16E0) = 0x430` e `U16(slot + 0x16E2) = 0x430` (a animação de queda/morte).
  - Em seguida (`0x800CBA1C..0x800CBA50`), `func_800CB660` usa `U8(slot + 0x16F5)` (`model_id`, `0x3A` no Engineer) na tabela `0x80109540`, chama `func_800532CC(slot)` (avanço da animação), `func_80069D44(slot)` (matrizes de ossos) e, como `model_id >= 0x22`, chama `func_8006B564(slot)` (skinning das juntas dinâmicas).

---

### 3.8. Arquitetura Interna do Pipeline de Renderização de Salas, Clear do Framebuffer e Skybox/Nuvens

Esta seção documenta o mapeamento completo de engenharia reversa do pipeline gráfico de salas internas/externas, limpeza do framebuffer e renderização da camada de nuvens + Sky Dome 3D obtido da ROM (`baserom.us.z64`):

#### 1. Sequência de Inicialização do Frame em `func_80025F10` e a Sobrescrita de `0x8016A40F`
- Na função principal de renderização `func_80025F10` (`0x80025F10..0x80026470`):
  1. `0x80025F38`: chama `func_80026A10(dl, 0, 0)` para inicializar o estado do RSP/RDP e o viewport padrão `0` (tela cheia `320x240` antes do loop de viewports).
     - Dentro de `func_80026A10` (`0x80026A10..0x80026EB4`), a instrução em **`0x80026A68`** (`sb $s1, 0xA40F($at)`, onde `$s1 = $a1 = 0` e `$at = 0x80170000`) **grava `0` em `U8(0x8016A40F)`**!
     - **Descoberta crítica:** qualquer modificação em `U8(0x8016A40F)` feita em `coop_draw_begin` (na entrada de `func_80025F10`) era sobrescrita de volta para `0` em `0x80025F38`, **antes** de `func_80026EB8` ser chamada em `0x80025F90`.
  2. `0x80025F90`: chama **`func_80026EB8()`** (`0x80026EB8..0x8002713C`), que limpa o Z-buffer e o framebuffer de cor da tela inteira (`0, 0` até `319, 239`).
  3. `0x80025FA0..0x80026074`: quando `U8(0x8016A40E) & 2` (modo Split-Screen) e `U8(0x8016A40C) == 2` (`PLAYERS == 2`), executa um loop para `vp = 0` e `vp = 1`:
     - Chama `func_80026A10(dl, vp, 1)` (que grava `U8(0x8016A40F) = vp` em `0x80026A68` e configura o `gSPViewport` e `gDPSetScissor` para a metade superior `Y=0..119` quando `vp=0` ou inferior `Y=120..239` quando `vp=1`);
     - Chama `func_80027F90()` e `func_800280C4()`, que percorrem as listas de objetos de cena desenhando o mapa (`func_8003E614`), o objeto de Céu/Nuvens (`func_800C4FB0`), os jogadores e os inimigos no viewport atual.

#### 2. Tabela de Salas (`0x801D3750`), `func_8003F53C` e o Clear do Framebuffer em `func_80026EB8`
- A tabela de salas da fase carregada começa em **`0x801D3750`**, com entradas de **`0x58` (`88`) bytes** por sala (`room_entry = 0x801D3750 + room_id * 0x58`):
  - `+0x10` (`0x801D3760 + room * 0x58`, `float`): altura do teto (`ceiling_y`) da sala, usada para posicionar a altura `Y` do Sky Dome.
  - `+0x14` (`0x801D3764 + room * 0x58`, `u8`): índice de paleta de iluminação/céu da sala na tabela `0x80103DB0`.
  - `+0x15` (`0x801D3765 + room * 0x58`, `u8`): byte de flags da sala, onde o **bit `0` (`& 1`)** indica o tipo de ambiente:
    - **`(flags & 1) == 0`**: sala **externa (outdoor)** — possui céu aberto;
    - **`(flags & 1) == 1`**: sala **interna (indoor)** — ambiente fechado com fundo preto.
- Dentro de `func_80026EB8` (`0x80026EB8..0x8002713C`):
  - Em `0x80026F68`, chama **`func_8003F53C()`** (`0x8003F53C..0x8003F5F8`), que lê o viewport ativo `vp = U8(0x8016A40F) ? 1 : 0`, carrega `room = S16(0x8016A410 + vp * 4)` e `subroom = S16(0x8016A412 + vp * 4)`, e verifica `U8(0x801D3765 + r * 0x58) & 1` tanto para `room` quanto para `subroom` (caso `subroom >= 0`, quando uma porta está aberta conectando duas salas).
  - Se pelo menos uma das salas for externa (`(flags & 1) == 0`), `func_8003F53C()` retorna `1`; se ambas forem internas, retorna `0`.
  - Em `0x80026F70..0x80026FC8`:
    - Se `func_8003F53C()` retornou `1` (outdoor), `func_80026EB8` lê os 3 componentes RGB da cor do céu em **`U16(0x801B8C74)`** (R), **`U16(0x801B8C76)`** (G) e **`U16(0x801B8C78)`** (B) (atualizados por `func_8003E800` em `0x8003EA18..0x8003EA40` a partir da paleta `0x80103DB3..5`), converte para `RGBA5551` de 16 bits duplicado em 32 bits (`(c << 16) | c`) no registrador `$s5`.
    - Se `func_8003F53C()` retornou `0` (indoor), define `$s5 = 0x00010001` (preto puro em `RGBA5551` com alpha = 1).
  - Em `0x800270F4..0x80027110`, emite para o ponteiro global de Display List **`U32(0x801D2C80)`**:
    - `gDPSetFillColor($s5)` (`0xF7000000, $s5`);
    - `gDPFillRectangle(0, 0, 319, 239)` (`0xF64FC3BC, 0x00000000`);
    - `gDPPipeSync()` (`0xE7000000, 0x00000000`).
- **Como `coop_fb_clear_begin` / `coop_fb_clear_end` dividem o Clear do Framebuffer:**
  - Na entrada de `func_80026EB8`, `coop_fb_clear_begin` define `U8(0x8016A40F) = (!fb_p0_out && fb_p1_out) ? 1 : 0`, garantindo que `func_80026EB8` preencha o framebuffer inteiro com a cor do céu sempre que pelo menos um jogador estiver vendo área externa.
  - No retorno (`coop_fb_clear_end`), como o RDP ainda está em modo `G_CYC_FILL` apontando para o color framebuffer atual, quando `fb_p0_out != fb_p1_out`, o hook anexa diretamente 3 comandos Fast3DEX (24 bytes) em `U32(0x801D2C80)`:
    1. `gDPSetFillColor(0x00010001)` (`0xF7000000, 0x00010001`);
    2. `gDPFillRectangle(0, uly, 319, lry)` (`0xF6000000 | (319 << 14) | (lry << 2), (uly << 2)`), onde `uly=0, lry=119` quando P1 é indoor ou `uly=120, lry=239` quando P2 é indoor;
    3. `gDPPipeSync()` (`0xE7000000, 0x00000000`).

#### 3. O Objeto de Céu / Nuvens (`func_800C52BC`, Update `func_800C4EE0`, Draw `func_800C4FB0`)
- Durante o carregamento da fase (`0x8004BF24`), a função **`func_800C52BC`** (`0x800C52BC..0x800C53B8`) aloca um único objeto de tarefa (`func_80027470(2, 4, 0x100)`) responsável por renderizar tanto a camada de nuvens 2D quanto o domo 3D do céu (Sky Dome):
  - `obj + 0x20` (`parent`): gravado em `0x800C52E8` como `U32(U32(0x801390C0) + 0x1740)` — isto é, **sempre o objeto de tarefa do Player 1**!
  - `obj + 0x24` (`update_fn`): **`0x800C4EE0`** (`func_800C4EE0`).
  - `obj + 0x28` (`draw_fn`): **`0x800C4FB0`** (`func_800C4FB0`).
  - `obj + 0x35`: `1` (flag que faz o dispatcher `func_800280C4` chamar `draw_fn` em todos os viewports ativos).
  - `data + 0x168`: `0x01006640` (ID do modelo 3D do Sky Dome).
- **Função de Update `func_800C4EE0` (`0x800C4EE0..0x800C4FAC`):**
  - Lê `parent_player = U32(U32(obj + 0x20) + 0x2C)` (struct do Player 1) e `room = S16(parent_player + 0x8E)`.
  - Copia `X` (`+0x00`) e `Z` (`+0x08`) do jogador para `data + 0x00` e `data + 0x08`, copia `ceiling_y` (`F32(0x801D3760 + room * 0x58)`) para `data + 0x04`, atualiza o offset UV das nuvens e chama `func_80028740(data)` (`0x800C4FA0`) para construir a matriz de transformação 3D (`data + 0x30`) do Sky Dome.
  - Se o P1 estivesse em uma sala interna baixa e o P2 do lado de fora, a matriz `data + 0x30` era montada na posição e altura de teto da sala interna do P1!
- **Função de Draw `func_800C4FB0` (`0x800C4FB0..0x800C52B8`):**
  - Chamada uma vez para cada viewport (`vp = 0` e `vp = 1`).
  - Em **`0x800C4FC0..0x800C5008`**, ela **ignorava completamente `U8(0x8016A40F)`**: lia sempre `parent_player = U32(U32(obj + 0x20) + 0x2C)` (Player 1) e `room = S16(parent_player + 0x8E)`, e testava:
    ```mips
    0x800C4FF8: addu $v0, $v0, $v1          # room * 0x58 + 0x801D0000
    0x800C4FFC: lbu  $t6, 0x3765($v0)       # U8(0x801D3765 + room * 0x58)
    0x800C5004: andi $t7, $t6, 0x0001       # bit 0 == indoor?
    0x800C5008: bne  $t7, $zero, 0x800C52AC # se indoor, pula TUDO e retorna!
    ```
  - Caso não pulasse (`bit 0 == 0`, outdoor):
    - `0x800C5078..0x800C5218`: desenha a camada de nuvens 2D em movimento usando a textura IA16 em `0x802B4938`;
    - `0x800C521C..0x800C529C`: lê `p0 = U32(0x801390C0)` (que durante a renderização é sempre o P1!) e chama `func_8003E800(S16(p0 + 0x8E))` para carregar a cor/iluminação do céu daquela sala;
    - `0x800C52A4`: chama `func_800282BC(data)` para desenhar o modelo 3D do Sky Dome (`0x01006640`).
  - **Consequência dupla do bug original:**
    1. Quando o P1 entrava em uma sala interna e fechava a porta enquanto o P2 ficava lá fora, `0x800C5008` via a sala interna do P1 e abortava o desenho das nuvens e do Sky Dome 3D também no viewport do P2!
    2. Quando o P2 entrava em uma sala interna e o P1 ficava lá fora, `0x800C5008` via a sala externa do P1 e desenhava as nuvens e o Sky Dome 3D dentro da sala fechada do P2!
- **Solução em `coop_sky_update_begin/end` (`func_800C4EE0`) e `coop_sky_draw_begin/end` (`func_800C4FB0`):**
  - A função auxiliar `get_outdoor_room(room, subroom)` retorna `room` se `(U8(0x801D3765 + room * 0x58) & 1) == 0`, senão `subroom` se `(U8(0x801D3765 + subroom * 0x58) & 1) == 0`, ou `-1` se ambas forem internas.
  - Em `func_800C4EE0`, se o P1 estiver interno (`out0 < 0`) e o P2 externo (`out1 >= 0`), `obj + 0x20` aponta temporariamente para o slot de objeto do P2 (`U32(p1 + 0x1740)`) com `S16(p1 + 0x8E) = out1`.
  - Em `func_800C4FB0`, para o viewport atual `vp = U8(0x8016A40F) ? 1 : 0`, `obj + 0x20` e `PLAYER_PTRS[0]` apontam temporariamente para `p_vp = U32(PLAYER_PTRS + vp * 4)`, e `S16(p_vp + 0x8E)` recebe `out_room` (quando `>= 0`, garantindo nuvens + Sky Dome mesmo olhando para fora através de uma porta aberta) ou `room` (quando `< 0`, fazendo o `bne` em `0x800C5008` pular limpa e nativamente o desenho naquele viewport interno).

### 3.16 `func_800CF6A8` — HUD Customizada com Retrato 3D e Barra de Vida Inclinada
- **Endereço na ROM:** `0x800CF6A8` (`size = 0xC4`).
- **Tipo de interceptação:** `RECOMP_PATCH` (`func_800CF6A8(int player_idx, int x, int y)`).
- **Engenharia Reversa do Sistema de Sprites 2D (`func_8007A470` / `func_8007A754` / `func_8007B8F4`):**
  - No início do desenho da HUD (`func_800B5420`), `func_8007A394(0x801D2C80)` emite `gSPDisplayList(0x8010AF58)`, configurando o RDP em `G_CYC_1CYCLE`, `G_TP_NONE`, `G_AC_THRESHOLD` e combiner `G_CC_MODULATEIA_PRIM` (`FC119623 FF2FFFFF`).
  - `func_8007B8F4(dl_ptr_ptr, sprite_ptr, x, y)` grava `(x, y)` em `sprite_ptr + 0x14` / `+0x16` (`func_8007B8E8`) e chama o renderizador universal de sprites `func_8007A754(dl_ptr_ptr, sprite_ptr)`.
  - Layout da estrutura de sprite de 40 bytes (`0x28` bytes):
    - `+0x00` (`u8 fmt`): `0` = `G_IM_FMT_RGBA`, `2` = `G_IM_FMT_CI`, `3` = `G_IM_FMT_IA`, `4` = `G_IM_FMT_I`.
    - `+0x01` (`u8 siz`): `0` = `4b`, `1` = `8b`, `2` = `16b` (`G_IM_SIZ_16b`).
    - `+0x02` (`u16 width`), `+0x04` (`u16 height`), `+0x06` (`u16 pad`).
    - `+0x08` (`u32 tex_ptr`): Ponteiro KSEG0 no Segmento 0 (`0x80000000..0x807FFFFF`). Importante: o RT64 resolve `G_SETTIMG` como `segments[(addr >> 24) & 0xF] + (addr & 0xFFFFFF)` dentro dos 8 MB de RDRAM, portanto texturas dinâmicas do mod (cujo `.rodata` linka em `0x81000000`) devem ser copiadas para a faixa de Expansion Pak `< 8MB` (`0x80500000`).
    - `+0x0C` (`u32 tlut_ptr`): `0` para `RGBA16`.
    - `+0x10` (`u16 flags`): bit `0x0001` = ativo/visível; bit `0x0020` = `G_TF_POINT` (quando `0`, usa `G_TF_BILERP`); bit `0x2000` = pula `func_8007A470` (deve ser limpo antes de cada chamada para que `func_8007A470` configure `G_SETPRIMCOLOR` e `G_SETOTHERMODE_L = 0x0F0A7008`).
    - `+0x14` (`s16 x`), `+0x16` (`s16 y`).
    - `+0x18` (`f32 scale_x`), `+0x1C` (`f32 scale_y`): Em `0.5f` (`0x3F000000`), cada bloco de `32x32` texels fatiado por `func_8007A754` mapeia para `16x16` pixels exatos na tela `320x240` com `dsdx = dtdy = 2048` (2 texels por pixel N64, renderizados em resolução total pelo RT64).
    - `+0x20..0x23` (`u8 r, g, b, a`): `255, 255, 255, 255`.
- **Engenharia Reversa do Descompressor LZSS da ROM (`func_8005230C` $\rightarrow$ `func_80071FE0`):**
  - `func_8005230C(rom_offset, dst_ram)` faz DMA de blocos de `0x400` bytes da ROM e chama `func_80071FE0` (`0x80071FE0..0x800720BC`).
  - `func_80071FE0` lê um bitstream MSB-first (`mask = 0x80 >> bit`):
    - Bit de controle `1`: lê 8 bits literais e grava no destino e no ring buffer de `0x4000` bytes (`0x8021FA00`, cursor inicial `r = 1`);
    - Bit de controle `0`: lê 14 bits de posição no ring buffer (`0` = fim do stream) e 4 bits de comprimento (`count = len4 + 3`, pois `0x80072064: addiu $s2, $v0, 2` e o loop copia de `0` até `$s2` inclusive via `slt $v0, $s2, $s1`).

### 3.17 `func_80098F20` & `func_8003C58C` — Arquitetura Interna de Transição de Sala/Escada Intra-Fase e Indexação de `ROOM_STATE`

Esta seção documenta o mapeamento completo de engenharia reversa da transição de área por escada (`func_80098F20`), do rastreador de sala/subsala (`func_8003C58C`) e do desbloqueio atrasado de cutscenes (`func_800A0F58` / `func_800A10B8`):

#### 1. Diferença entre Mudança de Estágio (`0x8009B1A0`) e Transição Intra-Fase / Escada (`0x8009B1EC` $\rightarrow$ `func_80098F20`)
- No dispatcher de opcodes da VM de scripts de fase (`0x8009B180..0x8009B200`):
  - **Mudança de Estágio Completa (`0x8009B1A0..0x8009B1E8`):**
    - Grava `U16(0x8016A418) = 1` (flag de requisição de carregamento de novo estágio), `U16(0x8016A41A) = next_stage_id` e chama `func_8009E184()`, disparando um novo ciclo completo `func_8004C438` $\rightarrow$ `func_8006B310` (`coop_stage_players`).
  - **Transição Intra-Fase / Escada (`0x8009B1EC..0x8009B1F8`):**
    - Único ponto em toda a ROM que chama **`func_80098F20`** (`0x8009B1F0: jal 0x80098F20; addu $a1, $zero, $zero`), passando `a0 = pos_id` e `a1 = 0` (`player_idx = 0`).
    - Como todas as salas de uma fase estão simultaneamente carregadas na RAM e cada viewport (`vp = 0` e `vp = 1`) renderiza a sala em `ROOM_STATE[vp]` (`0x8016A410` e `0x8016A414`), os dois jogadores podem permanecer em salas/áreas completamente distintas dentro da mesma fase.

#### 2. Funcionamento Interno de `func_80098F20(int pos_id, int player_idx)` (`0x80098F20..0x8009906C`)
- `0x80098F50`: chama `func_800513FC(pos_id, sp + 0x10, sp + 0x30)` para buscar na tabela de pontos de entrada da fase:
  - `sp + 0x10..0x18`: coordenadas `X, Y, Z` (`float[3]`) da nova posição;
  - `sp + 0x20`: ângulo de rotação inicial (`float`);
  - `sp + 0x2A`: `start_id` (`u16`, índice de polígono/ponto de partida);
  - `sp + 0x30..0x38`: coordenadas `X, Y, Z` (`float[3]`) de câmera inicial.
- `0x80098F54..0x80098FD8`: grava os valores buscados nos arrays globais indexados por `player_idx` (que na chamada da VM em `0x8009B1F4` é sempre `0`):
  - `U16(0x8016A41E) = pos_id`;
  - `START_POS[player_idx]` (`0x8016A424 + player_idx * 12`) = `X, Y, Z`;
  - `START_ID[player_idx]` (`0x8016A39A + player_idx * 2`) = `start_id`;
  - `START_HEAD[player_idx]` (`0x8016A454 + player_idx * 4`) = `heading`;
  - `START_CAM[player_idx]` (`0x8016A464 + player_idx * 12`) = `cam_X, cam_Y, cam_Z`.
- `0x80098FDC..0x80099050`: carrega `p = U32(0x801390C0 + player_idx * 4)` (`PLAYER_PTRS[player_idx]`), copia `X, Y, Z` para `p + 0x00..0x08`, `heading` para `p + 0x64`, `start_id` para `U16(p + 0x8A)`, resolve a sala correspondente via `room = func_8003CB80(start_id & 0x0FFF)` (`0x80099044`) e grava `U16(p + 0x8E) = room`.
- `0x80099054`: chama **`func_8003C58C(p)`** para atualizar `ROOM_STATE` com a nova sala de `p`.

#### 3. Indexação de `ROOM_STATE` por `S16(player + 0x90)` em `func_8003C58C(unsigned int player)` (`0x8003C58C..0x8003CB68`)
- Em **`0x8003C5CC..0x8003C5EC`**:
  ```mips
  0x8003C5CC: lh  $v0, 0x0090($s0)   # idx = S16(player + 0x90) (0 p/ SLOT0_DATA, 1 p/ SLOT1_DATA)
  0x8003C5D0: lhu $v1, 0x008E($s0)   # room = U16(player + 0x8E)
  0x8003C5DC: sll $v0, $v0, 2        # offset = idx * 4
  0x8003C5E0: lui $at, 0x8017
  0x8003C5E4: addu $at, $at, $v0
  0x8003C5EC: sh  $v1, 0xA410($at)   # U16(0x8016A410 + idx * 4) = room (ROOM_STATE[idx].room)
  ```
- Em **`0x8003C6A0..0x8003C84C`** (quando `U8(0x8016A40E) & 2 != 0`, isto é, `MODE == 2`):
  - Atualiza a subsala `U16(0x8016A412 + idx * 4)` (`ROOM_STATE[idx].subroom`) e os estados de transição `0x8012D400[idx]` / `0x801CF9B8[idx]` exclusivamente para o índice `idx = S16(player + 0x90)`, sincronizando subsalas de portas abertas caso ambos os jogadores estejam na mesma sala (`0x8003C7A8..0x8003C828`).
- Em **`0x8003C850..0x8003CB50`** (quando `U8(0x8016A40E) & 2 == 0`, isto é, `MODE == 1`):
  - Executa a lógica single-player que lê `0x8016A410` (`ROOM_STATE[0].room`) e `0x8012D400[0]` fixamente no índice `0`.
- **Por que ocorria corrupção de sala quando o Player 2 subia uma escada:**
  1. Como `func_80098F20` chama `func_8003C58C` **dentro** da cutscene da escada (`in_cutscene == 1`), `coop_active()` retornava `0` e `U8(MODE)` permanecia `1` durante `func_8003C58C`.
  2. Quando o Player 2 (`SLOT1_DATA`, cujo `S16(player + 0x90)` é fixamente **`1`**) subia uma escada, `swap_all_player_state()` havia trocado `ROOM_STATE[0]` ↔ `ROOM_STATE[1]` (colocando a sala do P2 em `ROOM_STATE[0]` e a sala do P1 em `ROOM_STATE[1]`).
  3. Ao rodar com `ROOM_STATE` trocado, `func_8003C58C(SLOT1_DATA)` usava `S16(SLOT1_DATA + 0x90) == 1` e gravava a nova sala do P2 em `ROOM_STATE[1]` (sobrescrevendo a sala do P1!), deixando a sala antiga do P2 em `ROOM_STATE[0]`. No fim da cutscene, quando `ensure_normal_player_order()` destroca `ROOM_STATE[0]` ↔ `ROOM_STATE[1]`, o P1 ficava com a sala nova do P2 e o P2 voltava para sua sala antiga.
- **Solução em `coop_room_begin` / `coop_room_end`:**
  - `coop_room_begin` ativa `U8(MODE) = 2` sempre que `coop_story_session() && U8(MODE) == 1` (cobrindo tanto gameplay normal quanto cutscenes/escadas).
  - Se `U32(PLAYER_PTRS) == SLOT1_DATA && U32(PLAYER_PTRS + 4) == SLOT0_DATA` (indicando que `ROOM_STATE` está invertido), `coop_room_begin` desinverte temporariamente `ROOM_STATE[0]` ↔ `ROOM_STATE[1]` antes de `func_8003C58C` rodar e `coop_room_end` restaura a inversão no retorno.

#### 4. Detecção da Ação de Escada (`0x16EC == 23`) e Callbacks de Saída de Cutscene (`func_800A0F58` / `func_800A10B8`)
- Em `func_8009E1E0(3)` (`0x8009E888..0x8009E998`), ao iniciar uma cutscene in-game:
  - `0x8009E888`: liga o bit `0x20000000` em `U32(PLAYER_PTRS[0] + 0x00F4)`.
  - `0x8009E8EC..0x8009E918`: inspeciona `U16(PLAYER_PTRS[0] + 0x16EC)` procurando especificamente as ações:
    - `25` (`0x19`): acionamento de painel/switch;
    - **`23` (`0x17`)**: subir/descer escada;
    - `12` (`0x0C`) / `13` (`0x0D`): abrir porta.
- Ao encerrar a cutscene em `func_8009E1E0(0)` (`0x8009E4F0` / `0x8009E58C`), o jogo cria uma tarefa temporária de 2 frames (`func_800A1190` $\rightarrow$ callback **`func_800A10B8`** quando `STATE & 1 != 0`, ou `func_800A1048` $\rightarrow$ callback **`func_800A0F58`** quando `STATE & 1 == 0`) que, 2 frames depois, limpa `U8(0x8010EDE4) = 0` e executa em `PLAYER_PTRS[0]`:
  - `U16(p + 0x16EC) = 0`, `U8(p + 0x16F0) = 1`, `U16(p + 0x0088) = 0x1000`, `U32(p + 0x00F4) = (U32(p + 0x00F4) & ~0x20000000u) | 2`.
- Como `coop_cutscene_mode_end` restaura a ordem normal (`PLAYER_PTRS[0] = SLOT0_DATA`) imediatamente no retorno de `func_8009E1E0(0)`, ele também aplica esse mesmo desbloqueio diretamente em `SLOT1_DATA` para garantir que o Player 2 nunca fique com o bit `0x20000000` preso em `+0x00F4` após usar uma escada ou porta.

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
| `func_8007B8F4` | `void func_8007B8F4(unsigned int dl_ptr_ptr, unsigned int sprite_ptr, int x, int y)` | Desenha o sprite `RGBA16` (`224x48`, escala `0.5f`) da HUD customizada na Display List principal (`0x801D2C80`). |

---

## 5. Símbolos Registrados em `winback.us.syms.toml`

Lista completa dos símbolos em [winback.us.syms.toml](file:///C:/Users/Alberto/Documents/projetos/game/winback/generated/winback.us.syms.toml) (`section "main"`, `rom = 0x1000`, `vram = 0x80025C00`, `size = 0xD8E60`):

| Símbolo | VRAM | Size | Papel |
| :--- | :--- | :--- | :--- |
| `func_80025F10` | `0x80025F10` | `0x560` | Loop de renderização do frame |
| `func_80026EB8` | `0x80026EB8` | `0x288` | Clear do Z-buffer e do framebuffer de cor (céu vs preto) |
| `func_80027470` | `0x80027470` | `0x168` | Alocador de objetos/tarefas |
| `func_800337A8` | `0x800337A8` | `0xBC0` | Update de IA/visibilidade de inimigos |
| `func_80034364` | `0x80034364` | `0x1A0` | Setup do spawner de inimigos |
| `func_8003A054` | `0x8003A054` | `0x388` | Lock-on inicial da câmera ao apertar Y / C-Down |
| `func_8003A6EC` | `0x8003A6EC` | `0x2A4` | Troca/ciclo de alvo de lock-on da câmera |
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
| `func_8007B8F4` | `0x8007B8F4` | `0x48` | Desenho de sprite 2D posicionado em `(x, y)` |
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
| `func_800C4EE0` | `0x800C4EE0` | `0xD0` | Update do objeto de Céu / Nuvens (matriz do Sky Dome) |
| `func_800C4FB0` | `0x800C4FB0` | `0x30C` | Draw do objeto de Céu / Nuvens (nuvens 2D + Sky Dome 3D) |
| `func_800C958C` | `0x800C958C` | `0x68` | Checagem de pickup de item solto |
| `func_800CF6A8` | `0x800CF6A8` | `0xC4` | Desenho da barra de vida (`LIFE`) do jogador |
| `func_800D1E24` | `0x800D1E24` | `0x2A8` | Atualização de dados da HUD |
