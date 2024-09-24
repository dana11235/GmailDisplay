#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>

void initializeWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_USERNAME, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  Serial.print("Connected, IP address: ");
  Serial.println(WiFi.localIP());

  // Set time via NTP, as required for x.509 validation
  configTime(3 * 3600, 0, "time.google.com", "time2.google.com");

  Serial.print("Waiting for NTP time sync: ");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("");
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.print("Current time: ");
  Serial.println(asctime(&timeinfo));
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
  bool connected = false;
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

String SendTCPCommand(WiFiClientSecure &client, String tcpCmd) {
  client.println(tcpCmd);
  client.println();
  String fullResponse = "";
  client.setTimeout(1000);
  while (client.connected()) {
    if (client.available()) {
      char c = client.read();
      fullResponse += c;
    }
  }

  Serial.println("");
  Serial.println("Full Response");
  Serial.println("-------");
  Serial.println(fullResponse);
  Serial.println("-------");
  return fullResponse;
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
    Response response = {true, version, statusCode, statusMessage, {}};
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
  WiFiClientSecure client;
  client.setInsecure();

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
    int cipStartPort = 80;
    String cipStartProtocol = "TCP";
    if (protocol == "https") {
      cipStartPort = 443;
      cipStartProtocol = "SSL";
    }
    Serial.println(host + " " + String(cipStartPort));

    httpClient.begin(wifiClient, protocol + "://" + host);
    
    if (!client.connect(host, uint16_t(cipStartPort))) {
      Serial.println("connection failed");
      return response;
    } else {
      response.connected = true;
    };

    std::vector<String> requestHeaders = {};
    requestHeaders.push_back("Host: " + host);
    requestHeaders.push_back("User-Agent: esp8266/0.1");
    requestHeaders.push_back("Connection: close");
    //requestHeaders.push_back("Accept-Encoding: br");
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
    Serial.println(tcpString);
    //String tcpResponse = SendTCPCommand(*client, tcpString);
    client.println(tcpString);
    client.println();
    String tcpResponse = "";
    client.setTimeout(10000);
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        tcpResponse += c;
      }
    }

    Serial.println("");
    Serial.println("Full Response");
    Serial.println("-------");
    Serial.println(tcpResponse);
    Serial.println("-------");

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