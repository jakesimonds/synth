#include <map>
#include <string>

// constants & pins
const int ROW_PINS[] = {36, 39, 34, 21};
const int COL_PINS[] = {27, 14, 12};
const int NUM_ROWS = 4;
const int NUM_COLS = 3;
const int DAC_PIN = 25;
const int BUTTON_PIN = 4;
const int RED_LED_PIN = 15;  
const int POT1_PIN = 26;
const int POT2_PIN = 13;
const int POT3_PIN = 35;
const int POT4_PIN = 32;
const int SEQ_PIN = 33;

const int WAVE_PINS[] = {23, 22, 18}; 
const int WAVE_PIN_RED = 19;

unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 200; 

// ------------------------------------------------------------
// ---------------- STRUCTS & CLASSES -------------------------
// ------------------------------------------------------------

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

WaveType currentWave = WaveType::SQUARE; 

// ----------------------------------------------------------------------------------------------------------------
// ------------------------------------------ FUNCTIONS ---------------------------------------------------------------
// ----------------------------------------------------------------------------------------------------------------


void playNote(const char* noteName, WaveType waveType = WaveType::SQUARE, int durationMs = 500, uint8_t toneShift = 0, float upDown = 0, float noise = 0, int seqVal = 0) {
    digitalWrite(RED_LED_PIN, HIGH);
    static const std::map<std::string, float> noteToFreq = {
        {"C",  261.63}, {"C#", 277.18},
        {"D",  293.66}, {"D#", 311.13},
        {"E",  329.63},
        {"F",  349.23}, {"F#", 369.99},
        {"G",  392.00}, {"G#", 415.30},
        {"A",  440.00}, {"A#", 466.16},
        {"B",  493.88}
    };
    
    // Look up the frequency
    auto it = noteToFreq.find(noteName);
    if (it == noteToFreq.end()) {
        Serial.println("Invalid note!");
        return;
    }
    
    float baseFrequency = it->second;
    const float maxFreqShift = 30.0; // Maximum frequency shift
    const float TWO_PI_ = 6.28318530718;
    const int SAMPLES_PER_PERIOD = 32;
    
    // Calculate how much to increase frequency per millisecond
    float freqShiftPerMs = 0;
    if (toneShift > 0) {
        // Scale the shift based on toneShift value (0-255)
        float totalShift = (maxFreqShift * toneShift) / 255.0;
        freqShiftPerMs = totalShift / durationMs;
    }
    
    unsigned long startTime = millis();
    int dacValue = 0;
    
    // Generate waveform until duration is reached
    while(millis() - startTime < durationMs) {
        // Calculate current frequency based on time elapsed
        unsigned long elapsed = millis() - startTime;
        float currentFreq = baseFrequency + (freqShiftPerMs * elapsed * upDown);

        //if (durationMs < 200) {
        currentFreq = currentFreq + seqVal;
        //}
        
        // Calculate timing based on current frequency
        int period = (int)(1000000.0 / currentFreq);
        //Serial.printf("period before: %d\n", period);
        int randomSeed = random(-3,3);
        int noiseToAdd = randomSeed * noise;
        //Serial.printf("noise to add: %d\n", noiseToAdd);
        period = period + noiseToAdd;

        //Serial.printf("period after: %d\n", period);
        
        int halfPeriod = period / 2;
        int sampleTime = period / SAMPLES_PER_PERIOD; 
        
        static float phase = 0.0;
        
        switch(waveType) {
            case WaveType::SQUARE:
                // Simple square wave with adjusted frequency
                dacWrite(25, 255);  // HIGH
                delayMicroseconds(halfPeriod);
                dacWrite(25, 0);    // LOW
                delayMicroseconds(halfPeriod);
                break;
                
            case WaveType::SINE:
                // Generate sine wave with adjusted frequency
                for(int i = 0; i < SAMPLES_PER_PERIOD; i++) {
                    phase = TWO_PI_ * i / SAMPLES_PER_PERIOD;
                    dacValue = (int)(127.5 + 127.5 * sin(phase));
                    dacWrite(25, dacValue);
                    delayMicroseconds(sampleTime);
                }
                break;
                
            case WaveType::SAW:
                // Generate sawtooth wave with adjusted frequency
                for(int i = 0; i < SAMPLES_PER_PERIOD; i++) {
                    dacValue = (i * 255) / SAMPLES_PER_PERIOD;
                    dacWrite(25, dacValue);
                    delayMicroseconds(sampleTime);
                }
                break;
        }
    }
    
    // Ensure DAC is off when done
    dacWrite(25, 0);
    digitalWrite(RED_LED_PIN, LOW);
}


WaveType getNextWave(WaveType current) {
    switch(current) {
        case WaveType::SQUARE:
            return WaveType::SINE;
        case WaveType::SINE:
            return WaveType::SAW;
        case WaveType::SAW:
            return WaveType::SQUARE;
        default:
            return WaveType::SQUARE;
    }
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

// Function to identify the note from active keys
const char* identifyNoteFromKeys(KeyPosition activeKeys[], int keyCount) {
    bool rowCols[NUM_ROWS][NUM_COLS] = {false};  // Track active columns for each row

    // Populate rowCols based on active keys
    for (int i = 0; i < keyCount; i++) {
        int row = activeKeys[i].row;
        int col = activeKeys[i].col;
        if (row < NUM_ROWS && col < NUM_COLS) {
            rowCols[row][col] = true;
        }
    }

    // Identify specific note patterns based on active columns in each row
    if (rowCols[3][0] && rowCols[3][1] && rowCols[3][2]) return "C";
    //if (rowCols[3][0]) return "C"; // this will make C play, but you get lots of false positves too
    if (rowCols[2][0] && rowCols[2][1] && rowCols[2][2]) return "C#";
    if (rowCols[1][0] && rowCols[1][1] && rowCols[1][2]) return "D";
    if (rowCols[0][0] && rowCols[0][1] && rowCols[0][2]) return "D#";
    if (rowCols[0][1] && rowCols[0][2] && !rowCols[0][0]) return "E";
    if (rowCols[1][1] && rowCols[1][2] && !rowCols[1][0]) return "F";
    if (rowCols[2][1] && rowCols[2][2] && !rowCols[2][0]) return "F#";
    //if (rowCols[3][1]) return "G"; // ditto to C
    if (rowCols[3][2] && !rowCols[3][1] && !rowCols[3][0]) return "G#";
    if (rowCols[2][2] && !rowCols[2][1] && !rowCols[2][0]) return "A";
    if (rowCols[1][2] && !rowCols[1][1] && !rowCols[1][0]) return "A#";
    if (rowCols[0][2] && !rowCols[0][1] && !rowCols[0][0]) return "B";

    return NULL; // No matching note found
}

// ----------------------------------------------------------------------------------------------------------------
// ------------------------------------------ SETUP ---------------------------------------------------------------
// ----------------------------------------------------------------------------------------------------------------


void setup() {
    
    Serial.begin(115200);
    Serial.println("Starting Piano Matrix Scanner...");

    // Set column pins as outputs
    for (int i = 0; i < NUM_COLS; i++) {
        pinMode(COL_PINS[i], OUTPUT);
        digitalWrite(COL_PINS[i], LOW);
    }

    // Set row pins as inputs with pulldown
    for (int i = 0; i < NUM_ROWS; i++) {
        pinMode(ROW_PINS[i], INPUT_PULLDOWN);
    }

    analogReadResolution(12);
    analogSetWidth(12);
    analogSetAttenuation(ADC_11db);

    pinMode(RED_LED_PIN, OUTPUT);
    digitalWrite(RED_LED_PIN, LOW);

    pinMode(BUTTON_PIN, INPUT_PULLDOWN); // button pin takes input

    for (int i = 0; i < 3; i++){
      pinMode(WAVE_PINS[i], INPUT_PULLDOWN);
    }

    pinMode(WAVE_PIN_RED, OUTPUT);
    digitalWrite(WAVE_PIN_RED, LOW);

}

// ----------------------------------------------------------------------------------------------------------------
// ------------------------------------------ LOOP ---------------------------------------------------------------
// ----------------------------------------------------------------------------------------------------------------


void loop() {

    static unsigned long lastUpdate = 0;
    const unsigned long UPDATE_INTERVAL = 50; 

    // Only update parameters periodically
    if (millis() - lastUpdate >= UPDATE_INTERVAL) {
        // Read all inputs
        int pot1Val = analogRead(POT1_PIN);
        int pot2Val = analogRead(POT2_PIN);
        int pot3Val = analogRead(POT3_PIN);
        int pot4Val = analogRead(POT4_PIN);
        int seqVal = analogRead(SEQ_PIN);
        //Serial.printf("duration %d\n", pot1Val);
        //Serial.printf("pot2 %d\n", pot2Val);
        //Serial.printf("pot3 %d\n", pot3Val);
        //Serial.printf("pot4 %d\n", pot4Val);
        //Serial.printf("seq: %d\n", seqVal);

        // Read wave and button state
        // digitalWrite(WAVE_PIN_RED, HIGH);
        // WaveType newWave = getWaveFromStates(
        //     digitalRead(WAVE_PINS[0]),
        //     digitalRead(WAVE_PINS[1]),
        //     digitalRead(WAVE_PINS[2])
        // );
        // digitalWrite(WAVE_PIN_RED, LOW);


    bool buttonState = digitalRead(BUTTON_PIN);
    if (buttonState == HIGH && (millis() - lastDebounceTime) > debounceDelay) {
    currentWave = getNextWave(currentWave);
    Serial.printf("Switched to %s wave\n", getWaveTypeName(currentWave));
    lastDebounceTime = millis();  // Reset debounce timer
  }

    // Temporary storage for active keys
    KeyPosition activeKeys[NUM_ROWS * NUM_COLS];
    int keyCount = 0; // Tracks the number of active keys

    // Scan through each column
    for (int col = 0; col < NUM_COLS; col++) {
        // Set current column HIGH
        digitalWrite(COL_PINS[col], HIGH);
        delayMicroseconds(50);

        // Check all rows for this column
        for (int row = 0; row < NUM_ROWS; row++) {
            if (digitalRead(ROW_PINS[row]) == HIGH) {
                // Store row, col position as active
                activeKeys[keyCount++] = { row, col };
            }
        }

        // Set column back to LOW before moving to next
        digitalWrite(COL_PINS[col], LOW);
        delayMicroseconds(50);
    }

    // Identify the active note
    const char* note = identifyNoteFromKeys(activeKeys, keyCount);


    if (note != NULL) {
        //Serial.print("Note pressed: ");
        //Serial.println(note);
        playNote(note, currentWave, map(pot1Val, 0, 4095, 0, 1000), map(pot2Val, 0, 4095, 0, 255), map(pot3Val, 0, 4095, -1, 1), map(pot4Val, 0, 4095, 0, 255), map(seqVal, 0, 4095, 0, 255) ); 

    } 
    // Delay before next scan
    delay(50);
}

}