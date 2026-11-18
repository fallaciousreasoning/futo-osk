/*
 * futo-swipe-decoder: decodes swipes with FUTO Swipe for futo-osk, over a
 * line protocol on stdin/stdout.
 *
 *   futo-swipe-decoder --encoder PATH --vocab PATH [--decoder PATH] [--threads N]
 *
 * Prints "ready" once the models are loaded (or "error MESSAGE" and exits).
 * Then, one request per line, one reply line each:
 *
 *   layout N LETTER X Y ...  ->  ok | error MESSAGE
 *       Key centres, normalised so the letter rows span 0..1 both ways.
 *   swipe N X Y T ...        ->  words WORD ...
 *       Points in the same frame, T in milliseconds from the first point.
 *       Up to 8 candidates, best first; none if the swipe made no word.
 *
 * Copyright (C) 2026 Jay Harris
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "swipe_decoder/engine.hpp"
#include "swipe_decoder/layout.hpp"
#include "swipe_decoder/trie.hpp"

using swipe_decoder::KeyboardLayout;
using swipe_decoder::SwipeEngine;

static bool parse_layout(std::istringstream &in, KeyboardLayout &layout) {
    int n = 0;
    if (!(in >> n) || n <= 0 || n > 64) return false;
    layout = KeyboardLayout();
    for (int i = 0; i < n; i++) {
        std::string letter;
        float x, y;
        if (!(in >> letter >> x >> y)) return false;
        layout.letters += letter;
        layout.key_cx.push_back(x);
        layout.key_cy.push_back(y);
    }
    layout.num_keys = n;
    layout.code_points = swipe_decoder::utf8_to_codepoints(layout.letters);
    if ((int)layout.code_points.size() != n) return false;
    layout.build_index();
    return true;
}

int main(int argc, char **argv) {
    SwipeEngine::Config config;
    config.top_k = 8;
    config.layout = swipe_decoder::default_en_qwerty();
    std::string vocab_path;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--encoder")) config.encoder_path = argv[i + 1];
        else if (!strcmp(argv[i], "--decoder")) config.decoder_path = argv[i + 1];
        else if (!strcmp(argv[i], "--vocab")) vocab_path = argv[i + 1];
        else if (!strcmp(argv[i], "--threads")) config.num_threads = atoi(argv[i + 1]);
    }
    if (config.encoder_path.empty() || vocab_path.empty()) {
        std::cout << "error usage: " << argv[0]
                  << " --encoder PATH --vocab PATH [--decoder PATH] [--threads N]" << std::endl;
        return 2;
    }

    auto trie = swipe_decoder::load_trie_simple(vocab_path);
    if (!trie) {
        std::cout << "error cannot load " << vocab_path << std::endl;
        return 1;
    }
    config.dictionaries = { trie->itrie() };

    SwipeEngine engine;
    if (!engine.init(config)) {
        std::cout << "error cannot load the models" << std::endl;
        return 1;
    }
    std::cout << "ready" << std::endl;

    std::string line;
    std::vector<float> xs, ys, ts;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string cmd;
        in >> cmd;
        if (cmd == "layout") {
            SwipeEngine::Mode mode;
            KeyboardLayout layout;
            if (!parse_layout(in, layout)) {
                std::cout << "error bad layout" << std::endl;
                continue;
            }
            mode.layout = layout;
            std::cout << (engine.setMode(mode) ? "ok" : "error layout rejected") << std::endl;
        } else if (cmd == "swipe") {
            int n = 0;
            in >> n;
            xs.clear(), ys.clear(), ts.clear();
            float x, y, t;
            for (int i = 0; i < n && (in >> x >> y >> t); i++) {
                xs.push_back(x);
                ys.push_back(y);
                ts.push_back(t);
            }
            std::string reply = "words";
            if (xs.size() >= 2)
                for (const auto &w : engine.recognize(xs.data(), ys.data(), ts.data(), xs.size()))
                    reply += " " + w.word;
            std::cout << reply << std::endl;
        } else if (!cmd.empty()) {
            std::cout << "error unknown command " << cmd << std::endl;
        }
    }
    return 0;
}
