#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "esp_freertos_hooks.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_vfs_fat.h"
#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "driver/uart.h"

#include "core2forAWS.h"
#include "lvgl/lvgl.h"
#include "ft6336u.h"
#include "speaker.h"
#include "bm8563.h"
#include "mpu6886.h"
#include "axp192.h"
#include "cryptoauthlib.h"
#include "atecc608_test.h"
#include "sk6812_test.h"
#include "i2c_device.h"
#include "mic_fft_test.h"

static void brightness_slider_event_cb(lv_obj_t * slider, lv_event_t event);
static void strength_slider_event_cb(lv_obj_t * slider, lv_event_t event);
static void led_event_handler(lv_obj_t * obj, lv_event_t event);

static void speakerTask(void *arg);
static void ateccTask(void *arg);
static void sdcardTest();

// in disp_spi.c
extern SemaphoreHandle_t xGuiSemaphore;
extern SemaphoreHandle_t spi_mutex;

extern void spi_poll();

static const int RX_BUF_SIZE = 1024;
static const int RMC_BUF_SIZE = 80;
static const int LAT_BUF_SIZE = 12;

#define TXD_PIN (GPIO_NUM_14)
#define RXD_PIN (GPIO_NUM_13)

#define TIME_IDX 1
#define VALID_IDX 2
#define LAT_IDX 3
#define NORTH_SOUTH_IDX 4
#define LON_IDX 5
#define EAST_WEST_IDX 6
#define DATE_IDX 9

#define SATELLITE_NUM_IDX 7
#define HEIGHT_IDX 9

char dateTime[41];
char lat_lon[45];
char satelliteNum_height[40];

void init(void) {
    const uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    // We won't use a buffer for sending data.
    uart_driver_install(UART_NUM_1, RX_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

static void rx_task(void *arg)
{
    static const char *RX_TASK_TAG = "RX_TASK";
    esp_log_level_set(RX_TASK_TAG, ESP_LOG_INFO);
    uint8_t* data = (uint8_t*) malloc(RX_BUF_SIZE+1);
    char* data2 = (char*)malloc(RX_BUF_SIZE+1);
    char* gnrmcData = (char*)malloc(RMC_BUF_SIZE);
    char* gnggaData = (char*)malloc(RMC_BUF_SIZE);
    char gnrmcItem[LAT_BUF_SIZE];
    char gnggaItem[LAT_BUF_SIZE];
	char year[3];
    int yearInt;
	char month[3];
    int monthInt;
	char day[3];
    int dayInt;
    int dayAdd;
	char hour[3];
	int hourInt;
	char minitu[3];
	char second[3];
	char time[20];

	char validLatLon[2];
	char latitude0[3];
	char latitude1[9];
	char longitude0[4];
	char longitude1[9];
	char N_S[2];
	char E_W[2];

	char satelliteNum[3];
	char height[6];

    int itemIdx;
    int startIdx;
    int commaIdx;
    char pickChar;
    int gnrmcItemLen;
    int gnggaItemLen;

    while (1) {
        const int rxBytes = uart_read_bytes(UART_NUM_1, data, RX_BUF_SIZE, 1000 / portTICK_RATE_MS);
        if (rxBytes > 0) {
        	dateTime[0] = '\0';
        	data[RX_BUF_SIZE] = '\0';
        	strcpy(data2, (char*)data);
            gnrmcData = strstr((char*)data, "$GNRMC");
            strtok(gnrmcData, "\n");
//            strcpy(gnrmcData, "$GNRMC,051536.000,A,3557.92035,N,13759.11744,E,0.00,129.95,080221,,,A*7C");
            ESP_LOGI("GNRMC Data", "%s", gnrmcData);
            dayAdd = 0;
            itemIdx = 0;
            startIdx = 0;
            commaIdx = 0;
			while(1) {
				strncpy(&pickChar, gnrmcData + startIdx + commaIdx++, 1);
				if(pickChar == ',') {
					strncpy(gnrmcItem, gnrmcData + startIdx, commaIdx - 1);
                    *(gnrmcItem + commaIdx - 1) = '\0';
					startIdx += commaIdx;
					commaIdx = 0;
					gnrmcItemLen = strlen(gnrmcItem);
					switch(itemIdx++){
					case TIME_IDX:
						if(gnrmcItemLen > 0) {
							strncpy(hour, gnrmcItem, 2);
							hour[2] = '\0';
							hourInt = atoi(hour);
							hourInt += 9;
							if(hourInt >= 24) {
								dayAdd = 1;
								hourInt -= 24;
							}
							strncpy(minitu, gnrmcItem + 2, 2);
							minitu[2] = '\0';
							strncpy(second, gnrmcItem + 4, 2);
							second[2] = '\0';
							sprintf(time, "%02d:%s:%s", hourInt, minitu, second);
						}
						else {
							time[0] = '\0';
						}
						break;
					case VALID_IDX:
						strcpy(validLatLon, gnrmcItem);
						break;
					case LAT_IDX:
						if(gnrmcItemLen > 0) {
							strncpy(latitude0, gnrmcItem, 2);
							strcpy(latitude1, gnrmcItem + 2);
						}
						else {
							latitude0[0] = '\0';
							latitude1[0] = '\0';
						}
						break;
					case NORTH_SOUTH_IDX:
						if(gnrmcItemLen > 0) {
							strcpy(N_S, gnrmcItem);
						}
						else {
							N_S[0] = '\0';
						}
						break;
					case LON_IDX:
						if(gnrmcItemLen > 0) {
							strncpy(longitude0, gnrmcItem, 3);
							longitude0[3] = '\0';
							strcpy(longitude1, gnrmcItem + 3);
						}
						else {
							longitude0[0] = '\0';
							longitude1[0] = '\0';
						}
						break;
					case EAST_WEST_IDX:
						if(gnrmcItemLen > 0) {
							strcpy(E_W, gnrmcItem);
						}
						else {
							E_W[0] = '\0';
						}
						break;
					case DATE_IDX:
						if(gnrmcItemLen > 0) {
							strncpy(day, gnrmcItem, 2);
							day[2] = '\0';
							dayInt = atoi(day);
							dayInt += dayAdd;
							strncpy(month, gnrmcItem + 2, 2);
							month[2] = '\0';
							monthInt = atoi(month);
							switch(monthInt) {
								case 1:
								case 3:
								case 5:
								case 7:
								case 8:
								case 10:
								case 12:
									if(dayInt > 31) {
										monthInt++;
										dayInt = 1;
									}
									break;
								case 2:
									if(dayInt > 28) {
										monthInt++;
										dayInt = 1;
									}
									break;
								case 4:
								case 6:
								case 9:
								case 11:
									if(dayInt > 30) {
										monthInt++;
										dayInt = 1;
									}
									break;
								default:
									break;
							}
							strncpy(year, gnrmcItem + 4, 2);
							year[2] = '\0';
							yearInt = atoi(year);
							if(monthInt > 12) {
								yearInt++;
								monthInt = 1;
							}
							sprintf(dateTime, "20%02d/%02d/%02d ", yearInt, monthInt, dayInt);
						}
						else {
							dateTime[0] = '\0';
						}
						break;
					default:
						break;
					}
				}
				if(itemIdx >= 10) break;
			}
            strcat(dateTime, time);
            dateTime[strlen(dateTime)] = '\n';
            printf(dateTime);

            if(validLatLon[0] == 'A') {
            	sprintf(lat_lon, "lat. %s %s' %s/long. %s %s' %s\n", latitude0, latitude1, N_S, longitude0, longitude1, E_W);
            }
            else {
            	sprintf(lat_lon, "NG\n");
            }
            printf(lat_lon);

			// GNGGA data
			gnggaData = strstr(data2, "$GNGGA");
			strtok(gnggaData, "\n");
//			strcpy(gnggaData, "$GNGGA,051540.000,3557.92081,N,13759.11679,E,1,09,1.3,767.7,M,0.0,M,,*7A");
            ESP_LOGI("GNGGA Data", "%s", gnggaData);
			itemIdx = 0;
			startIdx = 0;
			commaIdx = 0;
			while(1) {
				strncpy(&pickChar, gnggaData + startIdx + commaIdx++, 1);
				if(pickChar == ',') {
					strncpy(gnggaItem, gnggaData + startIdx, commaIdx - 1);
					*(gnggaItem + commaIdx - 1) = '\0';
					startIdx += commaIdx;
					commaIdx = 0;
					gnggaItemLen = strlen(gnggaItem);
					switch(itemIdx++){
					case SATELLITE_NUM_IDX:
						if(gnggaItemLen > 0) {
							strcpy(satelliteNum, gnggaItem);
						}
						else {
							satelliteNum[0] = '\0';
						}
						break;
					case HEIGHT_IDX:
						if(gnggaItemLen > 0) {
							strcpy(height, gnggaItem);
						}
						else {
							height[0] = '\0';
						}
						break;
					}
				}
				if(itemIdx >= 10) break;
			}
			if(strlen(height) > 0) {
				sprintf(satelliteNum_height, "Satellite number:%s, Height:%s\n", satelliteNum, height);
			}
			else {
				sprintf(satelliteNum_height, "Satellite number:%s\n", satelliteNum);
			}
			printf(satelliteNum_height);
        }
    }
    free(data);
}

void app_main(void)
{
    esp_log_level_set("gpio", ESP_LOG_NONE);
    esp_log_level_set("ILI9341", ESP_LOG_NONE);

    spi_mutex = xSemaphoreCreateMutex();

    Core2ForAWS_Init();
    FT6336U_Init();
    Core2ForAWS_LCD_Init();
    Core2ForAWS_Button_Init();
    Core2ForAWS_Sk6812_Init();
    BM8563_Init();
    MPU6886_Init();
    sdcardTest();
    sk6812Test();

    xTaskCreatePinnedToCore(speakerTask, "speak", 4096*2, NULL, 4, NULL, 1);
    
    rtc_date_t date;
    date.year = 2020;
    date.month = 9;
    date.day = 30;

    date.hour = 13;
    date.minute = 40;
    date.second = 10;    
    BM8563_SetTime(&date);

    xSemaphoreTake(xGuiSemaphore, portMAX_DELAY);

    lv_obj_t * time_label = lv_label_create(lv_scr_act(), NULL);
    lv_obj_set_pos(time_label, 10, 5);
    lv_label_set_align(time_label, LV_LABEL_ALIGN_LEFT);

    lv_obj_t * mpu6886_lable = lv_label_create(lv_scr_act(), NULL);
    lv_obj_align(mpu6886_lable, time_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);

    lv_obj_t * touch_label = lv_label_create(lv_scr_act(), NULL);
    lv_obj_align(touch_label, mpu6886_lable, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);

//    lv_obj_t * pmu_label = lv_label_create(lv_scr_act(), NULL);
//    lv_obj_align(pmu_label, touch_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);

    lv_obj_t * led_label = lv_label_create(lv_scr_act(), NULL);
    lv_obj_align(led_label, touch_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 27);
    lv_label_set_text(led_label, "Power LED & SK6812");
    
    lv_obj_t *sw1 = lv_switch_create(lv_scr_act(), NULL);
    lv_obj_set_size(sw1, 60, 20);
    lv_obj_align(sw1, led_label, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    lv_obj_set_event_cb(sw1, led_event_handler);
    lv_switch_on(sw1, LV_ANIM_ON);

    Core2ForAWS_LED_Enable(1);
    sk6812TaskResume();

    lv_obj_t * brightness_label = lv_label_create(lv_scr_act(), NULL);
    lv_obj_align(brightness_label, led_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);

    lv_label_set_text(brightness_label, "Screen brightness");

    lv_obj_t * brightness_slider = lv_slider_create(lv_scr_act(), NULL);
    lv_obj_set_width(brightness_slider, 130);
    lv_obj_align(brightness_slider, brightness_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    lv_obj_set_event_cb(brightness_slider, brightness_slider_event_cb);
    lv_slider_set_value(brightness_slider, 50, LV_ANIM_OFF);
    lv_slider_set_range(brightness_slider, 30, 100);

    lv_obj_t * motor_label = lv_label_create(lv_scr_act(), NULL);
    lv_obj_align(motor_label, brightness_label, LV_ALIGN_OUT_RIGHT_MID, 20, 0);
    lv_label_set_text(motor_label, "Motor strength");

    lv_obj_t * strength_slider = lv_slider_create(lv_scr_act(), NULL);
    lv_obj_set_width(strength_slider, 130);
    lv_obj_align(strength_slider, motor_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_obj_set_event_cb(strength_slider, strength_slider_event_cb);
    lv_slider_set_value(strength_slider, 0, LV_ANIM_OFF);
    lv_slider_set_range(strength_slider, 0, 100);

    xSemaphoreGive(xGuiSemaphore);

    xTaskCreatePinnedToCore(ateccTask, "ateccTask", 4096*2, NULL, 1, NULL, 1);

//    char label_stash[200];

    init();
    xTaskCreate(rx_task, "uart_rx_task", 1024*4, NULL, configMAX_PRIORITIES, NULL);

    for (;;) {
//        BM8563_GetTime(&date);
//        sprintf(label_stash, "Time: %d-%02d-%02d %02d:%02d:%02d\r\n",
//                date.year, date.month, date.day, date.hour, date.minute, date.second);
        xSemaphoreTake(xGuiSemaphore, portMAX_DELAY);
        lv_label_set_text(time_label, dateTime);
        xSemaphoreGive(xGuiSemaphore);

//        float ax, ay, az;
//        MPU6886_GetAccelData(&ax, &ay, &az);
//        sprintf(label_stash, "MPU6886 Acc x: %.2f, y: %.2f, z: %.2f\r\n", ax, ay, az);
        xSemaphoreTake(xGuiSemaphore, portMAX_DELAY);
        lv_label_set_text(mpu6886_lable, lat_lon);
        xSemaphoreGive(xGuiSemaphore);

//        uint16_t x, y;
//        bool press;
//        FT6336U_GetTouch(&x, &y, &press);
//        sprintf(label_stash, "Touch x: %d, y: %d, press: %d\r\n", x, y, press);
        xSemaphoreTake(xGuiSemaphore, portMAX_DELAY);
        lv_label_set_text(touch_label, satelliteNum_height);
        xSemaphoreGive(xGuiSemaphore);

//        sprintf(label_stash, "Bat %.3f V, %.3f mA\r\n", Core2ForAWS_PMU_GetBatVolt(), Core2ForAWS_PMU_GetBatCurrent());
//        xSemaphoreTake(xGuiSemaphore, portMAX_DELAY);
//        lv_label_set_text(pmu_label, label_stash);
//        xSemaphoreGive(xGuiSemaphore);
        
        vTaskDelay(pdMS_TO_TICKS(100));

        if (Button_WasPressed(button_left)) {
            printf("button left press\r\n");
        }
        if (Button_WasReleased(button_middle)) {
            printf("button middle release\r\n");
        }
        if (Button_WasLongPress(button_right, 500)) {
            printf("button right long pressed\r\n");
        }
    }
}

static void brightness_slider_event_cb(lv_obj_t * slider, lv_event_t event) {
    if(event == LV_EVENT_VALUE_CHANGED) {
        Core2ForAWS_LCD_SetBrightness(lv_slider_get_value(slider));
    }
}

static void strength_slider_event_cb(lv_obj_t * slider, lv_event_t event) {
    if(event == LV_EVENT_VALUE_CHANGED) {
        Core2ForAWS_Motor_SetStrength(lv_slider_get_value(slider));
    }
}

static void led_event_handler(lv_obj_t * obj, lv_event_t event) {
    if(event == LV_EVENT_VALUE_CHANGED) {
        Core2ForAWS_LED_Enable(lv_switch_get_state(obj));
        if (lv_switch_get_state(obj)) {
            sk6812TaskResume();
        } else {
            sk6812TaskSuspend();
        }
    }
}

static void speakerTask(void *arg) {
    Speaker_Init();
//    Core2ForAWS_Speaker_Enable(1);
    extern const unsigned char music[120264];
    Speaker_WriteBuff((uint8_t *)music, 120264, portMAX_DELAY);
    Core2ForAWS_Speaker_Enable(0);
    Speaker_Deinit();
    xTaskCreatePinnedToCore(microphoneTask, "microphoneTask", 4096*2, NULL, 1, NULL, 0);
    vTaskDelete(NULL);
}

static void ateccTask(void *arg) {
    atecc_test();

    vTaskDelete(NULL);
}

/*
//  note: Because the SD card and the screen use the same spi bus so if want use sd card api, must
xSemaphoreTake(spi_mutex, portMAX_DELAY);
// call spi poll to solve spi share bug (maybe a bug)
spi_poll();
// call sd card api
xSemaphoreGive(spi_mutex);
*/
static void sdcardTest() {
    #define MOUNT_POINT "/sdcard"
    sdmmc_card_t* card;
    esp_err_t ret;

    ret = Core2ForAWS_Sdcard_Init(MOUNT_POINT, &card);

    if (ret != ESP_OK) {
        ESP_LOGE("sdcard", "Failed to initialize the sd card");
        return;
    } 

    ESP_LOGI("sdcard", "Success to initialize the sd card");
    sdmmc_card_print_info(stdout, card);

    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    spi_poll();
    char test_file[] =  MOUNT_POINT"/hello.txt";
    ESP_LOGI("sdcard", "Write file to %s", test_file);
    FILE* f = fopen(test_file, "w+");
    if (f == NULL) {
        ESP_LOGE("sdcard", "Failed to open file for writing");
        xSemaphoreGive(spi_mutex);
        return;
    }
    ESP_LOGI("sdcard", "Write -> Hello %s!\n", "SD Card");
    fprintf(f, "Hello %s!\r\n", "SD Card");
    fclose(f);

    ESP_LOGI("sdcard", "Reading file %s", test_file);
    f = fopen(test_file, "r");
    if (f == NULL) {
        ESP_LOGE("sdcard", "Failed to open file for reading");
        xSemaphoreGive(spi_mutex);
        return;
    }
    char line[64];
    fgets(line, sizeof(line), f);
    fclose(f);
    ESP_LOGI("sdcard", "Read <- %s", line);
    xSemaphoreGive(spi_mutex);
}
