# 🎬 Roteiro do vídeo — Physical Computing (máx. 4 minutos)

**Formato:** gravação de tela (Wokwi + slides/site) com narração. Ex.: OBS Studio ou o gravador do Windows (Win+Alt+R).
**Antes de gravar:** abra o projeto no Wokwi pelo link de compartilhamento, deixe o Serial Monitor visível, confira se a `API_URL` está configurada (ngrok ou httpbin) e ensaie uma vez com cronômetro.

| Tempo | O que aparece na tela | Fala (sugestão) |
|---|---|---|
| **0:00 – 0:25** | Slide/título "SombraViva" + foto do Calçadão de Osasco | "Somos a equipe SombraViva. Na revitalização do Calçadão de Osasco, as árvores foram substituídas por estruturas artificiais. Nossa pergunta foi: quanto mais quente fica a cidade sem sombra?" |
| **0:25 – 0:55** | Website/dashboard do SombraViva (ou diagrama da solução) | "O SombraViva é uma plataforma de inteligência climática urbana. O site mostra dados reais da Open-Meteo, o backend é uma API FastAPI com MySQL e a segurança usa JWT e auditoria. Faltava o dado do chão da cidade, e é aí que entra o ESP32." |
| **0:55 – 1:35** | Wokwi parado, com zoom no circuito, apontando cada peça | "Nossa estação tem dois sensores DHT22: um simula uma zona arborizada e o outro o Calçadão sem vegetação. O LDR mede a incidência solar. O display OLED mostra as leituras, o semáforo de LEDs mostra o risco, o buzzer avisa em calor extremo e o botão troca a tela ou força o envio." |
| **1:35 – 2:05** | `sketch.ino` (trechos: `calcularIndiceCalor`, limiares, `enviarMedicao`) | "O ESP32 calcula o índice de calor pela fórmula do NOAA e a diferença entre as zonas, que é a intensidade da ilha de calor. O risco segue as faixas do NOAA: abaixo de 27 é normal, depois atenção, alerta, e acima de 41 graus é perigo." |
| **2:05 – 3:05** | ▶ Simulação rodando: Serial Monitor + LEDs + OLED. Alterar o DHT do Calçadão: **24 °C** (verde) → **30 °C** (amarelo) → **33 °C/55 %** (vermelho) → **38 °C/60 %** (piscando + buzzer). Clicar no botão para mostrar as telas "Ilha de calor" e "Osasco x Sensor". | "Vamos simular uma tarde de verão no Calçadão... A 24 graus está tudo verde. Subindo para 30, a ilha de calor já passa de 3 graus e acende o amarelo. Com 33 graus, alerta. Com 38 graus e 60% de umidade, a sensação térmica passa de 50 graus: perigo, LED piscando e buzzer. Nesta tela comparamos o sensor com a temperatura oficial de Osasco na Open-Meteo." |
| **3:05 – 3:35** | Segurar o botão (envio forçado) → Serial com `HTTP 201`/`200` → Swagger/terminal da API ou dashboard recebendo o dado | "Segurando o botão, o ESP32 envia o JSON para a nossa API REST no endpoint /api/v1/medicoes, com token Bearer do dispositivo. Os dados vão para o MySQL e aparecem no dashboard. Um token inválido é recusado e registrado no log de auditoria, que é a parte de Cybersecurity." |
| **3:35 – 3:55** | Slide final: ODS 3, 11, 13 + nomes da equipe | "Com isso, o SombraViva transforma sensores em evidência para defender a arborização urbana, ligado aos ODS 3, 11 e 13. Obrigado!" |

**Checklist do que o professor pede (não pode faltar):**
- [ ] Apresentação da solução (SombraViva) — 0:00–0:55
- [ ] Explicação de como o ESP32 se integra à solução — 0:55–2:05 e 3:05–3:35
- [ ] Demonstração do circuito funcionando no Wokwi — 2:05–3:35
- [ ] Duração total **≤ 4:00**. Deixe uma folga de 5 s.

**Dicas:** grave em 1080p, aumente a fonte do Serial Monitor (Ctrl + roda do mouse), suba o vídeo no YouTube como **"Não listado"** e teste o link numa aba anônima antes de entregar.
