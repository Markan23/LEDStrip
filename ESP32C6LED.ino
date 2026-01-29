#include <WiFi.h>
#include <PubSubClient.h>
#include <FastLED.h>

// WiFi credentials
const char* ssid = "WiFi SSID";
const char* password = "WiFi Password";

// MQTT Broker settings
const char* mqtt_server = "Broker Address";
const int mqtt_port = 1883;
const char* mqtt_user = "mqtt username";
const char* mqtt_password = "mqtt password";

// Pin definitions
#define NEOPIXEL_PIN 23
#define SWITCH1_PIN 6
#define SWITCH2_PIN 7
#define OUTPUT1_PIN 4
#define OUTPUT2_PIN 5
#define BUZZER_PIN 9

// FastLED setup
#define NUM_LEDS 10
CRGB leds[NUM_LEDS];
CRGB baseColors[NUM_LEDS];

// MQTT topics
const char* color_topic = "esp32c6led/neopixel/color";
const char* brightness_topic = "esp32c6led/neopixel/brightness";
const char* state_topic = "esp32c6led/neopixel/state";
const char* flash_topic = "esp32c6led/neopixel/flash";
const char* individual_color_topic = "esp32c6led/neopixel/led/color";
const char* individual_brightness_topic = "esp32c6led/neopixel/led/brightness";
const char* switch1_topic = "esp32c6led/switches/switch1";
const char* switch2_topic = "esp32c6led/switches/switch2";
const char* output1_topic = "esp32c6led/outputs/output1";
const char* output2_topic = "esp32c6led/outputs/output2";
const char* beep_topic = "esp32c6led/buzzer/beep";

// Variables
uint8_t brightness = 10;
CRGB defaultColor = CRGB(255, 255, 255);
bool powerState = false;
bool flashing = false;
int flashInterval = 500;
unsigned long lastFlashTime = 0;
int lastSwitch1State = HIGH;
int lastSwitch2State = HIGH;
uint8_t individualBrightness[NUM_LEDS];

// Buzzer variables
unsigned long buzzerStartTime = 0;
bool buzzerActive = false;
const int beepDuration = 500;    // Duration of each beep
const int beepInterval = 1000;   // Time between beeps in pattern
unsigned long lastBeepTime = 0;
bool buzzerPatternOn = false;
const int buzzerFrequency = 1000; // Frequency in Hz (adjustable)

// WiFi and MQTT clients
WiFiClient espClient;
PubSubClient client(espClient);

// Function to generate a tone
void playTone(int frequency, int duration) {
  unsigned long startTime = millis();
  long period = 1000000 / frequency;  // Period in microseconds
  long halfPeriod = period / 2;
  
  while (millis() - startTime < duration) {
    digitalWrite(BUZZER_PIN, HIGH);
    delayMicroseconds(halfPeriod);
    digitalWrite(BUZZER_PIN, LOW);
    delayMicroseconds(halfPeriod);
  }
}

// Setup function
void setup() {
  Serial.begin(115200);
  
  // Initialize pins
  pinMode(SWITCH1_PIN, INPUT_PULLUP);
  pinMode(SWITCH2_PIN, INPUT_PULLUP);
  pinMode(OUTPUT1_PIN, OUTPUT);
  pinMode(OUTPUT2_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(OUTPUT1_PIN, LOW);
  digitalWrite(OUTPUT2_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  
  // Initialize FastLED
  FastLED.addLeds<NEOPIXEL, NEOPIXEL_PIN>(leds, NUM_LEDS);
  
  // Initialize all LEDs
  for (int i = 0; i < NUM_LEDS; i++) {
    baseColors[i] = defaultColor;
    individualBrightness[i] = brightness;
    leds[i] = defaultColor;
    leds[i].nscale8(brightness);
  }
  FastLED.show();
  
  // Connect to WiFi
  setup_wifi();
  
  // Setup MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

// WiFi connection
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

// MQTT callback function
void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  // Group controls
  if (String(topic) == color_topic) {
    int r, g, b;
    sscanf(message.c_str(), "%d,%d,%d", &r, &g, &b);
    defaultColor = CRGB(r, g, b);
    for (int i = 0; i < NUM_LEDS; i++) {
      baseColors[i] = defaultColor;
    }
    updateNeoPixel();
  }
  else if (String(topic) == brightness_topic) {
    brightness = message.toInt();
    if (brightness < 0) brightness = 0;
    if (brightness > 255) brightness = 255;
    for (int i = 0; i < NUM_LEDS; i++) {
      individualBrightness[i] = brightness;
    }
    updateNeoPixel();
  }
  else if (String(topic) == state_topic) {
    powerState = (message == "ON");
    updateNeoPixel();
  }
  else if (String(topic) == flash_topic) {
    if (message.startsWith("ON")) {
      flashing = true;
      if (message.indexOf(',') != -1) {
        flashInterval = message.substring(message.indexOf(',') + 1).toInt();
      }
    } else if (message == "OFF") {
      flashing = false;
      updateNeoPixel();
    }
  }
  // Individual controls
  else if (String(topic) == individual_color_topic) {
    int index, r, g, b;
    sscanf(message.c_str(), "%d,%d,%d,%d", &index, &r, &g, &b);
    if (index >= 0 && index < NUM_LEDS) {
      baseColors[index] = CRGB(r, g, b);
      updateNeoPixel();
    }
  }
  else if (String(topic) == individual_brightness_topic) {
    int index, bright;
    sscanf(message.c_str(), "%d,%d", &index, &bright);
    if (index >= 0 && index < NUM_LEDS && bright >= 0 && bright <= 255) {
      individualBrightness[index] = bright;
      updateNeoPixel();
    }
  }
  // Other controls
  else if (String(topic) == output1_topic) {
    digitalWrite(OUTPUT1_PIN, (message == "ON") ? HIGH : LOW);
  }
  else if (String(topic) == output2_topic) {
    digitalWrite(OUTPUT2_PIN, (message == "ON") ? HIGH : LOW);
  }
  else if (String(topic) == beep_topic) {
    if (message == "BEEP" && !buzzerActive) {
      buzzerActive = true;
      buzzerStartTime = millis();
      playTone(buzzerFrequency, beepDuration);
      buzzerActive = false;
    }
  }
}

// Update NeoPixel based on current settings
void updateNeoPixel() {
  if (!flashing) {
    if (powerState) {
      for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = baseColors[i];
        leds[i].nscale8(individualBrightness[i]);
      }
      FastLED.setBrightness(brightness);
      FastLED.show();
    } else {
      FastLED.clear();
      FastLED.show();
    }
  }
}

// Handle flashing
void handleFlashing() {
  if (flashing && powerState) {
    unsigned long currentTime = millis();
    if (currentTime - lastFlashTime >= flashInterval) {
      static bool flashState = false;
      flashState = !flashState;
      if (flashState) {
        for (int i = 0; i < NUM_LEDS; i++) {
          leds[i] = baseColors[i];
          leds[i].nscale8(individualBrightness[i]);
        }
        FastLED.setBrightness(brightness);
      } else {
        FastLED.clear();
      }
      FastLED.show();
      lastFlashTime = currentTime;
    }
  }
}

// Handle standalone buzzer pattern
void handleBuzzer() {
  unsigned long currentTime = millis();

  // Handle MQTT-triggered single beep
  if (buzzerActive) {
    if (currentTime - buzzerStartTime >= beepDuration) {
      buzzerActive = false;  // Reset after MQTT beep
    }
  }

  // Handle standalone pattern
  if (buzzerPatternOn && !buzzerActive) {  // Don't overlap with MQTT beeps
    if (currentTime - lastBeepTime >= beepInterval) {
      playTone(buzzerFrequency, 100);  // 100ms tone
      lastBeepTime = currentTime;
    }
  }
}

// MQTT reconnect function
void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
      Serial.println("connected");
      client.subscribe(color_topic);
      client.subscribe(brightness_topic);
      client.subscribe(state_topic);
      client.subscribe(flash_topic);
      client.subscribe(individual_color_topic);
      client.subscribe(individual_brightness_topic);
      client.subscribe(output1_topic);
      client.subscribe(output2_topic);
      client.subscribe(beep_topic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

// Main loop
void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  handleFlashing();
  handleBuzzer();

  int switch1State = digitalRead(SWITCH1_PIN);
  int switch2State = digitalRead(SWITCH2_PIN);

  // Switch 1: Toggle LED power
  if (switch1State != lastSwitch1State) {
    if (switch1State == LOW) {
      powerState = !powerState;
      updateNeoPixel();
    }
    client.publish(switch1_topic, switch1State == LOW ? "ON" : "OFF");
    lastSwitch1State = switch1State;
  }

  // Switch 2: Toggle buzzer pattern
  if (switch2State != lastSwitch2State) {
    if (switch2State == LOW) {
      buzzerPatternOn = !buzzerPatternOn;
    }
    client.publish(switch2_topic, switch2State == LOW ? "ON" : "OFF");
    lastSwitch2State = switch2State;
  }

  delay(50);
}
