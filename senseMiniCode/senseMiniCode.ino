/*
   license: CC-BY-SA
   senseMini by Václav Peloušek http://bastl-instruments.com
   sensor calibration module
   the code analyses sensor voltage and threshold voltage and converts them into:
    -LED indictaion
    -sound generation
    -midi information

   open for further ideas
   made for pifcamp 2018 http://pif.camp

   full documentation at:https://github.com/bastl-instruments/senseMini
*/
#include <SoftwareSerial.h>
#include <MIDI.h>
#include <EEPROM.h>

#define ANALOG_INPUT 0
#define TOUCH_INPUT 1
#define THRESHOLD 2
#define CLP_D 3
#define CLP_U 6
#define THR_U 4
#define THR_D 5
#define BUTTON_PIN 7
#define MIDI_IN 10
#define MIDI_OUT 9
#define TONE_OUT 8

const char PROGMEM sinetable[128] = {
  0, 0, 0, 0, 1, 1, 1, 2, 2, 3, 4, 5, 5, 6, 7, 9, 10, 11, 12, 14, 15, 17, 18, 20, 21, 23, 25, 27, 29, 31, 33, 35, 37, 40, 42, 44, 47, 49, 52, 54, 57, 59, 62, 65, 67, 70, 73, 76, 79, 82, 85, 88, 90, 93, 97, 100, 103, 106, 109, 112, 115, 118, 121, 124,
  128, 131, 134, 137, 140, 143, 146, 149, 152, 155, 158, 162, 165, 167, 170, 173, 176, 179, 182, 185, 188, 190, 193, 196, 198, 201, 203, 206, 208, 211, 213, 215, 218, 220, 222, 224, 226, 228, 230, 232, 234, 235, 237, 238, 240, 241, 243, 244, 245, 246, 248, 249, 250, 250, 251, 252, 253, 253, 254, 254, 254, 255, 255, 255,
};

SoftwareSerial softSerial(MIDI_IN, MIDI_OUT);

struct MySettings : public midi::DefaultSettings
{
  static const unsigned SysExMaxSize = 1; // Accept SysEx messages up to 1024 bytes long.
};

MIDI_CREATE_CUSTOM_INSTANCE(SoftwareSerial, softSerial, midiA, MySettings);


bool buttonState = false;

#define NUMBER_OF_SOUND_MODES 4
#define FREQUENCY 0
#define TIMBRE 1
#define AMPLITUDE 2

uint8_t timbre = 0;
uint8_t amplitude = 255;
uint8_t soundMode = 0;

#define MIDI_NOTE 56
#define MIDI_CC 30
#define MIDI_CHANNEL 1
#define LEARNING_CC 120
#define LEARNING_CHANNEL 16
#define PRESCALE _BV(CS10)

uint32_t _timeCounter, _time;
bool thresholdState = false;
bool otherIsLearning = false;
int analogValue, lastAnalogValue;
int threshold;
unsigned int _phase, _phase2;
uint8_t sample;
unsigned int frequency = 500;
bool flop;
unsigned char wavetable[256];


void handleNoteOn(byte channel, byte pitch, byte velocity)
{
  //if (channel == MIDI_CHANNEL && !buttonState)
  midiA.sendNoteOn(pitch + 1, velocity, MIDI_CHANNEL);
}

void handleNoteOff(byte channel, byte pitch, byte velocity)
{
  //if (channel == MIDI_CHANNEL && !buttonState)
  midiA.sendNoteOff(pitch + 1, velocity, MIDI_CHANNEL);
}

void handleControlChange(byte channel, byte number, byte value)
{
  //if (channel == MIDI_CHANNEL && !buttonState)
  midiA.sendControlChange(number + 1, value, MIDI_CHANNEL);

  if (channel == LEARNING_CHANNEL && number == LEARNING_CC) otherIsLearning = value >> 6;
}

void setTheTimers() {
  TCCR0A = 2 << COM0A0 | 2 << COM0B0 | 3 << WGM00;
  TCCR0B = 0 << WGM02 | 1 << CS00;

  bitWrite(TCCR0B, CS00, 1);
  bitWrite(TCCR0B, CS01, 0);
  bitWrite(TCCR0B, CS02, 0);

  TCCR1A = 0;
  TCCR1B = 0;
  TCCR1B |= (1 << WGM12); // configure timer1 for CTC mode
  //TIMSK1 |= (1 << OCIE1A); // enable the CTC interrupt
  bitWrite( TIMSK1,OCIE1A,soundMode!=3);
  sei(); // enable global interrupts
  OCR1A = 255; // Should return frequency ~1000hz
  TCCR1B |= PRESCALE;
}
#define SOUND_MODE_BYTE 7
void setup() {

  //set all the pins
  pinMode(ANALOG_INPUT, INPUT);
  pinMode(TOUCH_INPUT, INPUT);
  pinMode(THRESHOLD, INPUT);

  pinMode(CLP_D, OUTPUT);
  pinMode(CLP_U, OUTPUT);
  pinMode(THR_U, OUTPUT);
  pinMode(THR_D, OUTPUT);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  pinMode(MIDI_IN, INPUT);
  pinMode(MIDI_OUT, OUTPUT);
  pinMode(TONE_OUT, OUTPUT);
  
  soundMode=EEPROM.read(SOUND_MODE_BYTE);
  soundMode%=NUMBER_OF_SOUND_MODES;
  
  sineWave(); //fill the wavetable
  setTheTimers(); //some black magic to make the audio generation
  
  //init the midi
  midiA.begin(MIDI_CHANNEL_OMNI);
  midiA.turnThruOff();
  midiA.setHandleNoteOn(handleNoteOn);
  midiA.setHandleNoteOff(handleNoteOff);
  midiA.setHandleControlChange(handleControlChange);
  

}

void sineWave() {                                       //too costly to calculate on the fly, so it reads from the sine table. We use 128 values, then mirror them to get the whole cycle
  for (int i = 0; i < 128; ++i) {
    wavetable[i] = pgm_read_byte_near(sinetable + i);
  }
  wavetable[128] = 255;
  for (int i = 129; i < 256; ++i) {
    wavetable[i] = wavetable[256 - i] ;
  }
}
const uint8_t ledNumber[4]={CLP_U,THR_U,THR_D,CLP_D};
void loop() {

  midiA.read();

  bool newState = !digitalRead(BUTTON_PIN);
  if (!buttonState && newState) { //button just pressed
    soundMode++;
    soundMode = soundMode % NUMBER_OF_SOUND_MODES;
    EEPROM.write(SOUND_MODE_BYTE,soundMode);
    if(soundMode==3){
     // cli();
     
        bitWrite( TIMSK1,OCIE1A,0);
    }
    else  bitWrite( TIMSK1,OCIE1A,1);//sei();
    // midiA.sendControlChange(LEARNING_CC, 127, LEARNING_CHANNEL);
  }
  if (buttonState && !newState) {
    //  midiA.sendControlChange(LEARNING_CC, 0, LEARNING_CHANNEL);
  }
  buttonState = newState;

  switch (soundMode) {

    case FREQUENCY:
      frequency = (analogValue << 2) + 50;
      timbre = 0;
      amplitude = 255;
      break;

    case TIMBRE:
      timbre = constrain(analogValue >> 2, 1, 255);
      amplitude = 255;
      break;

    case AMPLITUDE:
      amplitude = analogValue >> 2;
      timbre = 128;

      break;

  }

  analogValue = analogRead(ANALOG_INPUT);



  if (_timeCounter - _time > 400) {
    if (1) { //!otherIsLearning) {
      if (analogValue >> 3 != lastAnalogValue) midiA.sendControlChange(MIDI_CC, analogValue >> 3, MIDI_CHANNEL);
      _time = _timeCounter;
      lastAnalogValue = analogValue >> 3;
    }
  }

  threshold = analogRead(THRESHOLD);

  bool newThresholdState = analogValue > threshold;

  if (newThresholdState && !thresholdState) {
    //if (!otherIsLearning)
    midiA.sendNoteOn(MIDI_NOTE, constrain(threshold >> 3, 1, 127), MIDI_CHANNEL);
  }
  if (!newThresholdState && thresholdState) {
    //if (!otherIsLearning)
    midiA.sendNoteOff(MIDI_NOTE, constrain(threshold >> 3, 1, 127), MIDI_CHANNEL);
  }
  thresholdState = newThresholdState;

  if (buttonState) {
    for(uint8_t i;i<4;i++){
      digitalWrite(ledNumber[i],LOW);
    }
    digitalWrite(ledNumber[soundMode],HIGH);
  }
  else {
    if (analogValue > 1000) digitalWrite(CLP_U, HIGH);
    else digitalWrite(CLP_U, LOW);

    if (analogValue < 23) digitalWrite(CLP_D, HIGH);
    else digitalWrite(CLP_D, LOW);

    if (thresholdState) {
      digitalWrite(THR_U, HIGH);
      digitalWrite(THR_D, LOW);
    }
    else {
      digitalWrite(THR_U, LOW);
      digitalWrite(THR_D, HIGH);
    }
  }
}

ISR(TIM1_COMPA_vect)  // render the oscillators in the interupt
{

  OCR0A = sample;
  _phase += frequency;
  _phase2 += frequency;
  _phase2 += frequency;
  uint8_t _phs = (_phase + (timbre * wavetable[_phase2 >> 8])) >> 8;
  sample = (amplitude * (wavetable[_phs] )) >> 8;

  _timeCounter++; //time reference for the main loop

}
