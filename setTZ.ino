#include <WiFi.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include <WebServer.h>

#define NTP_SERVER "pool.ntp.org"
// Note, this db is probably accurate, but is not necessarily
// exactly what is compiled into the toolchain, so YMMV
const char* TzdbUrl = "https://raw.githubusercontent.com/nayarsystems/posix_tz_db/master/zones.csv";
const char* TzdbFile = "/tzdb.csv";
const char* ssid = "myssid";
const char* passwd = "mypasswd";

WebServer server(80);

void handleTz() {
  File tzdb = SPIFFS.open(TzdbFile,"r");
  if (!tzdb) {
    server.send(404, "text/plain", "unable to open tzdb");
  }
  String html = "<html><body>";
  html += "<form method='POST' action='set'>";
  html += "<label for='locale'>Timezone:</label>";
  html += "<select id='locale' name='locale'>";
  while (tzdb.available()) {
    String r_line = tzdb.readStringUntil('\n');
    r_line.trim();
    String locale = r_line.substring(0,r_line.indexOf(","));
    String tz_def = r_line.substring(r_line.indexOf(",")+1);
    if (locale.indexOf('"') == 0) locale.remove(0,1);
    if (locale.lastIndexOf('"') == locale.length()-1) locale.remove(locale.length()-1,1);
    if (tz_def.indexOf('"') == 0) tz_def.remove(0,1);
    if (tz_def.lastIndexOf('"') == tz_def.length()-1) tz_def.remove(tz_def.length()-1,1);
    html += "<option value='"+ String(tz_def) + "'>";
    html += String(locale) + "</option>";
  }
  html += "</select><input type='submit' value='Set TZ'/>";
  html += "</form></body></html>";
  tzdb.close();
  server.send(200, "text/html", html);
}

void handleSet() {
  String tz = server.arg("locale");
  if (tz) {
    setenv("TZ", tz.c_str(), 1);
  }
  struct tm now;
  getLocalTime(&now, 5000);
  char time_buf[64];
  strftime(time_buf, 64, "%B %d %Y %H:%M:%S (%A)", &now);
  tz += ": " + String(time_buf);
  server.send(200, "text/plain", tz);
}

bool fillTzdb() {
  File tzdb = SPIFFS.open(TzdbFile,"w");
  HTTPClient http;
  log_v("GET %s",TzdbUrl);
  http.begin(TzdbUrl);
  int httpCode = http.GET();
  if(httpCode > 0) {
    if(httpCode == HTTP_CODE_OK) {
        int len = http.getSize();
        uint8_t buff[256] = { 0 };
        WiFiClient * stream = http.getStreamPtr();
        while(http.connected() && (len > 0 || len == -1)) {
            size_t size = stream->available();
            if(size) {
                int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
                tzdb.write(buff, c);

                if(len > 0) {
                    len -= c;
                }
            }
            delay(1);
        }
    } else {
      log_e("Unable to retrieve %s: %d", TzdbUrl,  httpCode);
      return 1;
    }
  } else {
      log_e("Unable to connect to %s", TzdbUrl);
      return 1;
  }
  tzdb.close();
  return 0;
}

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, passwd);
  WiFi.waitForConnectResult();
  if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Unable to connect to WiFi");
      return;
  }
  Serial.println(WiFi.localIP());
  if (!SPIFFS.begin(true)) {
      Serial.println("SPIFFS Mount Failed");
      return;
  }
  if (fillTzdb()) {
      Serial.println("Unable to download tzdb");
      return;
  }
  server.on("/", handleTz);
  server.on("/set", handleSet);
  server.begin();
  configTzTime("UTC", NTP_SERVER);
  struct tm now;
  getLocalTime(&now, 5000);
  Serial.println(&now, "UTC: %B %d %Y %H:%M:%S (%A)");
}

void loop() {
  server.handleClient();
  delay(100);
}
