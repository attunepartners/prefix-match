#include "pattern_trie.hpp"
#include "server.hpp"
#include <iostream>
#include <fstream>
#include <chrono>
#include <cstring>
#include <omp.h>
#include <signal.h>

using namespace prefix_match;

// Global server pointer for signal handling
static Server* g_server = nullptr;

void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        std::cerr << "\nShutting down server...\n";
        if (g_server) {
            g_server->stop();
        }
    }
}

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options]\n"
              << "\nBatch mode options:\n"
              << "  -p <file>    Pattern file (required)\n"
              << "  -s <file>    String file to match\n"
              << "  -w <file>    Stopwords file\n"
              << "  -t <num>     Number of threads (default: all cores)\n"
              << "  -m           Extract matching substring\n"
              << "  -L           Enable LCSS matching\n"
              << "  -W           Remove stopwords from patterns\n"
              << "  -v           Verify matches\n"
              << "  -l           Log pattern file processing\n"
              << "  -q           Quiet mode (minimal output)\n"
              << "\nServer mode options:\n"
              << "  -P <port>    Start TCP server on port\n"
              << "  -S <path>    Start Unix socket server on path\n"
              << "\nGeneral:\n"
              << "  -h           Show this help\n"
              << "\nExamples:\n"
              << "  Batch mode:  " << prog << " -p patterns.txt -s strings.txt -m\n"
              << "  TCP server:  " << prog << " -p patterns.txt -P 8080 -t 8 -m\n"
              << "  Unix socket: " << prog << " -p patterns.txt -S /tmp/pm.sock -t 8 -m\n";
}

int main(int argc, char* argv[]) {
    std::string pattern_file;
    std::string string_file;
    std::string stopword_file;
    std::string unix_socket_path;
    int tcp_port = 0;
    CallerArgs args;
    bool quiet = false;
    bool logperf = false;
    int num_threads = 0;  // 0 = use all available

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            pattern_file = argv[++i];
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            string_file = argv[++i];
        } else if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
            stopword_file = argv[++i];
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            num_threads = std::stoi(argv[++i]);
        } else if (strcmp(argv[i], "-P") == 0 && i + 1 < argc) {
            tcp_port = std::stoi(argv[++i]);
        } else if (strcmp(argv[i], "-S") == 0 && i + 1 < argc) {
            unix_socket_path = argv[++i];
        } else if (strcmp(argv[i], "-m") == 0) {
            args.matching = true;
        } else if (strcmp(argv[i], "-L") == 0) {
            args.lcssmatch = true;
        } else if (strcmp(argv[i], "-W") == 0) {
            args.removestopwords = true;
        } else if (strcmp(argv[i], "-v") == 0) {
            args.verify = true;
        } else if (strcmp(argv[i], "-l") == 0) {
            logperf = true;
        } else if (strcmp(argv[i], "-q") == 0) {
            quiet = true;
        } else if (strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (pattern_file.empty()) {
        std::cerr << "Error: Pattern file required (-p)\n";
        print_usage(argv[0]);
        return 1;
    }

    bool server_mode = (tcp_port > 0 || !unix_socket_path.empty());

    if (server_mode && tcp_port > 0 && !unix_socket_path.empty()) {
        std::cerr << "Error: Cannot specify both TCP port (-P) and Unix socket (-S)\n";
        return 1;
    }

    // Set thread count
    if (num_threads > 0) {
        omp_set_num_threads(num_threads);
    }
    int actual_threads = omp_get_max_threads();

    // Create loggers
    StdoutLogger stdout_logger;
    NullLogger null_logger;

    // Use stdout logger for pattern processing only if -l is specified
    Logger& pattern_logger = logperf ? static_cast<Logger&>(stdout_logger) : static_cast<Logger&>(null_logger);

    // Create pattern trie
    PatternTrie trie;

    // Load stopwords if specified
    if (!stopword_file.empty()) {
        trie.read_stopwords(stopword_file, pattern_logger);
    }

    // Load patterns
    auto load_start = std::chrono::high_resolution_clock::now();
    if (!trie.process_pattern_file(pattern_file, args, pattern_logger)) {
        return 1;
    }
    auto load_end = std::chrono::high_resolution_clock::now();
    auto load_duration = std::chrono::duration_cast<std::chrono::milliseconds>(load_end - load_start);

    if (!quiet) {
        std::cerr << "Loaded " << trie.get_pattern_count() << " patterns in "
                  << load_duration.count() << "ms\n";
        std::cerr << "Trie blocks: " << trie.get_block_count() << "\n";
        std::cerr << "Memory usage: " << (trie.memory_usage() / 1024) << " KB\n";
        std::cerr << "Using " << actual_threads << " threads\n";
    }

    // Server mode
    if (server_mode) {
        // For server mode, always enable matching substring extraction
        args.matching = true;

        Server server(trie, args, actual_threads);
        g_server = &server;

        // Set up signal handlers
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);

        bool success;
        if (tcp_port > 0) {
            success = server.start_tcp(tcp_port);
        } else {
            success = server.start_unix(unix_socket_path);
        }

        g_server = nullptr;
        return success ? 0 : 1;
    }

    // Batch mode - process string file if specified
    if (!string_file.empty()) {
        auto reader = open_file(string_file);
        if (!reader || !reader->is_open()) {
            std::cerr << "Error: Cannot open string file: " << string_file << "\n";
            return 1;
        }

        // Load all lines into memory for parallel processing
        auto read_start = std::chrono::high_resolution_clock::now();
        std::vector<std::string> lines;
        lines.reserve(1000000);  // Pre-allocate for performance
        std::string line;
        while (reader->getline(line)) {
            lines.push_back(std::move(line));
            line.clear();
        }
        reader->close();
        auto read_end = std::chrono::high_resolution_clock::now();

        if (!quiet) {
            auto read_duration = std::chrono::duration_cast<std::chrono::milliseconds>(read_end - read_start);
            std::cerr << "Read " << lines.size() << " lines in " << read_duration.count() << "ms\n";
        }

        // Prepare results storage - one vector per input line
        std::vector<std::vector<MatchResult>> all_results(lines.size());

        // Parallel matching
        auto match_start = std::chrono::high_resolution_clock::now();

        #pragma omp parallel
        {
            // Each thread gets its own MatchContext
            MatchContext ctx;
            ctx.ensure_capacity(trie.get_pattern_count());

            #pragma omp for schedule(dynamic, 1000)
            for (size_t i = 0; i < lines.size(); ++i) {
                all_results[i] = trie.match_pattern_to_string(lines[i], args, ctx);
            }
        }

        auto match_end = std::chrono::high_resolution_clock::now();
        auto match_duration = std::chrono::duration_cast<std::chrono::milliseconds>(match_end - match_start);

        // Output results (sequential to maintain order)
        size_t total_matches = 0;
        for (size_t i = 0; i < lines.size(); ++i) {
            for (const auto& match : all_results[i]) {
                ++total_matches;
                std::cout << "=\t" << match.pattern_xref << "\t"
                          << match.pattern_text << "\t";
                if (args.matching) {
                    std::cout << match.matching_string;
                } else {
                    std::cout << (i + 1);  // 1-indexed line number
                }
                std::cout << "\t" << lines[i] << "\n";
            }
        }

        if (!quiet) {
            std::cerr << "\nProcessed " << lines.size() << " strings in "
                      << match_duration.count() << "ms\n";
            std::cerr << "Total matches: " << total_matches << "\n";
            if (lines.size() > 0 && match_duration.count() > 0) {
                double strings_per_sec = (lines.size() * 1000.0) / match_duration.count();
                std::cerr << "Throughput: " << static_cast<int>(strings_per_sec) << " strings/sec\n";
            }
        }
    } else if (!server_mode) {
        std::cerr << "No string file (-s) or server mode (-P/-S) specified.\n";
        std::cerr << "Pattern file loaded successfully. Use -h for help.\n";
    }

    return 0;
}
