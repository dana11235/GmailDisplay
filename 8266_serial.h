// Timeout for Serial Requests
#define TIMEOUT 10000 // mS

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

#define ESP01_RX 5
#define ESP01_TX 4

void initializeWifi() {
    // Setup the UART we use to communicate with the ESP01
  Serial2.setRX(ESP01_RX);
  Serial2.setTX(ESP01_TX);
  Serial2.begin(115200);

  Serial2.println(""); // For some reason, there is some weird stuff sent on Serial2. This flushes it.
  SendCommandSilent("AT+CWMODE=1","OK"); // Set the 8266 into the corect wifi mode
  SendCommandSilent("AT+CWAUTOCONN=0","OK"); // Make it so that the access point doesn't automatically connect
  SendCommandSilent("AT+CWJAP=\"" + WIFI_USERNAME + "\",\"" + WIFI_PASSWORD + "\"","OK");  // Connect to WIFI
  SendCommandSilent("AT+CIPSNTPCFG=1,8,\"time.google.com\",\"time2.google.com\",\"time3.google.com\"", "OK"); // Sync with Google SNTP
  delay(100); // Seems to be necessary for the SNTP time to be set correctly
  SendCommandSilent("AT+CIPSSLCCONF=0", "OK");
}

// This function is needed because weird characters are showing up before and after the JSON from the Google API Responses.
// Hopefully I can figure out why this is happening so that this hack isn't required...
String cleanJSONBody(String body) {
    int startBracket = body.indexOf("{");
    int endBracket = body.lastIndexOf("}");
    return body.substring(startBracket - 1, endBracket + 1);
}

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