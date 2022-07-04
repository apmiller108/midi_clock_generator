#include <iostream>
#include <cstdlib>
#include <vector>
#include <atomic>
#include <chrono>
#include <thread>
#include <math.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include "deps/RtMidi.h"

// https://www.music.mcgill.ca/~gary/rtmidi/

// Linux ALSA

// g++ -Wall -D__LINUX_ALSA__ -o bin/mcg_alsa MidiClockGenerator.cpp deps/RtMidi.cpp -lasound -lpthread -std=c++20

// g++ -Wall -D__UNIX_JACK__ -o bin/mcg_jack MidiClockGenerator.cpp deps/RtMidi.cpp -ljack -lpthread -std=c++20

// MacOS
// g++ -Wall -D__MACOSX_CORE__ -o bin/mcg_macosx_core MidiClockGenerator.cpp RtMidi.cpp -framework CoreMIDI -framework CoreAudio -framework CoreFoundation -std=c++20 -pthread

using namespace std;
using namespace std::chrono;

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

  cout << "\nSelect MIDI output from the above options (enter the number)" << endl;
  cin >> portNumber;
  cout << "You entered: " << portNumber << endl;

  return portNumber;
}

// This attempts to combine sleeping and spin-locking the CPU to generate an
// accurate clock while using less CPU than full on spin locking.
// Adapted from Blat Blatnik's blog post:
// https://blat-blatnik.github.io/computerBear/making-accurate-sleep-function/
void sendMidiClock(jack_port_t *port, double clockRateSecs) {
  // const vector<unsigned char> midiClockMessage{0xF8};
  jack_midi_data_t midiClockMessage = 0xF8;

  cout << jack_port_short_name(port) << endl;

  void *buffer = jack_port_get_buffer(port, 512);

  double estimate = 5e-3;
  double mean = 5e-3;
  double m2 = 0;
  int64_t count = 1;

  // midiOut->sendMessage(&midiClockMessage, sizeof(midiClockMessage));
  auto prevTime = high_resolution_clock::now();
  while (true) {
    auto nextTime = high_resolution_clock::now();
    double deltaTimeSecs = (nextTime - prevTime).count() / 1e9;
    // printf("Message Time: %.2lf microseconds\n", deltaTimeSecs * 1e6);

    auto messageTime = high_resolution_clock::now();
    double waitSeconds = clockRateSecs - ((messageTime - nextTime).count() / 1e9);

    jack_midi_clear_buffer(buffer);

    // Sleep (0 CPU)
    while(waitSeconds > estimate) {
      auto startSleepTime = high_resolution_clock::now();
      this_thread::sleep_for(milliseconds(1));
      auto endSleepTime = high_resolution_clock::now();

      double sleptForSeconds = (endSleepTime - startSleepTime).count() / 1e9;
      waitSeconds -= sleptForSeconds;
      // printf("Remaining wait: %.2lf mcs\n", waitSeconds * 1e6);

      ++count;
      double delta = sleptForSeconds - mean;
      mean += delta / count;
      m2 += delta * (sleptForSeconds - mean);
      double stddev = sqrt(m2 / (count - 1));
      estimate = mean + stddev;
      // printf("Estimate: %.2lf mcs\n", estimate * 1e6);
    }

    // Spin lock (CPU thrash)
    auto spinStart = high_resolution_clock::now();
    while ((high_resolution_clock::now() - spinStart).count() / 1e9 < waitSeconds);
    // midiOut->sendMessage(&midiClockMessage, sizeof(midiClockMessage));
    // int result;
    // result = jack_midi_event_write(buffer, count, &midiClockMessage, sizeof(midiClockMessage));
    uint8_t *pbuffer;
    pbuffer = jack_midi_event_reserve(buffer, 0, 1);
    pbuffer[0] = midiClockMessage;
    prevTime = nextTime;
  }
}

jack_client_t *jackClient;
jack_status_t status;
jack_port_t *port;
const unsigned char midiClockMessage[1] = {0xF8};

int process(jack_nframes_t nframes, void* arg) {
  // TODO: check out using
  //   JACK's Time functions
  //   https://jackaudio.org/api/group__TimeFunctions.html#gad52dc447fd9027922d466f695e697660

  // auto static prevTime = high_resolution_clock::now();
  // auto static nextTime = high_resolution_clock::now();
  // double *rate = (double *)arg;

  // void* buffer = jack_port_get_buffer(port, nframes);
  // jack_midi_clear_buffer(buffer);

  // // cout << std::chrono::system_clock::to_time_t(prevTime) << endl;
  // // cout << std::chrono::system_clock::to_time_t(nextTime) << endl;
  // printf("Rate: %.2lf microseconds\n", *rate * 1e6);
  // cout << nframes << endl;

  // nextTime = high_resolution_clock::now();
  // if ((nextTime - prevTime).count() / 1e9 > *rate) {
  //   double delta = (nextTime - prevTime).count() / 1e9;
  //   printf("Time since last message: %.2lf microseconds\n", delta * 1e6);
  //   jack_midi_event_write(buffer, nframes, midiClockMessage,sizeof(midiClockMessage));
  //   prevTime = high_resolution_clock::now();
  // }
  return 0;
}

int main()
{
  RtMidiOut *midiOut = new RtMidiOut();

  const float seconds = 60.0;
  const unsigned int ppq = 24;

  unsigned int bpm;
  bpm = selectBPM();

  double clockRate = (seconds / (ppq * bpm));
  cout << "MIDI Clock rate: " << (clockRate * 1e6) << " microseconds" << endl;

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
  string portName = midiOut->getPortName(portNumber);

  jackClient = jack_client_open("MidiClock", JackNoStartServer, &status);
  // port = jack_port_register(jackClient, "Out", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
  // jack_set_process_callback(jackClient, process, &clockRate);
  // jack_activate(jackClient);

  // jack_port_t *port;
  port = jack_port_by_name(jackClient, portName.c_str());

  jack_set_process_callback(jackClient, process, &clockRate);
  jack_activate(jackClient);

  // cout << jack_port_short_name(port) << endl;

  // void *buffer = jack_port_get_buffer(port, 512);

  // midiOut->openPort(portNumber);

  // thread t1(sendMidiClock, std::ref(port), clockRate);
  // t1.detach();
  cout << "Clock is running...\n";

  while (true);
  return 0;
}

