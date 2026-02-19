// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 19.02.2026.
//

// C++ port of Rotator.ROT_HERMES from the Hermes Minecraft mod
// (https://github.com/DuncanRuns/Hermes).
//
// Hermes encrypts its live play.log (in the world's hermes/restricted/ folder)
// using a modified ROT47 cipher: the printable-ASCII charset is first shuffled
// with a seeded Java LCG, then shifted by 47, and finally a partial byte-array
// reversal ("halfReverse") is applied on top of the substitution.
//
// This file provides DECRYPTION only, which is all Advancely needs.
// Usage:
//
//   HermesRotator rot;                        // build tables once
//   std::string json = rot.decryptLine(line); // decrypt one ciphered line
//

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ============================================================
//  JavaRandom
//
//  Replicates java.util.Random with a 64-bit seed.
//  Must match the JDK implementation exactly; the 48-bit LCG
//  and the rejection-sampling in nextInt() are both critical.
// ============================================================
class JavaRandom {
    int64_t seed_;

    static constexpr int64_t MULTIPLIER = 0x5DEECE66DLL;
    static constexpr int64_t ADDEND     = 0xBLL;
    static constexpr int64_t MASK       = (1LL << 48) - 1;

public:
    explicit JavaRandom(int64_t seed) {
        seed_ = (seed ^ MULTIPLIER) & MASK;
    }

    // Advance the LCG and return the top `bits` bits of the new state.
    int next(int bits) {
        seed_ = (seed_ * MULTIPLIER + ADDEND) & MASK;
        return (int)((uint64_t)seed_ >> (48 - bits));
    }

    // Matches java.util.Random.nextInt(int bound) exactly, including
    // the power-of-two fast-path and the rejection-sampling loop.
    int nextInt(int bound) {
        if (bound <= 0) return 0;

        if ((bound & -bound) == bound)          // power of two
            return (int)((bound * (int64_t)next(31)) >> 31);

        int bits, val;
        do {
            bits = next(31);
            val  = bits % bound;
        } while (bits - val + (bound - 1) < 0); // reject to avoid modulo bias
        return val;
    }
};


// ============================================================
//  HermesRotator
//
//  Replicates Rotator.ROT_HERMES from the Hermes mod.
//
//  Encryption (done by Hermes):
//    1. rotate()       – substitution via shuffled+shifted swap table
//    2. halfReverse()  – partial array reversal
//
//  Decryption (what Advancely needs):
//    1. halfReverse()      – self-inverse; undoes step 2 above
//    2. inverseRotate()    – inverse substitution; undoes step 1 above
//
//  Construct once and reuse; the tables are constant after init.
// ============================================================
class HermesRotator {
    // Printable ASCII range: '!' (33) .. '~' (126) – 94 characters
    static constexpr uint8_t MIN_VAL     = 33;
    static constexpr uint8_t MAX_VAL     = 126;
    static constexpr int     N           = 94;   // charset size
    static constexpr int64_t SHUFFLE_SEED = 7499203634667178692LL;

    uint8_t swapArray_[N];    // forward substitution  (encrypt, not used by Advancely)
    uint8_t invSwapArray_[N]; // inverse substitution  (decrypt)

    // --------------------------------------------------------
    // Build swapArray_ and invSwapArray_ once at construction.
    // --------------------------------------------------------
    void buildTables() {
        // 1. Start with printable ASCII in order
        uint8_t chars[N];
        for (int i = 0; i < N; i++)
            chars[i] = (uint8_t)(MIN_VAL + i);

        // 2. Shuffle using Java's seeded LCG
        //    (pool.remove(random.nextInt(pool.size())) in Java)
        JavaRandom rng(SHUFFLE_SEED);
        std::vector<uint8_t> pool(chars, chars + N);
        for (int i = 0; i < N; i++) {
            int idx  = rng.nextInt((int)pool.size());
            chars[i] = pool[idx];
            pool.erase(pool.begin() + idx);
        }

        // 3. Initialise swap table to identity
        for (int i = 0; i < N; i++)
            swapArray_[i] = (uint8_t)(MIN_VAL + i);

        // 4. Apply rotation by N/2 = 47 positions
        const int shift = N / 2; // 47
        for (int i = 0; i < N; i++)
            swapArray_[chars[i] - MIN_VAL] = chars[(i + shift) % N];

        // 5. Derive the inverse table from the forward table
        for (int i = 0; i < N; i++)
            invSwapArray_[swapArray_[i] - MIN_VAL] = (uint8_t)(MIN_VAL + i);
    }

    // --------------------------------------------------------
    //  halfReverse – matches Rotator.halfReverse() in Java.
    //
    //  For i = 0, 2, 4, ... (even indices only) in [0, len/2):
    //    swap bytes[i] <-> bytes[len - 1 - i]
    //
    //  Odd-indexed positions in the first half are NOT touched.
    //  Calling this twice restores the original array (self-inverse).
    // --------------------------------------------------------
    static void halfReverse_(uint8_t* bytes, int len) {
        for (int i = 0; i < len / 2; i += 2) {
            uint8_t tmp        = bytes[i];
            bytes[i]           = bytes[len - 1 - i];
            bytes[len - 1 - i] = tmp;
        }
    }

    // Forward substitution (kept for completeness / future use)
    void rotate_(uint8_t* bytes, int len) const {
        for (int i = 0; i < len; i++) {
            uint8_t c = bytes[i];
            if (c >= MIN_VAL && c <= MAX_VAL)
                bytes[i] = swapArray_[c - MIN_VAL];
        }
    }

    // Inverse substitution – undoes rotate_()
    void inverseRotate_(uint8_t* bytes, int len) const {
        for (int i = 0; i < len; i++) {
            uint8_t c = bytes[i];
            if (c >= MIN_VAL && c <= MAX_VAL)
                bytes[i] = invSwapArray_[c - MIN_VAL];
        }
    }

public:
    // Build the substitution tables on construction (cheap; done once).
    HermesRotator() { buildTables(); }

    // ----------------------------------------------------------
    //  decryptLine – decrypt one line from hermes/restricted/play.log
    //
    //  Hermes encrypted it with: rotate() then halfReverse()
    //  We reverse both operations in reverse order.
    //
    //  `bytes` is modified in-place.  `len` must be the byte count
    //  of the line WITHOUT any trailing newline characters.
    // ----------------------------------------------------------
    void decryptLine(uint8_t* bytes, int len) const {
        halfReverse_(bytes, len);   // undo halfReverse first
        inverseRotate_(bytes, len); // then undo the substitution
    }

    // Convenience overload for std::string lines.
    // Strips a trailing '\r' if present (Windows line endings).
    std::string decryptLine(const std::string& line) const {
        std::string out = line;
        // Strip trailing CR if the file was written with CRLF endings
        if (!out.empty() && out.back() == '\r')
            out.pop_back();

        decryptLine(reinterpret_cast<uint8_t*>(out.data()), (int)out.size());
        return out;
    }

    // ----------------------------------------------------------
    //  encryptLine – provided for completeness / testing only.
    //  Advancely never writes to the play.log.
    // ----------------------------------------------------------
    std::string encryptLine(const std::string& line) const {
        std::string out = line;
        uint8_t* b = reinterpret_cast<uint8_t*>(out.data());
        rotate_(b, (int)out.size());
        halfReverse_(b, (int)out.size());
        return out;
    }
};