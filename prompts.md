# Registro de uso de IA — SombraViva (Physical Computing: IoT & IoB)

> Regra da Global Solution: a IA foi usada como copiloto e os prompts estão registrados aqui.
> Cada integrante deve conseguir explicar qualquer parte do código e do circuito.
> O registro completo do projeto (API e VM) está em <https://github.com/doug-melo/sombraviva-so/blob/main/prompts.md>.

| # | Data | Ferramenta | Objetivo |
|---|---|---|---|
| 1 | 17/11/2026 | Claude | Integrar o ESP32 à API de SOA (contrato do `POST /api/v1/medicoes` e token do dispositivo) |
| 2 | 18/11/2026 | Claude | Editar o vídeo de IoT: ordem das gravações, legendas e destaques |
| 3 | 19/11/2026 | Claude | Ajustar a duração do vídeo (≤ 4 min), abertura com a logo e créditos da equipe |
| 4 | 19/11/2026 | Claude | Envio imediato à API quando o nível de risco muda |

## Prompt 1

```
no caso tem que modificar o projeto de SOA para que ele se encaixe dentro do processo de IOT e de SO?
```

**Resultado:** endpoint `POST /api/v1/medicoes` na API compatível com o JSON do `sketch.ino` e autenticação do ESP32 por token Bearer.

## Prompt 2

```
Você deve agir como editor de video e editar o video da entrega da matéria de IOT
análise o contexto e a ordem dos vídeos e coloque eles em perfeita ordem verifique se eu não esqueci de gravar algo
não esqueça de colocar legendas e também outras partes importantes do vídeo
```

**Resultado:** vídeo montado na ordem problema → arquitetura → circuito → lógica → demonstração → ODS, com legendas e destaques (níveis de risco, envio HTTP 201, painel).

## Prompt 3

```
o primeiro a duração ficou um pouco curta [...] aumente a duração do video de IOT
me passe um pequeno roteirinho das partes do video de IOT que ficou sem áudio que eu vou gravar algo e te mandar
```

**Resultado:** trechos novos explicando os componentes do circuito e a lógica do `sketch.ino`, narrados pelo Douglas a partir de um roteiro curto; vídeo final com 3:51.

## Prompt 4

```
faça a mudança na lógica do projeto do workwi para quando for detectada alguma mudança dentro os parâmetros
por exemplo do de atenção para o de perigo obviamente também ficar mandando de tempos em tempos
```

**Resultado:** o ESP32 continua enviando a cada 30 s e passa a enviar na hora em que o nível de risco muda (`riscoEnviado`/`houveEnvio` no `loop()`), testado no Wokwi com `ATENCAO -> PERIGO` e `HTTP 201`.

<!-- Integrantes: adicionem abaixo os prompts que cada um usou. -->
