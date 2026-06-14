/**
 * @file hal.cpp
 * @author Forairaaaaa
 * @brief
 * @version 0.1
 * @date 2023-06-28
 *
 * @copyright Copyright (c) 2023
 *
 */
#include "hal.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include <string.h>
#include <cmath>

namespace HAL
{
    const char* TAG = "hal";

    /* Blend two RGB888 colors (t in 0..1) */
    static uint32_t _lerp_color(uint32_t a, uint32_t b, float t)
    {
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
        int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
        int r = ar + (int)((br - ar) * t);
        int g = ag + (int)((bg - ag) * t);
        int bl = ab + (int)((bb - ab) * t);
        return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
    }

    void HAL::_display_init()
    {
        ESP_LOGI(TAG, "display init");
        display.init();

        /* Init tp right after lcd (sharing rst pin) */
        tp.init();
        i2c_scan(I2C_NUM_0);

        // display.setColorDepth(16);
        // display.setTextSize(3);
        // // display.setTextSize(1);
        // // display.setTextColor((uint32_t)0xF3E9D2);
        // // display.setFont(&fonts::efontCN_24);
        // display.setTextColor(TFT_WHITE);
        // display.drawCenterString("StampRing", display.width() / 2, display.height() / 2 - 32);
        // for (int i = 0; i < 128; i++)
        // {
        //     display.setBrightness(i);
        //     delay(4);
        // }
        // delay(200);

        /* Create canvas */
        canvas = new LGFX_Sprite(&display);
        canvas->createSprite(display.width(), display.height());
        // printf("free block: %d\n", heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

        /* Simple start up anim */
        display.setBrightness(0);
        delay(100);

        /* ---- Synthwave splash ---- */
        {
            const uint32_t SKY_TOP = 0x2A0B47;  /* deep violet      */
            const uint32_t SKY_BOT = 0xFF6EC7;  /* pink at horizon  */
            const uint32_t BG_BOT  = 0x05010F;  /* near-black navy  */
            const uint32_t SUN_TOP = 0xFFE66D;  /* sun yellow       */
            const uint32_t SUN_BOT = 0xFF2D95;  /* sun magenta      */
            const uint32_t GRID    = 0x6C2DC7;  /* grid purple      */
            const uint32_t GRID_HI = 0x00E5FF;  /* horizon glow     */
            const uint32_t NEON    = 0x00E5FF;  /* title cyan       */
            const uint32_t NEON_MG = 0xFF2D95;  /* title magenta    */

            const int W = display.width();      /* 240 */
            const int H = display.height();     /* 240 */
            const int horizon = 150;

            /* Sky gradient: violet -> pink down to the horizon */
            for (int y = 0; y < horizon; y += 2)
            {
                uint32_t col = _lerp_color(SKY_TOP, SKY_BOT, (float)y / horizon);
                canvas->fillRect(0, y, W, 2, col);
            }
            /* Ground: dark, fading to black */
            for (int y = horizon; y < H; y += 2)
            {
                uint32_t col = _lerp_color(0x1A0826, BG_BOT, (float)(y - horizon) / (H - horizon));
                canvas->fillRect(0, y, W, 2, col);
            }

            /* Retro sun (gradient disc with horizontal slits) */
            const int sunCx = W / 2, sunCy = horizon - 26, sunR = 46;
            for (int dy = -sunR; dy <= sunR; dy++)
            {
                float t = (dy + sunR) / (2.0f * sunR);
                uint32_t col = _lerp_color(SUN_TOP, SUN_BOT, t);
                int half = (int)sqrtf((float)(sunR * sunR - dy * dy));
                canvas->drawFastHLine(sunCx - half, sunCy + dy, half * 2, col);
            }
            /* Slits across the lower half, widening downward */
            for (int i = 0; i < 8; i++)
            {
                int sy = sunCy + 4 + i * 4;
                int sh = 1 + i / 2;
                canvas->fillRect(sunCx - sunR, sy, sunR * 2, sh, SKY_BOT);
            }

            /* Perspective grid below the horizon */
            for (int gx = -260; gx <= 260; gx += 26)
                canvas->drawLine(W / 2, horizon, W / 2 + gx, H + 4, GRID);
            int yy = horizon, step = 4;
            for (int i = 0; i < 10; i++)
            {
                yy += step; step += 4;
                if (yy > H) break;
                canvas->drawFastHLine(0, yy, W, GRID);
            }
            /* Glowing horizon line */
            canvas->drawFastHLine(0, horizon,     W, GRID_HI);
            canvas->drawFastHLine(0, horizon + 1, W, GRID);

            /* Neon title with chromatic-aberration glow */
            canvas->setFont(&fonts::efontCN_24);
            canvas->setTextSize(1.5);
            int tx = W / 2, ty = horizon + 28;
            canvas->setTextColor(NEON_MG);
            canvas->drawCenterString("M5-DIAL", tx + 2, ty);
            canvas->setTextColor(NEON);
            canvas->drawCenterString("M5-DIAL", tx - 2, ty);
            canvas->setTextColor(TFT_WHITE);
            canvas->drawCenterString("M5-DIAL", tx, ty);
            canvas->setTextSize(1);

            canvas->pushSprite(0, 0);
        }

        /* Restore saved brightness (persisted in NVS), default 127 */
        int saved_brightness = 127;
        {
            Preferences prefs;
            prefs.begin("settings", true);
            saved_brightness = prefs.getInt("bright", 127);
            prefs.end();
        }
        if (saved_brightness < 5)   saved_brightness = 5;   /* never fully dark on boot */
        if (saved_brightness > 255) saved_brightness = 255;

        /* Ramp up to the saved level */
        for (int i = 0; i <= saved_brightness; i++)
        {
            display.setBrightness(i);
            delay(4);
        }
        display.setBrightness(saved_brightness);
        delay(200);

        // display.setBrightness(128);
    }

    static int _last_count = 0;
    void HAL::_encoder_moved_callback(ESP32Encoder* encoder, void* userData)
    {
        int new_shit = static_cast<int>(encoder->getCount()) / 2;
        if (new_shit < _last_count)
        {
            // printf("???\n");
            ((HAL*)userData)->buzz.tone(7000, 20);
            _last_count = new_shit;
        }
        else
        {
            // printf("666\n");
            ((HAL*)userData)->buzz.tone(6000, 20);
            _last_count = new_shit;
        }
    }

    void HAL::_encoder_button_pressed_callback(Button* button, void* userData)
    {
        // printf("114514\n");
        ((HAL*)userData)->buzz.tone(2000, 20);
    }

    void HAL::_encoder_init()
    {
        /* Encoder */
        // encoder.init(41, 40, 42);
        // encoder.init(HAL_PIN_ENCODER_A, HAL_PIN_ENCODER_B, HAL_PIN_PWR_WAKE_UP);
        encoder.attachHalfQuad(HAL_PIN_ENCODER_A, HAL_PIN_ENCODER_B);

        /* Set moved callback */
        // encoder.setMovedCallback(_encoder_moved_callback, this);
        encoder.setMovedCallback(_encoder_moved_callback, this);

        /* Set button pressed callback */
        // encoder.btn.setPressedCallback(_encoder_button_pressed_callback, this);
        encoder.btn.setPin(HAL_PIN_PWR_WAKE_UP);
        encoder.btn.setPressedCallback(_encoder_button_pressed_callback, this);
    }

    void HAL::_buzz_init()
    {
        /* Buzz */
        // buzz.init(3);
        buzz.init(HAL_PIN_BUZZER);

        // buzz.tone(4000, 50);
        // delay(100);
        // buzz.tone(4000, 50);
    }

    void HAL::init()
    {
        ESP_LOGI(TAG, "init");

        /* Start power holding */
        powerOn();

        _encoder_init();

        /* Init i2c port 0 (for Tp) */
        // i2c_init(I2C_NUM_0, 11, 12, 100000, true);
        i2c_init(I2C_NUM_0, HAL_PIN_TP_I2C_SDA, HAL_PIN_TP_I2C_SCL, 100000, true);

        /* Display init */
        _display_init();

        _buzz_init();

// /* Init file system */
// wear_levelling_init();

/* Init lvgl */
#if LVGL_ENABLE
        lvgl.init(&display, &encoder, &tp);
#endif

        // _encoder_init();
    }

    void HAL::powerOn()
    {
        gpio_num_t pin_pwr_holding = (gpio_num_t)HAL_PIN_PWR_HOLDING;

        gpio_reset_pin(pin_pwr_holding);
        gpio_set_direction(pin_pwr_holding, GPIO_MODE_OUTPUT);
        gpio_set_pull_mode(pin_pwr_holding, GPIO_PULLUP_PULLDOWN);

        /* Keep power on */
        gpio_set_level(pin_pwr_holding, 1);
    }

    void HAL::powerOff()
    {
        gpio_num_t pin_pwr_holding = (gpio_num_t)HAL_PIN_PWR_HOLDING;

        gpio_reset_pin(pin_pwr_holding);
        gpio_set_direction(pin_pwr_holding, GPIO_MODE_OUTPUT);
        gpio_set_pull_mode(pin_pwr_holding, GPIO_PULLUP_PULLDOWN);

        /* Release power holding */
        gpio_set_level(pin_pwr_holding, 0);
    }

    bool i2c_init(i2c_port_t i2cPort, int sda, int scl, uint32_t clkSpeed, bool pullUpEnable)
    {
        ESP_LOGD(TAG, "init i2c");

        esp_err_t ret;

        /* I2C config */
        i2c_config_t conf;
        memset(&conf, 0, sizeof(i2c_config_t));
        conf.mode = I2C_MODE_MASTER;
        conf.sda_io_num = sda;
        conf.scl_io_num = scl;
        conf.master.clk_speed = clkSpeed;
        if (pullUpEnable)
        {
            conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
            conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
        }
        else
        {
            conf.sda_pullup_en = GPIO_PULLUP_DISABLE;
            conf.scl_pullup_en = GPIO_PULLUP_DISABLE;
        }
        conf.clk_flags = I2C_SCLK_SRC_FLAG_FOR_NOMAL;
        ret = i2c_param_config(i2cPort, &conf);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "I2C config failed");
            return false;
        }
        ESP_LOGI(TAG, "I2C config ok");

        /* Install driver */
        ret = i2c_driver_install(i2cPort, I2C_MODE_MASTER, 0, 0, 0);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "I2C driver install failed");
            return false;
        }
        ESP_LOGI(TAG, "I2C driver install ok");

        return true;
    }

    void encoder_test(HAL& hal)
    {
        // int shit = 0;
        // int old_shit = 0;
        // hal.encoder.resetPosition();

        // while (1)
        // {
        //     if (hal.encoder.wasMoved())
        //     {
        //         // printf("%d %d\n", hal.encoder.btn.read(), hal.encoder.getPosition());
        //         if (hal.encoder.getPosition() > old_shit)
        //         {
        //             shit++;
        //         }
        //         else
        //         {
        //             shit--;
        //         }
        //         printf("%d\n", shit);
        //         old_shit = hal.encoder.getPosition();
        //     }

        //     if (hal.encoder.btn.pressed())
        //     {
        //         // hal.encoder.resetPosition();
        //         // old_shit = 0;
        //         // printf("%d %d\n", hal.encoder.btn.read(), hal.encoder.getPosition());

        //         shit = 0;
        //         printf("%d\n", shit);

        //     }

        //     delay(1);
        // }
    }

    void tp_test(HAL& hal)
    {
        // // i2c_scan(I2C_NUM_0);
        // // while (1)
        // // {
        // //     i2c_scan(I2C_NUM_0);
        // //     delay(500);
        // // }

        // hell:
        // hal.display.fillScreen(TFT_WHITE);
        // hal.display.fillCircle(hal.display.width() / 2, hal.display.height() / 2, 2, TFT_WHITE);
        // hal.display.drawCircle(hal.display.width() / 2, hal.display.height() / 2, hal.display.width() / 2 - 1, TFT_GREEN);
        // hal.display.drawCircle(hal.display.width() / 2, hal.display.height() / 2, hal.display.width() / 4, TFT_GREEN);
        // hal.display.drawLine(0, hal.display.height() / 2, hal.display.width(), hal.display.height() / 2, TFT_GREEN);
        // hal.display.drawLine(hal.display.width() / 2, 0, hal.display.width() / 2, hal.display.height(), TFT_GREEN);

        // // uint8_t shit = 0;
        // while (1)
        // {
        //     // printf("%d %d %d\n", hal.tp.readPos().touch_num, hal.tp.readPos().x, hal.tp.readPos().y);
        //     // delay(5);

        //     if (hal.tp.isTouched())
        //     {
        //         hal.tp.update();
        //         printf("%d %d %d\n", hal.tp.getTouchPointBuffer().touch_num, hal.tp.getTouchPointBuffer().x,
        //         hal.tp.getTouchPointBuffer().y); hal.display.fillCircle(hal.tp.getTouchPointBuffer().x,
        //         hal.tp.getTouchPointBuffer().y, 2, TFT_BLUE);
        //     }

        //     if (hal.btn.pressed())
        //     {
        //         goto hell;
        //     }

        //     // shit++;
        //     // printf("%d\n", shit);
        //     delay(5);
        // }
    }

    void i2c_scan(i2c_port_t i2c_master_port)
    {

        uint8_t WRITE_BIT = I2C_MASTER_WRITE; /*!< I2C master write */
        // uint8_t READ_BIT = I2C_MASTER_READ;    /*!< I2C master read */
        uint8_t ACK_CHECK_EN = 0x1; /*!< I2C master will check ack from slave*/
        // uint8_t ACK_CHECK_DIS = 0x0;           /*!< I2C master will not check ack from slave */

        uint8_t address;
        printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
        for (int i = 0; i < 128; i += 16)
        {
            printf("%02x: ", i);
            for (int j = 0; j < 16; j++)
            {
                fflush(stdout);
                address = i + j;
                i2c_cmd_handle_t cmd = i2c_cmd_link_create();
                i2c_master_start(cmd);
                i2c_master_write_byte(cmd, (address << 1) | WRITE_BIT, ACK_CHECK_EN);
                i2c_master_stop(cmd);
                esp_err_t ret = i2c_master_cmd_begin(i2c_master_port, cmd, pdMS_TO_TICKS(50));
                i2c_cmd_link_delete(cmd);
                if (ret == ESP_OK)
                {
                    printf("%02x ", address);
                }
                else if (ret == ESP_ERR_TIMEOUT)
                {
                    printf("UU ");
                }
                else
                {
                    printf("-- ");
                }
            }
            printf("\r\n");
        }
        // i2c_driver_delete(i2c_master_port);
    }

    void rtc_test(HAL& hal)
    {
        // while (1)
        // {
        //     if (hal.encoder.btn.pressed())
        //         break;

        //     delay(50);
        // }

        // tm my_time;
        // while (1)
        // {
        //     hal.rtc.getTime(my_time);
        //     printf("%d:%d:%d\n", my_time.tm_hour, my_time.tm_min, my_time.tm_sec);

        //     delay(1000);
        // }
    }
} // namespace HAL
