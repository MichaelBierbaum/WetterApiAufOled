#include <U8g2lib.h>
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE);
u8g2_uint_t offset;

#define DEBUG

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "credentials.h"
const String webseite = "http://api.openweathermap.org/data/2.5/forecast?q=Gaukoenigshofen,DE&units=metric&APPID=";


const int BUTTON_PIN = 18;

enum AUSGABE
{
  TEMP,
  FEEL,
  RAIN,
  SNOW,
  HUMIDITY,
  CLOUDS,
  WINDSPEED,
};
const char *ausgabeStr[] =
    {
        "temp",
        "feels like",
        "rain",
        "snow",
        "humidity",
        "clouds",
        "windspeed"};

const int DIM = 40;
struct DATENSTRUKTUR
{
  float temp;
  float feels_like;
  int humidity;
  String main;
  String description;
  int clouds;
  float windSpeed;
  float rain;
  float snow;
  int timeUTC;
};
DATENSTRUKTUR daten[DIM];

class Pos
{
private:
  int _x;
  int _y;

public:
  Pos(int x = 0, int y = 0)
  {
    SetX(x);
    SetY(y);
  }

  void SetX(int x)
  {
    if (x < 0 || x > u8g2.getDisplayHeight())
      return;

    _x = x;
  }

  void SetY(int y)
  {
    //Serial.print("SetY: "), Serial.println(y);
    if (y < 0 || y > u8g2.getDisplayHeight())
      return;

    _y = y;
  }

  void SetPosToLineNr(int lineNr)
  {
    SetX(0);
    SetY(10 + 10 * lineNr);
  }

  int GetX() { return _x; }
  int GetY() { return _y; }
};
Pos *pos;

volatile bool tasterGedrueckt = false;
volatile AUSGABE ausgabe = AUSGABE::TEMP;
//--------------------------------------------

int aufstehenNr = 99;
int kaffeeNr = 99;
const int countOfData = 8;

int aufstehenStd = 6;
int kaffeeStd = 15;
//--------------------------------------------------

void setup(void)
{
  Serial.begin(115200);
  delay(10);

  setup_OLED();
  setupTaster();
  startWiFi();
  holeWetterdaten();
  delay(1000);
  Serial.print("setup abgeschlossen");
}

void loop(void)
{
  //Serial.println("u8g2_prepare");
  u8g2_prepare();

  //Serial.println("showLineHeader");
  showLineHeader();

  //Serial.print("showLines Beginn");
  for (int lineNr = 0; lineNr < 5 && tasterGedrueckt == false; ++lineNr)
    showLine(lineNr);
  //Serial.println("  Ende");

  u8g2.sendBuffer();
  delay(1000);
  tasterGedrueckt = false;
}

void u8g2_prepare(void)
{
  u8g2.setFont(u8g2_font_6x10_tf);
  //u8g2.setFont(u8g2_font_u8glib_4_tf);
  u8g2.setFontRefHeightExtendedText();
  u8g2.setDrawColor(1);
  u8g2.setFontPosTop();
  u8g2.setFontDirection(0);
  u8g2.clearBuffer(); // clear the internal memory once
  u8g2.setFontMode(1);
}

String getWeatherData()
{
  String payload = "";
  HTTPClient http;

  //Serial.print("Webseite: "); Serial.println(webseite + key);
  http.begin(webseite + key); //Specify the URL
  int httpCode = http.GET();  //Make the request

  if (httpCode == 200)
  { //Check for the returning code
    payload = http.getString();
  }

  else
  {
    payload = "E";
  }
  http.end(); //Free the resources
              // Serial.println(payload);
  // only first 8 elements of array. Delete rest
  int i = 0;
  int last = payload.indexOf(",{"
                             "dt",
                             0);
  while (i < countOfData)
  {
    last = payload.indexOf(",{", last + 1);
    i = i + 1;
  }

  return payload.substring(0, last) + "]}";
}

void decodeWeather(String JSONline)
{

  //const size_t bufferSize = 4 * JSON_ARRAY_SIZE(1) + JSON_ARRAY_SIZE(4) + 8 * JSON_OBJECT_SIZE(1) + 4 * JSON_OBJECT_SIZE(2) + 5 * JSON_OBJECT_SIZE(4) + 4 * JSON_OBJECT_SIZE(7) + 4 * JSON_OBJECT_SIZE(8) + 1270;
  const size_t bufferSize = 8 * JSON_ARRAY_SIZE(1) + JSON_ARRAY_SIZE(8) + 22 * JSON_OBJECT_SIZE(1) + 8 * JSON_OBJECT_SIZE(2) + 9 * JSON_OBJECT_SIZE(4) + 10 * JSON_OBJECT_SIZE(9) + 6 * JSON_OBJECT_SIZE(10) + 2489;

  DynamicJsonBuffer jsonBuffer(bufferSize);
  const char *json = JSONline.c_str();
  JsonObject &root = jsonBuffer.parseObject(json);
  JsonArray &list = root["list"];
  for (int i = 0; i < countOfData; i++)
  {
    parsePaket(i, list[i]);
  }
}

void parsePaket(int index, JsonObject &liste)
{
  daten[index].temp = liste["main"]["temp"];
  daten[index].feels_like = liste["main"]["feels_like"];
  daten[index].humidity = liste["main"]["humidity"];

  liste["weather"]["0"]["main"].printTo(daten[index].main);
  liste["weather"]["0"]["description"].printTo(daten[index].description);

  daten[index].clouds = liste["clouds"]["all"];

  daten[index].windSpeed = liste["wind"]["speed"];

  if (IsSnow(index))
    daten[index].snow = liste["snow"]["3h"];
  else if (IsRain(index))
    daten[index].rain = liste["rain"]["3h"];

  const char *list_dt_txt = liste["dt_txt"]; // "2018-10-17 15:00:00"
  daten[index].timeUTC = (list_dt_txt[11] - '0') * 10 + list_dt_txt[12] - '0';
}

bool IsSnow(int index) { return IsMain(index, "Snow"); }
bool IsRain(int index) { return IsMain(index, "Rain"); }
bool IsCloud(int index) { return IsMain(index, "Clouds"); }
bool IsClear(int index) { return IsMain(index, "Clear"); }
bool IsMain(int index, const char *txt)
{
  return !daten[index].main.compareTo(txt);
}

void holeWetterdaten()
{
  if ((WiFi.status() == WL_CONNECTED)) //Check the current connection status
  {
    String weather;
    do
    {
      weather = getWeatherData();
      //Serial.println(weather);

    } while (weather[0] == 'E');
    decodeWeather(weather);

    setNrFromStd();
  }
}

void showLineHeader()
{
  u8g2.setCursor(0, 0);

  u8g2.printf("%6s|", "Time ");
  u8g2.printf(" %-s", ausgabeStr[ausgabe]);
}

void showLine(int lineNr)
{
  //Serial.println("SetPosToLineNr");
  pos->SetPosToLineNr(lineNr);

  //Serial.println("setCursor");
  u8g2.setCursor(pos->GetX(), pos->GetY());

  u8g2.printf("%02i:00 |", daten[lineNr].timeUTC);
  switch (ausgabe)
  {
  case AUSGABE::TEMP:
    showLineItemTemp(lineNr);
    break;
  case AUSGABE::FEEL:
    showLineItemFeel(lineNr);
    break;
  case AUSGABE::RAIN:
    showLineItemRain(lineNr);
    break;
  case AUSGABE::SNOW:
    showLineItemSnow(lineNr);
    break;
  case AUSGABE::HUMIDITY:
    showLineItemHumi(lineNr);
    break;
  case AUSGABE::CLOUDS:
    showLineItemClou(lineNr);
    break;
  case AUSGABE::WINDSPEED:
    showLineItemWind(lineNr);
    break;
  }
}

void showLineItemTemp(int lineNr) { u8g2.printf("%6.2f", daten[lineNr].temp); }
void showLineItemFeel(int lineNr) { u8g2.printf("%6.2f", daten[lineNr].feels_like); }
void showLineItemRain(int lineNr) { u8g2.printf("%6.2f", daten[lineNr].rain); }
void showLineItemSnow(int lineNr) { u8g2.printf("%6.2f", daten[lineNr].snow); }
void showLineItemHumi(int lineNr) { u8g2.printf("%6i", daten[lineNr].humidity); }
void showLineItemDesc(int lineNr) { u8g2.printf("%s", daten[lineNr].description.c_str()); }
void showLineItemClou(int lineNr) { u8g2.printf("%6i", daten[lineNr].clouds); }
void showLineItemWind(int lineNr) { u8g2.printf("%6.2f km/h", daten[lineNr].windSpeed); }

void showSymbol()
{
  u8g2.setFont(u8g2_font_unifont_t_symbols);
  pos->SetX((double)u8g2.getDisplayHeight() * 2.0 / 3.0);
  if (IsSnow(kaffeeNr))
    u8g2.drawUTF8(pos->GetX(), pos->GetY(), "☃");
  else if (IsRain(kaffeeNr))
  {
    if (daten[kaffeeNr].rain > 5.0)
      u8g2.drawUTF8(pos->GetX(), pos->GetY(), "☔");
    else
    {
      u8g2.drawUTF8(pos->GetX(), pos->GetY(), "☂");
    }
  }
  else if (IsCloud(kaffeeNr))
    u8g2.drawUTF8(pos->GetX(), pos->GetY(), "☁");
  else if (IsClear(kaffeeNr))
    u8g2.drawUTF8(pos->GetX(), pos->GetY(), "☀");
}

void setup_OLED()
{
  u8g2.begin();
  //u8g2.setFont(u8g2_font_logisoso32_tf);
  u8g2.setFont(u8g2_font_6x10_tf);
  pos = new Pos(0, 0);
}

void setupTaster()
{
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), OnPushButton, RISING);
}

void OnPushButton()
{
  if (!tasterGedrueckt)
  {
    tasterGedrueckt = true;
    IncrementAusgabe();
  }
}

void IncrementAusgabe()
{
  switch (ausgabe)
  {
  case AUSGABE::TEMP:
    ausgabe = AUSGABE::FEEL;
    break;

  case AUSGABE::FEEL:
    ausgabe = AUSGABE::RAIN;
    break;

  case AUSGABE::RAIN:
    ausgabe = AUSGABE::SNOW;
    break;

  case AUSGABE::SNOW:
    ausgabe = AUSGABE::HUMIDITY;
    break;

  case AUSGABE::HUMIDITY:
    ausgabe = AUSGABE::CLOUDS;
    break;

  case AUSGABE::CLOUDS:
    ausgabe = AUSGABE::WINDSPEED;
    break;

  case AUSGABE::WINDSPEED:
    ausgabe = AUSGABE::TEMP;
    break;
  }
}

void startWiFi()
{
  WiFi.begin(ssid, password);

  u8g2.setCursor(10, 10);
  u8g2.printf("connecting to Wifi");
  u8g2.setCursor(30, 30);
  u8g2.printf("\"%s\"", ssid);
  u8g2.setCursor(30, 30);
  u8g2.sendBuffer();

  int counter = 0;
  //Serial.println("connect WiFi");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(1000);
  }

  Serial.println("");
  Serial.print("WiFi connected with IP address: ");
  Serial.println(WiFi.localIP());
}

void setNrFromStd()
{
  for (int i = 0; i < countOfData; i++)
  {
    int time = daten[i].timeUTC;
    if (time >= aufstehenStd && time < aufstehenStd + 3)
      aufstehenNr = i;
    if (time >= kaffeeStd && time < kaffeeStd + 3)
      kaffeeNr = i;
  }
  //Serial.print("aufstehenNr "); Serial.println(aufstehenNr);
  //Serial.print("kaffeeNr ");Serial.println(kaffeeNr);
}
