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
//
// If this class is copied for external tools using live data, please also include the warning!

/**
 * WARNING: USING CIPHERED DATA DURING THE RUN IS NOT ALLOWED, PLEASE CONTACT THE SPEEDRUN.COM/MC MODERATION TEAM IF YOU WANT TO USE CIPHERED DATA.
 * <br>
 * AVERTISSEMENT : L’UTILISATION DE DONNÉES CHIFFRÉES PENDANT L’EXÉCUTION N’EST PAS AUTORISÉE. VEUILLEZ CONTACTER L’ÉQUIPE DE MODÉRATION DE SPEEDRUN.COM/MC SI VOUS SOUHAITEZ UTILISER DES DONNÉES CHIFFRÉES.
 * <br>
 * ADVERTENCIA: NO SE PERMITE EL USO DE DATOS CIFRADOS DURANTE LA EJECUCIÓN. POR FAVOR, CONTACTE AL EQUIPO DE MODERACIÓN DE SPEEDRUN.COM/MC SI DESEA UTILIZAR DATOS CIFRADOS.
 * <br>
 * WARNUNG: DIE VERWENDUNG VON VERSCHLÜSSELTEN DATEN WÄHREND DES LAUFS IST NICHT ERLAUBT. BITTE KONTAKTIEREN SIE DAS MODERATIONSTEAM VON SPEEDRUN.COM/MC, WENN SIE VERSCHLÜSSELTE DATEN VERWENDEN MÖCHTEN.
 * <br>
 * ПОПЕРЕДЖЕННЯ: ВИКОРИСТАННЯ ЗАШИФРОВАНИХ ДАНИХ ПІД ЧАС СПІДРАНУ НЕ ДОЗВОЛЕНО. БУДЬ ЛАСКА, ЗВ’ЯЖІТЬСЯ З МОДЕРАЦІЙНОЮ КОМАНДОЮ SPEEDRUN.COM/MC, ЯКЩО ВИ ХОЧЕТЕ ВИКОРИСТОВУВАТИ ЗАШИФРОВАНІ ДАНІ.
 * <br>
 * AVVISO: L’USO DI DATI CIFRATI DURANTE L’ESECUZIONE NON È CONSENTITO. SI PREGA DI CONTATTARE IL TEAM DI MODERAZIONE DI SPEEDRUN.COM/MC SE SI DESIDERA UTILIZZARE DATI CIFRATI.
 * <br>
 * AVISO: NÃO É PERMITIDO O USO DE DADOS CIFRADOS DURANTE A EXECUÇÃO. POR FAVOR, ENTRE EM CONTATO COM A EQUIPE DE MODERAÇÃO DE SPEEDRUN.COM/MC SE QUISER UTILIZAR DADOS CIFRADOS.
 * <br>
 * ПРЕДУПРЕЖДЕНИЕ: ИСПОЛЬЗОВАНИЕ ЗАШИФРОВАННЫХ ДАННЫХ ВО ВРЕМЯ ЗАПУСКА ЗАПРЕЩЕНО. ЕСЛИ ВЫ ХОТИТЕ ИСПОЛЬЗОВАТЬ ЗАШИФРОВАННЫЕ ДАННЫЕ, ПОЖАЛУЙСТА, СВЯЖИТЕСЬ С МОДЕРАЦИОННОЙ КОМАНДОЙ SPEEDRUN.COM/MC.
 * <br>
 * 警告：运行过程中不允许使用加密数据，如需使用加密数据，请联系 SPEEDRUN.COM/MC 的管理团队。
 * <br>
 * 警告：実行中に暗号化されたデータを使用することは許可されていません。暗号化データを使用したい場合は、SPEEDRUN.COM/MC のモデレーションチームに連絡してください。
 * <br>
 * 경고: 실행 중 암호화된 데이터를 사용하는 것은 허용되지 않습니다. 암호화된 데이터를 사용하려면 SPEEDRUN.COM/MC의 모더레이션 팀에 문의하십시오.
 */

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

class HermesRotator {
    static constexpr uint8_t MIN_VAL     = 33;
    static constexpr uint8_t MAX_VAL     = 126;
    static constexpr int     N           = 94;
    static constexpr int64_t SHUFFLE_SEED = 7499203634667178692LL;

    uint8_t substitutionTable_[N];

    void buildTables() {
        uint8_t chars[N];
        for (int i = 0; i < N; i++)
            chars[i] = (uint8_t)(MIN_VAL + i);

        JavaRandom rng(SHUFFLE_SEED);
        std::vector<uint8_t> pool(chars, chars + N);
        for (int i = 0; i < N; i++) {
            int idx  = rng.nextInt((int)pool.size());
            chars[i] = pool[idx];
            pool.erase(pool.begin() + idx);
        }

        for (int i = 0; i < N; i++)
            substitutionTable_[i] = (uint8_t)(MIN_VAL + i);

        const int shift = N / 2;
        for (int i = 0; i < N; i++)
            substitutionTable_[chars[i] - MIN_VAL] = chars[(i + shift) % N];
    }

    static void halfReverse_(uint8_t* bytes, int len) {
        for (int i = 0; i < len / 2; i += 2) {
            uint8_t tmp        = bytes[i];
            bytes[i]           = bytes[len - 1 - i];
            bytes[len - 1 - i] = tmp;
        }
    }

    void applySubstitution_(uint8_t* bytes, int len) const {
        for (int i = 0; i < len; i++) {
            uint8_t c = bytes[i];
            if (c >= MIN_VAL && c <= MAX_VAL)
                bytes[i] = substitutionTable_[c - MIN_VAL];
        }
    }

public:
    HermesRotator() { buildTables(); }

    void processLine(uint8_t* bytes, int len) const {
        halfReverse_(bytes, len);
        applySubstitution_(bytes, len);
    }

    std::string processLine(const std::string& line) const {
        std::string out = line;
        if (!out.empty() && out.back() == '\r')
            out.pop_back();

        processLine(reinterpret_cast<uint8_t*>(out.data()), (int)out.size());
        return out;
    }
};