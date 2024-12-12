#include <map>
#include <string>
#include <mutex>
#include <thread>


// Define pins
const int ROW_PINS[] = {36, 39, 34, 21};
const int COL_PINS[] = {27, 14, 12};          // Output pins 1K RESISTOR!!!!!!
const int NUM_ROWS = 4;
const int NUM_COLS = 3;
const int RED_LED_PIN = 15;  // New pin for C note output
const int POT1_PIN = 26;
const int POT2_PIN = 13;
const int POT3_PIN = 35; // frequency in drone mode
const int POT4_PIN = 32;
const int SEQ_PIN = 33; // SEQUENCER IN NOT POT
const int BUTTON_PIN = 4;

const int DAC_PIN = 25;
const int WAVE_PINS[] = {23, 22, 18}; // input?
const int WAVE_PIN_RED = 19; // output?

char* PREV_NOTE;



bool REDBOOLEAN = true; 
const int SCAN_DELAY = 100; // Increase from 50 to 100 microseconds
const unsigned long KEY_DEBOUNCE = 20; // 20ms debounce for key readings


static unsigned long lastKeyTime = 0;

unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 200;  // Debounce delay in ms


// CONSTANTS
const int LFO_TIME[] = {500, 100000}; // NOT WORKING? 
const int LFO_SHIFT[] = {0, 1000};
const int FREQ_RANGE[] = {12.0, 6000.0};
const int HIGH_FILTER = 6000; // not getting much noticible above 6K
const int LOW_FILTER = 10;



// --------------------------------------------------------------------------------------------
// ----------------------------- STRUCTS AND CLASSES ------------------------------------------
// --------------------------------------------------------------------------------------------


// Structure to hold row, col pairs
struct KeyPosition {
    int row;
    int col;
};

// Wave type enum
enum class WaveType {
    SQUARE,
    SINE,
    SAW
};

// Define the states
enum class State {
    KEYBOARD,
    SEQUENCER,
    KEYLOCK
};


struct DroneParams {
    std::mutex paramMutex;
    WaveType waveType = WaveType::SQUARE;
    int durationMs = 500; // POT 1
    uint8_t toneShift = 0; // POT 2
    float frequency = 329.63; // Default to E, will be set by POT3 or SEQ_VAL depending on state
    float highFilter = 6000.0; // pot4
    bool isPlaying = false;
    bool shouldStop = false;
};

DroneParams* droneParams = nullptr;



State currentState = State::KEYBOARD;
WaveType currentWave = WaveType::SQUARE; 



// Function to get the name of the state as a string (for debugging)
const char* getStateName(State state) {
    switch (state) {
        case State::KEYBOARD: return "KEYBOARD";
        case State::SEQUENCER: return "SEQUENCER";
        case State::KEYLOCK: return "KEYLOCK";
        default: return "UNKNOWN";
    }
}


void changeState(State newState) {
    currentState = newState;
    Serial.printf("State changed to: %s\n", getStateName(currentState));
}

void playNote(char* noteName, WaveType waveType = WaveType::SQUARE, int durationMs = 500, uint8_t toneShift = 0) {
    static const std::map<std::string, float> noteToFreq = {
        {"C",  261.63}, {"C#", 277.18},
        {"D",  293.66}, {"D#", 311.13},
        {"E",  329.63},
        {"F",  349.23}, {"F#", 369.99},
        {"G",  392.00}, {"G#", 415.30},
        {"A",  440.00}, {"A#", 466.16},
        {"B",  493.88}
    };
    
    auto it = noteToFreq.find(noteName);
    if (it == noteToFreq.end()) {
        Serial.println("Invalid note!");
        return;
    }
    
    float baseFrequency = it->second;
    const float maxFreqShift = 200.0;  // Increased from 30.0 to 200.0 for more dramatic effect
    const float TWO_PI_ = 6.28318530718;
    const int SAMPLES_PER_PERIOD = 100;
    
    // Calculate frequency shift parameters
    float freqShiftPerMs = 0;
    if (toneShift > 0) {
        // Scale the shift based on toneShift value (0-255)
        float totalShift = (maxFreqShift * toneShift) / 255.0;
        // Double the shift rate since we need to go up and down in the same duration
        freqShiftPerMs = (4 * totalShift) / durationMs;  // Multiply by 4 for steeper curve
    }
    
    unsigned long startTime = millis();
    int dacValue = 0;
    
    // Generate waveform until duration is reached
    while(millis() - startTime < durationMs) {
        unsigned long elapsed = millis() - startTime;
        
        // Calculate current frequency with triangle wave modulation
        float modulationPhase = (float)elapsed / durationMs;  // 0 to 1
        float freqShift;
        
        if (modulationPhase < 0.5) {
            // First half: frequency goes up
            freqShift = (freqShiftPerMs * elapsed);
        } else {
            // Second half: frequency comes down
            freqShift = (freqShiftPerMs * (durationMs - elapsed));
        }
        
        float currentFreq = baseFrequency + freqShift;
        
        // Calculate timing based on current frequency
        int period = (int)(1000000.0 / currentFreq);
        int halfPeriod = period / 2;
        int sampleTime = period / SAMPLES_PER_PERIOD;
        
        static float phase = 0.0;
        
        switch(waveType) {
            case WaveType::SQUARE:
                dacWrite(25, 255);
                delayMicroseconds(halfPeriod);
                dacWrite(25, 0);
                delayMicroseconds(halfPeriod);
                break;
                
            case WaveType::SINE:
                for(int i = 0; i < SAMPLES_PER_PERIOD; i++) {
                    phase = TWO_PI_ * i / SAMPLES_PER_PERIOD;
                    dacValue = (int)(127.5 + 127.5 * sin(phase));
                    dacWrite(25, dacValue);
                    delayMicroseconds(sampleTime);
                }
                break;
                
            case WaveType::SAW:
                for(int i = 0; i < SAMPLES_PER_PERIOD; i++) {
                    dacValue = (i * 255) / SAMPLES_PER_PERIOD;
                    dacWrite(25, dacValue);
                    delayMicroseconds(sampleTime);
                }
                break;
        }
    }
    
    dacWrite(25, 0);
}

// Function to identify the note from active keys
char* identifyNoteFromKeys(KeyPosition activeKeys[], int keyCount) {
    bool rowCols[NUM_ROWS][NUM_COLS] = {false};  // Track active columns for each row

    // Populate rowCols based on active keys
    for (int i = 0; i < keyCount; i++) {
        int row = activeKeys[i].row;
        int col = activeKeys[i].col;
        if (row < NUM_ROWS && col < NUM_COLS) {
            rowCols[row][col] = true;
        }
    }
    // 31 & 32 both on for C and G 
    // [3][1] ON MAYBE????????
    // Identify specific note patterns based on active columns in each row
    //if (rowCols[3][0] && rowCols[3][1] && rowCols[3][2]) return "C";
    if (rowCols[3][0]) return "C"; // just 3 2 still fails

    if (rowCols[2][0] && rowCols[2][1] && rowCols[2][2]) return "C#";
    if (rowCols[1][0] && rowCols[1][1] && rowCols[1][2]) return "D";
    if (rowCols[0][0] && rowCols[0][1] && rowCols[0][2]) return "D#";
    if (rowCols[0][1] && rowCols[0][2] && !rowCols[0][0]) return "E";
    if (rowCols[1][1] && rowCols[1][2] && !rowCols[1][0]) return "F";
    if (rowCols[2][1] && rowCols[2][2] && !rowCols[2][0]) return "F#";
    //if (rowCols[3][1] && rowCols[3][2] && !rowCols[3][0]) return "G";
    if (rowCols[3][1]) return "G";
    if (rowCols[3][2] && !rowCols[3][1] && !rowCols[3][0]) return "G#";
    if (rowCols[2][2] && !rowCols[2][1] && !rowCols[2][0]) return "A";
    if (rowCols[1][2] && !rowCols[1][1] && !rowCols[1][0]) return "A#";
    if (rowCols[0][2] && !rowCols[0][1] && !rowCols[0][0]) return "B";

    return NULL; // No matching note found
}


const char* getWaveTypeName(WaveType wave) {
    switch(wave) {
        case WaveType::SQUARE:
            return "Square";
        case WaveType::SINE:
            return "Sine";
        case WaveType::SAW:
            return "Saw";
        default:
            return "Unknown";
    }
}

WaveType getWaveFromStates(int state0, int state1, int state2) {
    // Priority encoding - first HIGH signal determines the wave type
    if (state0 == HIGH) {
        return WaveType::SQUARE;
    } else if (state1 == HIGH) {
        return WaveType::SINE;
    } else if (state2 == HIGH) {
        return WaveType::SAW;
    } else {
        return WaveType::SQUARE;  // Default to square if no input is HIGH
    }
}

void drone_note(DroneParams* params) {
    const float TWO_PI_ = 6.28318530718;
    const int SAMPLES_PER_PERIOD = 100;
    unsigned long startTime = millis();  // Track time for LFO
    bool prevCycle = false;
    
    while (!params->shouldStop) {
        vTaskDelay(1);  // Critical for watchdog
        
        // Thread-safe parameter capture
        WaveType currentWave;
        int lfoRate;  // renamed from duration to better reflect its purpose
        uint8_t toneShiftAmount;
        float baseFreq;
        float currentHighFilter;
        
        {
            std::lock_guard<std::mutex> lock(params->paramMutex);
            currentWave = params->waveType;
            lfoRate = params->durationMs;  // Now controls LFO speed
            toneShiftAmount = params->toneShift;
            baseFreq = params->frequency;
            currentHighFilter = params->highFilter;
        }

        // Calculate LFO modulation
        unsigned long currentTime = millis();
        //float lfoPhase = (float)((currentTime - startTime) % lfoRate) / lfoRate;  // 0 to 1
        
        float lfoPhase = TWO_PI_ * ((currentTime - startTime) % lfoRate) / lfoRate;
        float lfoValue = (sin(lfoPhase) + 1.0) / 2.0;  // Smooth 0 to 1

        bool currentCycle = lfoPhase < PI;  // First half of cycle
        if (currentCycle != prevCycle) {  // Transition detected
            REDBOOLEAN = !REDBOOLEAN;
            digitalWrite(RED_LED_PIN, REDBOOLEAN ? HIGH : LOW);
            prevCycle = currentCycle;
        }

        // Triangle LFO waveform
        //float lfoValue;
        // if (lfoValue < 0.5) {
        //     lfoValue = lfoPhase * 2;  // 0 to 1
        // } else {
        //     lfoValue = 2.0 - (lfoPhase * 2);  // 1 to 0
        // }
        
        float shiftRatio = pow(2, (toneShiftAmount / 255.0) * 4);  // Up to 4 octaves shift
        float currentFreq = baseFreq * (1.0 + ((shiftRatio - 1.0) * lfoValue));

        // Apply tone shift modulation
        //float shiftAmount = toneShiftAmount * lfoValue;  // Modulate by LFO
        //float currentFreq = baseFreq + shiftAmount;
        //Serial.printf("freqqqq: %f \n", currentFreq);
        
        // Generate one cycle of the waveform
        for (int i = 0; i < SAMPLES_PER_PERIOD; i++) {
          int dacVal = 0;

          switch(currentWave) {
                case WaveType::SAW: // Sawtooth
                    dacVal = map(i, 0, SAMPLES_PER_PERIOD, 0, 255);
                    break;
                case WaveType::SINE: // Sine
                    dacVal = (sin(2 * PI * i / SAMPLES_PER_PERIOD) + 1) * 127.5;
                    break;
                case WaveType::SQUARE: // Square
                    dacVal = (i < SAMPLES_PER_PERIOD / 2) ? 255 : 0;
                    break;
            }
            
            // FILTERING
            if (dacVal < LOW_FILTER) {
              dacVal = LOW_FILTER;
            }
            if (dacVal > currentHighFilter) {
              dacVal = currentHighFilter;
            }

            dacWrite(DAC_PIN, dacVal);
            delayMicroseconds(1000000 / (currentFreq * SAMPLES_PER_PERIOD));
        }
        }
    Serial.printf("OUT OF LOOP!");
    }





float getFrequencyFromKeys() {
    // Static map of note to frequency
    Serial.print(" INSIDE getFREQ \n");
    static const std::map<std::string, float> noteToFreq = {
        {"C",  261.63}, {"C#", 277.18},
        {"D",  293.66}, {"D#", 311.13},
        {"E",  329.63},
        {"F",  349.23}, {"F#", 369.99},
        {"G",  392.00}, {"G#", 415.30},
        {"A",  440.00}, {"A#", 466.16},
        {"B",  493.88}
    };

    // Scan keyboard to get active keys
    KeyPosition activeKeys[NUM_ROWS * NUM_COLS];
    int keyCount = 0;

    // Scan through each column
    for (int col = 0; col < NUM_COLS; col++) {
        digitalWrite(COL_PINS[col], HIGH);
        delayMicroseconds(100);

        // Check all rows for this column
        for (int row = 0; row < NUM_ROWS; row++) {
            if (digitalRead(ROW_PINS[row]) == HIGH) {
                activeKeys[keyCount++] = { row, col };
            }
        }

        digitalWrite(COL_PINS[col], LOW);
        delayMicroseconds(50);
    }

    // Identify the note from active keys
    const char* currentNote = identifyNoteFromKeys(activeKeys, keyCount);
    Serial.printf("%s \n", currentNote);

    // If a note is pressed, find and return its frequency
    if (currentNote != NULL) {
        auto it = noteToFreq.find(currentNote);
        
        if (it != noteToFreq.end()) {
            return it->second;
        }
    }

    return 0.0;  // Return 0 Hz when no note is pressed
}





// ----------------------------------------------------------------------------------------------------------------
// ------------------------------------------ SETUP ---------------------------------------------------------------
// ----------------------------------------------------------------------------------------------------------------





void setup() {
    Serial.begin(115200);
    Serial.println("Starting Piano Matrix Scannerr...");

    //initing state as keyboard
    changeState(State::SEQUENCER); 

    analogReadResolution(12);
    analogSetWidth(12);
    analogSetAttenuation(ADC_11db);


    pinMode(BUTTON_PIN, INPUT_PULLDOWN); // button pin takes input


    // Set column pins as outputs
    for (int i = 0; i < NUM_COLS; i++) {
        pinMode(COL_PINS[i], OUTPUT);
        digitalWrite(COL_PINS[i], LOW);
    }

    // Set row pins as inputs with pulldown
    for (int i = 0; i < NUM_ROWS; i++) {
        pinMode(ROW_PINS[i], INPUT_PULLDOWN);
    }

    // Set up the output pin for C note (RED LIGHT)
    pinMode(RED_LED_PIN, OUTPUT);
    digitalWrite(RED_LED_PIN, LOW);

    // set up Wave-selector pins
    for (int i = 0; i < 3; i++){
      pinMode(WAVE_PINS[i], INPUT_PULLDOWN);
    }

    pinMode(WAVE_PIN_RED, OUTPUT);
    digitalWrite(WAVE_PIN_RED, LOW);

    State newState = State::SEQUENCER;

    droneParams = new DroneParams();
    droneParams->isPlaying = true;
    std::thread droneThread(drone_note, droneParams);
    droneThread.detach();


}


// ----------------------------------------------------------------------------------------------------------------
// ------------------------------------------ LOOP ---------------------------------------------------------------
// ----------------------------------------------------------------------------------------------------------------



void loop() {
    static unsigned long lastUpdate = 0;
    const unsigned long UPDATE_INTERVAL = 50;  // Update every 50ms

    // Only update parameters periodically
    if (millis() - lastUpdate >= UPDATE_INTERVAL) {
        // Read all inputs
        int pot1Val = analogRead(POT1_PIN);
        int pot2Val = analogRead(POT2_PIN);
        int pot3Val = analogRead(POT3_PIN);
        int pot4Val = analogRead(POT4_PIN);
        int seqVal = analogRead(SEQ_PIN);

        // Read wave and button state
        digitalWrite(WAVE_PIN_RED, HIGH);
        WaveType newWave = getWaveFromStates(
            digitalRead(WAVE_PINS[0]),
            digitalRead(WAVE_PINS[1]),
            digitalRead(WAVE_PINS[2])
        );
        digitalWrite(WAVE_PIN_RED, LOW);

        // if keyboard state, read keyboard...


        bool buttonState = digitalRead(BUTTON_PIN);

        // Handle state changes
        if (buttonState == HIGH && (millis() - lastDebounceTime) > debounceDelay) {
            lastDebounceTime = millis();
            State newState = (newWave == WaveType::SQUARE) ? State::SEQUENCER :
                           (newWave == WaveType::SINE) ? State::KEYLOCK :
                           State::KEYBOARD;
            
            if (newState != currentState) {
                changeState(newState);
            }
        }

        // Update parameters if drone is active
        if (droneParams != nullptr) {
            std::lock_guard<std::mutex> lock(droneParams->paramMutex);
            droneParams->waveType = newWave;
            //droneParams->durationMs = map(pot1Val, 0, 4095, LFO_TIME[0], LFO_TIME[1]);
            droneParams->durationMs = map(pot1Val, 0, 4095, 200, 10000);
            droneParams->toneShift = map(pot2Val, 0, 4095, 0, 1000);
            droneParams->highFilter = map(pot4Val, 0, 4095, 0, HIGH_FILTER);

        float newFreq;
        switch (currentState) {
            case State::KEYLOCK:
                newFreq = map(pot3Val, 0, 4095, 12, 6000);
                //droneParams->frequency = newFreq;
                break;
                
            case State::SEQUENCER:
                newFreq = map(seqVal, 0, 4095, 12, 6000);
                //droneParams->frequency = newFreq;
                break;
                
            case State::KEYBOARD:
                //newFreq = getFrequencyFromKeys(); // TRASH!!!!!
                newFreq = 261.63;

                //Serial.print(newFreq);
                break;
              
        }
        droneParams->frequency = newFreq;
        }

        lastUpdate = millis();
    }

    // Short delay to prevent tight loop
    delay(10);
}