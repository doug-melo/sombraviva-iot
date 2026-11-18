# 🌱 SombraViva — Estação IoT de Ilha de Calor (ESP32 + Wokwi)

**Global Solution FIAP 2026 — Physical Computing: IoT & IoB**
Projeto CitizenTech **SombraViva — Inteligência Climática Urbana** (estudo de caso: Osasco-SP).

> 🔗 **Link do projeto no Wokwi:** `COLE_AQUI_O_LINK_DO_WOKWI`
> 🎬 **Vídeo (até 4 min):** `COLE_AQUI_O_LINK_DO_VIDEO`
> 👥 **Integrantes:** veja [`integrantes.txt`](integrantes.txt)

| Repositório da mesma CitizenTech | Papel |
|---|---|
| **sombraviva-iot** (este) | Circuito ESP32 no Wokwi (Physical Computing) |
| [Global-Solution---2-semestre---Cyber-Soa](https://github.com/JoaoKraideM/Global-Solution---2-semestre---Cyber-Soa) | API REST que recebe as medições em `POST /api/v1/medicoes` |
| [sombraviva-so](https://github.com/doug-melo/sombraviva-so) | VM Linux que hospeda a API e publica o túnel HTTPS usado pelo Wokwi |

---

## 1. O que o circuito faz

A retirada das árvores do Calçadão de Osasco inspirou a pergunta central do SombraViva:
**quanto mais quente fica um espaço urbano quando a vegetação some?**

Este circuito é a "estação de campo" do SombraViva. Ele mede, ao mesmo tempo, **duas zonas**:

| Zona | Sensor | Representa |
|---|---|---|
| `arborizada` | DHT22 #1 | Praça/rua com árvores (referência) |
| `sem_vegetacao` | DHT22 #2 | Calçadão de Osasco, sem árvores |

A cada 5 segundos o ESP32:

1. Lê **temperatura e umidade** das duas zonas e a **luminosidade (lux)** pelo LDR (incidência solar).
2. Calcula o **Índice de Calor** (sensação térmica, fórmula do NOAA) de cada zona.
3. Calcula a **intensidade da ilha de calor** = temperatura do Calçadão − temperatura da zona arborizada.
4. Classifica o **nível de risco** (normal / atenção / alerta / perigo).
5. Sinaliza localmente: **semáforo de LEDs**, **buzzer** (perigo) e **display OLED**.

A cada 30 segundos ele envia as medições (JSON) para a **API REST do SombraViva (FastAPI + MySQL)**, com **token Bearer do dispositivo**. A cada 10 minutos consulta a **temperatura oficial de Osasco na Open-Meteo** para comparar sensor x dado meteorológico.

## 2. Como o circuito se integra ao SombraViva (não é um projeto paralelo)

```
 ┌──────────── ESP32 (Wokwi) ────────────┐        HTTPS + Bearer token        ┌──────── SOA ────────┐
 │ DHT22 Arborizada  DHT22 Calçadão  LDR │ ─── POST /api/v1/medicoes ───────▶ │ FastAPI  ──▶ MySQL │
 │ Índice de calor · Ilha de calor · Risco│                                    │ log de auditoria   │
 │ LEDs · Buzzer · OLED · Botão           │ ◀── GET Open-Meteo (Osasco) ───   └─────────┬──────────┘
 └────────────────────────────────────────┘                                              │ GET /api/v1/medicoes
                                                                              ┌──────────▼──────────┐
                                                                              │ Website / Dashboard │
                                                                              │ Arborizada x Calçadão│
                                                                              └─────────────────────┘
```

| Disciplina | Como o circuito participa |
|---|---|
| **SOA** | Envia as medições para `POST /api/v1/medicoes`, que grava no MySQL. Exemplo pronto em `api_medicoes_exemplo.py`. |
| **Cybersecurity** | Dispositivo autenticado por **token Bearer** + `X-Device-Id`; tokens inválidos geram `401` e um registro `ACESSO_NEGADO` no log de auditoria; a API impede que um dispositivo publique em nome de outro (`403`). |
| **Análise de dados / Frontend** | O dashboard consome `GET /api/v1/medicoes` para mostrar o gráfico Arborizada x Calçadão e compará-lo com a Open-Meteo. |
| **ODS 3, 11 e 13** | Alertas de calor protegem a saúde (ODS 3), os dados apoiam o planejamento urbano e a arborização (ODS 11) e evidenciam a adaptação climática (ODS 13). |

## 3. Componentes e pinagem

| Componente (Wokwi) | ID no diagrama | Pino do componente | Pino do ESP32 | Observação |
|---|---|---|---|---|
| ESP32 DevKitC V4 (`board-esp32-devkit-c-v4`) | `esp` | — | — | Placa principal |
| DHT22 — Zona Arborizada (`wokwi-dht22`) | `dht_arborizada` | SDA / VCC / GND | **GPIO 4** / 3V3 / GND | Temperatura e umidade |
| DHT22 — Zona Sem Vegetação (`wokwi-dht22`) | `dht_calcadao` | SDA / VCC / GND | **GPIO 16** / 3V3 / GND | Temperatura e umidade |
| Módulo LDR (`wokwi-photoresistor-sensor`) | `ldr` | AO / VCC / GND | **GPIO 34** / 3V3 / GND | ADC1 (funciona com Wi-Fi ligado) |
| Display OLED 128x64 I2C (`board-ssd1306`) | `oled` | SDA / SCL / VCC / GND | **GPIO 21** / **GPIO 22** / 3V3 / GND | Endereço 0x3C |
| LED verde + resistor 220 Ω | `led_verde`, `r_verde` | A (via resistor) / C | **GPIO 25** / GND | Risco NORMAL |
| LED amarelo + resistor 220 Ω | `led_amarelo`, `r_amarelo` | A (via resistor) / C | **GPIO 26** / GND | Risco ATENÇÃO |
| LED vermelho + resistor 220 Ω | `led_vermelho`, `r_vermelho` | A (via resistor) / C | **GPIO 27** / GND | ALERTA (fixo) / PERIGO (piscando) |
| Buzzer (`wokwi-buzzer`) | `buzzer` | 2 (+) / 1 (−) | **GPIO 13** / GND | Toca intermitente em PERIGO |
| Botão (`wokwi-pushbutton`) | `botao` | 1.r / 2.r | **GPIO 33** / GND | `INPUT_PULLUP` |

**Por que esses pinos?** Evitamos os pinos de *strapping* (0, 2, 5, 12 e 15), que influenciam o boot. Os GPIOs 34–39 são só de entrada, por isso usamos o 34 apenas para o LDR. Ele pertence ao ADC1, porque o ADC2 não funciona com o Wi-Fi ligado. Todas as saídas (13, 25, 26, 27) são GPIOs com saída normal.

## 4. Regras de risco (limiares)

O nível de cada zona vem do **Índice de Calor** (sensação térmica), pelas faixas do **NOAA / National Weather Service** ([weather.gov/ama/heatindex](https://www.weather.gov/ama/heatindex)):

| Índice de calor | Nível | Efeito à saúde (NOAA) | Sinalização |
|---|---|---|---|
| < 27 °C | **normal** | sem risco relevante | 🟢 LED verde |
| 27 a 31,9 °C | **atenção** | fadiga com exposição prolongada | 🟡 LED amarelo |
| 32 a 40,9 °C | **alerta** | cãibras e exaustão por calor possíveis | 🔴 LED vermelho fixo |
| ≥ 41 °C | **perigo** | insolação provável | 🔴 vermelho piscando + 🔊 buzzer |

**Ilha de calor:** se o Calçadão estiver **3 °C ou mais** acima da zona arborizada, o risco geral sobe para pelo menos **atenção** e o display mostra "Calcadao MAIS QUENTE!". A EPA aponta que áreas urbanas sem vegetação costumam ficar de 1 a 3 °C mais quentes durante o dia ([epa.gov/heatislands](https://www.epa.gov/heatislands/learn-about-heat-islands)).

O **risco geral**, usado pelos LEDs e pelo buzzer, é o **pior** entre as duas zonas, porque a população está exposta ao pior caso.

## 5. Como montar no Wokwi e gerar o link

1. Acesse [wokwi.com](https://wokwi.com), faça login (para conseguir salvar) e clique em **New Project → ESP32** (template *Arduino ESP32*).
2. Na aba **`sketch.ino`**, apague tudo e cole o conteúdo do nosso `sketch.ino`.
3. Na aba **`diagram.json`**, apague tudo e cole o nosso `diagram.json`. O circuito aparece completo.
4. Clique na seta ▾ ao lado das abas → **New file…** → `libraries.txt` e cole o nosso `libraries.txt`. Outra opção é usar a aba **Library Manager** e adicionar `DHT sensor library for ESPx`, `Adafruit SSD1306`, `Adafruit GFX Library` e `ArduinoJson`.
5. Clique em **Save**, dê o nome **"SombraViva — Estação IoT de Ilha de Calor"**.
6. Clique em **Share** e copie o link (formato `https://wokwi.com/projects/XXXXXXXXXXXX`). **Esse é o link que vai na entrega.** Confira abrindo o link numa aba anônima.
7. Clique em ▶ **Start Simulation** e acompanhe o **Serial Monitor**.

## 6. Configurar a API (opcional, mas recomendado para o vídeo)

O Wokwi conecta na rede `Wokwi-GUEST` e **acessa a internet**, mas **não enxerga o `localhost` do seu PC**. Por isso a `API_URL` precisa ser pública:

- **Teste rápido (padrão):** `API_URL = "https://httpbin.org/post"`. O httpbin só devolve o JSON recebido, então o Serial mostra `HTTP 200` e prova que o envio funciona.
- **Com a API do SombraViva (usado na entrega):** a API do repositório [Global-Solution---2-semestre---Cyber-Soa](https://github.com/JoaoKraideM/Global-Solution---2-semestre---Cyber-Soa) roda na VM do repositório [sombraviva-so](https://github.com/doug-melo/sombraviva-so). Na VM:
  ```bash
  sudo ./scripts/06_tunnel.sh      # imprime https://XXXX.trycloudflare.com
  ```
  No `sketch.ino`, use `API_URL = "https://XXXX.trycloudflare.com/api/v1/medicoes"`. O `DEVICE_ID`/`DEVICE_TOKEN` precisam ser iguais a `SOMBRAVIVA_DEVICE_TOKENS` em `config/app.conf` da VM (padrão: `esp32-osasco-calcadao-01:sv-dev-TROQUE-ESTE-TOKEN`). As medições aparecem no portal e em `GET /api/v1/medicoes/resumo`; um token errado devolve `401` e vira alerta no painel `/admin`.
- **Teste local sem VM:** `uvicorn api_medicoes_exemplo:app --port 8000` + `ngrok http 8000` (API mínima de exemplo deste repositório).
- Se a API estiver fora do ar, o ESP32 **continua funcionando localmente** (LEDs, buzzer e display) e escreve no Serial `[API] Indisponivel ... Dado mantido apenas no log`.

Exemplo do JSON enviado (um por zona):

```json
{
  "device_id": "esp32-osasco-calcadao-01",
  "zona": "sem_vegetacao",
  "temperatura": 30.0,
  "umidade": 50.0,
  "indice_calor": 31.0,
  "luminosidade": 20000,
  "nivel_risco": "atencao",
  "delta_ilha_calor": 5.0,
  "temperatura_oficial": 24.3,
  "timestamp": "2026-11-17T14:30:00-03:00"
}
```
Cabeçalhos: `Authorization: Bearer <DEVICE_TOKEN>` e `X-Device-Id: <DEVICE_ID>`.

## 7. Como testar cada nível de risco no simulador

Durante a simulação, **clique no DHT22** e ajuste os controles de temperatura e umidade. Faça o mesmo no LDR para mudar o lux. Os valores abaixo foram calculados com a mesma fórmula do sketch:

| Cenário | DHT Arborizada | DHT Calçadão | Índice de calor (pior zona) | Resultado esperado |
|---|---|---|---|---|
| Normal | 24 °C / 60 % | 24 °C / 60 % | 24,0 °C | 🟢 verde |
| Ilha de calor | 22 °C / 60 % | 26 °C / 50 % | 26,0 °C (Δ = +4 °C) | 🟡 amarelo, mesmo com índice < 27 |
| Atenção (padrão ao iniciar) | 25 °C / 60 % | 30 °C / 50 % | 31,0 °C | 🟡 amarelo |
| Alerta | 25 °C / 60 % | 33 °C / 55 % | 37,8 °C | 🔴 vermelho fixo |
| Perigo | 25 °C / 60 % | 38 °C / 60 % | 55,0 °C | 🔴 piscando + 🔊 buzzer |

**Botão azul:**
- **clique curto** troca a tela do OLED: Arborizada → Calçadão → Ilha de calor → Osasco (Open-Meteo) x Sensor;
- **segurar por mais de 1 s** força o envio imediato para a API (bom para o vídeo).

**Teste de segurança:** troque o `DEVICE_TOKEN` por um valor errado. O Serial mostra `Token do dispositivo REJEITADO (HTTP 401)` e a API registra `ACESSO_NEGADO` no log de auditoria.

## 8. Arquivos

| Arquivo | Conteúdo |
|---|---|
| `diagram.json` | Circuito completo do Wokwi |
| `sketch.ino` | Firmware do ESP32 (comentado em português) |
| `libraries.txt` | Bibliotecas usadas no Wokwi |
| `api_medicoes_exemplo.py` | Endpoint FastAPI + Pydantic + validação do token, para integrar ao backend de SOA |
| `roteiro_video.md` | Roteiro cronometrado do vídeo (até 4 min) |
| `integrantes.txt` | Nomes e RMs da equipe (entregável) |

> ⚠️ Usamos apenas **dados simulados** e tokens fictícios. Nenhum dado pessoal real é coletado.
