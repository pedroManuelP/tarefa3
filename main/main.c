#include <stdio.h>
#include <driver/gpio.h>    // Pinos I/O
#include <driver/ledc.h>    //  TIMER e PWM
#include <esp_log.h>    //  ESP_LOGI()
#include <driver/dac_oneshot.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>

#define DAC_CHAN_OUTPUT DAC_CHAN_0 // GPIO25
#define BUTTON1_PIN GPIO_NUM_32 // Botão para mudar o formato de onda.
#define BUTTON2_PIN GPIO_NUM_35 // Botão para mudar a amplitude da onda.
#define BUTTON3_PIN GPIO_NUM_34 // Botão para mudar a frequência da onda.
#define WAVE_SIZE 1024 // Qtde. de pontos da onda.

static const char* TAG_WAVE_FORM = "WAVE_FORM";
static const char* TAG_WAVE_AMP = "WAVE_AMP";
static const char* TAG_WAVE_FREQ = "WAVE_FREQ";

uint8_t wave_buffer[WAVE_SIZE];  // Conjunto de pontos da onda.

void change_freq(int* wave_freq_hz, float* wave_sample_period_ms){// Cicla entre 10 valores de amplitude entre 100 e 1000
    if(*wave_freq_hz==1000){
        *wave_freq_hz=100;
    }else{
        *wave_freq_hz+=100;   // = (1000/10)
    }
    ESP_LOGI(TAG_WAVE_FREQ, "%d", *wave_freq_hz);
    *wave_sample_period_ms=(1000/(*wave_freq_hz))/(WAVE_SIZE);
}

void change_amp(uint8_t* wave_amp){// Cicla entre 5 valores de amplitude entre 0 e 255
    if(*wave_amp==255){
        *wave_amp=0;
    }else{
        *wave_amp+=51;   // = (255/5)
    }
    ESP_LOGI(TAG_WAVE_AMP, "%d", *wave_amp);
}

void generate_wave(int wave_type, uint8_t* wave_amp){// Recalcula o wave_buffer
    float temp_buffer[WAVE_SIZE];    // Usados para os cálculos com ponto flutuante.
    float alpha = *wave_amp/(WAVE_SIZE/2);

    // Cálculo os pontos da curva(em float)
    for(int i=0;i<WAVE_SIZE;i++){
        switch (wave_type){
            case 0:
                // Onda quadrada
                if(i < (WAVE_SIZE/2)){
                    temp_buffer[i]=0;
                }else{
                    temp_buffer[i] = *wave_amp;
                }
            break;
        
            case 1:
                // Onda dente-de-serra
                temp_buffer[i] = (alpha/2)*i;
            break;

            case 2:
                // Onda triangular
                if(i <= (WAVE_SIZE/2)){
                    // 0 < i <= N/2
                    temp_buffer[i] = alpha*i;
                }else{
                    // N/2 < i < N
                    temp_buffer[i] = 2 * (*wave_amp) - alpha*i;
                }
            break;
            
            case 3:
                // Onda senoidal
                temp_buffer[i] = ((*wave_amp)/2) * sinf(2*M_PI*(i/WAVE_SIZE)) + ((*wave_amp)/2);
            break;

            default:
            break;
        }

        //  Saturação do Buffer e conversão para uint8_t
        if(temp_buffer[i] < 0){
            wave_buffer[i] = 0;
        }else if(temp_buffer[i] > *wave_amp){
            wave_buffer[i] = *wave_amp;
        }else{
            wave_buffer[i] = (uint8_t)(temp_buffer[i]);
        }
    }// Fim do laço

    // Escreve quando os pontos terminarem de serem calculados
    if(wave_type==0){
        ESP_LOGI(TAG_WAVE_FORM, "Quadrada");
    }else if(wave_type==1){
        ESP_LOGI(TAG_WAVE_FORM, "Dente-de-serra");
    }else if(wave_type==2){
        ESP_LOGI(TAG_WAVE_FORM, "Triangular");
    }else if(wave_type==3){
        ESP_LOGI(TAG_WAVE_FORM, "Senoidal");
    }
}

void app_main(void)
{
    dac_oneshot_handle_t dac_handle;
    dac_oneshot_config_t dac_cfg={
        .chan_id=DAC_CHAN_OUTPUT,
    }; 
    dac_oneshot_new_channel(&dac_cfg, &dac_handle);

    //==========//==========//==========//==========//==========//==========//==========//==========//

    bool button1_level, button2_level, button3_level;
    bool button1_pressed=false, button2_pressed=false, button3_pressed=false;

    gpio_reset_pin(BUTTON1_PIN);
    gpio_set_direction(BUTTON1_PIN, GPIO_MODE_INPUT);
    gpio_pulldown_en(BUTTON1_PIN);

    gpio_reset_pin(BUTTON2_PIN);
    gpio_set_direction(BUTTON2_PIN, GPIO_MODE_INPUT);
    gpio_pulldown_en(BUTTON2_PIN);

    gpio_reset_pin(BUTTON3_PIN);
    gpio_set_direction(BUTTON3_PIN, GPIO_MODE_INPUT);
    gpio_pulldown_en(BUTTON3_PIN);

    //==========//==========//==========//==========//==========//==========//==========//==========//
    
    int wave_type=0;    // Quadrada(0), Dente de serra(1), Triangular(2), Senóide(3).
    uint8_t wave_amp = 255; // Amplitude da onda no wave_buffer[].
    int wave_freq_hz=100;   // Frequência da onda no wave_buffer[].
    float wave_sample_period_ms=(1000/wave_freq_hz)/(WAVE_SIZE);
    generate_wave(wave_type, &wave_amp);

    int idx=0;
    while(1){
        button1_level = gpio_get_level(BUTTON1_PIN);
        if(button1_level && !button1_pressed){
            button1_pressed = true;

            // Muda o formato de onda
            wave_type++;
            if(wave_type == 4)wave_type=0;
            generate_wave(wave_type, &wave_amp);
        }else if(!button1_level){
            button1_pressed = false;
        }

        button2_level = gpio_get_level(BUTTON2_PIN);
        if(button2_level && !button2_pressed){
            button2_pressed = true;

            // Muda a amplitude da onda
            change_amp(&wave_amp);
            generate_wave(wave_type, &wave_amp);
        }else if(!button2_level){
            button2_pressed = false;
        }

        button3_level = gpio_get_level(BUTTON3_PIN);
        if(button3_level && !button3_pressed){
            button3_pressed = true;

            // Muda a frequência da onda
            change_freq(&wave_freq_hz, &wave_sample_period_ms);
        }else if(!button3_level){
            button3_pressed = false;
        }

        dac_oneshot_output_voltage(dac_handle, wave_buffer[idx]);

        idx++;
        if(idx==WAVE_SIZE)idx=0;
        vTaskDelay(wave_sample_period_ms/portTICK_PERIOD_MS);
    }
}
