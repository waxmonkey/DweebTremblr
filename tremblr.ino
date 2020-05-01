/*
 * DwTremblr -- Control an output pin through JSON commands received on the serial port.
 * I will be using a mega2560 so need to test that works
 * The control buttons need to send commands via the rc-switch chip
 * 
 * 
 * 
 * (C) 2018 DeviceWeb.org / onwrikbaar@hotmail.com
 * Note: Only tested on Arduino Uno! 
 */
//Signals for each button on the remote control

#define SPEED_UP 16076992
#define POWER_TOGGLE 16076812
#define SPEED_DOWN 16076848
#define SUCK_LESS 16076803
#define SUCK_MORE 16077568
// Define Libraries
#include <RCSwitch.h>
#include "DwRingBuffer.hpp"
#include "JsonUtils.hpp"


// RF transmitter
RCSwitch mySwitch = RCSwitch();


enum Avail : uint8_t { AVAIL_NONE, AVAIL_LOCAL, AVAIL_REMOTE };

// Note that pin LED_BUILTIN does *not* support PWM; it will simply be on for dutycycle >= 50%, off otherwise.
// To see the effect of changing the dutycycle variable, use one of the PWM-capable pins instead of the builtin LED.
static const int outPin = LED_BUILTIN;
static Avail avail = AVAIL_LOCAL;
static DwRingBuffer outputBuffer;                   // Buffer holding the data to be sent to the client.

static struct {
    const char *key;
    long val;
    const char *desc;
} vars[] = {                                        // The application variables that can be changed by the client.
    // The descriptor strings specify the controls that will be displayed in the web interface.
    { "mode", 2, R"("type":"PickList","label":"mood","action":"set_mode","options":["Off","On","Blinx"])" },
    { "dutycycle", 55, R"("type":"Spinner","label":"Duty cycle [%]","action":"set_dutycycle","max":100,"step":5)" },
//Drop down menu needs to be replaced with buttons for each control. 
  
// I need a button for POWER
// I need a button for speed up
// I need a button for speed down
// I need a button for suck more
// I need a button for suck less
    
};

// Switch the output pin depending on the current blink mode. For pins that support PWM,
// variable 'dutycycle' is used to set the pulse width (scaled from [0..100%] to [0..255]).
static void updatePinLevel(int outputPin, int blinkMode, int dutycycle) {
    switch (blinkMode) {
        static unsigned long endMillis = 0UL;
        case 0:                                     // Off.
            analogWrite(outputPin, 0);
            endMillis = 0UL;
            break;
        case 1:                                     // On.
            analogWrite(outputPin, (int)(2.55f * dutycycle + 0.5f));
            endMillis = 0UL;
            break;
        default: {                                  // Everything else means Blink.
            auto const currentMillis = millis();    // What time is it, cuckoo?
            if (endMillis == 0UL) {                 // Timer not set,
                endMillis = currentMillis;          //   so set it now.
            } else if (currentMillis > endMillis) { // It's time to toggle the pin.
                digitalWrite(outputPin, digitalRead(outputPin) ^ 1);
                endMillis += 250;                   // Set the next deadline (ms).
            }
        }
    }
}


static const char *availStr(Avail a) {
    static const char *const an[] = {"none", "local", "remote"};
    return an[a <= AVAIL_REMOTE ? a : AVAIL_NONE];
}

// Send this application's relevant parameters to the client.
static void sendState() {
    JsonWriter jw{obWrite};
    char buf[12];
    jw.begin();
    jw.writeKQV("avail", availStr(avail));
    jw.writeKV(vars[0].key, itoa(vars[0].val, buf, 10));
    // The dutycycle control must only be shown in the UI for mode = 1 ("On").
    jw.writeKV(vars[1].key, vars[0].val == 1 ? itoa(vars[1].val, buf, 10) : "null");
    jw.end();
}

// Send the descriptors for the UI elements to the client.
static void sendDescriptors() {
    JsonWriter jw{obWrite};
    for (auto& parm : vars) {
        if (parm.desc != NULL) {
            jw.writeDescForParam(parm.key, parm.desc);
        }
    }
}

// Copy (part of) a byte array to the output buffer.
static size_t obWrite(const char *buf, size_t nb) {
    return outputBuffer.write((const uint8_t *)buf, nb);
}

// Copy a null-terminated string's contents to the output stream.
static size_t postCString(const char *nts) {
    return obWrite(nts, strlen(nts));
}

// Copy a String object's contents to the output stream.
size_t postString(String const& s) {
    return obWrite(s.c_str(), s.length());
}

// Handle a 'set_xxx' request from the client.
static void updateParam(const char *nbp) {
    const char *nep = strstr(nbp, "\":");           // Find end of parameter name.
    if (nep == NULL) return;

    size_t nlen = nep - nbp;
    for (auto& parm : vars) {                       // Search the parameter in the table.
        if (strncmp(nbp, parm.key, nlen) == 0) {    // The parameter exists, so update it.
            char *endp;
            auto const iv = strtol(nep + 2, &endp, 10);
            if (endp != NULL) parm.val = iv;        // Currently only suitable for integer variables.
            break;
        }
    }
}

// Process a request received on the serial port.
static void handleInputLine(const char *line, size_t) {
    if (line[0] == '\0') {                          // Empty request,
        postCString("# {}\n");                      // send empty, but valid, response.
        return;
    }
    if (strstr(line, "{}") == line) {               // Request for the device's descriptor and state.
        postCString(R"(# {"klass":"arduino","name":"DeviceWeb Tremblr","controls":[)");
        sendDescriptors();
        postCString("],\"state\":");
        sendState();
        postCString("}\n");
        return;
    }

    if (strstr(line, "{\"reserve\":") == line) {
        avail = AVAIL_REMOTE;                       // Accept remote commands.
        // TODO Disable the local controls, if any.
    } else if (strstr(line, "{\"release\":") == line) {
        avail = AVAIL_LOCAL;                        // Refuse remote commands.
        // TODO Re-enable the local controls, if any.
    }
    if (avail == AVAIL_REMOTE) {                    // Client is allowed to change values.
        const char *np = strstr(line, "\"set_");    // Look for a "set_..." request.
        if (np != NULL) updateParam(np + 5);        // Param name starts after "set_
    }
    postCString("# ");
    sendState();
    postCString("\n");
}

// One-time initialisation.
void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    Serial.begin(19200);
    outputBuffer.clear();
    postCString("# DwTremblr v1.92\n");

// Transmitter is connected to Arduino Pin #2  
  mySwitch.enableTransmit(2);
}



// The main program loop.
void loop() {
///////////////////// control arduino pin

        /*  
          //This section requires extensive work to update where the control presses are obtained from. 
          if(readString.indexOf('2') >0)//checks for 2
          {
           mySwitch.send(SPEED_UP, 24); // Speed Up
            Serial.println("Speed Up");
            Serial.println();
          }
          if(readString.indexOf('3') >0)//checks for 3
          {
          mySwitch.send(SPEED_DOWN, 24);  // Speed Down
            Serial.println("Speed Down");
            Serial.println();
          }
          if(readString.indexOf('4') >0)//checks for 4
          {
           mySwitch.send(SUCK_MORE, 24);    // Suck more
            Serial.println("Suck More");
            Serial.println();
          }
          if(readString.indexOf('5') >0)//checks for 5
          {
           mySwitch.send(SUCK_LESS, 24);    // Suck less
            Serial.println("Suck Less");
            Serial.println();
          }
          if(readString.indexOf('6') >0)//checks for 6
          {
             mySwitch.send(POWER_TOGGLE, 24);   // Toggle Power
            Serial.println("Power");
            Serial.println();
          }
          //clearing string for next read
          readString="";

        }
  */
    static JsonReader jr{handleInputLine};

    if (Serial.availableForWrite() > 0) {           // Handle serial output one byte at a time, without blocking.
        noInterrupts();                             // Keep the buffer safe while we're accessing it.
        auto const b = outputBuffer.getNextByte();
        interrupts();
        if (b >= 0) Serial.write((uint8_t)b);
    }
    if (Serial.available() > 0) {                   // Handle serial input one byte at a time, without blocking.
        auto const b = (uint8_t)Serial.read();
        jr.write(&b, 1);
    }
    // Check whether anything must be done with the output pin.
    updatePinLevel(outPin, vars[0].val, vars[1].val);
}
