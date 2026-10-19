#include "cli_common.h"

#include <taktful/api.h>
#include <taktful/notes.h>
#include <taktful/wav_io.h>

#include <cstdio>
#include <cstring>
#include <string>

using namespace taktful;
using namespace taktful::cli;

static void usage() {
    std::fprintf(stderr,
        "Usage:\n"
        "  notes_probe [options] <path-to-wav>\n"
        "\n"
        "Options:\n"
        "  --json                  Output JSON (default).\n"
        "  --text                  Output human-readable text.\n"
        "  --fft-size <N>          FFT size (256..16384, power of two). Default 4096.\n"
        "  --hop-size <N>          Hop size. Default 512.\n"
        "  --threshold <V>         Minimum presence threshold (0.0-1.0). Default 0.05.\n"
        "  --help                  Show this help.\n");
}

int main(int argc, char** argv) {
    bool as_json = true;
    Params params;
    const char* path = 0;
    float threshold = 0.05f;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
            usage();
            return 0;
        } else if (std::strcmp(a, "--json") == 0) {
            as_json = true;
        } else if (std::strcmp(a, "--text") == 0) {
            as_json = false;
        } else if (std::strcmp(a, "--fft-size") == 0 && i + 1 < argc) {
            params.fft_size = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(a, "--hop-size") == 0 && i + 1 < argc) {
            params.hop_size = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(a, "--threshold") == 0 && i + 1 < argc) {
            threshold = static_cast<float>(std::atof(argv[++i]));
        } else if (a[0] == '-') {
            std::fprintf(stderr, "unknown option: %s\n", a);
            usage();
            return 64;
        } else if (!path) {
            path = a;
        } else {
            std::fprintf(stderr, "unexpected argument: %s\n", a);
            return 64;
        }
    }
    if (!path) {
        usage();
        return 64;
    }

    WavData wav;
    const WavError err = load_wav(path, wav);
    if (err != WavError::OK) {
        std::fprintf(stderr, "load_wav: %s\n", wav_error_string(err));
        return wav_error_to_exit(err);
    }

    validate_params(params);
    NotesResult result = detect_notes(wav, params);

    if (as_json) {
        std::printf("{\n");
        std::printf("  \"file\": \"%s\",\n", json_escape(path).c_str());
        std::printf("  \"total_energy\": %.4f,\n", static_cast<double>(result.total_energy));
        std::printf("  \"dominant_note\": \"%s\",\n", 
                    result.dominant_pc >= 0 ? pitch_class_to_string(static_cast<PitchClass>(result.dominant_pc)) : "none");
        std::printf("  \"dominant_presence\": %.4f,\n", static_cast<double>(result.dominant_presence));
        std::printf("  \"notes\": [\n");
        bool first_note = true;
        for (const NoteInfo& note : result.notes) {
            if (note.presence < threshold) {
                continue;
            }
            if (!first_note) {
                std::printf(",\n");
            }
            first_note = false;
            std::printf("    {\"note\": \"%s\", \"presence\": %.4f, \"confidence\": %.4f}",
                        pitch_class_to_string(note.pitch_class),
                        static_cast<double>(note.presence),
                        static_cast<double>(note.confidence));
        }
        std::printf("\n  ]\n");
        std::printf("}\n");
    } else {
        std::printf("file: %s\n", path);
        std::printf("total_energy: %.4f\n", static_cast<double>(result.total_energy));
        std::printf("dominant_note: %s (presence: %.4f)\n",
                    result.dominant_pc >= 0 ? pitch_class_to_string(static_cast<PitchClass>(result.dominant_pc)) : "none",
                    static_cast<double>(result.dominant_presence));
        std::printf("\nNotes (presence >= %.2f):\n", static_cast<double>(threshold));
        for (const NoteInfo& note : result.notes) {
            if (note.presence < threshold) {
                continue;
            }
            std::printf("  %-6s presence: %6.2f%%  confidence: %.2f\n",
                        pitch_class_to_note_name(note.pitch_class),
                        static_cast<double>(note.presence) * 100.0,
                        static_cast<double>(note.confidence));
        }
    }
    return 0;
}
