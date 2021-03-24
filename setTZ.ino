#include <WiFi.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include <WebServer.h>

const char* ssid = "myssid";
const char* passwd = "mypasswd";
const String Ipstack_key = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";

#define NTP_SERVER "pool.ntp.org"
// Note, this db is probably accurate, but is not necessarily
// exactly what is compiled into the toolchain, so YMMV
const char* TzdbUrl = "https://raw.githubusercontent.com/nayarsystems/posix_tz_db/master/zones.csv";
const char* TzdbFile = "/tzdb.csv";
const String GeolocateUrl = "http://api.ipstack.com/check?access_key=";
const char* ZoneDetectUrl = "https://timezone.bertold.org/timezone?lat=%0.16f&lon=%0.16f&s=1";

WebServer server(80);

void handleTz() {
  File tzdb = SPIFFS.open(TzdbFile, "r");
  if (!tzdb) {
    server.send(404, "text/plain", "unable to open tzdb");
    return;
  }
  String html;
  if (html.reserve(tzdb.size() * 1.7) == 0) {
    server.send(503, "text/plain", "insufficient memory");
    return;
  }
  html = "<html><body>";
  html += "<form method='POST' action='set'>";
  html += "<label for='locale'>Timezone:</label>";
  html += "<select id='locale' name='locale'>";
  while (tzdb.available()) {
    String r_line = tzdb.readStringUntil('\n');
    r_line.trim();
    String locale = r_line.substring(0, r_line.indexOf(","));
    String tz_def = r_line.substring(r_line.indexOf(",") + 1);
    if (locale.indexOf('"') == 0) locale.remove(0, 1);
    if (locale.lastIndexOf('"') == locale.length() - 1) locale.remove(locale.length() - 1, 1);
    if (tz_def.indexOf('"') == 0) tz_def.remove(0, 1);
    if (tz_def.lastIndexOf('"') == tz_def.length() - 1) tz_def.remove(tz_def.length() - 1, 1);
    html += "<option value='" + String(tz_def) + "'>";
    html += String(locale) + "</option>";
  }
  html += "</select><input type='submit' value='Set TZ'/>";
  html += "</form></body></html>";
  tzdb.close();
  server.send(200, "text/html", html);
}

void handleSet() {
  String tz = server.arg("locale");
  if (tz.length()) {
    setenv("TZ", tz.c_str(), 1);
  } else {
    tz = String(getenv("TZ"));
  }
  struct tm now;
  getLocalTime(&now, 5000);
  char time_buf[64];
  strftime(time_buf, 64, "%B %d %Y %H:%M:%S (%A)", &now);
  tz += ": " + String(time_buf);
  server.send(200, "text/plain", tz);
}

bool fillTzdb() {
  File tzdb = SPIFFS.open(TzdbFile, "w");
  HTTPClient http;
  log_v("GET %s", TzdbUrl);
  http.begin(TzdbUrl);
  int httpCode = http.GET();
  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      int len = http.getSize();
      uint8_t buff[256] = { 0 };
      WiFiClient * stream = http.getStreamPtr();
      while (http.connected() && (len > 0 || len == -1)) {
        size_t size = stream->available();
        if (size) {
          int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
          tzdb.write(buff, c);

          if (len > 0) {
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

void setIana(const char* tz_name) {
  File tzdb = SPIFFS.open(TzdbFile, "r");
  if (!tzdb) {
    server.send(404, "text/plain", "unable to open tzdb");
    return;
  }
  String tz;
  while (tzdb.available() && tz.length()==0) {
    String r_line = tzdb.readStringUntil('\n');
    if (r_line.indexOf(tz_name) > 0) {
      tz = r_line.substring(r_line.indexOf(",")+2, r_line.length()-1);
      log_i("tz associated: %s", tz.c_str());
    }
  }
  if (!tz.length()) {
    Serial.printf("No timezone associated to %s", tz_name);
    return;
  }
  setenv("TZ", tz.c_str(), 1);
}

void autoZone() {
  HTTPClient http1;
  log_v("GET %s", GeolocateUrl + Ipstack_key);
  http1.begin(GeolocateUrl + Ipstack_key);
  int httpCode = http1.GET();
  if (httpCode != 200) {
    Serial.println("Unable to geolocate from IPStack");
    return;
  }
  String geoJson = http1.getString();
  http1.end();
  log_v("payload: %s", geoJson.c_str());
  if (geoJson.indexOf("invalid_access_key") > 0) {
    Serial.println("Invalid IPStack access key");
    return;
  }
  float latit = geoJson.substring(geoJson.indexOf("\"latitude\":") + 11, geoJson.indexOf("\"latitude\":") + 27).toFloat();
  float longi = geoJson.substring(geoJson.indexOf("\"longitude\":") + 12, geoJson.indexOf("\"longitude\":") + 28).toFloat();
  log_i("lat: %0.16f\tlong: %0.16f", latit, longi);
  HTTPClient http2;
  char zd[256];
  snprintf(zd, 255, ZoneDetectUrl, latit, longi);
  http2.begin(zd);
  httpCode = http2.GET();
  if (httpCode != 200) {
    Serial.println("Unable to geolocate from ZoneDetect");
    return;
  }
  String zdJson = http2.getString();
  log_v("payload: %s", zdJson.c_str());
  String iana_tz = zdJson.substring(zdJson.indexOf("\"Result\":") + 11, zdJson.length() - 3);
  log_i("IANA zone: %s", iana_tz.c_str());
  setIana(iana_tz.c_str());
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
  setenv("TZ", "UTC", 1);
  autoZone();
  const char* tz = getenv("TZ");
  configTzTime(tz, NTP_SERVER);
  server.on("/", handleTz);
  server.on("/set", handleSet);
  server.begin();
  struct tm now;
  getLocalTime(&now, 5000);
  Serial.print(String(tz) + ": ");
  Serial.println(&now, "%B %d %Y %H:%M:%S (%A)");
}

void loop() {
  server.handleClient();
  delay(100);
}
