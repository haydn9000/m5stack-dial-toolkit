/**
 * @file more_menu_selected_callback.cpp
 * @author Forairaaaaa
 * @brief 
 * @version 0.1
 * @date 2023-08-10
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#include "app_more_menu.h"
#include "../common_define.h"


// USE_LVGL_DEMO requires LVGL component which is not included in this build
// #define USE_LVGL_DEMO


#ifdef USE_LVGL_DEMO
#include <hal/lvgl/hal_lvgl.hpp>
#include <hal/hal.h>
#include <lv_demos.h>
#endif


using namespace MOONCAKE::USER_APP;



void MoreMenu::_run_app(MOONCAKE::APP_BASE* app, uint32_t themeColor, const uint16_t* iconPic)
{
    if (app == nullptr)
        return;

    /* Init gui module if the app has one */
    if (app->getGui() != nullptr)
    {
        if (iconPic != nullptr)
            _app_icon.pushImage(0, 0, 42, 42, iconPic);
        app->getGui()->setThemeColor(themeColor);
        app->getGui()->init(_data.hal->canvas, &_app_icon);
    }

    /* Run lifecycle until the app asks to quit */
    app->setUserData((void*)_data.hal);
    app->onSetup();
    app->onCreate();
    while (1)
    {
        app->onRunning();
        if (app->isGoingDestroy())
        {
            app->resetGoingDestroyFlag();
            app->onDestroy();
            break;
        }
    }

    delete app;
}


void MoreMenu::_item_selected_callback(uint8_t selectedNum)
{
    // _log("%d", selectedNum);
    // _log("selected: %s\n", _data.menu->getMenu()->getItemList()[selectedNum]->tag.c_str());

    std::string selected_item_tag = _data.menu->getMenu()->getItemList()[selectedNum]->tag.c_str();
    _log("selected: %s", selected_item_tag.c_str());


    if (selected_item_tag == "Quit")
    {
        destroyApp();
        return;
    }


    /* Sub-apps moved here from the main launcher */
    if (selected_item_tag == "LCD Test")
    {
        _run_app(new MOONCAKE::USER_APP::LCD_Test, 0xFD5C4C, image_data_icon_lcd);
        return;
    }
    if (selected_item_tag == "RTC Clock")
    {
        _run_app(new MOONCAKE::USER_APP::RTC_Test, 0x577EFF, image_data_icon_rtc);
        return;
    }
    if (selected_item_tag == "Set Time")
    {
        _run_app(new MOONCAKE::USER_APP::SetTime, 0x33B5C4, image_data_icon_rtc);
        return;
    }
    if (selected_item_tag == "RFID Scan")
    {
        _run_app(new MOONCAKE::USER_APP::RFID_Test, 0x03A964, image_data_icon_rfid);
        return;
    }
    if (selected_item_tag == "WiFi Scan")
    {
        _run_app(new MOONCAKE::USER_APP::WiFi_Scan, 0xEB8429, image_data_icon_wifi);
        return;
    }
    if (selected_item_tag == "BLE Server")
    {
        _run_app(new MOONCAKE::USER_APP::BLE_Server, 0x04A279, image_data_icon_ble);
        return;
    }
    if (selected_item_tag == "Temp Demo")
    {
        _run_app(new MOONCAKE::USER_APP::VideoShit, 0x008CD6, image_data_icon_temp);
        return;
    }


    if (selected_item_tag == "Power Off")
    {
        _data.hal->canvas->fillScreen(TFT_BLACK);
        _canvas_update();


        _data.hal->rtc.clearIRQ();
        _data.hal->rtc.disableIRQ();

        delay(500);

        _data.hal->powerOff();

        
        delay(4000);
        _data.hal->powerOn();
    }



    /* Lvgl demos */
    if (selected_item_tag.find("LVGL") != std::string::npos)
    {
        _callback_lvgl(selected_item_tag);
    }

}



void MoreMenu::_callback_lvgl(const std::string& selectedTag)
{
    #ifdef USE_LVGL_DEMO

    /* Deinit canvas */
    // _log_mem();

    // _log_block();
    // _data.hal->canvas->deleteSprite();
    // _log_block();

    
    /* Init lvgl */
    _log_mem();
    LVGL_PORT lvgl;
    lvgl.init(&_data.hal->display, &_data.hal->encoder, &_data.hal->tp, _data.hal->canvas->getBuffer());
    _log_mem();



    /* Start demo */
    if (selectedTag == "LVGL Widgets")
    {
        lv_demo_widgets();
    }
    else if (selectedTag == "LVGL Stress")
    {
        lv_demo_stress();
    }
    else if (selectedTag == "LVGL Benchmark")
    {
        lv_demo_benchmark();
    }



    uint32_t button_time_count = millis();

    _data.hal->encoder.btn.setPin(42);
    _data.hal->encoder.btn.begin();

    /* Lvgl loop */
    while (1)
    {
        lv_timer_handler();
        delay(5);

        /* Quit when button pressed */
        if ((millis() - button_time_count) > 100)
        {
            // _log("%d", _data.hal->encoder.btn.read());
            if (!_data.hal->encoder.btn.read())
            {
                while (!_data.hal->encoder.btn.read())
                {
                    delay(10);
                }
                break;
            }
            button_time_count = millis();
        }
    }

    
    // /* Deinit */
    // _log_mem();
    // // lvgl.deInit();
    // // lv_mem_deinit();
    // lv_obj_del(lv_scr_act());
    // lv_mem_buf_free_all();
    // _log_mem();


    /* Jsut restart suan le */
    _data.hal->canvas->fillScreen(TFT_BLACK);
    _data.hal->canvas->setTextColor((uint32_t)0xF3E9D2);
    _data.hal->canvas->drawCenterString("QUITING LVGL", 120, 120 - 12);
    _data.hal->canvas->pushSprite(0, 0);
    delay(800);
    _data.hal->canvas->fillScreen(TFT_BLACK);
    _data.hal->canvas->pushSprite(0, 0);
    esp_restart();


    #endif
}
