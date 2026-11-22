# Guia Completo do Fluxo de Compilação do Mod (`winback_coop.nrm`)

Este documento explica exclusivamente **como funciona o processo de compilação** do mod `winback_coop`, desde o código-fonte em C ([coop.c](file:///C:/Users/Alberto/Documents/projetos/game/winback/winback_coop/winback_coop/src/coop.c)) até a geração do pacote final [winback_coop.nrm](file:///C:/Users/Alberto/Documents/projetos/game/winback/WinBackRecompiled-v0.3.0-Windows-x64/mods/winback_coop.nrm) carregado pelo `WinBackRecompiled.exe`.

---

## 1. Visão Geral do Pipeline

Mesmo rodando no PC (Windows x64), um mod `.nrm` do `N64Recomp` **não é compilado para x86_64** — ele é compilado para **código de máquina MIPS de 32 bits (Big-Endian) do Nintendo 64**. Quando o jogo (`WinBackRecompiled.exe`) inicia e carrega o `.nrm`, o runtime do `N64Recomp` lê esse binário MIPS + a tabela de símbolos/relocations e o recompila em tempo de execução (*JIT/AOT mod recompiler*) conectando os hooks às funções do jogo.

```mermaid
flowchart LR
    A["src/coop.c + include/winback_mod.h"] -->|"1. Compilação C -> MIPS (clang.exe)"| B["build/coop.o (Objeto ELF MIPS)"]
    B -->|"2. Linkedição em 0x81000000 (ld.lld.exe + mod.ld)"| C["build/mod.elf (Executável ELF c/ Relocs)"]
    C -->|"3. Empacotamento (RecompModTool.exe)"| D["build/winback_coop.nrm (ZIP: mod.json + mod_syms.bin + mod_binary.bin)"]
    E["mod.toml"] -->|"Lido na Etapa 3"| D
    F["generated/winback.us.syms.toml"] -->|"Lido na Etapa 3"| D
    D -->|"4. Cópia Automática (build.bat)"| G["WinBackRecompiled/mods/winback_coop.nrm"]
```

---

## 2. Arquivos e Ferramentas Envolvidos

| Arquivo / Ferramenta | Caminho no Projeto | Papel na Compilação |
| :--- | :--- | :--- |
| **Código-fonte C** | [src/coop.c](file:///C:/Users/Alberto/Documents/projetos/game/winback/winback_coop/winback_coop/src/coop.c) | Implementação dos hooks e da lógica do Co-op. |
| **Header da API de Mods** | [include/winback_mod.h](file:///C:/Users/Alberto/Documents/projetos/game/winback/winback_coop/winback_coop/include/winback_mod.h) | Define as macros `RECOMP_HOOK`, `RECOMP_HOOK_RETURN`, `RECOMP_PATCH` e imports (`recomp_get_config_u32`, `recomp_printf`, etc.). |
| **Linker Script** | [mod.ld](file:///C:/Users/Alberto/Documents/projetos/game/winback/winback_coop/winback_coop/mod.ld) | Define o endereço virtual base do mod (`RAMBASE = 0x81000000`, fora dos `8 MB` originais do N64 `0x80000000..0x807FFFFF`). |
| **Manifesto do Mod** | [mod.toml](file:///C:/Users/Alberto/Documents/projetos/game/winback/winback_coop/winback_coop/mod.toml) | Metadados (`id`, `version`, `display_name`), caminhos de entrada e opções de configuração do menu. |
| **Símbolos da ROM** | [generated/winback.us.syms.toml](file:///C:/Users/Alberto/Documents/projetos/game/winback/generated/winback.us.syms.toml) | Mapeia o nome de cada função da ROM (`func_800593EC`, `func_800A8B10`, etc.) para seu endereço VRAM e tamanho exato. |
| **Compilador C (MIPS)** | `C:\msys64\mingw64\bin\clang.exe` | Compila C para instruções MIPS II Big-Endian (`build/coop.o`). |
| **Linker ELF (MIPS)** | `C:\msys64\mingw64\bin\ld.lld.exe` | Linka `build/coop.o` em `build/mod.elf` preservando todas as relocations (`--emit-relocs`). |
| **Empacotador NRM** | [RecompModTool.exe](file:///C:/Users/Alberto/Documents/projetos/game/winback/winback_coop/winback_coop/RecompModTool.exe) | Converte `mod.toml` + `build/mod.elf` + `winback.us.syms.toml` no arquivo final `.nrm`. |
| **Script Automatizado** | [build.bat](file:///C:/Users/Alberto/Documents/projetos/game/winback/winback_coop/winback_coop/build.bat) | Executa todas as etapas em sequência e copia o `.nrm` gerado para a pasta `mods` do jogo. |

---

## 3. Passo a Passo Detalhado das 4 Etapas

### Etapa 1: Compilação do C para Objeto MIPS (`coop.c` $\rightarrow$ `build/coop.o`)

Comando executado pelo [build.bat](file:///C:/Users/Alberto/Documents/projetos/game/winback/winback_coop/winback_coop/build.bat):
```bat
clang.exe -target mips -mips2 -mabi=32 -O2 -G0 -mno-abicalls -fno-pic -ffreestanding -nostdlib -Wall -Wextra -Iinclude -c src/coop.c -o build/coop.o
```

**O que cada flag faz e por que é obrigatória:**
- `-target mips`: Gera código MIPS de 32 bits **Big-Endian** (arquitetura da CPU VR4300 do Nintendo 64).
- `-mips2`: Conjunto de instruções MIPS II suportado pelo N64 e pelo recompilador `N64Recomp`.
- `-mabi=32`: Convenção de chamada padrão **O32** (argumentos em `$a0..$a3`, retorno em `$v0`, registradores salvos `$s0..$s7`, `$ra`).
- `-O2`: Otimização de código.
- `-G0`: Desativa o uso do registrador `$gp` (*Global Pointer / Small Data*), garantindo que todas as variáveis globais/estáticas sejam acessadas via pares explícitos `lui` (`R_MIPS_HI16`) + `lw`/`sw`/`addiu` (`R_MIPS_LO16`).
- `-mno-abicalls -fno-pic`: Desativa código dependente de posição (PIC/GOT) de sistemas Linux, gerando chamadas diretas `jal` (`R_MIPS_26`).
- `-ffreestanding -nostdlib`: Compila sem depender da biblioteca padrão C (`libc`) do sistema operacional.
- `-Iinclude`: Inclui a pasta `include/` onde está o [winback_mod.h](file:///C:/Users/Alberto/Documents/projetos/game/winback/winback_coop/winback_coop/include/winback_mod.h).

> **Como funcionam as macros `RECOMP_HOOK` durante esta etapa:**
> No [winback_mod.h](file:///C:/Users/Alberto/Documents/projetos/game/winback/winback_coop/winback_coop/include/winback_mod.h), quando você escreve:
> ```c
> RECOMP_HOOK("func_800593EC") void coop_p2_model(void)
> ```
> A macro cria automaticamente uma string especial na seção `.recomp_export` do arquivo `.o` (com o prefixo `@recomp_hook:func_800593EC:coop_p2_model`), que o `RecompModTool.exe` lerá na Etapa 3 para saber qual função do jogo deve acionar essa função do mod.

---

### Etapa 2: Linkedição para ELF Relocável (`build/coop.o` $\rightarrow$ `build/mod.elf`)

Comando executado pelo [build.bat](file:///C:/Users/Alberto/Documents/projetos/game/winback/winback_coop/winback_coop/build.bat):
```bat
ld.lld.exe -m elf32btsmip -nostdlib -T mod.ld --emit-relocs --unresolved-symbols=ignore-all -e 0 build/coop.o -o build/mod.elf
```

**O que cada flag faz:**
- `-m elf32btsmip`: Instrui o `ld.lld` do MSYS2 a operar no modo linker **ELF 32-bit Big-Endian Traditional MIPS** (em vez do modo Windows PE/COFF).
- `-T mod.ld`: Usa o script [mod.ld](file:///C:/Users/Alberto/Documents/projetos/game/winback/winback_coop/winback_coop/mod.ld), que aloca o código e dados do mod a partir do endereço `0x81000000` e descarta seções desnecessárias (`.got`, `.MIPS.abiflags`, `.reginfo`, `.comment`).
- `--emit-relocs`: **Essencial!** Mantém as tabelas de relocação (`.rel.text`) dentro do `mod.elf` final. Sem essa flag, o `RecompModTool.exe` não saberia onde estão os saltos (`jal`), ponteiros de strings (`%hi`/`%lo`) e variáveis estáticas (`BSS`).
- `--unresolved-symbols=ignore-all`: Permite que símbolos externos (como funções internas da ROM `func_800A8B10`, `func_8004D6E8`, `func_8006B3B8` e funções da API `recomp_get_config_u32`, `recomp_printf`) fiquem como símbolos indefinidos (`SHN_UNDEF`) no ELF para que o `RecompModTool.exe` os resolva na Etapa 3.
- `-e 0`: Define o ponto de entrada do ELF como `0` (já que um mod é uma coleção de hooks e não tem função `main`).

---

### Etapa 3: Empacotamento em `.nrm` (`RecompModTool.exe`)

Comando executado pelo [build.bat](file:///C:/Users/Alberto/Documents/projetos/game/winback/winback_coop/winback_coop/build.bat):
```bat
RecompModTool.exe mod.toml build
```

O [RecompModTool.exe](file:///C:/Users/Alberto/Documents/projetos/game/winback/winback_coop/winback_coop/RecompModTool.exe) lê três entradas:
1. O manifesto [mod.toml](file:///C:/Users/Alberto/Documents/projetos/game/winback/winback_coop/winback_coop/mod.toml);
2. O executável MIPS `build/mod.elf` (caminho definido em `elf_path = "build/mod.elf"` no `mod.toml`);
3. O arquivo de símbolos da ROM [generated/winback.us.syms.toml](file:///C:/Users/Alberto/Documents/projetos/game/winback/generated/winback.us.syms.toml) (caminho definido em `func_reference_syms_file = "../../generated/winback.us.syms.toml"`).

A partir deles, o `RecompModTool.exe` valida todos os hooks e chamadas externas e gera o arquivo `build/winback_coop.nrm` (que é um arquivo `.zip` contendo 3 arquivos internos):

1. **`mod.json`:** Versão em JSON do manifesto e das opções de configuração do `mod.toml`.
2. **`mod_binary.bin`:** Os bytes brutos das seções `.rodata`, `.text` e `.data` extraídos do `build/mod.elf`.
3. **`mod_syms.bin` (formato binário `N64RSYMS`):**
   - Lista de funções do mod (offset e tamanho de cada função em `mod_binary.bin`);
   - Lista de relocations convertidas do ELF (indicando se cada instrução aponta para a própria seção do mod `0x80000000`, para uma função importada de API `0xFFFFFFFE`, ou para uma função da ROM base na seção `0x00001000`);
   - Lista de hooks (`RECOMP_HOOK` / `RECOMP_HOOK_RETURN`) ligando o índice da função do mod ao endereço VRAM da função na ROM original.

---

### Etapa 4: Instalação Automática na Pasta `mods`

Ao final do [build.bat](file:///C:/Users/Alberto/Documents/projetos/game/winback/winback_coop/winback_coop/build.bat):
```bat
copy /y build\winback_coop.nrm "..\..\WinBackRecompiled-v0.3.0-Windows-x64\mods\winback_coop.nrm" >nul
copy /y build\winback_coop.nrm "..\..\winback_coop.nrm" >nul
```
O arquivo recém-compilado é copiado automaticamente para a pasta `mods` do jogo executável ([WinBackRecompiled-v0.3.0-Windows-x64/mods/winback_coop.nrm](file:///C:/Users/Alberto/Documents/projetos/game/winback/WinBackRecompiled-v0.3.0-Windows-x64/mods/winback_coop.nrm)) e para a raiz do projeto.

---

## 4. Como Compilar na Prática

### Pelo Terminal (CMD ou PowerShell) ou Duplo-Clique:
Basta executar o script [build.bat](file:///C:/Users/Alberto/Documents/projetos/game/winback/winback_coop/winback_coop/build.bat):
```powershell
C:\Users\Alberto\Documents\projetos\game\winback\winback_coop\winback_coop\build.bat
```
> **Atenção:** Certifique-se de que o jogo (`WinBackRecompiled.exe`) esteja **fechado** no momento em que rodar o `build.bat`, pois o Windows bloqueia a substituição do arquivo `mods\winback_coop.nrm` enquanto o jogo estiver aberto.

---

## 5. Como Adicionar Novos Hooks ou Novas Funções da ROM no Futuro

Se você quiser chamar ou colocar um hook em uma **nova função da ROM** (por exemplo, `func_800XXXXX`) dentro do [coop.c](file:///C:/Users/Alberto/Documents/projetos/game/winback/winback_coop/winback_coop/src/coop.c):

1. **Verifique se a função está em [generated/winback.us.syms.toml](file:///C:/Users/Alberto/Documents/projetos/game/winback/generated/winback.us.syms.toml):**
   - Se ainda não estiver listada lá, adicione uma linha na lista `functions = [ ... ]` em ordem crescente de `vram`:
     ```toml
     { name = "func_800XXXXX", vram = 0x800XXXXX, size = 0x100 },
     ```
2. **Use no [coop.c](file:///C:/Users/Alberto/Documents/projetos/game/winback/winback_coop/winback_coop/src/coop.c):**
   - Para **interceptar** a função: use `RECOMP_HOOK("func_800XXXXX")` ou `RECOMP_HOOK_RETURN("func_800XXXXX")`.
   - Para **chamar** a função diretamente: declare `extern void func_800XXXXX(...);` no topo do `coop.c` e chame-a normalmente.
3. **Execute o [build.bat](file:///C:/Users/Alberto/Documents/projetos/game/winback/winback_coop/winback_coop/build.bat)** para gerar e atualizar o `.nrm`.

---

## 6. Símbolos extras exigidos pela versão atual

As funções abaixo foram adicionadas nesta fase do mod (menu 1P/2P, painel de munição, inventário separado, céu). Elas precisam estar em `generated/winback.us.syms.toml`; se faltarem, o `RecompModTool` ou o carregador de mods do jogo acusam "Reference symbol does not exist". Os tamanhos foram medidos no disassembly da ROM US.

```toml
    { name = "func_8004DA40", vram = 0x8004DA40, size = 0x6C },  # Novo jogo: zera o bloco de estado da História (hook)
    { name = "func_8008E6C0", vram = 0x8008E6C0, size = 0x68 },  # Entrada do menu título, todos os controles (chamada)
    { name = "func_8008E9BC", vram = 0x8008E9BC, size = 0x30 },  # Som de confirmar no menu (chamada)
    { name = "func_8008E9EC", vram = 0x8008E9EC, size = 0x30 },  # Som de cancelar no menu (chamada)
    { name = "func_8008EFFC", vram = 0x8008EFFC, size = 0x264 },  # Vai para um estado do menu título (chamada)
    { name = "func_80090024", vram = 0x80090024, size = 0x64 },  # Cursor do menu para baixo (chamada)
    { name = "func_80090088", vram = 0x80090088, size = 0x60 },  # Cursor do menu para cima (chamada)
    { name = "func_800904E4", vram = 0x800904E4, size = 0x1764 },  # Lógica do menu título (hook)
    { name = "func_800CEC1C", vram = 0x800CEC1C, size = 0x7AC },  # Desenho da coluna de balas original (hook)
    { name = "func_80028740", vram = 0x80028740, size = 0x194 },  # Monta a matriz do Sky Dome (chamada pelo mod)
```

`func_80059E5C` (update do jogador, hook do inventário separado) já estava na lista; o tamanho real dela é `0xC9C`.

**Inimigos mais espertos (v0.2.2, `src/enemy_ai.h`).** Mais estas quatro (tamanhos do `gen_recomp_syms.py` atual sobre a ROM US):

```toml
    { name = "func_80031100", vram = 0x80031100, size = 0x48 },   # Tamanho da rajada do inimigo (RECOMP_PATCH)
    { name = "func_800CE518", vram = 0x800CE518, size = 0x200 },  # Percepção: distância (chamada)
    { name = "func_800CE718", vram = 0x800CE718, size = 0x13C },  # Percepção: olhar + linha de visão (hook e chamada)
    { name = "func_800CE854", vram = 0x800CE854, size = 0x2D4 },  # Percepção: reação (chamada)
```

**Chefes (v0.2.8):**

```toml
    { name = "func_800AD9F0", vram = 0x800AD9F0, size = 0x50 },   # Update dos chefes (hook)
```

**Nascimento do P2 fora das paredes (v0.2.4 a v0.2.6; desde a v0.2.7 o mod não chama mais esta função, então ela não é necessária):**

```toml
    { name = "func_80044928", vram = 0x80044928, size = 0x470 },  # Teste de linha livre (paredes/objetos) (chamada)
```

Um `winback.us.syms.toml` gerado de novo pelo `tools/regen.sh` do WinBackRecomp já traz todas (e as da lista acima). Nele `func_800337A8` tem `0xB64` (a função termina em `0x8003430C`); listas antigas com `0xBC0` passavam do fim dela.

---

## 7. Armadilhas conhecidas do build

- **Nada de divisão inteira por variável.** O clang emite `teq` (trap de divisão por zero) junto com `div`/`divu`, e o recompilador do jogo não aceita essa instrução: o mod falha com "Failed to load mod code (Failed to recompile mod)". Divisão por constante vira multiplicação e é segura; para dividir por variável use laço ou float. Confira com `llvm-objdump -d build/coop.o | grep -E "\steq\s"` (deve dar 0 linhas).
- **`src/map_data.h` não vai para o repositório.** Gere a partir da sua ROM com `python tools/extract_maps.py <rom.z64>`.
- **Arte gerada:** `src/weapon_icons.h` (`tools/gen_weapon_icons.py`) e `src/ammo_tex.h` (`tools/gen_ammo_tex.py`, usa a fonte Montserrat) já estão no repositório; só regenere se mudar a arte.
- **Hooks que rodam durante o desenho em tela dividida** não podem usar `coop_active()`: `coop_draw_begin` põe `MODE = 2`, e `coop_active()` exige `MODE & 1`. Use `drawing_split` (foi isso que deixava o céu do P2 e o inventário do P2 no HUD errados).

### Cuidado ao subir a versão
No `mod.toml`, troque só a linha `version = "..."` (linha 5). `minimum_recomp_version` é a versão mínima do WinBack Recompiled (0.3.0) e não muda com a versão do mod. Na v0.3.1 uma troca de texto mudou as duas e o jogo recusou o mod ("Mod requires a newer version of this project").
