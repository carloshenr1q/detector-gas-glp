# Detector de Vazamento de Gás com ESP32 e Alertas via Telegram

Este repositório contém o código-fonte e a documentação para um sistema de detecção de vazamento de gás inteligente, construído com a placa ESP32. O sistema é projetado para monitorar continuamente os níveis de gás em um ambiente, calibrar-se dinamicamente para maior precisão e enviar alertas em tempo real para um chat do Telegram, além de acionar alertas locais sonoros e visuais.

O projeto também inclui funcionalidades de gerenciamento remoto através de comandos do Telegram, tornando-o um dispositivo de IoT completo e robusto.

## ✨ Funcionalidades Principais

* **✅ Calibração Dinâmica e Adaptativa:** O sistema aprende o "nível de ar limpo" do ambiente após o aquecimento e se recalibra periodicamente a cada 6 horas para compensar variações e o envelhecimento do sensor.
* **🔔 Alertas em Tempo Real via Telegram:** Em caso de detecção de vazamento, notificações são enviadas instantaneamente para um chat do Telegram, informando o nível de gás detectado.
* **⚙️ Comandos Remotos:** Gerencie o dispositivo remotamente através do Telegram:
    * `/status`: Recebe um relatório completo sobre a saúde do sistema (leitura atual, média, ping da internet, etc.).
    * `/reset`: Reinicia o dispositivo remotamente.
* **💡 Alertas Locais:** Um LED vermelho piscante e um buzzer com bips intermitentes fornecem alertas imediatos no local.
* **🔧 Setup Interativo:** Na inicialização, o sistema pergunta via Telegram qual será o tempo de aquecimento do sensor, tornando-o flexível para diferentes cenários.

## 🛠️ Componentes Necessários (Hardware)

* 1x Placa de Desenvolvimento **ESP32 DEVKITV1** (com módulo ESP32-WROOM-32)
* 1x Módulo Sensor de Gás **MQ-5** (ou MQ-2 para simulação)
* 1x **LED Vermelho** 5mm (para alerta)
* 1x **LED Branco** 5mm (para status)
* 1x **Buzzer Passivo** 5V
* 2x **Resistores de 2.2kΩ** (para os LEDs)
* 1x **Protoboard** 830 pontos
* **Cabos Dupont Jumper** (Macho-Macho)
* 1x Cabo Micro-USB para alimentação e programação do ESP32

## 📦 Software e Bibliotecas

1.  **Arduino IDE:**
    * É necessário ter a versão mais recente da [Arduino IDE](https://www.arduino.cc/en/software) instalada.
    * **Suporte para ESP32:** Você precisa adicionar o suporte para placas ESP32 na IDE. Vá em `Arquivo > Preferências` e no campo "URLs Adicionais para Gerenciadores de Placas", adicione o seguinte link:
        ```
        [https://espressif.github.io/arduino-esp32/package_esp32_index.json](https://espressif.github.io/arduino-esp32/package_esp32_index.json)
        ```
    * Depois, vá em `Ferramentas > Placa > Gerenciador de Placas...`, procure por `esp32` e instale o pacote da "Espressif Systems".

2.  **Bibliotecas:**
    * As bibliotecas `<WiFi.h>` e `<WiFiClientSecure.h>` já vêm incluídas com o pacote do ESP32.
    * **UniversalTelegramBot:** Instale via Gerenciador de Bibliotecas. Vá em `Sketch > Incluir Biblioteca > Gerenciar Bibliotecas...`, procure por `UniversalTelegramBot` e instale a biblioteca de Brian Lough.

## 🤖 Configuração do Bot no Telegram

Para o sistema enviar mensagens, você precisa criar um "Bot" no Telegram.

1.  **Converse com o BotFather:** No Telegram, procure por `@BotFather` (é um bot oficial com um selo de verificação) e inicie uma conversa.
2.  **Crie seu Bot:** Envie o comando `/newbot`. O BotFather pedirá um nome para o seu bot (ex: "Detector de Gás da Sala") e um nome de usuário (que deve terminar em "bot", ex: `MeuDetectorDeGasBot`).
3.  **Salve o Token:** Ao final, o BotFather lhe dará um **Token de API**. Ele se parece com `123456789:ABCdefGhIJKLmnopQrSTUvwxYZ`. Copie e guarde este token com segurança. Ele será o seu `BOT_TOKEN`.
4.  **Obtenha o seu Chat ID:**
    * Encontre seu bot recém-criado no Telegram (pelo `@username` dele) e envie qualquer mensagem para ele (ex: "Olá").
    * Abra um navegador de internet e acesse o seguinte URL, substituindo `SEU_TOKEN_AQUI` pelo seu token real:
        `https://api.telegram.org/botSEU_TOKEN_AQUI/getUpdates`
    * Na página que carregar, procure por `"chat":{"id":SEU_CHAT_ID, ...}`. O número que aparece em `SEU_CHAT_ID` é o ID que você precisa. Se for para um grupo, adicione o bot ao grupo, envie uma mensagem lá e o ID do grupo (geralmente um número negativo) aparecerá. Este será o seu `CHAT_ID`.

## 🔌 Montagem do Circuito

Conecte os componentes ao ESP32 DEVKITV1 da seguinte forma:
<details><summary color=#2A7AEF">Clique para ver o Diagrama</summary>

<img src="https://drive.google.com/uc?export=view&id=1OBRSQ4ESibNGVsIuGQmXRkdpcuH6eBZC"></img>

</details>

## 📝 Configuração do Código

Antes de carregar o código no seu ESP32, você **precisa** editar as seguintes linhas no topo do arquivo `.ino`:

```cpp
// --- SUAS CONFIGURAÇÕES ---
const char* WIFI_SSID = "NOME_DA_SUA_REDE_WIFI";
const char* WIFI_PASSWORD = "SENHA_DA_SUA_REDE_WIFI";
const char* BOT_TOKEN = "SEU_TOKEN_DO_BOT_AQUI";
const char* CHAT_ID = "SEU_CHAT_ID_AQUI";
```

## 🚀 Como Usar

1.  **Carregue o código** no ESP32 após preencher suas configurações.
2.  **Ligue o dispositivo.** Você receberá uma mensagem no Telegram solicitando o tempo de aquecimento.
3.  **Responda** com um número (ex: `15` para 15 minutos).
4.  **Aguarde** o período de aquecimento e, em seguida, o período de 1 minuto para a calibração da baseline. O bot lhe informará quando o sistema estiver ativo.
5.  Pronto! O sistema está monitorando. Você pode enviar `/status` a qualquer momento para um relatório ou `/reset` para reiniciar o dispositivo.

## 📊 Fluxograma do Sistema

<details>
<summary style="color: #blue;">Clique para ver o Fluxograma Operacional</summary>

<img width=410px src="https://drive.google.com/uc?export=view&id=19N0nF6CgvDnGPq1t4TJmZ9oIGsbsNQre"></img> <img width=410px src="https://drive.google.com/uc?export=view&id=1vyU5ibvtZqL3aClBi25MU3r3wDiiUXyH"></img> 

</details>
