#define TIMEOUT 10000 // mS
#include <Arduino.h>
#include <vector>
#include <ArduinoJson.h>
#include <Regexp.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "constants.h"

// These pins are used for the SPI OLED display
#define OLED_MOSI 10
#define OLED_CLK 11
#define OLED_DC 12
#define OLED_RST 13

// These are used for the various HTTP requests we are making
const String OAUTH_REFRESH_URL = "https://oauth2.googleapis.com/token";
const String USERINFO_URL = "https://www.googleapis.com/oauth2/v2/userinfo";
const char GMAIL_INBOX_REQUEST[] = "https://gmail.googleapis.com/gmail/v1/users/%s/messages";
const String GMAIL_INBOX_QUERY = "q=in%3Ainbox%20is%3Aunread%20-category%3A%28promotions%20OR%20social%29";
char GMAIL_USER_ID[50]; // This will hold the user's gmail ID once it is set...

struct ResponseHeader {
  String name;
  String content;
};

struct Response {
  String version;
  String statusCode;
  String statusMessage;
  std::vector<ResponseHeader> headers;
  String body;
};

struct Request {
  String url;
  String body;
  String method;
  String contentType;
  String bearerToken;
};

// Setup the 1106 display
Adafruit_SH1106G display = Adafruit_SH1106G(128, 64,OLED_MOSI, OLED_CLK, OLED_DC, OLED_RST, -1);

String accessToken;
JsonDocument doc;
DeserializationError error;
unsigned long expirationTime;

void setup() {
  // Setup the UART we use to communicate with the ESP01
  Serial2.setRX(5);
  Serial2.setTX(4);
  Serial2.begin(115200);

  // Initialize the internal serial port
  Serial.begin(9600);

  // Start OLED
  display.begin(0, true);
  display.setContrast(0);
  display.clearDisplay(); // Clear the Adafruit logo ;-)
  display.display();

  initialize8266();
}

void initialize8266() {
  //delay(3000); // I originally thought this was needed to give the 8266 time to boot, although apparently not needed...
  Serial2.println(""); // For some reason, there is some weird stuff sent on Serial2. This flushes it.
  SendCommandSilent("AT+CWMODE=1","OK"); // Set the 8266 into the corect wifi mode
  SendCommandSilent("AT+CWAUTOCONN=0","OK"); // Make it so that the access point doesn't automatically connect
  SendCommandSilent("AT+CWJAP=\"" + WIFI_USERNAME + "\",\"" + WIFI_PASSWORD + "\"","OK");  // Connect to WIFI
  SendCommandSilent("AT+CIPSNTPCFG=1,8,\"time.google.com\",\"time2.google.com\",\"time3.google.com\"", "OK"); // Sync with Google SNTP
  delay(100); // Seems to be necessary for the SNTP time to be set correctly
}

void loop() {
  if (!expirationTime || (millis() / 1000) > expirationTime) {
    refreshToken();
  }
  if (strlen(GMAIL_USER_ID) == 0) { // Fetch the user's GMAIL ID using the token
    Response userInfoResponse = httpGetRequest({.url = USERINFO_URL, .bearerToken = accessToken});
    error = deserializeJson(doc, userInfoResponse.body);
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
    } else {
      strncpy(GMAIL_USER_ID, doc["id"], sizeof(GMAIL_USER_ID)); // we keep this as a char[] since I want to use sprintf later...
    }
  }
  if (accessToken && strlen(GMAIL_USER_ID) > 0) {
    char gmailRequestUrl[sizeof(GMAIL_INBOX_REQUEST) + strlen(GMAIL_USER_ID)];
    sprintf(gmailRequestUrl, GMAIL_INBOX_REQUEST, GMAIL_USER_ID);
    String fullGmailRequestUrl = String(gmailRequestUrl) + "?" + GMAIL_INBOX_QUERY;
    Response inboxResponse = httpGetRequest({.url = fullGmailRequestUrl, .bearerToken = accessToken});
    error = deserializeJson(doc, inboxResponse.body);
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
    } else {
      outputNumMessages(doc["resultSizeEstimate"]);
    }
    delay(60000); // Only check email every 60 seconds

  }
}

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

// This function writes the number of messages to the SPI OLED
void outputNumMessages(int numMessages) {
  char header[50];
  char data[4];
  sprintf(header, "GMail Inbox");
  sprintf(data, "%d", numMessages);
  Serial.println(String(header) + ": " + String(data));
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

// This function is needed because weird characters are showing up before and after the JSON from the Google API Responses.
// Hopefully I can figure out why this is happening so that this hack isn't required...
String cleanJSONBody(String body) {
    int startBracket = body.indexOf("{");
    int endBracket = body.lastIndexOf("}");
    return body.substring(startBracket - 1, endBracket + 1);
}

struct Response httpRequest(Request request){
  Response response;

  // These 3 lines convert the URL into a Char buffer
  int arrayLength = request.url.length() + 1;
  char charArray[arrayLength];
  request.url.toCharArray(charArray, arrayLength);

  MatchState ms;
  ms.Target(charArray);
  // Use a regexp to parse out the various components of the URL
  char result = ms.Match("(https?)://([^/]*)(.*)");

  // Only continue if the regex is valid
  if (result == REGEXP_MATCHED) {
    char buf [1000];  // large enough to hold expected string
    String protocol = String(ms.GetCapture(buf, 0));
    String host = String(ms.GetCapture(buf, 1));
    String uri = String(ms.GetCapture(buf, 2));
    String cipStartPort = "80";
    String cipStartProtocol = "TCP";
    if (protocol == "https") {
      cipStartPort = "443";
      cipStartProtocol = "SSL";
      SendCommandSilent("AT+CIPSSLCCONF=0", "OK");
    }
    SendCommandSilent("AT+CIPSTART=\"" + cipStartProtocol + "\",\"" + host + "\"," + cipStartPort, "OK");
    std::vector<String> requestHeaders = {};
    requestHeaders.push_back("Host: " + host);
    requestHeaders.push_back("User-Agent: esp8266/0.1");
    requestHeaders.push_back("Connection: close");
    requestHeaders.push_back("Accept-Encoding: br");
    if (request.method == "POST") {
      requestHeaders.push_back("Content-length: " + String(request.body.length()));
    }
    if (request.contentType) {
      requestHeaders.push_back("content-type: " + request.contentType);
    }
    if (request.bearerToken) {
      requestHeaders.push_back("Authorization: Bearer " + request.bearerToken);
    }
    String tcpString = request.method + " " + uri + " HTTP/1.1\r\n";
    for (int i = 0; i < requestHeaders.size(); i++) {
      tcpString += requestHeaders.at(i) + "\r\n";
    }
    tcpString += "\r\n";
    if (request.body) {
      tcpString += request.body;
    }
    String tcpResponse = SendTCPCommand(tcpString);

    // The headers are separated from the body by 2 CR/NLs
    int doubleReturn = tcpResponse.indexOf("\r\n\r\n");
    String headers = tcpResponse.substring(0, doubleReturn);
    response = FillHeaders(headers);
    response.body = cleanJSONBody(tcpResponse.substring(doubleReturn + 4)); // We need to clean the body because there are strange characters before and after the JSON
  } else if (result == REGEXP_NOMATCH) {
    Serial.println("No Match");
  }
  return response;
}

boolean SendCommand(String cmd, String ack){
  Serial2.println(cmd); // Send "AT+" command to module
  if (echoFind(ack)) { // timed out waiting for ack string
    return true; // ack blank or ack found
  } else {
    return false;
  }
}

boolean SendCommandSilent(String cmd, String ack){
  Serial2.println(cmd); // Send "AT+" command to module
  if (find(ack)) { // timed out waiting for ack string
    return true; // ack blank or ack found
  } else {
    return false;
  }
}

void SendCommandNoAck(String cmd){
  Serial2.println(cmd); // Send "AT+" command to module
  echo();
}

void SendCommandOnly(String cmd){
  Serial2.println(cmd); // Send "AT+" command to module 
}

const int NO_MODE = 0;
const int IPD = 1;
const int IN_DATA = 2;
const int READING_LENGTH = 3;
const int CLOSE = 4;


void GetTCPResponses(std::vector<String> &responses) {
  byte current_char = 0;

  int mode = NO_MODE;

  const String closed = "CLOSED";
  const byte closed_length = closed.length();
  
  const String ipd = "+IPD,";
  const byte ipd_length = ipd.length();

  int data_len = 0;
  String data_len_str;
  String current_response;

  long deadline = millis() + TIMEOUT;
  while(millis() < deadline){
    if (Serial2.available()){
      char ch = Serial2.read();
      switch (mode) {
      case IPD:
        if (ch == ipd[current_char]) {
          if (++current_char == ipd_length) {
            current_char = 0;

            mode = READING_LENGTH;
            data_len_str = "";
          }
        } else {
          mode = NO_MODE;
          current_char = 0;
        }
        break;
      case CLOSE:
        if (ch == closed[current_char]) {
          if (++current_char == closed_length) {
            return;
          }
        } else {
          mode = NO_MODE;
          current_char = 0;
        }
        break;
      case READING_LENGTH:
        if (ch == ':') {
          mode = IN_DATA;
          data_len = data_len_str.toInt();
          current_response = "";
        } else {
          data_len_str += ch;
        }
        break;
      case IN_DATA:
        current_response += ch;
        data_len--;
        if (data_len == 0) {
          mode = NO_MODE;
          responses.push_back(current_response);
        }
        break;
      default:
        // Check to see if we are starting either IPD or CLOSED
        if (ch == closed[0]) {
          mode = CLOSE;
          current_char++;
        } else if (ch == ipd[0]) {
          mode = IPD;
          current_char++;
        }
      }
    }
  }
}

struct Response FillHeaders(String headerBody) {
  int statusLineEnd = headerBody.indexOf("\r\n");
  if (statusLineEnd != -1) {
    // Parse the HTTP Version, Status Code, and Status Message from the first line of the headers:w
    String statusLine = headerBody.substring(0, statusLineEnd);
    int versionEnd = statusLine.indexOf(" ");
    int responseCodeEnd = statusLine.lastIndexOf(" ");
    String version = statusLine.substring(0, versionEnd);
    String statusCode = statusLine.substring(versionEnd + 1, responseCodeEnd);
    String statusMessage = statusLine.substring(responseCodeEnd + 1);

    // Now, parse out each of the individual headers
    int startPosition = statusLineEnd + 2;
    int endPosition = startPosition;
    Response response = {version, statusCode, statusMessage, {}};
    while (endPosition != -1) {
      endPosition = headerBody.indexOf("\r\n", startPosition);
      if (endPosition != -1) {
        String headerLine = headerBody.substring(startPosition, endPosition);
        int delimiter = headerLine.indexOf(": ");
        response.headers.push_back({headerLine.substring(0, delimiter), headerLine.substring(delimiter + 2)});
        startPosition = endPosition + 2;
      }
    }
    return response;
  }
  return {};
}

String SendTCPCommand(String tcpCmd) {
  String cmdLen = String(tcpCmd.length());
  boolean succeeded = SendCommandSilent("AT+CIPSEND=" + cmdLen, "OK"); // Send the length of the TCP command
  if (succeeded) {
    delay(500);
    Serial2.println(tcpCmd);
    std::vector<String> responses = {};
    GetTCPResponses(responses);
    String fullResponse = "";
    for (int i = 0; i < responses.size(); i++) {
      fullResponse += responses.at(i);
    }
    return fullResponse;
  } else {
    Serial.println("error opening connection");
  }
  return "";
}

boolean echoFind(String keyword){
 byte current_char = 0;
 byte keyword_length = keyword.length();
 long deadline = millis() + TIMEOUT;
 while(millis() < deadline){
  if (Serial2.available()){
    char ch = Serial2.read();
    Serial.write(ch);
    if (ch == keyword[current_char])
      if (++current_char == keyword_length){
       return true;
    }
   }
  }
 return false; // Timed out
}

boolean find(String keyword){
 byte current_char = 0;
 byte keyword_length = keyword.length();
 long deadline = millis() + TIMEOUT;
 while(millis() < deadline){
  if (Serial2.available()){
    char ch = Serial2.read();
    if (ch == keyword[current_char]) {
      if (++current_char == keyword_length){
        return true;
      }
    } else {
      current_char = 0; // If the next character isn't correct, reset to the beginning
    }
  }
 }
 return false;
}

void echo() {
 long deadline = millis() + TIMEOUT;
 while(millis() < deadline){
  if (Serial2.available()){
    char ch = Serial2.read();
    Serial.write(ch);
  }
 }
}
