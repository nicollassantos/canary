# Issues abertas do upstream (opentibiabr/canary) — triagem em 2026-11-13

Base: worktree `/home/nicollas/lundaria/canary-upstream-issues` (branch `nicollassantos/upstream-issues`, HEAD `7e517fb00`).
O `main` do upstream está em `666a60f6f`, a base do nosso último merge: nenhuma correção nova do upstream ficou de fora.
58 issues abertas (+17 PRs). Baseline da worktree: ut 1007/1007, it 90/90.
A triagem foi só leitura, mais um programa avulso (#4154) que usa o fmt do projeto.

Níveis de prova:
- **executado**: rodei e vi o defeito;
- **lido nos dois lados**: li o código que produz e o que consome, e eles se contradizem;
- **leitura + relato**: o código explica o sintoma relatado, mas não rodei (precisa de client);
- **não verificável aqui**: depende de algo que não temos.

## A. Reais (9)

| # | Problema | Prova | Nível | Correção proposta |
|---|---|---|---|---|
| 4154 | Datas com nanossegundos e em UTC (carta, "banned until") | fmt 12.2 do projeto: `22:13:20.000000000` (UTC) contra `19:13:20` local | executado | portar o PR #4179 (só `tools.cpp` + teste) |
| 3478 | Freequests não põem Secret Library e Barbarian Test 2/3 no questlog | falta `TheSecretLibrary.Questlog` (storage de início); missões 2/3 recebem 3, `endValue = 2`, e `missionIsStarted` esconde `valor > endValue` | lido nos dois lados | corrigir as entradas + harness que confere **todas** as entradas do freequests contra o catálogo |
| 3453 | Porta do Oasis: alavanca abre, player volta | a alavanca vira a porta 1662 (aid 12107) em 1663, que está na `QuestDoorTable`; `closing_door.lua` exige storage 12107 e teleporta de volta. Auditoria: único script que abre porta de quest sem dar o storage | lido nos dois lados | quem acha a cenoura recebe o storage 12107 (decisão do usuário) |
| 4125 (charm) | Resetar charms no client oficial loga `[error] Not enough data` | a ação 3 manda só o byte da ação; `parseSendBuyCharmRune` lê sempre charmId e raceId (log do comentário aponta essa linha) | lido nos dois lados | ler só o que existe; teste |
| 4153 | 0x14 (erro de login/kick) sem o byte de motivo do 15.23+; client oficial 15.25 fecha | `protocolgame.cpp:638` e `:679` mandam só a string; perfil atual = 1525 | leitura + relato | helper único que acrescenta `0x00` com `!oldProtocol && version >= 1523`; teste dos bytes |
| 4114 | Banner de avanço troca escudo↔distância e espada↔pesca | `getCipbiaSkill` manda 6 (escudo) e 8 (espada); no mapa do PR #4181, 6 = distância e 8 = pesca: bate exatamente com o relato. Só vai para perfis modernos (`oldProtocol` barra) | leitura + relato | portar o PR #4181 para os components; teste |
| 3513 | `afterEnter` de zona que mexe no player (ex.: `changeSpeed`) derruba o client no login | `internalPlaceCreature` roda os callbacks de zona; só depois `placeCreature` manda `sendCreatureAppear` (o pacote de entrada do próprio player) | leitura + relato | callbacks de zona adiados só no login do player (decisão do usuário) |
| 3742 (saldo) | Saldo do store enviado 2× na compra, o 1º com valor antigo | `removeCoinsBalance` envia antes de remover; `parseBuyStoreOffer` envia de novo | lido nos dois lados | tirar o envio antecipado (opcional: cosmético) |
| 2083 (parcial) | Preço médio não atualiza na Cyclopedia | preços recalculados a cada 30 min, `sendItemsPrice` só no login | leitura | reenviar aos online após o recálculo (opcional: a cor no client não dá para conferir) |

## B. Parciais ou dependentes do mapa (4)

- **3374** saque gigante: o upstream #3692 tirou a queda no chão e devolve o que não coube, mas o mantenedor diz que a issue só fecha com teto no saque. O resíduo (até milhares de inserções de stack no dispatcher para player com muita capacidade) **não foi medido**. É exatamente o modelo de ameaça do fork.
- **3458** soulpit com summon: `blockFamiliars` (desde 2025-01-28) já barra summon de player na colocação e no movimento (o master é definido antes do `placeCreature`). Sobra o caminho de convencer um monstro da onda. Decidível com teste.
- **4091** recompensa de monk na Inquisition: falta no script (só uids 1300–1308; 50261 em lugar nenhum). Precisa de baú com uid novo no mapa.
- **4057** santuários do Way of the Monk: o `shrines.lua` já tem os andares certos; a parte do mapa (paredes, porta sem aid) não conferi. O `.otbm` está no checkout principal (185 MB): uma varredura só de leitura pode confirmar #4057, #4091 e #3438.

## C. Já corrigidas no código atual (5)

- **4175** VIP: corrigido na branch (§5b do `tasks/AGENTS.md`).
- **3160** chain em NPC/PZ: o picker de `chained_penance`/`spiritual_outburst` exclui NPC, o próprio e PZ.
- **2066** Barkless: o Tigo não checa mais o tópico; `LeidenHeal` só no Leiden.
- **3770** Ice Islands: script coerente; outro jogador fez e confirmou.
- **3534** Beregar: caminho correto (storage do Maris + pick 3456).

## D. Não reproduzíveis ou não são bug (9)

- **2396** treino offline de ML: sobe pouco, pela fórmula (o comentário mostra 21,08 → 21,09%).
- **3414** amplification potion: o mantenedor mediu +8%.
- **3424** taints: o reset do NPC limpa também os chefes mortos; matar de novo dá taint.
- **2730** Snapper: o mantenedor mostrou o fluxo de storage.
- **3479** Brain Head: GOD tem `IgnoredByMonsters`; com player normal não reproduziu.
- **3428** cama: funciona no mantenedor.
- **3407** visão dos monstros: alcance configurável (design).
- **3329** lâmpada na diagonal: regra do lado de trás da parede igual ao TFS; sem referência oficial.
- **4125 (store)**: só com OTClient.

## E. Decidíveis aqui com teste ou smoke, se você liberar (7)

- **3584**/**3426** campo e bomba sem dano inicial: teste C++ com monstro e campo com dono.
- **3745** troca de aljava com arco: teste com arco e duas aljavas (a lógica existe em `player_interaction_service.cpp:1560`).
- **2553**/**3549** hazard sem pods: smoke com mapa e sonda no log (`monster:hazard()`).
- **3059** `/n` (GM): smoke; a mensagem "not enough room" também sai quando o NPC não está registrado (enganosa).
- **3438** Lion's Rock: varredura dos uids 25006–25009 no mapa.

## F. Só com client ou sessão em jogo (14)

- Protocolo 15.25: **4059** (imbuement), **3645** (wand), **4144** (expert-pvp: o mantenedor diz que só o perfil 11.00 tem a mão de PvP), **3485** (outfit de hireling: o `0xD2` ignora o alvo, payload desconhecido).
- Mecânicas de boss/quest: **3500**, **3506** (PR #4183 aberto no upstream), **3605** (o mantenedor reproduziu o crash), **3875**, **3259**.
- Vagos ou intermitentes: **917**, **2272**, **1919**, **3180**, **3427**, **2639**.

## G. Conteúdo ou feature (10)

560, 615, 618, 3803, 3841, 4003, 4076, 3470, 3413, 3599 (e a parte "disabled reasons" da 3742).

## Achados extras (fora das issues)

- Chaves repetidas nas tabelas de startup (o Lua guarda só a última, a anterior some sem aviso): `item.lua` `[12107]` (alavanca e porta do Oasis) e `[4649]`; `chest.lua` `[5000]`, `[6117]`, `[6121]`; `teleport_item.lua` `[24873]` (14×); `storage_keys_update.lua` `[2]`. A conferir uma a uma.
- PR #4150 (sem issue): com 10+ criaturas no tile, o próprio pacote de entrada vira atualização de tile e o client fica em "connecting".

## Plano

Tasks 25.1–25.12 em `tasks/plan.md` (Phase 25). Fora: grupo F (precisa de client) e conteúdo/feature.
A worktree saiu do HEAD commitado; as mudanças não commitadas do checkout principal (entre elas
`protocolgame.cpp`) não estão nela e precisam ser conciliadas ao juntar as branches.
