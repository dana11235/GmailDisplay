#include <Arduino.h>
#include <vector>
#include <ArduinoJson.h>
#include <Regexp.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "constants.h"
#include "8266_serial.h"

// These pins are used for the SPI OLED display
#define OLED_MOSI 10
#define OLED_CLK 11
#define OLED_DC 12
#define OLED_RST 13

// Setup the 1106 display
Adafruit_SH1106G display = Adafruit_SH1106G(128, 64,OLED_MOSI, OLED_CLK, OLED_DC, OLED_RST, -1);

String accessToken;
JsonDocument doc;
DeserializationError error;
unsigned long expirationTime;

void initializeOled() {
  // Start OLED
  display.begin(0, true);
  display.setContrast(0);
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
  Response userInfoResponse = httpGetRequest({.url = USERINFO_URL, .bearerToken = accessToken});
  error = deserializeJson(doc, userInfoResponse.body);
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
  } else {
    strncpy(GMAIL_USER_ID, doc["id"], sizeof(GMAIL_USER_ID)); // we keep this as a char[] since I want to use sprintf later...
  }
}

const char GMAIL_INBOX_REQUEST[] = "https://gmail.googleapis.com/gmail/v1/users/%s/messages";
const String GMAIL_INBOX_QUERY = "q=in%3Ainbox%20is%3Aunread%20-category%3A%28promotions%20OR%20social%29";

int getInboxUnreadMessages() {
  int inboxUnreadMessages;

  char gmailRequestUrl[sizeof(GMAIL_INBOX_REQUEST) + strlen(GMAIL_USER_ID)];
  sprintf(gmailRequestUrl, GMAIL_INBOX_REQUEST, GMAIL_USER_ID);
  String fullGmailRequestUrl = String(gmailRequestUrl) + "?" + GMAIL_INBOX_QUERY;
  Response inboxResponse = httpGetRequest({.url = fullGmailRequestUrl, .bearerToken = accessToken});
  error = deserializeJson(doc, inboxResponse.body);
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
  } else {
    inboxUnreadMessages = doc["resultSizeEstimate"];
  }
  return inboxUnreadMessages;
}

const String OAUTH_REFRESH_URL = "https://oauth2.googleapis.com/token";

void refreshToken() {
  Response response = httpPostRequest(
    {.url = OAUTH_REFRESH_URL, .body = OAUTH_REFRESH_BODY, .contentType = "application/x-www-form-urlencoded"}
  );
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

const int X_OFFSET = 5;

void displayToOLED(char header[], char data[]) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(X_OFFSET, 0);
  display.println(header);
  display.setCursor(X_OFFSET, 17);
  display.setTextSize(2);
  display.println(data);
  display.drawLine(X_OFFSET, 12, 65 + X_OFFSET, 12, SH110X_WHITE);
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
  Serial.begin(9600);

  // Initialize the display and Wifi
  initializeOled();
  initializeWifi();
}

void loop() {
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
