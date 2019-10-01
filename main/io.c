
// io.c functions for status-LED and reset-button

/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain 
 * this notice you can do whatever you want with this stuff. If we meet some day, 
 * and you think this stuff is worth it, you can buy me a beer in return. 
 * ----------------------------------------------------------------------------
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
//#include "esp_timer.h"  note: don't call system_restart() from a esp_timer callback see https://esp32.com/viewtopic.php?f=13&t=9542
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h" // use a FreeRTOS timer instead
#include "esp_system.h"  // for system_restart();
#include "io.h"

// these for erase partition:
#include "esp_ota_ops.h"
#include "esp_flash_partitions.h"
#include "esp_image_format.h"

static const char* TAG = "io.c";
#define RST_BTN_SAMPLE_MS	(100) //ms

static enum BLE_state_e ble_state = BLE_STATE_OFF;
void set_status_ind_ble(enum BLE_state_e new_state){
	if (new_state < BLE_STATE_NUM_STATES) ble_state = new_state;
}
static enum WIFI_state_e wifi_state = WIFI_STATE_OFF;
void set_status_ind_wifi(enum WIFI_state_e new_state){
	if (new_state < WIFI_STATE_NUM_STATES) wifi_state = new_state;
}

void ioLed(int ledgpio, int en) {
	int level = (en)?1:0;

	gpio_set_level(ledgpio, level);
}

#define SM_INIT	(1)
#define SM_NO_INIT	(0)

/**
 * @brief   TTP_config-blink State Machine
 *
 */
static void blink_stateMachine(int init, uint32_t delta_time, int num1, int num2) {
	// State names for the Blink_stateMachine
	static enum {
		BLINK_STATE_INIT,
		BLINK_STATE_PAUSE_START,
		BLINK_STATE_N1_BLINK_ON,
		BLINK_STATE_N1_BLINK_OFF,
		BLINK_STATE_PAUSE_MID,
		BLINK_STATE_N2_BLINK_ON,
		BLINK_STATE_N2_BLINK_OFF,
		BLINK_STATE_PAUSE_END,
	}blink_state;

#define BLINK_tSTART (500)  // beginning off-time
#define BLINK_tNh    (250)  // num1 blink on-time
#define BLINK_tNl    (250)  // num1 blink off-time
#define BLINK_tMID   (0 + BLINK_tNl)  // middle off-time
#define BLINK_tN2h  (350)  // num2 blink on-time
#define BLINK_tN2l  (150)  // num2 blink off-time
#define BLINK_tEND   (500 + BLINK_tN2l)  // End off-time
	static int32_t t = 0;    // local timer
	static uint8_t myN = 0;  // local counter
	t += delta_time;
	if (init == SM_INIT) blink_state = BLINK_STATE_INIT;

	switch (blink_state){
	case BLINK_STATE_INIT:
		blink_state = BLINK_STATE_PAUSE_START;
		ioLed(LED_WIFI, 0);  // Turn off LED1
		ioLed(LED_BLE, 0);  // Turn off LED2
		t = 0;
		break;
	case BLINK_STATE_PAUSE_START:
		if (num1 == 0) blink_state = BLINK_STATE_PAUSE_MID;
		else if (t >= BLINK_tSTART) {
			t -= BLINK_tSTART;
			blink_state = BLINK_STATE_N1_BLINK_ON;
			ioLed(LED_WIFI, 1);  // Turn ON LED1
			myN = 0;
		}
		break;
	case BLINK_STATE_N1_BLINK_ON:
		if (t >= BLINK_tNh) {
			t -= BLINK_tNh;
			blink_state = BLINK_STATE_N1_BLINK_OFF;
			ioLed(LED_WIFI, 0); // Turn OFF LED1
			myN += 1;
			//ESP_LOGI(TAG, "BLINK_STATE_N1_BLINK_OFF: myN: %i", myN);
		}
		break;
	case BLINK_STATE_N1_BLINK_OFF:
		if (myN >= num1) {
			blink_state = BLINK_STATE_PAUSE_MID;
			//LED_pattern_init(0, palette[C_OFF]); // Turn OFF
			//t = 0;
		}
		else if (t >= BLINK_tNl) {
			t -= BLINK_tNl;
			blink_state = BLINK_STATE_N1_BLINK_ON;
			ioLed(LED_WIFI, 1);  // Turn ON LED1
		}
		break;
	case BLINK_STATE_PAUSE_MID:
		if (num2 == 0) blink_state = BLINK_STATE_PAUSE_END;
		if (t >= BLINK_tMID) {
			t -= BLINK_tMID;
			blink_state = BLINK_STATE_N2_BLINK_ON;
			ioLed(LED_BLE, 1); // Turn ON LED2
			myN = 0;
		}
		break;
	case BLINK_STATE_N2_BLINK_ON:
		if (t >= BLINK_tN2h) {
			t -= BLINK_tN2h;
			blink_state = BLINK_STATE_N2_BLINK_OFF;
			ioLed(LED_BLE, 0); // Turn OFF LED2
			myN += 1;
			//ESP_LOGI(TAG, "BLINK_STATE_N2_BLINK_OFF: myN: %i", myN);
		}
		break;
	case BLINK_STATE_N2_BLINK_OFF:
		if (myN >= num2){
			blink_state = BLINK_STATE_PAUSE_END;
			//LED_pattern_init(0, palette[C_OFF]); // Turn OFF
			//t = 0;
		}
		else if (t >= BLINK_tN2l){
			t -= BLINK_tN2l;
			blink_state = BLINK_STATE_N2_BLINK_ON;
			ioLed(LED_BLE, 1); // Turn ON LED2

		}
		break;
	case BLINK_STATE_PAUSE_END:
		if (t >= BLINK_tEND){
			t -= BLINK_tEND;
			blink_state = BLINK_STATE_PAUSE_START;
		}
		break;
	default:
		break;
	}
	/** Use plantuml.com or PlantUML Eclipse-plugin to render this state-machine-diagram:

@startuml
title State Machine Diagram for Blink
[*] --> INIT : Reset
INIT --> PAUSE_START : [set t=0]
PAUSE_START --> N1_BLINK_ON : t > tSTART\n[set t=0]
N1_BLINK_ON -d-> N1_BLINK_OFF : t > tN_on\n[set t=0]\n[set myN+=1]
N1_BLINK_OFF --> N1_BLINK_ON : t > tN_on\n[set t=0]
N1_BLINK_OFF --> PAUSE_MID : myN >= num1\n[set t=0]
PAUSE_MID --> N2_BLINK_ON : t > tMID\n[set t=0]
N2_BLINK_ON -d-> N2_BLINK_OFF : t > tN2h\n[set t=0]\n[set myN+=1]
N2_BLINK_OFF --> N2_BLINK_ON : t > tN2l\n[set t=0]
N2_BLINK_OFF --> PAUSE_END : myN >= num2\n[set t=0]
PAUSE_END --> INIT : t > tEND
@enduml

	 */
}

static void resetBtnTimerCb(void *arg) {
	static int resetCnt=0;
	if (gpio_get_level(BTN_GPIO)==0) {
		// button is depressed
		resetCnt++;
		if (resetCnt>=3000/RST_BTN_SAMPLE_MS) { //>3 sec pressed
			// Blink like crazy to indicate that button has been pressed long-enough.
			blink_stateMachine(SM_NO_INIT, 50*RST_BTN_SAMPLE_MS, 1, 1);
		} else {
			// Force LEDs off while button is depressed <3s.
			ioLed(LED_WIFI, 0); // Turn OFF LED1
			ioLed(LED_BLE, 0); // Turn OFF LED2
		}
	} else {
		// button not pressed

		// Button was released after 3s pressed.  Reset networking settings.
		if (resetCnt>=3000/RST_BTN_SAMPLE_MS) {
			const esp_partition_t *wanted_partition = NULL;
			esp_err_t err = ESP_FAIL;
			printf("Reset WiFi and Bluetooth settings.\n");
			ESP_LOGI(TAG, "Erase NVS command!");
			wanted_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"nvs");
			err = esp_partition_erase_range(wanted_partition, 0, wanted_partition->size);
			if (err != ESP_OK) {
				ESP_LOGE(TAG, "erase partition failed! err=0x%x", err);
			}
			else
			{
				ESP_LOGW(TAG, "NVS partition is erased now!  Must reboot to reformat it!");
			}
			ioLed(LED_WIFI, 1); // Turn ON LED1
			ioLed(LED_BLE, 1); // Turn ON LED2

			printf("Restarting system...\n");
			esp_restart();  // note: don't call this from a esp_timer callback use a FreeRTOS timer instead see https://esp32.com/viewtopic.php?f=13&t=9542

			// Button was released before 3s, restart
		} else if (resetCnt >= 1) {
			ioLed(LED_WIFI, 1); // Turn ON LED1
			ioLed(LED_BLE, 1); // Turn ON LED2

			printf("Restarting system...\n");
			esp_restart();  // note: don't call this from a esp_timer callback use a FreeRTOS timer instead see https://esp32.com/viewtopic.php?f=13&t=9542
		}
		resetCnt=0;

		blink_stateMachine(SM_NO_INIT, RST_BTN_SAMPLE_MS, wifi_state, ble_state);  // BLE state blinks blue LED, WiFi state blinks green LED.
	}
}

void ioInit() {
	ioLed(LED_WIFI, 0);  // Turn off LED1
	ioLed(LED_BLE, 0);  // Turn off LED2

	/* Configure the IOMUX register for pad GPIO */
	gpio_pad_select_gpio(LED_WIFI);
	gpio_pad_select_gpio(LED_BLE);
	gpio_pad_select_gpio(LED_CGI);
	gpio_pad_select_gpio(BTN_GPIO);
	/* Set the GPIO as a push/pull output */
	gpio_set_direction(LED_WIFI, GPIO_MODE_INPUT_OUTPUT);
	gpio_set_direction(LED_BLE, GPIO_MODE_INPUT_OUTPUT);
	gpio_set_direction(LED_CGI, GPIO_MODE_INPUT_OUTPUT);
	gpio_set_direction(BTN_GPIO, GPIO_MODE_INPUT);

	blink_stateMachine(SM_INIT, 0,0,0);  // Init blink_stateMachine

	/* Create a periodic timer which will run every 0.1s */
	TimerHandle_t resetBtnTimer = xTimerCreate(    "rstTmr",       // Just a text name, not used by the kernel.
			( RST_BTN_SAMPLE_MS / portTICK_RATE_MS ),   // The timer period in ticks.
			pdTRUE,        // The timers will auto-reload themselves when they expire.
			0,  // Assign each timer a unique id equal to its array index.
			resetBtnTimerCb // Each timer calls the same callback when it expires.
	);
	/* The timer has been created but is not running yet */

	/* Start the timers */
	xTimerStart( resetBtnTimer, 0 );
	ESP_LOGI(TAG, "Started resetBtn timer");
}

