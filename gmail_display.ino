#include <Arduino.h>
#include <vector>
#include <ArduinoJson.h>
#include <Regexp.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "constants.h"
#include "8266_serial.h"

// Setup the 1306 display
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display = Adafruit_SSD1306(128, 64, &Wire, OLED_RESET);

String accessToken;
JsonDocument doc;
DeserializationError error;
unsigned long expirationTime;

void initializeOled() {
  // Start OLED
  Wire.begin(2,0);
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  display.clearDisplay(); // Clear the Adafruit logo ;-)
  display.display();
}

// Syntactic sugar for a GET request
struct Response httpGetRequest(Request request){
  request.method = "GET";
  return httpRequest(request);
}

// Syntactic sugar for a POST request
struct Response httpPostRequest(Request request) {
  request.method = "POST";
  return httpRequest(request);
}

const String USERINFO_URL = "https://www.googleapis.com/oauth2/v2/userinfo";
char GMAIL_USER_ID[50]; // This will hold the user's gmail ID once it is set...

void getGmailId() {
  Response response = httpGetRequest({.url = USERINFO_URL, .bearerToken = accessToken});
  if (response.connected) {
    error = deserializeJson(doc, response.body);
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
    } else {
      strncpy(GMAIL_USER_ID, doc["id"], sizeof(GMAIL_USER_ID)); // we keep this as a char[] since I want to use sprintf later...
      Serial.println("GMAIL ID: " + String(GMAIL_USER_ID));
    }
  }
}

const char GMAIL_INBOX_REQUEST[] = "https://gmail.googleapis.com/gmail/v1/users/%s/messages";
const String GMAIL_INBOX_QUERY = "q=in%3Ainbox%20is%3Aunread%20-category%3A%28promotions%20OR%20social%29";

int getInboxUnreadMessages() {
  int inboxUnreadMessages;

  char gmailRequestUrl[sizeof(GMAIL_INBOX_REQUEST) + strlen(GMAIL_USER_ID)];
  sprintf(gmailRequestUrl, GMAIL_INBOX_REQUEST, GMAIL_USER_ID);
  String fullGmailRequestUrl = String(gmailRequestUrl) + "?" + GMAIL_INBOX_QUERY;
  Response response = httpGetRequest({.url = fullGmailRequestUrl, .bearerToken = accessToken});
  if (response.connected) {
    error = deserializeJson(doc, response.body);
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
    } else {
      inboxUnreadMessages = doc["resultSizeEstimate"];
    }
  }
  return inboxUnreadMessages;
}

const String OAUTH_REFRESH_URL = "https://oauth2.googleapis.com/token";

void refreshToken() {
  Response response = httpPostRequest(
    {.url = OAUTH_REFRESH_URL, .body = OAUTH_REFRESH_BODY, .contentType = "application/x-www-form-urlencoded"}
  );
  if (response.connected) {
    error = deserializeJson(doc, response.body);
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
    } else {
      accessToken = String(doc["access_token"]);
      expirationTime = (millis() / 1000) + long(doc["expires_in"]);
      Serial.println("Expires at: " + String(expirationTime));
    }
  }
}

const int X_OFFSET = 5;

void displayToOLED(char header[], char data[]) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(X_OFFSET, 0);
  display.println(header);
  display.setCursor(X_OFFSET, 17);
  display.setTextSize(2);
  display.println(data);
  display.drawLine(X_OFFSET, 12, 65 + X_OFFSET, 12, SSD1306_WHITE);
  display.display();
}

// This function writes the number of messages to the SPI OLED
void outputNumMessages(int numMessages) {
  char header[50];
  char data[4];
  sprintf(header, "GMail Inbox");
  sprintf(data, "%d", numMessages);
  Serial.println(String(header) + ": " + String(data));
  displayToOLED(header, data);
}

void setup() {
  // Initialize the internal serial port
  Serial.begin(115200);

  Serial.println("RST: " + ESP.getResetReason());
  /*ESP.wdtDisable();
  *((volatile uint32_t*) 0x60000900) &= ~(1); // Hardware WDT OFF*/

  // Initialize the display and Wifi
  initializeOled();
  initializeWifi();
}

void loop() {
  ESP.wdtFeed();
  if (!expirationTime || (millis() / 1000) > expirationTime) {
    refreshToken();
  }
  if (strlen(GMAIL_USER_ID) == 0) { // Fetch the user's GMAIL ID using the token
    getGmailId();
  }
  if (accessToken && strlen(GMAIL_USER_ID) > 0) {
    int inboxUnreadMessages = getInboxUnreadMessages();
    outputNumMessages(inboxUnreadMessages);
  }
  delay(60000); // Only check email every 60 seconds
}
