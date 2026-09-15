#include <stdio.h>
#include <driver/gpio.h>    // Pinos I/O
#include <driver/ledc.h>
#include <driver/gptimer.h>
#include <esp_log.h>    //  ESP_LOGI()
#include <driver/dac_oneshot.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>

#define DAC_CHAN_OUTPUT DAC_CHAN_0 // GPIO25
#define BUTTON1_PIN GPIO_NUM_13 // Botão para mudar o formato de onda.
#define BUTTON2_PIN GPIO_NUM_12 // Botão para mudar a amplitude da onda.
#define BUTTON3_PIN GPIO_NUM_14 // Botão para mudar a frequência da onda.
#define WAVE_SIZE 1024 // Qtde. de pontos da onda.

static const char* TAG_WAVE_FORM = "WAVE_FORM";
static const char* TAG_WAVE_AMP = "WAVE_AMP";
static const char* TAG_WAVE_FREQ = "WAVE_FREQ";

uint8_t wave_buffer[WAVE_SIZE];  // Conjunto de pontos da onda.

void change_freq(int* wave_freq_hz, uint32_t* wave_sample_period_us){// Cicla entre os valores de frequência
    if(*wave_freq_hz>=10){
        *wave_freq_hz=1;
    }else{
        *wave_freq_hz+=1;
    }
    float periodo_us =
        1000000.0f /
        ((float)(*wave_freq_hz) * (float)WAVE_SIZE);
    *wave_sample_period_us = (uint32_t)lroundf(periodo_us);

    if (*wave_sample_period_us < 1) {
        *wave_sample_period_us = 1;
    }
    ESP_LOGI(TAG_WAVE_FREQ, "%d Hz", *wave_freq_hz);
}

void change_amp(uint8_t* wave_amp){// Cicla entre 5 valores de amplitude entre 0 e 255
    if(*wave_amp>=255){
        *wave_amp=0;
    }else{
        *wave_amp+=51;   // = (255/5)
    }
    ESP_LOGI(TAG_WAVE_AMP, "%u", (unsigned)*wave_amp);
}

void generate_wave(int wave_type, uint8_t* wave_amp){// Recalcula o wave_buffer
    float temp_value=0;    // Usados para os cálculos com ponto flutuante.
    float alpha = (float)(*wave_amp)/((float)WAVE_SIZE/2);

    // Cálculo os pontos da curva(em float)
    for(int i=0;i<WAVE_SIZE;i++){
        switch (wave_type){
            case 0:
                // Onda quadrada
                if(i < (WAVE_SIZE/2)){
                    temp_value=0;
                }else{
                    temp_value = (float)(*wave_amp);
                }
            break;
        
            case 1:
                // Onda dente-de-serra
                temp_value = (alpha/2.0f)*(float)i;
            break;

            case 2:
                // Onda triangular
                if(i <= (WAVE_SIZE/2)){
                    // 0 < i <= N/2
                    temp_value = alpha*(float)i;
                }else{
                    // N/2 < i < N
                    temp_value = (float)(2.0f*(*wave_amp)) - alpha*(float)i;
                }
            break;
            
            case 3:
                // Onda senoidal
                temp_value = ((float)(*wave_amp)/2.0f) * sinf(2*M_PI*((float)i/(float)(WAVE_SIZE))) + ((float)(*wave_amp)/2.0f);
            break;

            default:
            break;
        }

        //  Saturação do Buffer e conversão para uint8_t
        if(temp_value < 0){
            wave_buffer[i] = 0;
        }else if(temp_value > *wave_amp){
            wave_buffer[i] = *wave_amp;
        }else{
            wave_buffer[i] = (uint8_t)(lroundf(temp_value));
        }
    }// Fim do laço

    // Escreve quando os pontos terminarem de serem calculados
    if(wave_type==0){
        ESP_LOGI(TAG_WAVE_FORM, "Quadrada");
    }else if(wave_type==1){
        ESP_LOGI(TAG_WAVE_FORM, "Dente de serra");
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
    ESP_ERROR_CHECK(dac_oneshot_new_channel(&dac_cfg, &dac_handle));

    //==========//==========//==========//==========//==========//==========//==========//==========//

    bool button1_level=false, button2_level=false, button3_level=false;
    bool button1_pressed=false, button2_pressed=false, button3_pressed=false;

    gpio_reset_pin(BUTTON1_PIN);
    gpio_set_direction(BUTTON1_PIN, GPIO_MODE_INPUT);
    gpio_pullup_en(BUTTON1_PIN);

    gpio_reset_pin(BUTTON2_PIN);
    gpio_set_direction(BUTTON2_PIN, GPIO_MODE_INPUT);
    gpio_pullup_en(BUTTON2_PIN);

    gpio_reset_pin(BUTTON3_PIN);
    gpio_set_direction(BUTTON3_PIN, GPIO_MODE_INPUT);
    gpio_pullup_en(BUTTON3_PIN);

    //==========//==========//==========//==========//==========//==========//==========//==========//
    
    int wave_type=0;    // Quadrada(0), Dente de serra(1), Triangular(2), Senóide(3).
    uint8_t wave_amp = 255; // Amplitude da onda no wave_buffer[].
    int wave_freq_hz=10;   // Frequência da onda no wave_buffer[].
    uint32_t wave_sample_period_us =
    (uint32_t)lroundf(
        1000000.0f /
        ((float)wave_freq_hz * (float)WAVE_SIZE)
    );  //  Delay entre a impressão de 2 pontos consecutivos da onda no wave_buffer[] = 1/(wave_freq_hz*WAVE_SIZE)
    if (wave_sample_period_us < 1) {
        wave_sample_period_us = 1;
    }
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
            change_freq(&wave_freq_hz, &wave_sample_period_us);
        }else if(!button3_level){
            button3_pressed = false;
        }

        dac_oneshot_output_voltage(dac_handle, wave_buffer[idx]);

        idx++;
        if(idx==WAVE_SIZE)idx=0;
        esp_rom_delay_us(wave_sample_period_us);
    }
}
