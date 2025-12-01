/**
 * M5Cardputer - Conector simples de Wi-Fi
 *
 * Funcionalidade:
 *  - Faz scan das redes Wi-Fi.
 *  - Mostra as 10 redes com melhor sinal (RSSI).
 *  - Permite selecionar a rede com o teclado.
 *  - Se a rede for aberta: conecta direto.
 *  - Se for protegida: pede senha e tenta conectar.
 *
 * Controles:
 *  - W / S  : mover seleção para cima / baixo
 *  - ENTER  : selecionar rede / confirmar senha
 *  - R / r  : refazer scan de redes (a partir de qualquer estado)
 *  - Q / q  : voltar da tela de senha para a lista
 *  - DEL    : apagar último caractere da senha
 *
 * Observação:
 *  - Este código é pensado para ser usado como src/main.cpp no PlatformIO.
 */

#include <WiFi.h>
#include "M5Cardputer.h"

// ---------------------- CONFIGURAÇÕES GERAIS ----------------------

// Máximo de redes exibidas (top 10 por sinal)
static const int MAX_NETWORKS = 10;

// Estrutura para guardar dados de uma rede encontrada
struct NetworkInfo {
    String ssid;       // Nome da rede
    int32_t rssi;      // Força do sinal (dBm)
    uint8_t encType;   // Tipo de criptografia (WIFI_AUTH_OPEN, etc.)
};

// Vetor que guarda as redes (ordenadas por melhor sinal)
NetworkInfo g_networks[MAX_NETWORKS];
int g_networkCount = 0;        // Quantidade de redes válidas
int g_selectedIndex = 0;       // Índice da rede atualmente selecionada

// Dados da rede que o usuário escolheu
String g_selectedSsid;
bool g_selectedIsOpen = false;
String g_password;             // Buffer da senha digitada

// Controle de redesenho de tela
bool g_needRedraw = true;

// ---------------------- ESTADOS DE INTERFACE ----------------------

enum UiState {
    UI_NO_NETWORKS,     // Nenhuma rede encontrada
    UI_SELECT_NETWORK,  // Escolhendo rede
    UI_ENTER_PASSWORD,  // Digitando senha
    UI_CONNECTING,      // Tentando conectar
    UI_CONNECTED,       // Conectado
    UI_CONNECT_FAILED   // Falha na conexão
};

UiState g_uiState = UI_NO_NETWORKS;

// ---------------------- FUNÇÕES AUXILIARES ----------------------

/**
 * Apaga a tela e posiciona cursor no topo.
 */
void clearScreen() {
    M5Cardputer.Display.clear(BLACK);
    M5Cardputer.Display.setCursor(4, 4);
}

/**
 * Converte o tipo de autenticação em um texto amigável.
 */
String authTypeToString(uint8_t auth) {
    switch (auth) {
        case WIFI_AUTH_OPEN:             return "Aberta";
        case WIFI_AUTH_WEP:              return "WEP";
        case WIFI_AUTH_WPA_PSK:          return "WPA";
        case WIFI_AUTH_WPA2_PSK:         return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:     return "WPA/WPA2";
        case WIFI_AUTH_WPA2_ENTERPRISE:  return "WPA2-ENT";
        case WIFI_AUTH_WPA3_PSK:         return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK:    return "WPA2/WPA3";
        default:                         return "Desconhecido";
    }
}

/**
 * Insere rede no vetor g_networks mantendo ordenação por RSSI (maior para menor).
 * Mantém no máximo MAX_NETWORKS entradas.
 */
void insertNetworkSorted(const String &ssid, int32_t rssi, uint8_t encType) {
    // Se já temos MAX_NETWORKS e esta é pior que a última, ignoramos
    if (g_networkCount == MAX_NETWORKS && rssi <= g_networks[g_networkCount - 1].rssi) {
        return;
    }

    // Descobre posição de inserção
    int pos = g_networkCount;
    if (pos > MAX_NETWORKS) pos = MAX_NETWORKS;

    for (int i = 0; i < g_networkCount; ++i) {
        if (rssi > g_networks[i].rssi) {
            pos = i;
            break;
        }
    }

    // Se vetor está cheio, abrimos espaço até MAX_NETWORKS - 1
    int limit = (g_networkCount < MAX_NETWORKS) ? g_networkCount : (MAX_NETWORKS - 1);

    // Move elementos para baixo para abrir espaço em "pos"
    for (int i = limit; i > pos; --i) {
        g_networks[i] = g_networks[i - 1];
    }

    // Insere novo elemento
    g_networks[pos].ssid    = ssid;
    g_networks[pos].rssi    = rssi;
    g_networks[pos].encType = encType;

    // Ajusta quantidade (máx MAX_NETWORKS)
    if (g_networkCount < MAX_NETWORKS) {
        g_networkCount++;
    }
}

/**
 * Executa o scan de redes Wi-Fi e preenche g_networks com as 10 melhores.
 */
void scanNetworks() {
    g_networkCount = 0;
    g_selectedIndex = 0;

    clearScreen();
    M5Cardputer.Display.println("Scan de redes Wi-Fi...");
    M5Cardputer.Display.setCursor(4, 20);
    M5Cardputer.Display.println("Aguarde alguns segundos.");

    // Garante modo station e desconecta de redes anteriores
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(100);

    // Faz o scan bloqueante
    int n = WiFi.scanNetworks();

    if (n <= 0) {
        g_uiState = UI_NO_NETWORKS;
    } else {
        // Para cada rede encontrada, insere ordenado por RSSI
        for (int i = 0; i < n; ++i) {
            String ssid    = WiFi.SSID(i);
            int32_t rssi   = WiFi.RSSI(i);
            uint8_t enc    = WiFi.encryptionType(i);

            // Ignora redes sem SSID (SSID oculto)
            if (ssid.length() == 0) {
                continue;
            }

            insertNetworkSorted(ssid, rssi, enc);
        }

        if (g_networkCount == 0) {
            g_uiState = UI_NO_NETWORKS;
        } else {
            g_selectedIndex = 0;
            g_uiState = UI_SELECT_NETWORK;
        }
    }

    g_needRedraw = true;
}

/**
 * Desenha a tela quando nenhuma rede foi encontrada.
 */
void drawNoNetworksScreen() {
    clearScreen();
    M5Cardputer.Display.println("Nenhuma rede encontrada.");
    M5Cardputer.Display.setCursor(4, 20);
    M5Cardputer.Display.println("Pressione R para novo scan.");
}

/**
 * Desenha a lista das redes Wi-Fi.
 */
void drawNetworkListScreen() {
    clearScreen();

    M5Cardputer.Display.println("Selecione rede Wi-Fi:");
    M5Cardputer.Display.drawLine(0, 16, M5Cardputer.Display.width(), 16, GREEN);

    int y = 22;

    for (int i = 0; i < g_networkCount; ++i) {
        // Define posição vertical
        M5Cardputer.Display.setCursor(4, y);

        // Indica rede selecionada com ">"
        if (i == g_selectedIndex) {
            M5Cardputer.Display.print("> ");
        } else {
            M5Cardputer.Display.print("  ");
        }

        // Monta linha com SSID, RSSI e segurança
        String line = g_networks[i].ssid;
        line += " (";
        line += String(g_networks[i].rssi);
        line += "dBm) ";
        line += authTypeToString(g_networks[i].encType);

        M5Cardputer.Display.println(line);
        y += 12;

        // Evita estourar a tela se houver muitas redes (mas aqui no máx 10)
        if (y > (M5Cardputer.Display.height() - 24)) {
            break;
        }
    }

    y += 4;
    M5Cardputer.Display.setCursor(4, y);
    M5Cardputer.Display.println("W/S: mover   ENTER: conectar");
    y += 12;
    M5Cardputer.Display.setCursor(4, y);
    M5Cardputer.Display.println("R: novo scan");
}

/**
 * Desenha a tela de entrada de senha (ou aviso de rede aberta).
 */
void drawPasswordScreen() {
    clearScreen();

    M5Cardputer.Display.println("Rede selecionada:");
    M5Cardputer.Display.setCursor(4, 18);
    M5Cardputer.Display.println(g_selectedSsid);
    M5Cardputer.Display.drawLine(0, 30, M5Cardputer.Display.width(), 30, GREEN);

    int y = 38;

    if (g_selectedIsOpen) {
        M5Cardputer.Display.setCursor(4, y);
        M5Cardputer.Display.println("Rede aberta (sem senha).");
        y += 14;
        M5Cardputer.Display.setCursor(4, y);
        M5Cardputer.Display.println("ENTER: conectar   Q: voltar");
        return;
    }

    M5Cardputer.Display.setCursor(4, y);
    M5Cardputer.Display.println("Digite a senha Wi-Fi:");
    y += 14;

    // Exibe a senha digitada (sem mascarar; troque por '*' se quiser)
    M5Cardputer.Display.setCursor(4, y);
    M5Cardputer.Display.println(g_password);
    y += 18;

    M5Cardputer.Display.setCursor(4, y);
    M5Cardputer.Display.println("ENTER: conectar");
    y += 12;
    M5Cardputer.Display.setCursor(4, y);
    M5Cardputer.Display.println("DEL: apagar  Q: voltar");
}

/**
 * Desenha a tela de "conectando".
 */
void drawConnectingScreen() {
    clearScreen();

    M5Cardputer.Display.println("Conectando em:");
    M5Cardputer.Display.setCursor(4, 18);
    M5Cardputer.Display.println(g_selectedSsid);
    M5Cardputer.Display.drawLine(0, 30, M5Cardputer.Display.width(), 30, GREEN);

    M5Cardputer.Display.setCursor(4, 40);
    M5Cardputer.Display.println("Aguarde...");
    M5Cardputer.Display.setCursor(4, 56);
    M5Cardputer.Display.println("R: cancelar e refazer scan");
}

/**
 * Desenha a tela de conexão bem-sucedida.
 */
void drawConnectedScreen() {
    clearScreen();

    M5Cardputer.Display.println("Wi-Fi conectado!");
    M5Cardputer.Display.drawLine(0, 16, M5Cardputer.Display.width(), 16, GREEN);

    int y = 24;
    M5Cardputer.Display.setCursor(4, y);
    M5Cardputer.Display.print("SSID: ");
    M5Cardputer.Display.println(WiFi.SSID());
    y += 14;

    M5Cardputer.Display.setCursor(4, y);
    M5Cardputer.Display.print("IP:   ");
    M5Cardputer.Display.println(WiFi.localIP().toString());
    y += 14;

    M5Cardputer.Display.setCursor(4, y);
    M5Cardputer.Display.println("R: desconectar e refazer scan");
}

/**
 * Desenha a tela de falha na conexão.
 */
void drawConnectFailedScreen() {
    clearScreen();

    M5Cardputer.Display.println("Falha ao conectar.");
    M5Cardputer.Display.drawLine(0, 16, M5Cardputer.Display.width(), 16, RED);

    int y = 24;
    M5Cardputer.Display.setCursor(4, y);
    M5Cardputer.Display.println("Verifique SSID / senha.");
    y += 14;
    M5Cardputer.Display.setCursor(4, y);
    M5Cardputer.Display.println("R: tentar novamente (novo scan)");
}

// ---------------------- CONEXÃO ----------------------

/**
 * Inicia o processo de conexão à rede selecionada.
 * Se for aberta, conecta sem senha; caso contrário, usa g_password.
 */
void startConnection() {
    g_uiState = UI_CONNECTING;
    g_needRedraw = true;

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);
    delay(100);

    if (g_selectedIsOpen) {
        WiFi.begin(g_selectedSsid.c_str());
    } else {
        WiFi.begin(g_selectedSsid.c_str(), g_password.c_str());
    }
}

// ---------------------- TRATAMENTO DO TECLADO ----------------------

/**
 * Lê o teclado do Cardputer e aplica ações dependendo do estado da UI.
 */
void handleKeyboard() {
    // Só processa quando há mudança no teclado
    if (!M5Cardputer.Keyboard.isChange()) {
        return;
    }
    if (!M5Cardputer.Keyboard.isPressed()) {
        return;
    }

    Keyboard_Class::KeysState ks = M5Cardputer.Keyboard.keysState();

    // Atalho global: R/r sempre refaz o scan
    for (auto ch : ks.word) {
        if (ch == 'r' || ch == 'R') {
            WiFi.disconnect(true);
            scanNetworks();
            return;
        }
    }

    switch (g_uiState) {
        case UI_NO_NETWORKS: {
            // Nada além de R (já tratado acima)
            break;
        }

        case UI_SELECT_NETWORK: {
            // W/S para navegar
            for (auto ch : ks.word) {
                if (ch == 'w' || ch == 'W') {
                    if (g_networkCount > 0) {
                        g_selectedIndex--;
                        if (g_selectedIndex < 0) {
                            g_selectedIndex = g_networkCount - 1;
                        }
                        g_needRedraw = true;
                    }
                } else if (ch == 's' || ch == 'S') {
                    if (g_networkCount > 0) {
                        g_selectedIndex++;
                        if (g_selectedIndex >= g_networkCount) {
                            g_selectedIndex = 0;
                        }
                        g_needRedraw = true;
                    }
                }
            }

            // ENTER para selecionar rede
            if (ks.enter && g_networkCount > 0) {
                g_selectedSsid = g_networks[g_selectedIndex].ssid;
                g_selectedIsOpen = (g_networks[g_selectedIndex].encType == WIFI_AUTH_OPEN);
                g_password = "";
                g_uiState = UI_ENTER_PASSWORD;
                g_needRedraw = true;
            }
            break;
        }

        case UI_ENTER_PASSWORD: {
            // Q/q: volta para lista
            for (auto ch : ks.word) {
                if (ch == 'q' || ch == 'Q') {
                    g_uiState = UI_SELECT_NETWORK;
                    g_password = "";
                    g_needRedraw = true;
                    return;
                }
            }

            // Rede aberta: ENTER conecta direto
            if (g_selectedIsOpen) {
                if (ks.enter) {
                    startConnection();
                }
                break;
            }

            // Rede protegida: DIGITAÇÃO de senha
            for (auto ch : ks.word) {
                // Ignora Q/q aqui (já tratamos como "voltar")
                if (ch == 'q' || ch == 'Q') {
                    continue;
                }
                g_password += ch;
                g_needRedraw = true;
            }

            // DEL apaga último caractere
            if (ks.del && g_password.length() > 0) {
                g_password.remove(g_password.length() - 1);
                g_needRedraw = true;
            }

            // ENTER inicia conexão
            if (ks.enter) {
                startConnection();
            }
            break;
        }

        case UI_CONNECTING: {
            // Somente R global (já tratado no topo)
            break;
        }

        case UI_CONNECTED: {
            // Somente R global (já tratado no topo)
            break;
        }

        case UI_CONNECT_FAILED: {
            // Somente R global (já tratado no topo)
            break;
        }

        default:
            break;
    }
}

// ---------------------- ATUALIZAÇÃO DE INTERFACE ----------------------

/**
 * Atualiza a interface (desenha na tela) conforme o estado atual.
 */
void updateDisplayIfNeeded() {
    if (!g_needRedraw) {
        return;
    }
    g_needRedraw = false;

    switch (g_uiState) {
        case UI_NO_NETWORKS:
            drawNoNetworksScreen();
            break;
        case UI_SELECT_NETWORK:
            drawNetworkListScreen();
            break;
        case UI_ENTER_PASSWORD:
            drawPasswordScreen();
            break;
        case UI_CONNECTING:
            drawConnectingScreen();
            break;
        case UI_CONNECTED:
            drawConnectedScreen();
            break;
        case UI_CONNECT_FAILED:
            drawConnectFailedScreen();
            break;
        default:
            break;
    }
}

// ---------------------- SETUP E LOOP PRINCIPAIS ----------------------

void setup() {
    // Inicializa Serial para debug (opcional)
    Serial.begin(115200);
    delay(500);

    // Inicializa o M5Cardputer com teclado habilitado
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);

    // Ajusta a tela
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    M5Cardputer.Display.setTextSize(1);

    // Faz o primeiro scan de redes
    scanNetworks();
}

void loop() {
    // Atualiza estado do Cardputer (teclado, etc.)
    M5Cardputer.update();

    // Processa entradas do teclado
    handleKeyboard();

    // Se estiver conectando, monitora o status da conexão
    if (g_uiState == UI_CONNECTING) {
        wl_status_t st = WiFi.status();
        if (st == WL_CONNECTED) {
            g_uiState = UI_CONNECTED;
            g_needRedraw = true;
        } else if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL) {
            g_uiState = UI_CONNECT_FAILED;
            g_needRedraw = true;
        }
        // Poderia adicionar um timeout manual aqui, se quiser
    }

    // Atualiza a tela se necessário
    updateDisplayIfNeeded();

    // Pequeno delay para aliviar a CPU
    delay(10);
}
