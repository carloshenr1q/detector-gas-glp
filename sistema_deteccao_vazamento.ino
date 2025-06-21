// ====================================================================================
// PROJETO: Detector de Vazamento de Gás com ESP32, Sensor MQ e Notificações Telegram
// DATA DA VERSÃO FINAL: 12 de Junho de 2025
//
// DESCRIÇÃO: Este sistema monitora os níveis de gás em um ambiente usando um
// sensor da série MQ. Ele possui uma calibração dinâmica e adaptativa, envia
// alertas via Telegram em caso de vazamento e aceita comandos remotos.
// ====================================================================================

// --- INCLUSÃO DE BIBLIOTECAS ---
// Bibliotecas necessárias para a conectividade Wi-Fi e comunicação HTTPS segura.
#include <WiFi.h>
#include <WiFiClientSecure.h>
// Biblioteca para facilitar a comunicação com a API de Bots do Telegram.
#include <UniversalTelegramBot.h>

// --- CONFIGURAÇÕES DO USUÁRIO ---
// Insira aqui os dados da sua rede Wi-Fi.
const char* WIFI_SSID = "********";
const char* WIFI_PASSWORD = "*********";

// Insira aqui os dados do seu Bot do Telegram (obtidos com o @BotFather).
const char* BOT_TOKEN = "********************************************";
const char* CHAT_ID = "************";

// --- MAPEAMENTO DE PINOS (HARDWARE) ---
// Define a quais pinos do ESP32 cada componente está conectado.
const int MQ_SENSOR_ANALOG_PIN = 34;
const int RED_LED_PIN = 2;
const int WHITE_LED_PIN = 4;
const int BUZZER_PIN = 15;

// --- CONSTANTES DE CONFIGURAÇÃO DO SISTEMA ---
// Parâmetros que ajustam o comportamento do sistema.
const unsigned long GAS_READ_INTERVAL = 3000;                       // Intervalo entre leituras do sensor de gás (3 segundos).
const unsigned long TELEGRAM_LEAK_INTERVAL = 10000;                 // Intervalo para reenviar mensagens de alerta de vazamento (10 segundos).
const unsigned long LED_BLINK_INTERVAL = 500;                       // Velocidade do pisca-pisca do LED de alerta (meio segundo).
const unsigned long BUZZER_SHORT_BEEP_DURATION = 150;               // Duração dos bips curtos do alarme sonoro.
const int MQ_SENSOR_INITIAL_MIN_PLAUSIBLE = 10;                     // Leitura analógica mínima para considerar o sensor conectado na inicialização.
const int MQ_SENSOR_INITIAL_MAX_PLAUSIBLE = 4090;                   // Leitura analógica máxima para considerar o sensor conectado na inicialização.
const unsigned long WHITE_LED_READING_DURATION = 500;               // Duração do pulso do LED branco para indicar uma leitura.
const unsigned long BASELINE_COLLECTION_DURATION = 60000;           // Duração da calibração da baseline (1 minuto).
const unsigned long BASELINE_UPDATE_INTERVAL = 6UL * 60UL * 60UL * 1000UL; // Intervalo para recalibrar a baseline automaticamente (6 horas).
const int LEAK_SENSITIVITY_OFFSET = 450;                            // O "botão de sensibilidade": o alarme dispara se a leitura for > (baseline + este valor).
const int COMMAND_POLLING_INTERVAL_MS = 2000;                       // Intervalo para verificar novos comandos do Telegram (2 segundos).
const int NUM_READINGS = 20;                                        // Número de leituras a serem armazenadas para o cálculo da média (60s / 3s por leitura).


// --- OBJETOS GLOBAIS ---
// Cliente Wi-Fi seguro necessário para a comunicação HTTPS com o Telegram.
WiFiClientSecure clientSecure;
// Objeto da biblioteca do Telegram, que gerencia a comunicação com o bot.
UniversalTelegramBot bot(BOT_TOKEN, clientSecure);

// --- VARIÁVEIS GLOBAIS ---
// Variáveis usadas para controlar o estado e o tempo do sistema.

// Variável para armazenar o tempo de aquecimento definido pelo usuário via Telegram.
unsigned long MQ_SENSOR_WARMUP_TIME_MS; 

// Variáveis para controle de tempo não-bloqueante (usando a função millis()).
unsigned long previousGasReadMillis = 0;
unsigned long previousTelegramMillis = 0;
unsigned long previousLedBlinkMillis = 0;
unsigned long previousBuzzerBeepMillis = 0;
unsigned long startupTimestamp = 0;
unsigned long whiteLedTurnedOnAt = 0;
unsigned long baselineCollectionStartMillis = 0;
unsigned long lastBaselineUpdateMillis = 0;
unsigned long lastTimeBotRan;

// Flags de estado: controlam o que o programa está fazendo em um determinado momento (máquina de estados).
bool redLedState = LOW;
bool buzzerBeepState = LOW;
bool leakDetected = false;
bool systemInitializedCorrectly = false;
bool sensorWarmupCompleteAndTelegramSent = false;
bool whiteLedActiveForReadingPulse = false;
bool firstLeakNotificationSent = false;
bool collectingBaseline = false;

// Variáveis para o cálculo da calibração dinâmica (baseline).
int baselineSum = 0;
int baselineCount = 0;
int baselineGasLevel = 0;

// Variáveis para o buffer de leituras (usado no comando /status).
int gasReadings[NUM_READINGS];
int readingIndex = 0;
int totalReadingsInHistory = 0;

// --- PROTÓTIPOS DE FUNÇÕES ---
// Declaração antecipada das funções para organização do código.
void connectWiFi();
bool checkMQSensorInitial();
void performActuatorsSelfTest();
void sendTelegramMessage(String message);
int readGasLevel();
void performLongBuzzerBeep(int duration);
void controlRedLedBlinking(bool active);
void controlBuzzerAlert(bool active);
void flushOldTelegramMessages();
void handleTelegramCommands();
int getAverageGasReading();
int getNetworkPing();

/**
 * @brief Função de configuração inicial. Executada apenas uma vez quando o ESP32 liga ou é reiniciado.
 */
void setup() {
    // Inicia a comunicação serial para depuração no Monitor Serial.
    Serial.begin(115200);
    while (!Serial);

    Serial.println("Iniciando Sistema de Detecção de Gás com Telegram...");

    // Inicializa o buffer de leituras do histórico com zeros.
    for (int i = 0; i < NUM_READINGS; i++) {
        gasReadings[i] = 0;
    }

    // Configura os pinos dos componentes como SAÍDA ou ENTRADA.
    pinMode(RED_LED_PIN, OUTPUT);
    pinMode(WHITE_LED_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(MQ_SENSOR_ANALOG_PIN, INPUT);

    // Garante que os atuadores comecem desligados.
    digitalWrite(RED_LED_PIN, LOW);
    digitalWrite(WHITE_LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);

    // Tenta se conectar à rede Wi-Fi.
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("ERRO CRÍTICO: Falha ao se conectar ao Wi-Fi!");
        digitalWrite(RED_LED_PIN, HIGH);    // Acende o LED vermelho para indicar falha crítica.
        while (true) { delay(1000); }    // Trava o sistema.
    }
    Serial.println("Conectado ao Wi-Fi com sucesso!");
    clientSecure.setInsecure(); // Permite conexões HTTPS sem verificar o certificado (bom para testes).
    flushOldTelegramMessages(); // Limpa mensagens antigas do Telegram para não processar comandos ou respostas de sessões anteriores.

    // Realiza uma verificação inicial do sensor para garantir que está conectado corretamente.
    if (!checkMQSensorInitial()) {
        Serial.println("ERRO CRÍTICO: Falha na verificação inicial do Sensor de Gás!");
        sendTelegramMessage("⚠️ ALERTA SISTEMA: Falha na verificação inicial do Sensor de Gás!");
        digitalWrite(RED_LED_PIN, HIGH);
        while (true) { delay(1000); }
    }
    Serial.println("Verificação inicial do Sensor de Gás OK.");

    // Lógica para perguntar o tempo de aquecimento via Telegram e aguardar a resposta do usuário.
    bool warmupTimeReceived = false;
    sendTelegramMessage("Sistema de Detecção de Gás INICIADO com sucesso. ✅\n\nInforme o tempo de aquecimento do sensor em minutos (ex: 5, 10, 15).");

    while (!warmupTimeReceived) {
        // Verifica por novas mensagens a cada 2 segundos.
        if (millis() > lastTimeBotRan + COMMAND_POLLING_INTERVAL_MS) {
            int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
            for (int i = 0; i < numNewMessages; i++) {
                if (String(bot.messages[i].chat_id) == CHAT_ID) {   // Processa apenas mensagens do usuário autorizado.
                    String text = bot.messages[i].text;
                    Serial.print("Mensagem recebida: "); Serial.println(text);
                    long minutes = text.toInt();    // Tenta converter o texto da mensagem para um número.
                    if (minutes > 0) {  // Se for um número válido...
                        MQ_SENSOR_WARMUP_TIME_MS = minutes * 60UL * 1000UL; // Calcula o tempo em milissegundos.
                        sendTelegramMessage("⏳ Tempo de aquecimento definido para " + String(minutes) + " minuto(s).");
                        warmupTimeReceived = true;  // Libera o sistema para continuar.
                        break;  // Sai do loop 'for'.
                    } else {    // Se não for um número válido...
                        sendTelegramMessage("❌ Entrada inválida. Por favor, envie apenas um número maior que zero (ex: 15).");
                    }
                }
            }
            lastTimeBotRan = millis();
        }
    }
    // Realiza um rápido auto-teste dos LEDs e do buzzer.
    performActuatorsSelfTest();
    Serial.println("Auto-teste dos atuadores concluido.");

    systemInitializedCorrectly = true;
    Serial.println("Verificação inicial dos componentes OK.");
    sendTelegramMessage("Iniciando aquecimento do Sensor de Gás. \nPor favor, aguarde...");
    performLongBuzzerBeep(300);

    // Inicia a contagem de tempo para o aquecimento do sensor.
    Serial.println("Aquecendo Sensor de Gás...");
    startupTimestamp = millis();
}

/**
 * @brief Função principal que roda em loop contínuo após o setup().
 * Organizada como uma máquina de estados (aquecimento, calibração, monitoramento).
 */
void loop() {
    if (!systemInitializedCorrectly) return;

    // A cada ciclo, verifica se há comandos do Telegram para processar.
    handleTelegramCommands(); 

    unsigned long currentMillis = millis();

    // Lógica para manter o Wi-Fi conectado
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Wi-Fi Desconectado! Tentando reconectar...");
        digitalWrite(RED_LED_PIN, HIGH);    // Acende o LED vermelho para indicar problema de conexão.
        connectWiFi();
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("Falha ao se reconectar o Wi-Fi. Alertas podem não ser enviados.");
        } else {
            Serial.println("Wi-Fi Reconectado!");
            digitalWrite(RED_LED_PIN, LOW); // Apaga o LED de erro.
        }
    }

    // Lógica para o pulso do LED branco
    if (whiteLedActiveForReadingPulse && (currentMillis - whiteLedTurnedOnAt >= WHITE_LED_READING_DURATION)) {
        digitalWrite(WHITE_LED_PIN, LOW);
        whiteLedActiveForReadingPulse = false;
    }

    // ==========================================================
    // MÁQUINA DE ESTADOS PRINCIPAL
    // O programa executa uma das 3 fases por vez:
    // FASE 1: Aquecimento
    // FASE 2: Coleta da Baseline
    // FASE 3: Monitoramento contínuo
    // ==========================================================

    // FASE 1: Aquecimento do Sensor
    if (!sensorWarmupCompleteAndTelegramSent) {
        if (currentMillis - startupTimestamp >= MQ_SENSOR_WARMUP_TIME_MS) {
            // Aquecimento concluído. Prepara para a próxima fase.
            Serial.println("Sensor de Gás aquecido e operacional.");
            sendTelegramMessage("Sensor de Gás LIGADO e sendo CALIBRADO. ✅");
            sensorWarmupCompleteAndTelegramSent = true;
            collectingBaseline = true; // Ativa a flag para iniciar a coleta da baseline.
            baselineCollectionStartMillis = millis();   // Inicia o timer da coleta.
            baselineSum = 0;    // Zera as variáveis de cálculo.
            baselineCount = 0;
            return; // Retorna para garantir uma transição de estado limpa para a próxima fase.
        } 
        else {
            // Enquanto aquece, imprime o tempo restante.
            unsigned long remainingWarmupSeconds = (MQ_SENSOR_WARMUP_TIME_MS - (currentMillis - startupTimestamp)) / 1000;
            if ((currentMillis - startupTimestamp) % 5000 < 50) {
                Serial.print("Aquecendo Sensor de Gás, aguarde: ");
                Serial.print(remainingWarmupSeconds);
                Serial.println("s");
            }
            return; // Sai do loop e espera o tempo passar.
        }
    }

    // FASE 2: Coleta da Linha de Base (Baseline)
    if (collectingBaseline) {
        // Se ainda não passou 1 minuto de coleta...
        if (currentMillis - baselineCollectionStartMillis < BASELINE_COLLECTION_DURATION) {
            // Pega uma nova leitura a cada 250ms para uma média estável
            if (currentMillis - previousGasReadMillis >= 250) {
                previousGasReadMillis = currentMillis;
                int gasLevel = readGasLevel();
                baselineSum += gasLevel;
                baselineCount++;
                // A linha abaixo pode ser comentada para um log mais limpo
                Serial.print("Coletando baseline... Amostra "); Serial.print(baselineCount); Serial.print(": "); Serial.println(gasLevel);
            }
        } else { // Terminou o tempo de coleta
            // Calcula a média.
            if (baselineCount > 0) {
                baselineGasLevel = baselineSum / baselineCount;
            } else {
                baselineGasLevel = 400; // Valor padrão seguro caso nenhuma amostra seja coletada
            }
            lastBaselineUpdateMillis = millis();    // Registra o tempo da última calibração.
            collectingBaseline = false; // Desativa a flag de coleta.
            Serial.print("---------------------------------------------------\n");
            Serial.print("Baseline inicial coletada! Media: "); Serial.println(baselineGasLevel);
            Serial.print("---------------------------------------------------\n");
            sendTelegramMessage("⚙️ Base de referência de gás calibrada com sucesso. Nível: " + String(baselineGasLevel));
        }
        return; // Sai do loop enquanto coleta a baseline
    }
    
    // Recalibração da baseline a cada 6 horas
    if (!collectingBaseline && currentMillis - lastBaselineUpdateMillis >= BASELINE_UPDATE_INTERVAL) {
        Serial.println("Iniciando recalibração periódica da baseline...");
        collectingBaseline = true;  // Ativa a coleta novamente.
        baselineCollectionStartMillis = currentMillis;
        baselineSum = 0;
        baselineCount = 0;
        sendTelegramMessage("⏳ Recalibrando a base de referência de gás...");
        return;
    }

    // FASE 3: Monitoramento Normal do Gás
    if (currentMillis - previousGasReadMillis >= GAS_READ_INTERVAL) {
        previousGasReadMillis = currentMillis;

        // Acende o LED branco para indicar que uma leitura está sendo feita.
        if (!leakDetected) {
            digitalWrite(WHITE_LED_PIN, HIGH);
            whiteLedActiveForReadingPulse = true;
            whiteLedTurnedOnAt = currentMillis;
        }

        int gasLevel = readGasLevel();

        // Salva a leitura no buffer
        gasReadings[readingIndex] = gasLevel;
        readingIndex = (readingIndex + 1) % NUM_READINGS; // Avança o índice de forma circular
        if (totalReadingsInHistory < NUM_READINGS) {
            totalReadingsInHistory++; // Conta até o buffer estar cheio
        }

        // Condição de vazamento usa a baseline + sensibilidade
        if (gasLevel > baselineGasLevel + LEAK_SENSITIVITY_OFFSET) {
            if (!leakDetected) {    // Ação executada apenas na primeira detecção.
                Serial.println("🚨🚨 !!! VAZAMENTO DE GÁS DETECTADO !!! 🚨🚨");
                leakDetected = true;
                firstLeakNotificationSent = false;
                digitalWrite(WHITE_LED_PIN, LOW);   // Garante que o LED de status apague.
                whiteLedActiveForReadingPulse = false;
            }
        } else {
            if (leakDetected) { // Ação executada apenas quando o nível volta ao normal.
                Serial.println("Nível de gás voltou ao normal.");
                sendTelegramMessage("⚠️ Nível de gás NORMALIZADO. ⚠️");
                leakDetected = false;
                firstLeakNotificationSent = false;
            }
        }
    }

    // Lógica de Alerta: ativa os atuadores se um vazamento for detectado.
    if (leakDetected) {
        controlRedLedBlinking(true);
        controlBuzzerAlert(true);
        digitalWrite(WHITE_LED_PIN, LOW);
        whiteLedActiveForReadingPulse = false;

        // Envia alertas repetidos via Telegram em intervalos definidos.
        if (!firstLeakNotificationSent || (currentMillis - previousTelegramMillis >= TELEGRAM_LEAK_INTERVAL)) {
            String msg = "🚨🚨 ALERTA: VAZAMENTO DE GÁS DETECTADO! Nível: " + String(readGasLevel());
            sendTelegramMessage(msg);
            previousTelegramMillis = currentMillis;
            firstLeakNotificationSent = true;
        }
    } else {
        // Garante que os alertas estejam desligados em estado normal.
        controlRedLedBlinking(false);
        controlBuzzerAlert(false);
    }
}

// ====================================================================================
// --- FUNÇÕES AUXILIARES ---
// ====================================================================================

/**
 * @brief Conecta à rede Wi-Fi definida nas constantes.
 */
void connectWiFi() {
    Serial.print("Conectando a "); Serial.println(WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWi-Fi conectado!");
        Serial.print("Endereco IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nFalha ao conectar ao Wi-Fi apos várias tentativas.");
    }
}

/**
 * @brief Verifica se a leitura inicial do sensor está dentro de uma faixa plausível.
 * @return true se a leitura for válida, false caso contrário.
 */

bool checkMQSensorInitial() {
    Serial.println("Verificando Sensor de Gás (leitura inicial)...");
    int initialReading = analogRead(MQ_SENSOR_ANALOG_PIN);
    Serial.print("Leitura Sensor de Gás (frio): "); Serial.println(initialReading);
    return initialReading >= MQ_SENSOR_INITIAL_MIN_PLAUSIBLE && initialReading <= MQ_SENSOR_INITIAL_MAX_PLAUSIBLE;
}

/**
 * @brief Pisca os LEDs e bipa o buzzer para um teste visual e sonoro na inicialização.
 */
void performActuatorsSelfTest() {
    Serial.println("Iniciando auto-teste dos atuadores...");
    digitalWrite(RED_LED_PIN, HIGH); delay(200); digitalWrite(RED_LED_PIN, LOW); delay(100);
    digitalWrite(WHITE_LED_PIN, HIGH); delay(200); digitalWrite(WHITE_LED_PIN, LOW); delay(100);
    performLongBuzzerBeep(300);
    Serial.println("Auto-teste dos atuadores concluído.");
}

/**
 * @brief Envia uma mensagem de texto para o chat definido no Telegram.
 * @param message A string de texto a ser enviada.
 */
void sendTelegramMessage(String message) {
    if (WiFi.status() == WL_CONNECTED) {
        if (bot.sendMessage(CHAT_ID, message, "")) {
            Serial.println("Mensagem Telegram enviada: " + message);
        } else {
            Serial.println("Falha ao enviar mensagem para o Telegram.");
            Serial.print("Erro retornado pela lib: ");
            Serial.println(bot._lastError);
        }
    } else {
        Serial.println("Wi-Fi desconectado. Mensagem nao enviada.");
    }
}

/**
 * @brief Lê o valor analógico do sensor de gás e imprime no Monitor Serial.
 * @return O valor lido (0-4095).
 */
int readGasLevel() {
    int valor = analogRead(MQ_SENSOR_ANALOG_PIN);
    Serial.print("Leitura Sensor de Gás (nível): ");
    Serial.println(valor);
    return valor;
}

/**
 * @brief Aciona o buzzer com um tom contínuo por uma duração específica.
 * @param duration A duração do bip em milissegundos.
 */
void performLongBuzzerBeep(int duration) {
    tone(BUZZER_PIN, 3500);
    delay(duration);
    noTone(BUZZER_PIN);
}

/**
 * @brief Controla o pisca-pisca do LED vermelho de alerta.
 * @param active true para ativar o pisca-pisca, false para desativar.
 */
void controlRedLedBlinking(bool active) {
    if (!active) {
        digitalWrite(RED_LED_PIN, LOW); redLedState = LOW; return;
    }
    unsigned long currentMillis = millis();
    if (currentMillis - previousLedBlinkMillis >= LED_BLINK_INTERVAL) {
        previousLedBlinkMillis = currentMillis;
        redLedState = !redLedState;
        digitalWrite(RED_LED_PIN, redLedState);
    }
}

/**
 * @brief Controla os bips curtos e contínuos do buzzer de alerta.
 * @param active true para ativar os bips, false para desativar.
 */
void controlBuzzerAlert(bool active) {
    if (!active) {
        noTone(BUZZER_PIN); buzzerBeepState = false; return;
    }
    unsigned long currentMillis = millis();
    if (currentMillis - previousBuzzerBeepMillis >= BUZZER_SHORT_BEEP_DURATION) {
        previousBuzzerBeepMillis = currentMillis;
        buzzerBeepState = !buzzerBeepState;
        if (buzzerBeepState) {
            tone(BUZZER_PIN, 4000);
        } else {
            noTone(BUZZER_PIN);
        }
    }
}

/**
 * @brief Lê e descarta todas as mensagens pendentes na fila do bot do Telegram.
 * Essencial para evitar o processamento de comandos antigos após uma reinicialização.
 */
void flushOldTelegramMessages() {
  Serial.println("Limpando mensagens antigas do Telegram...");
  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  while (numNewMessages) {
    Serial.print("Encontradas e descartadas "); Serial.print(numNewMessages); Serial.println(" mensagens pendentes.");
    numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    delay(500);
  }
  Serial.println("Fila de mensagens do Telegram está limpa.");
}

/**
 * @brief Verifica e processa novos comandos recebidos via Telegram (/reset, /status).
 */
void handleTelegramCommands() {
  // Apenas verifica por comandos se já passou o intervalo de tempo
  if (millis() - lastTimeBotRan > COMMAND_POLLING_INTERVAL_MS) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

    for (int i = 0; i < numNewMessages; i++) {
      String chat_id = String(bot.messages[i].chat_id);
      String text = bot.messages[i].text;

      // Responde apenas a comandos do chat autorizado
      if (chat_id == CHAT_ID) {
        Serial.print("Comando recebido: ");
        Serial.println(text);
        
        if (text == "/reset") {
          sendTelegramMessage("🔄 Reiniciando o sistema...");
          delay(1000); // Pequena pausa para garantir que a mensagem de confirmação seja enviada
          ESP.restart(); // Comando para reiniciar o ESP32
        } 
        // ADIÇÃO: Lógica para o comando /status
        else if (text == "/status") {
          // Coleta os dados para a mensagem de status
          int averageGas = getAverageGasReading();
          int ping = getNetworkPing();

          // Pega a última leitura válida do nosso buffer de histórico, em vez de ler o sensor novamente
          int lastReadingIndex = (readingIndex == 0) ? (NUM_READINGS - 1) : (readingIndex - 1);
          int lastGasReading = gasReadings[lastReadingIndex];

          // Formata a mensagem de resposta
          String statusMessage = "📉 **Status do Sistema** 📈\n\n";
          statusMessage += "▶️ **Leitura Atual:** " + String(lastGasReading) + "\n";
          statusMessage += "▶️ **Média (último minuto):** " + String(averageGas) + "\n";
          statusMessage += "▶️ **Baseline Atual:** " + String(baselineGasLevel) + "\n";
          statusMessage += "▶️ **Limiar de Alerta:** >" + String(baselineGasLevel + LEAK_SENSITIVITY_OFFSET) + "\n";
          
          if (ping != -1) {
            statusMessage += "▶️ **Ping da Internet:** " + String(ping) + " ms\n";
          } else {
            statusMessage += "▶️ **Ping da Internet:** Falha na conexão\n";
          }
          
          statusMessage += "▶️ **Status:** " + String(leakDetected ? "ALERTA DE VAZAMENTO 🚨" : "Normal ✅");

          // Envia a mensagem de status formatada para o modo Markdown para deixar em negrito
          bot.sendMessage(CHAT_ID, statusMessage, "Markdown");
        }
      }
    }
    lastTimeBotRan = millis(); // Atualiza o timer mesmo se não houver mensagens
  }
}

/**
 * @brief Calcula a média das últimas leituras de gás armazenadas no buffer.
 * @return A média das leituras.
 */
int getAverageGasReading() {
  if (totalReadingsInHistory == 0) {
    return 0; // Evita divisão por zero se nenhuma leitura foi feita ainda
  }
  
  long sum = 0;
  for (int i = 0; i < totalReadingsInHistory; i++) {
    sum += gasReadings[i];
  }
  
  return sum / totalReadingsInHistory;
}

/**
 * @brief Mede a latência da rede tentando uma conexão TCP com um servidor confiável.
 * @return O tempo de conexão em ms, ou -1 em caso de falha.
 */
int getNetworkPing() {
  WiFiClient client;
  unsigned long startTime = millis();
  // Tenta conectar ao servidor web do Google na porta 80
  if (client.connect("www.google.com", 80)) {
    unsigned long endTime = millis();
    client.stop();
    return endTime - startTime; // Retorna o tempo da conexão em ms
  } else {
    return -1; // Retorna -1 se a conexão falhar
  }
}