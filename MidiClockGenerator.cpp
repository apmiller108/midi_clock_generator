#include <iostream>
#include <cstdlib>
#include <vector>
#include <atomic>
#include <chrono>
#include <thread>
#include "deps/RtMidi.h"

// https://www.music.mcgill.ca/~gary/rtmidi/

// Linux ALSA

// g++ -Wall -D__LINUX_ALSA__ -o bin/mcg_alsa MidiClockGenerator.cpp deps/RtMidi.cpp -lasound -lpthread -std=c++20

// g++ -Wall -D__UNIX_JACK__ -o bin/mcg_jack MidiClockGenerator.cpp deps/RtMidi.cpp -ljack -lpthread -std=c++20

// MacOS
// g++ -Wall -D__MACOSX_CORE__ -o bin/mcg_macosx_core MidiClockGenerator.cpp RtMidi.cpp -framework CoreMIDI -framework CoreAudio -framework CoreFoundation -std=c++20 -pthread

using namespace std;

void sendMidiClock(RtMidiOut *o, int rate) {
  // TODO: make this more accurate
  while(true) {
    std::vector<unsigned char> message{0xF8};
    o->sendMessage(&message);
    std::this_thread::sleep_for(std::chrono::microseconds(rate));
  }
}

unsigned int selectBPM() {
  unsigned int bpm;
  cout << "Enter BPM" << endl;
  cin >> bpm;
  cout << "You entered: " << bpm << endl;
  return bpm;
}

unsigned int selectOutputPort(RtMidiOut *midiOut, unsigned int nPorts){
  unsigned int portNumber;
  string portName;
  for (unsigned int i = 0; i < nPorts; i++) {
    try {
      portName = midiOut->getPortName(i);
    } catch (RtMidiError &error) {
      error.printMessage();
      return 1;
    }
    cout << "(" << i << ") : " << portName << endl;
  }

  cin >> portNumber;
  cout << "\nSelect MIDI output." << endl;
  return portNumber;
}

int main()
{
  RtMidiOut *midiOut = new RtMidiOut();

  const float microseconds = 60'000'000.0;
  const unsigned int ppq = 24;

  unsigned int bpm;
  bpm = selectBPM();

  int clockRate = static_cast<int>(microseconds / (ppq * bpm));
  cout << "MIDI Clock rate: " << clockRate << endl;

  // Check available MIDI output ports.
  unsigned int nPorts = midiOut->getPortCount();
  if ( nPorts == 0 ) {
    cout << "No ports available!\n";
    return 1;
  } else {
    cout << "There are " << nPorts  << " MIDI output sources available:" << endl;
  }

  unsigned int portNumber;
  portNumber = selectOutputPort(midiOut, nPorts);

  midiOut->openPort(portNumber);

  thread t1(sendMidiClock, std::ref(midiOut), clockRate);
  t1.detach();
  cout << "Clock is running...\n";

  while (true);
  return 0;
}

