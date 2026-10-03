#include "project.h"
#include <stdio.h>

int main(void)
{
    uint16 slidervalue;
    uint16 oldslidervalue = 0xFFFF;
    char msg[16];
    
    CyGlobalIntEnable;
    
   // UART_Init();
    
    __enable_irq();
    UART_Start();
    CapSense_Start();
    CapSense_ScanAllWidgets();
    UART_UartPutString("Started\n");

    
    for(;;)
    {
        if(!CapSense_IsBusy())
        {   
            CapSense_ProcessAllWidgets();
            slidervalue = CapSense_GetCentroidPos(CapSense_LINEARSLIDER0_WDGT_ID);
            
            if (slidervalue != 0xFFFF)
            {   
                // turning off all leds after scrolling a slider
                LED_Slider_1_Write(1);
                LED_Slider_2_Write(1);
                LED_Slider_3_Write(1);
                LED_Slider_4_Write(1);
                LED_Slider_5_Write(1);
                
                if (slidervalue > 80)
                    LED_Slider_1_Write(0); 
                else if (slidervalue > 60)
                    LED_Slider_2_Write(0); 
                else if (slidervalue > 40)
                    LED_Slider_3_Write(0);
                else if (slidervalue > 20)
                    LED_Slider_4_Write(0);
                else if (slidervalue > 0)
                    LED_Slider_5_Write(0);
            }
            else
            {
                // if no capsense touched - all led are turn off
                LED_Slider_1_Write(1);
                LED_Slider_2_Write(1);
                LED_Slider_3_Write(1);
                LED_Slider_4_Write(1);
                LED_Slider_5_Write(1);
            }
            
            // output via uart
            if(slidervalue != 0xFFFF && slidervalue != oldslidervalue)
            {
                sprintf(msg, "Pos: %d\r\n", slidervalue);
                UART_UartPutString(msg);
                CyDelay(10);
                oldslidervalue = slidervalue;
            }

            CapSense_ScanAllWidgets();
        }
    }
}