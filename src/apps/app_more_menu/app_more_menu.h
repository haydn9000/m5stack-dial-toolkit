/**
 * @file app_more_menu.h
 * @author Forairaaaaa
 * @brief 
 * @version 0.1
 * @date 2023-08-07
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#pragma once
#include "../app.h"
#include "../../hal/hal.h"
#include "../utilities/smooth_menu/src/simple_menu/simple_menu.h"
#include "more_menu_render_callback.hpp"

#include "../app_lcd_test/app_lcd_test.h"
#include "../app_rtc_test/app_rtc_test.h"
#include "../app_rfid_test/app_rfid_test.h"
#include "../app_wifi_scan/app_wifi_scan.h"
#include "../app_ble_server/app_ble_server.h"
#include "../app_temp_demo/app_temp_demo.h"
#include "../launcher/launcher_icons/launcher_icons.h"


namespace MOONCAKE
{
    namespace USER_APP
    {
        namespace MORE_MENU
        {
            struct Data_t
            {
                HAL::HAL* hal = nullptr;
                SMOOTH_MENU::Simple_Menu* menu;
                MoreMenuRender_CB_t* menu_render_cb;
            };
        }

        class MoreMenu : public APP_BASE
        {
            private:
                const char* _tag = "MoreMenu";
                LGFX_Sprite _app_icon;
                void _create_menu();
                void _menu_loop();

                /* Launch a sub-app and run its lifecycle until it quits */
                void _run_app(MOONCAKE::APP_BASE* app, uint32_t themeColor, const uint16_t* iconPic);


                /* Selected callback */
                void _item_selected_callback(uint8_t selectedNum);
                void _callback_lvgl(const std::string& selectedTag);


            public:
                MORE_MENU::Data_t _data;


                /**
                 * @brief Lifecycle callbacks for derived to override
                 * 
                 */
                /* Setup App configs, called when App "install()" */
                void onSetup();

                /* Life cycle */
                void onCreate();
                void onRunning();
                void onDestroy();
        };
    }
}

