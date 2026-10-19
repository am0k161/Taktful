#include "tests/unit/wav_writer.h"
#include <vector>

int main() {
    using namespace taktful::test;
    
    // Create a simple chord: C major (C4, E4, G4)
    const float sr = 44100.0f;
    const float seconds = 2.0f;
    
    // C4 = 261.63 Hz, E4 = 329.63 Hz, G4 = 392.00 Hz
    std::vector<float> chord = triad(261.63f, 329.63f, 392.00f, sr, seconds, 0.3f);
    
    // Write the WAV file
    if (write_pcm16_wav("test_chord.wav", chord, static_cast<uint32_t>(sr))) {
        printf("Created test_chord.wav with C major chord\n");
        return 0;
    } else {
        printf("Failed to create test WAV file\n");
        return 1;
    }
}
