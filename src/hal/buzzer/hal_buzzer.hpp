/**
 * @file hal_buzzer.hpp
 * @author Forairaaaaa
 * @brief 
 * @version 0.1
 * @date 2023-05-21
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#pragma once
#include <stdint.h>
#include <Arduino.h>


namespace BUZZER {

    class BUZZER {
        private:
            uint8_t _pin;

        public:
            BUZZER() : _pin(15) {}

            inline void init(int8_t pin) { _pin = pin; }

            /* Wrap */
            inline void tone(unsigned int frequency, unsigned long duration = 0) { ::tone(_pin, frequency, duration); }
            inline void noTone() { ::noTone(_pin); }


            /* ---- Cyberpunk SFX: quick pitch sweeps & arps, not flat beeps ---- */

            /* Pitch glissando f0 -> f1 over ~ms — the core "zap" texture. */
            inline void sweep(unsigned int f0, unsigned int f1, unsigned int ms)
            {
                const int steps = 12;
                unsigned int dt = ms / steps; if (dt == 0) dt = 1;
                for (int i = 0; i <= steps; i++)
                {
                    long f = (long)f0 + (long)((int)f1 - (int)f0) * i / steps;
                    ::tone(_pin, (unsigned int)f);
                    delay(dt);
                }
                ::noTone(_pin);
            }

            /* Generic short button "pip" — used by the global press callback, so
             * it must stay short to not clash with an app's own action sound. */
            inline void fxPress()
            {
                ::tone(_pin, 3200); delay(7);
                ::tone(_pin, 6200); delay(9);
                ::noTone(_pin);
            }

            /* Encoder / field click — a tiny two-step blip. Same sound both
             * directions (the reverse rising-pitch variant read as too shrill). */
            inline void fxTick(bool /*up*/)
            {
                ::tone(_pin, 5400); delay(6);
                ::tone(_pin, 7000); delay(8);
                ::noTone(_pin);
            }

            /* Confirm / start / resume — bright rising arpeggio. */
            inline void fxConfirm()
            {
                ::tone(_pin, 4400); delay(26);
                ::tone(_pin, 6600); delay(26);
                ::tone(_pin, 9200); delay(40);
                ::noTone(_pin);
            }

            /* Pause / back — short falling pair. */
            inline void fxCancel()
            {
                ::tone(_pin, 7600); delay(24);
                ::tone(_pin, 4600); delay(40);
                ::noTone(_pin);
            }

            /* Reset — quick downward zap. */
            inline void fxReset() { sweep(7000, 2600, 90); }

            /* Scan / read — quick upward chirp. */
            inline void fxScan() { sweep(3800, 7600, 70); }

            /* One alarm burst — aggressive up/down warble (loop it for a siren). */
            inline void fxAlarm()
            {
                sweep(3200, 5200, 70);
                sweep(5200, 3200, 70);
            }

            /* Session / phase complete — rising sweep into a two-note stab. */
            inline void fxComplete()
            {
                sweep(3000, 6200, 120);
                ::tone(_pin, 7400); delay(90);
                ::tone(_pin, 9600); delay(140);
                ::noTone(_pin);
            }

    };

}
