#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/gpio_types.h"
#include "rom/gpio.h"
#include "string.h"
#include "driver/gpio.h"
#include <freertos/queue.h>
#include <esp_intr_alloc.h>
#include <sys/_stdint.h>
#include "driver/i2c.h"
#include "esp_system.h"
#include "esp_adc_cal.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "ds18b20.h" //Include library
#define DS_GPIO 3    // GPIO where you connected ds18b20
#define TRIGGER_PIN 0
#define ECHO_PIN 1



#define RELE_MOTOR_PIN 6 //mudar
#define RELE_RESIST_PIN 10 //mudar
#define ENABLE_RESIST_TRESHOLD 5
#define ENABLE_MOTOR_TRESHOLD 5


#include "driver/adc.h"
#define TAG "CAIXA DAGUA"

// Botoes
#define JOYSTICK_SW_PIN 12
#define JOYSTIC_Y_PIN 2

#define ADC_WIDTH_BIT 10
#define ADC_MAX_VALUE ((1<<ADC_WIDTH_BIT)-1)
#define DEFAULT_VREF    1100        // Valor de referência para a calibração ADC (em mV)
#define NO_OF_SAMPLES   64          // Número de amostras ADC para média

typedef enum display_status_t {
  READ = 0,
  WRITE_TEMP = 1,
  WRITE_NVL = 2
} display_status_t;

display_status_t display_status = READ;
int trigou = 0;
typedef enum joystic_status_t {
  MIDDLE = 0,
  UP = 1,
  DOWN = 2
} joystic_status_t;

// DIsplay
#include "display.c"

// Distance
struct HcSR04GetDist
{
  int isWorking;
  float distance;
};
typedef struct HcSR04GetDist HcSR04GetDist;
HcSR04GetDist distData = { 0, 0 };

uint64_t start_echo_time_check = 0;
uint64_t end_echo_time_check = 0;

// Temperature
Ds18b20GetTemp tempData;


struct ApplicationData {
  uint32_t desiredTemp;
  uint32_t desiredNvl;
};
typedef struct ApplicationData ApplicationData;
ApplicationData appData =  {30, 10};
uint32_t editTemp;
uint32_t editNvl;

#define MAX_TEMP 50
#define MIN_TEMP 10

#define MAX_LEVEL 90
#define MAX_DIST 29
#define MIN_DIST 10

#define STEP_TEMP 5
#define STEP_NVL 5

void IRAM_ATTR gpio_isr_handle_sw(void* arg) {
    trigou = !trigou;

}

void IRAM_ATTR gpio_isr_handle_echo(void* arg) { 

  
  int gpio_level = gpio_get_level(ECHO_PIN);


  if (gpio_level == 0) {
    end_echo_time_check = esp_timer_get_time();
    float time_diff = (float) end_echo_time_check - start_echo_time_check;
    float distance = time_diff / 58.0;

    if (distance > 400 || distance < 2) {
      distData.isWorking = 0;
    } else {
      distData.isWorking = 1;
      distData.distance = distance;
    }
  } else {
    start_echo_time_check = esp_timer_get_time();
  }
  
}

void trigger_echo(void *pvParameters) {
  while(1) {
    gpio_set_level(TRIGGER_PIN, 1);
    ets_delay_us(10);
    gpio_set_level(TRIGGER_PIN, 0);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void measure_temp(void *pvParameters) {
  ds18b20_init(DS_GPIO);
  while (1)
  {
    tempData = ds18b20_get_temp();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void config_measure_distance() {
  gpio_pad_select_gpio(TRIGGER_PIN);
  gpio_set_direction(TRIGGER_PIN, GPIO_MODE_OUTPUT);


  gpio_pad_select_gpio(ECHO_PIN);
  gpio_set_direction(ECHO_PIN, GPIO_MODE_INPUT);
  gpio_set_intr_type(ECHO_PIN, GPIO_INTR_ANYEDGE);
  
  gpio_isr_handler_add(ECHO_PIN, gpio_isr_handle_echo, (void*)ECHO_PIN);

  xTaskCreate(&trigger_echo, "trigger_echo", 2048, NULL, 5, NULL);
}

void config_reles() {
  // Iniciando os reles desativados
  gpio_pad_select_gpio(RELE_MOTOR_PIN);
  gpio_set_direction(RELE_MOTOR_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level(RELE_MOTOR_PIN, 1);

  gpio_pad_select_gpio(RELE_RESIST_PIN);
  gpio_set_direction(RELE_RESIST_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level(RELE_RESIST_PIN, 1);
  ESP_LOGI(TAG, "Reles configurados");
}

void config_measure_temp() {
  xTaskCreate(&measure_temp, "measure_temp", 1024, NULL, 5, NULL);
}

void active_motor() {
  gpio_set_level(RELE_MOTOR_PIN, 1);
  // ESP_LOGW(TAG, "Motor ligado");
}

void disable_motor() {
  gpio_set_level(RELE_MOTOR_PIN, 0);
  // ESP_LOGW(TAG, "Motor desligado");
}

void active_resist() {
  gpio_set_level(RELE_RESIST_PIN, 1);
}

void disable_resist() {
  gpio_set_level(RELE_RESIST_PIN, 0);
}


static esp_adc_cal_characteristics_t adc1_chars;
void configure_joystick_y() {
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_DEFAULT, 0, &adc1_chars);
  ESP_ERROR_CHECK(adc1_config_width(ADC_WIDTH_BIT_DEFAULT));
  ESP_ERROR_CHECK(adc1_config_channel_atten(ADC1_CHANNEL_2, ADC_ATTEN_DB_11));
}


joystic_status_t readJoystick() {
  uint32_t voltage = esp_adc_cal_raw_to_voltage(adc1_get_raw(ADC1_CHANNEL_2), &adc1_chars);
  if (voltage >= 2700) {
    return DOWN;
  }
  else if (voltage < 300) {
    return UP;
  } else {
    return MIDDLE;
  }
}


unsigned long currentMilis() {
  return xTaskGetTickCount() * portTICK_RATE_MS;
}


int get_current_level() {
  float prop = distData.distance / MAX_DIST;
  int percentEspacoVazio = prop * 100;

  if (percentEspacoVazio > 100) {
    percentEspacoVazio = 100;
  }
  return 100 - percentEspacoVazio;
}

void display_menu() {
  char display1[16];

  char * replaceTemp[3];
  if (tempData.isWorking) {
    sprintf(replaceTemp, "%.0fC", tempData.temp);
  } else {
    strcpy(replaceTemp, "ERR");
  }

  char * replaceNvl[3];
  if (distData.isWorking) {
    int fillPercentage = get_current_level();
    sprintf(replaceNvl, "%02d%%", fillPercentage);
  } else {
    strcpy(replaceNvl, "ERR");
  }

  sprintf(display1, " %s | %s |   ", replaceTemp, replaceNvl);
  write_on_lcd("TEMP | NVL | RD", 0);
  write_on_lcd(display1, 1);
}

void display_edit_nvl() {
  char display1[16];
  char * display0 = "     | %i%% |   ";
  sprintf(display1, display0, editNvl);
  write_on_lcd("     | NVL |   ", 0);
  write_on_lcd(display1, 1);
}

void display_edit_temp() {
  char display1[16];
  sprintf(display1, " %iC |          ", editTemp);
  write_on_lcd("TEMP |", 0);
  write_on_lcd(display1, 1);
}

void execute_temp_update() {
  appData.desiredTemp = editTemp;
}

void execute_nvl_update() {
  appData.desiredNvl = editNvl;
}

void sw_button_handler() {
  int last_led_state = 0;
  int button_current_state = 0;
  unsigned long last_debounce_time = 0;

  int pressed = 0;

  while(1) {
    button_current_state = !gpio_get_level(JOYSTICK_SW_PIN);
    
    if (button_current_state != last_led_state) {
      last_debounce_time = currentMilis();
      last_led_state = button_current_state;
    }

    if ((currentMilis() - last_debounce_time) > 100) {
      if (button_current_state == 1) {
        pressed = 1;
        ESP_LOGI(TAG, "Botao apertado");
      } else {
        if(pressed == 1) {
          ESP_LOGI(TAG, "Botao solto");
          switch(display_status) {
            case READ:
              clear_lcd();
              display_status = WRITE_TEMP;
              break;
            case WRITE_TEMP:
              clear_lcd();
              execute_temp_update();
              display_status = WRITE_NVL;
              break;
            case WRITE_NVL:
              clear_lcd();
              execute_nvl_update();
              display_status = READ;
              break;
          }
          pressed = 0;
        }
      }
    }
    vTaskDelay(100/ portTICK_PERIOD_MS);
  }
}


void configure_joystick_sw_button() {
  gpio_pad_select_gpio(JOYSTICK_SW_PIN);
  gpio_set_direction(JOYSTICK_SW_PIN, GPIO_MODE_INPUT);

  xTaskCreate(&sw_button_handler,"sw_button_handler",2048, NULL, configMAX_PRIORITIES, NULL);
}



void increase_temp() {
  uint32_t new_temp = editTemp + STEP_TEMP;

  if (new_temp <= MAX_TEMP && new_temp >= MIN_TEMP) {
    editTemp = new_temp;
  }
}

void decrease_temp() {
  uint32_t new_temp = editTemp - STEP_TEMP;

  if (new_temp <= MAX_TEMP && new_temp >= MIN_TEMP) {
    editTemp = new_temp;
  }
}

void increase_nvl() {
  uint32_t new_nvl = editNvl + STEP_NVL;
  
  if (new_nvl <= MAX_LEVEL && new_nvl >= MIN_DIST) {
    editNvl = new_nvl;
  }
}

void decrease_nvl() {
  uint32_t new_nvl = editNvl - STEP_NVL;
  if (new_nvl <= MAX_LEVEL && new_nvl >= MIN_DIST) {
    editNvl = new_nvl;
  }
}



void manage_actions() {
  if (get_current_level() < appData.desiredNvl &&  distData.isWorking &&  get_current_level() < 95) {
    active_motor();
  } else {
    disable_motor();
  }
  ESP_LOGI(TAG, "Temp desejada: %d; Temp atual: %f; funci: %i", appData.desiredTemp, tempData.temp, tempData.isWorking);
  if (tempData.temp < appData.desiredTemp && tempData.isWorking && get_current_level() > 10) {
    ESP_LOGI(TAG, "Distancia: %f", distData.distance);

    ESP_LOGW(TAG, "Resi ligado");
    active_resist();
  } else {
    ESP_LOGW(TAG, "Resi desligado");
    disable_resist();
  }
}


void app_main() {

  editTemp = appData.desiredTemp;
  editNvl = appData.desiredNvl;
  gpio_install_isr_service(0);
  config_measure_distance();
  config_reles();
  config_measure_temp();
  config_display();
  configure_joystick_y();
  configure_joystick_sw_button();


  joystic_status_t cur_joystick_status;
  joystic_status_t prev_joystick_status = MIDDLE;

  while(1) {
    switch(display_status) {
      case WRITE_TEMP:
        cur_joystick_status = readJoystick();
        switch(cur_joystick_status) {
          case MIDDLE:
            if (prev_joystick_status == UP) {
              increase_temp();
            } else if(prev_joystick_status == DOWN) {
              decrease_temp();
            }
            prev_joystick_status = MIDDLE;
            break;
          case UP:
            prev_joystick_status = UP;
            break;
          case DOWN:
            prev_joystick_status = DOWN;
            break;
        }
        display_edit_temp();
        break;
      case WRITE_NVL:
        cur_joystick_status = readJoystick();
        switch(cur_joystick_status) {
          case MIDDLE:
            if (prev_joystick_status == UP) {
              increase_nvl();
            } else if(prev_joystick_status == DOWN) {
              decrease_nvl();
            }
            prev_joystick_status = MIDDLE;
            break;
          case UP:
            prev_joystick_status = UP;
            break;
          case DOWN:
            prev_joystick_status = DOWN;
            break;
        }
        display_edit_nvl();
        break;
      case READ:
        display_menu();
        manage_actions();
          break;
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);  
  }
}