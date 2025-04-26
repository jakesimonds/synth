/*
This code is the 'brains' of my digital synth project. It's primary job is to take input from 
the keyboard (which was reverse-engineered from a children's toy & has some quirks)
and then make the corresponding sound. 

We also read in various potentiometer values and based off of that adjusts the timing and pitch 
of the notes as well. See demo video in README.md if you want to see it working. 

Sibling file 'drone.ino' was a more ambitious attempt to use threading to actually emulate LFO and other synth-like
effects. Never really got it working very well, this code is much simpler and works much better. 

Critical functions: playNote(), identifyNoteFromKeys(), and the Loop(). 
*/


#include <map>
#include <string>


// ROW-COL stuff is for matrix to scan for keyboard inputs 
const int ROW_PINS[] = {36, 39, 34, 21}; 
const int COL_PINS[] = {27, 14, 12};
const int NUM_ROWS = 4;
const int NUM_COLS = 3;

const int DAC_PIN = 25; // 
const int BUTTON_PIN = 4;
const int RED_LED_PIN = 15;  
const int POT1_PIN = 26;
const int POT2_PIN = 13;
const int POT3_PIN = 35;
const int POT4_PIN = 32;
const int SEQ_PIN = 33;

const int WAVE_PINS[] = {23, 22, 18}; // sqare, sine & saw waves implemented
const int WAVE_PIN_RED = 19; // as in red LED

unsigned long lastDebounceTime = 0; // for matrix scan of keyboard
const unsigned long debounceDelay = 200; 

// ------------------------------------------------------------
// ---------------- STRUCTS & CLASSES -------------------------
// ------------------------------------------------------------

struct KeyPosition {
    int row;
    int col;
};


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
    /* 
    We take in a noteName & a bunch of parameter values which have been set by the various potentiometers, and then based off of that we're adjusting the timing 
    of how we write a value to our output, DAC_PIN. 

    The timing of how we flip on and off the DAC_PIN is how we're changing frequency, adding random noise, etc etc. 
    */
    
    digitalWrite(RED_LED_PIN, HIGH); // helper red LED indicates you're playing a note

    // turn noteName into frequency number
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
    const float maxFreqShift = 30.0;
    const float TWO_PI_ = 6.28318530718;
    const int SAMPLES_PER_PERIOD = 32;
    

    // shift frequency, this will cause, over the course of the note being played, the freq to rise
    // scaled by toneShift param, which is connected to potentiometer
    float freqShiftPerMs = 0;
    if (toneShift > 0) {
        float totalShift = (maxFreqShift * toneShift) / 255.0;
        freqShiftPerMs = totalShift / durationMs;
    }
    
    unsigned long startTime = millis();
    int dacValue = 0;
    
    while(millis() - startTime < durationMs) {
        unsigned long elapsed = millis() - startTime;

        // upDown param will cause either frequency to rise or fall over course of note
        float currentFreq = baseFrequency + (freqShiftPerMs * elapsed * upDown);
        currentFreq = currentFreq + seqVal;
        int period = (int)(1000000.0 / currentFreq);
        
        
        // add random noise scaled by 'noise' param (another potentiometer)
        int randomSeed = random(-3,3);
        int noiseToAdd = randomSeed * noise;
        period = period + noiseToAdd;
        
        int halfPeriod = period / 2;
        int sampleTime = period / SAMPLES_PER_PERIOD; 
        
        static float phase = 0.0;
        

        // THIS IS WHERE SOUND GENERATION LITERALLY HAPPENS, DAC_WRITE()! 
        // everything else beforehard is just about setting up how it will happen
        switch(waveType) {
            case WaveType::SQUARE:
                dacWrite(DAC_PIN, 255);  // HIGH
                delayMicroseconds(halfPeriod);
                dacWrite(DAC_PIN, 0);    // LOW
                delayMicroseconds(halfPeriod);
                break;
                
            case WaveType::SINE:
                for(int i = 0; i < SAMPLES_PER_PERIOD; i++) {
                    phase = TWO_PI_ * i / SAMPLES_PER_PERIOD;
                    dacValue = (int)(127.5 + 127.5 * sin(phase));
                    dacWrite(DAC_PIN, dacValue);
                    delayMicroseconds(sampleTime);
                }
                break;
                
            case WaveType::SAW:
                for(int i = 0; i < SAMPLES_PER_PERIOD; i++) {
                    dacValue = (i * 255) / SAMPLES_PER_PERIOD;
                    dacWrite(DAC_PIN, dacValue);
                    delayMicroseconds(sampleTime);
                }
                break;
        }
    }
    
    dacWrite(DAC_PIN, 0);
    digitalWrite(RED_LED_PIN, LOW);
}



WaveType getNextWave(WaveType current) {
// when you press the wave button, it just cycles thru the three wave types
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

// 
const char* identifyNoteFromKeys(KeyPosition activeKeys[], int keyCount) {
    /*
    This function runs once per loop(). 

    activeKeys[] I think of as array of pairs of (row, col) values. 

    So each loop we wrote HIGH values along the COL, read back along the ROW, and when we got a HIGH result for 
    that ROW value we added (row, col) to the activeKeys[] array. 

    Now, we're taking this raw result and mapping it back to a keypress. 

    C and G generate lots of false positives due to wiring things that I don't understand
    and since it somewhat works I'm not going to take it all apart ¯\_(ツ)_/¯
    */

    bool rowCols[NUM_ROWS][NUM_COLS] = {false};  // Track active columns for each row

    
    for (int i = 0; i < keyCount; i++) {
        int row = activeKeys[i].row;
        int col = activeKeys[i].col;
        if (row < NUM_ROWS && col < NUM_COLS) {
            rowCols[row][col] = true;
        }
    }

    // Identify specific note patterns based on active columns in each row
    if (rowCols[3][0] && rowCols[3][1] && rowCols[3][2]) return "C";
    //if (rowCols[3][0]) return "C"; // 
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

    return NULL;
}

// ----------------------------------------------------------------------------------------------------------------
// ------------------------------------------ SETUP ---------------------------------------------------------------
// ----------------------------------------------------------------------------------------------------------------


void setup() {
    
    Serial.begin(115200); // baud rate
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
    /* 
    1: read in all potentiometer values
    2: do a matrix scan to see if a key is pressed
    3: if key is pressed, pass along note and potetiometer values to playNote()
    */

    static unsigned long lastUpdate = 0;
    const unsigned long UPDATE_INTERVAL = 50; 


    if (millis() - lastUpdate >= UPDATE_INTERVAL) {
        // Read all inputs
        int pot1Val = analogRead(POT1_PIN);
        int pot2Val = analogRead(POT2_PIN);
        int pot3Val = analogRead(POT3_PIN);
        int pot4Val = analogRead(POT4_PIN);
        int seqVal = analogRead(SEQ_PIN);


    bool buttonState = digitalRead(BUTTON_PIN);
    if (buttonState == HIGH && (millis() - lastDebounceTime) > debounceDelay) {
    currentWave = getNextWave(currentWave);
    Serial.printf("Switched to %s wave\n", getWaveTypeName(currentWave));
    lastDebounceTime = millis();  // Reset debounce timer
  }

    KeyPosition activeKeys[NUM_ROWS * NUM_COLS];
    int keyCount = 0; 

    for (int col = 0; col < NUM_COLS; col++) {
        digitalWrite(COL_PINS[col], HIGH);
        delayMicroseconds(50);

        for (int row = 0; row < NUM_ROWS; row++) {
            if (digitalRead(ROW_PINS[row]) == HIGH) {
                activeKeys[keyCount++] = { row, col };
            }
        }

        digitalWrite(COL_PINS[col], LOW);
        delayMicroseconds(50);
    }

    // Identify the active note
    const char* note = identifyNoteFromKeys(activeKeys, keyCount);


    if (note != NULL) {
        playNote(note, currentWave, map(pot1Val, 0, 4095, 0, 1000), map(pot2Val, 0, 4095, 0, 255), map(pot3Val, 0, 4095, -1, 1), map(pot4Val, 0, 4095, 0, 255), map(seqVal, 0, 4095, 0, 255) ); 

    } 
    delay(50);
}

}