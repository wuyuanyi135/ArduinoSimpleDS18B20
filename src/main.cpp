#include "ESP8266Init.h"
#include "PropertyNode.h"
#include <Arduino.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <PubSubClientInterface.h>
#define OW_BUS_PIN D1
#define MAX_NUM_DEVICE 6

ESP8266Init esp8266Init(
        "DCHost",
        "dchost000000",
        "192.168.43.1",
        1883,
        "Arduino DS18B20");

OneWire oneWire(OW_BUS_PIN);
DallasTemperature sensors(&oneWire);
DeviceAddress address[MAX_NUM_DEVICE];
ulong last_time = 0;
ulong wait_time = 0;
ulong request_time = 0;
PropertyNode<bool> enable("enable", true, false, true);
PropertyNode<int> interval("interval", 1000, false, true);
std::vector<PropertyNode<float>> temperatureNodes;

PubSubClientInterface mqttInterface(esp8266Init.client);
enum State {
    IDLE,
    WAIT_CONVERSION
};
State state = IDLE;

void setup() {
    // write your initialization code here
    Serial.begin(115200);
    if (esp8266Init.blocking_init() != ESP8266Init::FINISHED) {
        delay(1000);
        ESP.restart();
    }

    sensors.begin();
    // Pull up
    pinMode(OW_BUS_PIN, INPUT_PULLUP);
    sensors.setResolution(12);
    sensors.setWaitForConversion(false);
    wait_time = sensors.millisToWaitForConversion(12);
    int num = sensors.getDS18Count();
    Serial.print("Found ");
    Serial.print(num, DEC);
    Serial.println(" devices.");

    interval.set_validator([](int interval) { return interval > 250; });
    interval.register_interface(mqttInterface);

    enable.register_interface(mqttInterface);


    num = num > MAX_NUM_DEVICE ? MAX_NUM_DEVICE : num;

    for (int i = 0; i < num; ++i) {
        bool success = sensors.getAddress(address[i], i);
        if (success) {
            char hexAddr[17];
            hexAddr[16] = 0;
            for (int j = 0; j < 8; j++) {
                sprintf(&hexAddr[2 * j], "%02x", address[i][j]);
            }
            temperatureNodes.push_back(
                    PropertyNode<float>(std::string("temperature/") + hexAddr, -300, true));
            temperatureNodes[i].register_interface(mqttInterface);
        }
    }
}

void loop() {
    esp8266Init.client.loop();
    switch (state) {
        case IDLE:
            if (millis() - last_time >= interval.get_value() && enable.get_value()) {
                sensors.requestTemperatures();
                state = WAIT_CONVERSION;
                request_time = millis();
            }
            break;
        case WAIT_CONVERSION:
            if (millis() - request_time > wait_time) {
                for (int i = 0; i < temperatureNodes.size(); ++i) {
                    float tempC = sensors.getTempC(address[i]);
                    if (tempC == DEVICE_DISCONNECTED_C) {
                        continue;
                    } else {
                        temperatureNodes[i].set_value(tempC);
                        temperatureNodes[i].notify_get_request_completed();
                    }
                }
                state = IDLE;
                last_time = millis();
            }
            break;
    }
}