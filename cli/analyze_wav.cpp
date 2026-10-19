#include "cli_common.h"

#include <taktful/api.h>
#include <taktful/dsp.h>
#include <taktful/notes.h>
#include <taktful/wav_io.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(__linux__) && defined(__GLIBC__)
#include <malloc.h>
#endif

using namespace taktful;
using namespace taktful::cli;

static void usage() {
    std::fprintf(stderr,
        "Usage:\n"
        "  analyze_wav [options] <path-to-wav>\n"
        "\n"
        "Options:\n"
        "  --json                  Output JSON (default).\n"
        "  --text                  Output human-readable text.\n"
        "  --fft-size <N>          FFT size (256..16384, power of two). Default 4096.\n"
        "  --hop-size <N>          Hop size. Default 512.\n"
        "  --no-dp                 Disable Ellis DP refinement.\n"
        "  --fast                  Fast mode (no DP, no whitening).\n"
        "  --profiles <name>       Key profiles: krumhansl | temperley1999 | temperley2007\n"
        "  --alternatives <N>      Number of alternative keys to print. Default 5.\n"
        "  --notes                 Include note detection in output.\n"
        "  --notes-threshold <V>   Minimum note presence threshold (0.0-1.0). Default 0.05.\n"
        "  --stats                 Print memory + CPU stats at the end.\n"
        "  --verbose               Print every intermediate stage to stderr.\n"
        "  --help                  Show this help.\n");
}

int main(int argc, char** argv) {
    bool as_json = true;
    bool stats = false;
    bool verbose = false;
    bool include_notes = false;
    float notes_threshold = 0.05f;
    Params params;
    const char* path = 0;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
            usage();
            return 0;
        } else if (std::strcmp(a, "--json") == 0) {
            as_json = true;
        } else if (std::strcmp(a, "--text") == 0) {
            as_json = false;
        } else if (std::strcmp(a, "--no-dp") == 0) {
            params.enable_dp_refinement = false;
        } else if (std::strcmp(a, "--fast") == 0) {
            params.fast_mode = true;
        } else if (std::strcmp(a, "--stats") == 0) {
            stats = true;
        } else if (std::strcmp(a, "--verbose") == 0) {
            verbose = true;
        } else if (std::strcmp(a, "--fft-size") == 0 && i + 1 < argc) {
            params.fft_size = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(a, "--hop-size") == 0 && i + 1 < argc) {
            params.hop_size = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(a, "--profiles") == 0 && i + 1 < argc) {
            if (!parse_profiles(argv[++i], params.key_profiles)) {
                std::fprintf(stderr, "unknown profiles name\n");
                return 64;
            }
        } else if (std::strcmp(a, "--alternatives") == 0 && i + 1 < argc) {
            const int n = std::atoi(argv[++i]);
            if (n < 0 || n > 24) {
                std::fprintf(stderr, "alternatives must be 0..24\n");
                return 64;
            }
            params.num_alternatives = static_cast<uint8_t>(n);
        } else if (std::strcmp(a, "--notes") == 0) {
            include_notes = true;
        } else if (std::strcmp(a, "--notes-threshold") == 0 && i + 1 < argc) {
            notes_threshold = static_cast<float>(std::atof(argv[++i]));
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
    if (verbose) {
        std::fprintf(stderr, "loaded %zu samples, %u Hz, %u ch, %u bit\n",
                     wav.samples.size(), wav.sample_rate, wav.channels, wav.bits_per_sample);
    }

    validate_params(params);
    const auto t0 = std::chrono::steady_clock::now();
    AnalysisResult r = analyze(wav, params);
    const auto t1 = std::chrono::steady_clock::now();
    const double cpu_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    // Detect notes if requested
    NotesResult notes_result;
    if (include_notes) {
        notes_result = detect_notes(wav, params);
    }

    const double duration = (wav.sample_rate > 0)
        ? static_cast<double>(wav.samples.size()) / static_cast<double>(wav.sample_rate)
        : 0.0;

    double peak_kb = 0.0;
#if defined(__linux__) && defined(__GLIBC__)
#if defined(__GLIBC_PREREQ) && __GLIBC_PREREQ(2, 33)
    {
        struct mallinfo2 mi = mallinfo2();
        peak_kb = static_cast<double>(mi.uordblks) / 1024.0;
    }
#else
    {
        struct mallinfo mi = mallinfo();
        peak_kb = static_cast<double>(mi.uordblks) / 1024.0;
    }
#endif
#endif

    if (r.tempo.bpm <= 0.0f && r.key.key == Key::Unknown) {
        if (as_json) {
            std::printf("{\"file\":\"%s\",\"error\":\"analysis failed\"}\n", json_escape(path).c_str());
        } else {
            std::printf("analysis failed (silent or too short)\n");
        }
        return 5;
    }

    if (as_json) {
        std::printf("{\n");
        std::printf("  \"file\": \"%s\",\n", json_escape(path).c_str());
        std::printf("  \"duration_sec\": %.3f,\n", duration);
        std::printf("  \"sample_rate\": %u,\n", params.target_sample_rate);
        std::printf("  \"tempo\": {\n");
        std::printf("    \"bpm\": %.1f,\n", static_cast<double>(r.tempo.bpm));
        std::printf("    \"confidence\": %.2f,\n", static_cast<double>(r.tempo.confidence));
        std::printf("    \"beat_times_sec\": [");
        for (size_t i = 0; i < r.tempo.beat_times_sec.size(); ++i) {
            if (i) std::printf(", ");
            std::printf("%.3f", static_cast<double>(r.tempo.beat_times_sec[i]));
        }
        std::printf("]\n");
        std::printf("  },\n");
        std::printf("  \"key\": {\n");
        std::printf("    \"key\": \"%s\",\n", to_string(r.key.key));
        std::printf("    \"camelot\": \"%s\",\n", to_camelot(r.key.key));
        std::printf("    \"confidence\": %.2f,\n", static_cast<double>(r.key.confidence));
        std::printf("    \"alternatives\": [\n");
        for (size_t i = 0; i < r.key.alternatives.size(); ++i) {
            std::printf("      {\"key\": \"%s\", \"camelot\": \"%s\", \"confidence\": %.2f}%s\n",
                        to_string(r.key.alternatives[i].key),
                        to_camelot(r.key.alternatives[i].key),
                        static_cast<double>(r.key.alternatives[i].confidence),
                        (i + 1 < r.key.alternatives.size()) ? "," : "");
        }
        std::printf("    ]\n");
        std::printf("  }");
        if (include_notes) {
            std::printf(",\n  \"notes\": {\n");
            std::printf("    \"total_energy\": %.4f,\n", static_cast<double>(notes_result.total_energy));
            std::printf("    \"dominant_note\": \"%s\",\n",
                        notes_result.dominant_pc >= 0 ? pitch_class_to_string(static_cast<PitchClass>(notes_result.dominant_pc)) : "none");
            std::printf("    \"dominant_presence\": %.4f,\n", static_cast<double>(notes_result.dominant_presence));
            std::printf("    \"notes\": [\n");
            bool first_note = true;
            for (const NoteInfo& note : notes_result.notes) {
                if (note.presence < notes_threshold) {
                    continue;
                }
                if (!first_note) {
                    std::printf(",\n");
                }
                first_note = false;
                std::printf("      {\"note\": \"%s\", \"presence\": %.4f, \"confidence\": %.4f}",
                            pitch_class_to_string(note.pitch_class),
                            static_cast<double>(note.presence),
                            static_cast<double>(note.confidence));
            }
            std::printf("\n    ]\n");
            std::printf("  }");
        }
        if (stats) {
            std::printf(",\n  \"stats\": {\n");
            std::printf("    \"peak_heap_kb\": %.0f,\n", peak_kb);
            std::printf("    \"cpu_ms_total\": %.0f\n", cpu_ms);
            std::printf("  }\n");
        } else {
            std::printf("\n");
        }
        std::printf("}\n");
    } else {
        std::printf("file: %s\n", path);
        std::printf("duration: %.3f s\n", duration);
        std::printf("tempo: %.1f BPM (confidence %.2f)\n",
                    static_cast<double>(r.tempo.bpm),
                    static_cast<double>(r.tempo.confidence));
        std::printf("key: %s (%s, confidence %.2f)\n",
                    to_string(r.key.key), to_camelot(r.key.key),
                    static_cast<double>(r.key.confidence));
        if (!r.key.alternatives.empty()) {
            std::printf("alternatives:\n");
            for (size_t i = 0; i < r.key.alternatives.size(); ++i) {
                std::printf("  %zu. %-12s %.2f\n",
                            i + 1,
                            to_string(r.key.alternatives[i].key),
                            static_cast<double>(r.key.alternatives[i].confidence));
            }
        }
        if (include_notes) {
            std::printf("\nNotes (presence >= %.2f):\n", static_cast<double>(notes_threshold));
            for (const NoteInfo& note : notes_result.notes) {
                if (note.presence < notes_threshold) {
                    continue;
                }
                std::printf("  %-6s presence: %6.2f%%  confidence: %.2f\n",
                            pitch_class_to_note_name(note.pitch_class),
                            static_cast<double>(note.presence) * 100.0,
                            static_cast<double>(note.confidence));
            }
        }
        if (stats) {
            std::printf("stats: peak_heap_kb=%.0f cpu_ms=%.0f\n", peak_kb, cpu_ms);
        }
    }
    return 0;
}
