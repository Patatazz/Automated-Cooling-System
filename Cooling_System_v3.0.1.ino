#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Temperature and Humidity Sensor
#define DHT_Sensor_1 16
#define DHT_Sensor_2 15
#define DHTTYPE DHT22

// Relays
#define ExhaustRelay 13
#define WaterPumpRelay 25

// Button for changing temperature limit
#define TempToggleButton 4

// For LCD I2C 16x2 
LiquidCrystal_I2C lcd(0x27, 16, 2);

DHT dht1(DHT_Sensor_1, DHTTYPE);
DHT dht2(DHT_Sensor_2, DHTTYPE);

// For Temperature Logic
float avgTemp1 = 0, avgHum1 = 0;
float avgTemp2 = 0, avgHum2 = 0;
float tempLimit = 30; //default TempLimit

// TaskHandling of ESP32
TaskHandle_t TempTaskHandle = NULL;
TaskHandle_t RelayActivation = NULL;
TaskHandle_t DisplayTemps = NULL;
TaskHandle_t TempChangeButton = NULL;

// For Data updating
volatile bool lcdDataReady = false;
volatile bool relayDataReady = false;
volatile bool tempLimitChanged = false;

// For Rocker Switch
int buttonState;

// For LCD
SemaphoreHandle_t lcdMutex;

// For changing Temp Limit
void TempChange(void* parameter){
  while(true){
    buttonState = digitalRead(TempToggleButton);

    if (buttonState == LOW && tempLimit != 35) {
      tempLimit = 35;
      tempLimitChanged = true;
      Serial.println("Temp Limit 35°C");
    } else if (buttonState == HIGH && tempLimit != 30) {
      tempLimit = 30;
      tempLimitChanged = true;
      Serial.println("Temp Limit 30°C");
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// Displaying the Temp and Humidity
void TempDisplay(void* parameter){
  unsigned long tempLimitDisplayTime = 0;
  const unsigned long displayDuration = 2000;
  
  while (true) {

     if (tempLimitChanged) {
      if (xSemaphoreTake(lcdMutex, portMAX_DELAY)) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.printf("Temp Limit: %.0fC", tempLimit);
        xSemaphoreGive(lcdMutex);
      }
      tempLimitChanged = false;
      tempLimitDisplayTime = millis(); // start timestamp
    }

    // Display TempLimitChange for 2 seconds
    if (millis() - tempLimitDisplayTime < displayDuration) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    if(lcdDataReady == true){
      lcdDataReady = false;
      if (xSemaphoreTake(lcdMutex, portMAX_DELAY)) {
        lcd.setCursor(0, 0);
        if (!isnan(avgTemp1) && !isnan(avgHum1)) {
          lcd.printf("T1:%.1fC H1:%.0f%%  ", avgTemp1, avgHum1);
        } else {
          lcd.printf("Sensor 1 Error   ");
        }

        lcd.setCursor(0, 1);
        if (!isnan(avgTemp2) && !isnan(avgHum2)) {
          lcd.printf("T2:%.1fC H2:%.0f%%  ", avgTemp2, avgHum2);
        } else {
          lcd.printf("Sensor 2 Error   ");
        }
        xSemaphoreGive(lcdMutex);
      }
      Serial.printf("Display Task Stack Free: %u bytes\n", uxTaskGetStackHighWaterMark(NULL));  
    }
    vTaskDelay(pdMS_TO_TICKS(3000));
  }
}

// For Relay Activation
void RelayActivationLogic(void *parameter) {
  bool relayActive = false;
  unsigned long relayStartTime = 0;

  while (true) {
    if (relayDataReady){
      relayDataReady = false;
      int totalTemp = (avgTemp1 + avgTemp2) / 2;

      if (!relayActive && totalTemp > tempLimit) {
        // Turn ON relays
        Serial.println("Activating Exhaust Fan and Water Pump!");
        digitalWrite(ExhaustRelay, LOW);   // active LOW
        digitalWrite(WaterPumpRelay, HIGH); 
        relayActive = true;
        relayStartTime = millis();
      }
      // Serial.println("-------------------------");
      // Serial.printf("Relay Task Stack Free: %u bytes\n", uxTaskGetStackHighWaterMark(NULL));
    }

    // After 3 minutes, turn OFF relays
    if (relayActive && (millis() - relayStartTime >= 300000)) {
      digitalWrite(ExhaustRelay, HIGH);
      digitalWrite(WaterPumpRelay, LOW);
      relayActive = false;
      Serial.println("Relay OFF after 3 minutes");
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

// For Temp and Humnidity Reading Only
void TempHumReading(void *parameter){
  const int totalSamples = 10;
  const int delayPerSample = 2000;

  while (1) {
    float tempSum1 = 0, humSum1 = 0;
    float tempSum2 = 0, humSum2 = 0;
    int validCount1 = 0, validCount2 = 0;

    for (int currentSample = 0; currentSample < totalSamples; currentSample++) {
      // Read first sensor
      float t1 = dht1.readTemperature();
      float h1 = dht1.readHumidity();
      if (!isnan(t1) && !isnan(h1)) {
        tempSum1 += t1;
        humSum1  += h1;
        validCount1++;
      }

      // Read second sensor
      float t2 = dht2.readTemperature();
      float h2 = dht2.readHumidity();
      if (!isnan(t2) && !isnan(h2)) {
        tempSum2 += t2;
        humSum2  += h2;
        validCount2++;
      }

      vTaskDelay(pdMS_TO_TICKS(delayPerSample));
    }

    // Update global averages
    if (validCount1 > 0) {
      avgTemp1 = tempSum1 / validCount1;
      avgHum1  = humSum1  / validCount1;
    } else {
      avgTemp1 = NAN;
      avgHum1  = NAN;
    }

    if (validCount2 > 0) {
      avgTemp2 = tempSum2 / validCount2;
      avgHum2  = humSum2  / validCount2;
    } else {
      avgTemp2 = NAN;
      avgHum2  = NAN;
    }

    lcdDataReady = true;
    relayDataReady = true;
    
    // Serial.printf("Sensor 1 → Temp: %.2f °C | Hum: %.2f %%\n", avgTemp1, avgHum1);
    // Serial.printf("Sensor 2 → Temp: %.2f °C | Hum: %.2f %%\n", avgTemp2, avgHum2);
    // Serial.println("-------------------------");
    // Serial.printf("Reading Temp and Humidity Task Stack Free: %u bytes\n", uxTaskGetStackHighWaterMark(NULL));

    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}


void setup() {
  
  Serial.begin(115200);
  
  // Starting DHT22 Temp and Humidity
  dht1.begin();
  dht2.begin();

  // Create mutex for LCD
  lcdMutex = xSemaphoreCreateMutex();

  // LCD Initialization
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.printf("Starting Up!");

  pinMode(ExhaustRelay, OUTPUT);
  pinMode(WaterPumpRelay, OUTPUT);
  pinMode(TempToggleButton, INPUT_PULLUP);

  digitalWrite(ExhaustRelay, HIGH);
  
  xTaskCreatePinnedToCore(
    TempHumReading,       // Task function
    "TempHumReading",     // Task name
    3600,                 // Stack size
    NULL,                 // Parameters
    1,                    // Priority
    &TempTaskHandle,      // Task handle
    1                     // Core 1 = APP CPU
  );

  xTaskCreatePinnedToCore(
    RelayActivationLogic,        // Task function
    "RelayActivation",           // Task name
    3000,                        // Stack size
    NULL,                        // Parameters
    2,                           // Priority
    &RelayActivation,            // Task handle
    1                            // Core 1 = APP CPU
  );

  xTaskCreatePinnedToCore(
    TempDisplay,          // Task function
    "TempDisplay",        // Task name
    10000,                 // Stack size
    NULL,                 // Parameters
    3,                    // Priority
    &DisplayTemps,        // Task handle
    1                     // Core 1 = APP CPU
  );

   xTaskCreatePinnedToCore(
    TempChange,                   // Task function
    "TemperatureChange",          // Task name
    10000,                         // Stack size
    NULL,                         // Parameters
    4,                            // Priority
    &TempChangeButton,            // Task handle
    1                             // Core 1 = APP CPU
  );
}

void loop() {

}
