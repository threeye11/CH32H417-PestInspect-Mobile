/********************************** (C) COPYRIGHT  *******************************
* File Name          : lcd.c
* Author             : WCH
* Version            : V1.0.0
* Date               : 2025/09/17
* Description        : This file provides all the lcd firmware functions.
*********************************************************************************
* Copyright (c) 2025 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/
#include "debug.h"
#include "lcd.h"
#include "font.h"
_lcd_dev lcddev;
/* LCD brush and background colors */
vu16 POINT_COLOR=0x0000;
vu16 BACK_COLOR=0xFFFF;

/*********************************************************************
 * @fn      LCD_Reset_GPIO_Init
 *
 * @brief   Init LCD reset GPIO.
 *
 * @return  none
 */
void LCD_Reset_GPIO_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure={0};

    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOD, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(GPIOD, &GPIO_InitStructure);
    GPIO_SetBits(GPIOD,GPIO_Pin_3);
}

/*********************************************************************
 * @fn      FSMC_Init
 *
 * @brief   Init FMC
 *
 * @return  none
 */
void FMC_Init(void)
{
	FMC_NORSRAMInitTypeDef  FMC_NORSRAMInitStructure={0};
   	FMC_NORSRAMTimingInitTypeDef  readWriteTiming={0};
	FMC_NORSRAMTimingInitTypeDef  writeTiming={0};

    readWriteTiming.FMC_AddressSetupTime = 0x0f;
    readWriteTiming.FMC_AddressHoldTime = 0x0f;
    readWriteTiming.FMC_DataSetupTime = 0x3f;
    readWriteTiming.FMC_BusTurnAroundDuration = 0x0f;
    readWriteTiming.FMC_CLKDivision = 0x00;
    readWriteTiming.FMC_DataLatency = 0x00;
    readWriteTiming.FMC_AccessMode = FMC_AccessMode_A;

    writeTiming.FMC_AddressSetupTime = 0x08;
    writeTiming.FMC_AddressHoldTime = 0x08;
    writeTiming.FMC_DataSetupTime = 0x0f;
    writeTiming.FMC_BusTurnAroundDuration = 0x0f;
    writeTiming.FMC_CLKDivision = 0x00;
    writeTiming.FMC_DataLatency = 0x00;
    writeTiming.FMC_AccessMode = FMC_AccessMode_A;

    FMC_NORSRAMInitStructure.FMC_Bank = FMC_Bank1_NORSRAM1;
    
    FMC_NORSRAMInitStructure.FMC_DataAddressMux = FMC_DataAddressMux_Disable;
    FMC_NORSRAMInitStructure.FMC_MemoryType =FMC_MemoryType_SRAM;
    FMC_NORSRAMInitStructure.FMC_MemoryDataWidth = FMC_MemoryDataWidth_16b;
    FMC_NORSRAMInitStructure.FMC_BurstAccessMode =FMC_BurstAccessMode_Disable;
    FMC_NORSRAMInitStructure.FMC_WaitSignalPolarity = FMC_WaitSignalPolarity_Low;
    FMC_NORSRAMInitStructure.FMC_AsynchronousWait=FMC_AsynchronousWait_Disable;
    FMC_NORSRAMInitStructure.FMC_WaitSignalActive = FMC_WaitSignalActive_BeforeWaitState;
    FMC_NORSRAMInitStructure.FMC_WriteOperation = FMC_WriteOperation_Enable;
    FMC_NORSRAMInitStructure.FMC_WaitSignal = FMC_WaitSignal_Disable;
    FMC_NORSRAMInitStructure.FMC_ExtendedMode = FMC_ExtendedMode_Enable;
    FMC_NORSRAMInitStructure.FMC_WriteBurst = FMC_WriteBurst_Disable;
    FMC_NORSRAMInitStructure.FMC_ReadWriteTimingStruct = &readWriteTiming;
    FMC_NORSRAMInitStructure.FMC_WriteTimingStruct = &writeTiming;

    FMC_NORSRAMInit(&FMC_NORSRAMInitStructure);

	FMC_NORSRAMCmd(FMC_Bank1_NORSRAM1, ENABLE);

}

/*********************************************************************
 * @fn      LCD_GPIO_Init
 *
 * @brief   Init LCD GPIO
 *
 * @return  none
 */
void LCD_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_HBPeriphClockCmd(RCC_HBPeriph_FMC,ENABLE);
	RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOB|RCC_HB2Periph_GPIOD|RCC_HB2Periph_GPIOE|RCC_HB2Periph_AFIO,ENABLE);

 	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_14;
 	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
 	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Very_High;
 	GPIO_Init(GPIOB, &GPIO_InitStructure);


	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0|GPIO_Pin_1|GPIO_Pin_4|GPIO_Pin_5|GPIO_Pin_8|GPIO_Pin_9|GPIO_Pin_10|GPIO_Pin_14|GPIO_Pin_15;
 	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
 	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Very_High;
 	GPIO_Init(GPIOD, &GPIO_InitStructure); 
	GPIO_PinAFConfig(GPIOD,GPIO_PinSource0,GPIO_AF12 );
	GPIO_PinAFConfig(GPIOD,GPIO_PinSource1,GPIO_AF12 );
	GPIO_PinAFConfig(GPIOD,GPIO_PinSource4,GPIO_AF12 );
	GPIO_PinAFConfig(GPIOD,GPIO_PinSource5,GPIO_AF12 );
	GPIO_PinAFConfig(GPIOD,GPIO_PinSource8,GPIO_AF12 );
	GPIO_PinAFConfig(GPIOD,GPIO_PinSource9,GPIO_AF12 );
	GPIO_PinAFConfig(GPIOD,GPIO_PinSource10,GPIO_AF12 );
	GPIO_PinAFConfig(GPIOD,GPIO_PinSource14,GPIO_AF12 );
	GPIO_PinAFConfig(GPIOD,GPIO_PinSource15,GPIO_AF12 );

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7|GPIO_Pin_8|GPIO_Pin_9|GPIO_Pin_10|GPIO_Pin_11|GPIO_Pin_12|GPIO_Pin_13|GPIO_Pin_14|GPIO_Pin_15;
 	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
 	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Very_High;
 	GPIO_Init(GPIOE, &GPIO_InitStructure); 
	GPIO_PinAFConfig(GPIOE,GPIO_PinSource7,GPIO_AF12 );
	GPIO_PinAFConfig(GPIOE,GPIO_PinSource8,GPIO_AF12 );
	GPIO_PinAFConfig(GPIOE,GPIO_PinSource9,GPIO_AF12 );
	GPIO_PinAFConfig(GPIOE,GPIO_PinSource10,GPIO_AF12 );
	GPIO_PinAFConfig(GPIOE,GPIO_PinSource11,GPIO_AF12 );
	GPIO_PinAFConfig(GPIOE,GPIO_PinSource12,GPIO_AF12 );
	GPIO_PinAFConfig(GPIOE,GPIO_PinSource13,GPIO_AF12 );
	GPIO_PinAFConfig(GPIOE,GPIO_PinSource14,GPIO_AF12 );
	GPIO_PinAFConfig(GPIOE,GPIO_PinSource15,GPIO_AF12 );
	
    /*   RS:PD12  */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(GPIOD, &GPIO_InitStructure);
	GPIO_PinAFConfig(GPIOD,GPIO_PinSource12,GPIO_AF12 );

    /* CS: PD11*/
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(GPIOD, &GPIO_InitStructure);
    GPIO_ResetBits(GPIOD,GPIO_Pin_11);
}


/*********************************************************************
 * @fn      LCD_WR_REG
 *
 * @brief   Write register
 *
 * @param   regval - register value
 *
 * @return  none
 */
void LCD_WR_REG(vu16 regval)
{
        regval = regval;		
    LCD->LCD_REG = regval;
}

/*********************************************************************
 * @fn      LCD_WR_DATA
 *
 * @brief   Write data
 *
 * @param   data
 *
 * @return  none
 */
void LCD_WR_DATA(vu16 data)
{
    LCD->LCD_RAM = data;
}

/*********************************************************************
 * @fn      LCD_RD_DATA
 *
 * @brief   Read data
 *
 * @return  ram - read data
 */
u16 LCD_RD_DATA(void)
{
    vu16 ram;                  
    ram=LCD->LCD_RAM;
    return ram;	 
}

/*********************************************************************
 * @fn      LCD_WriteReg
 *
 * @brief   Write value to register
 *
 * @param   LCD_Reg - register addr
 *          LCD_RegValue - value
 *
 * @return  none
 */
void LCD_WriteReg(u16 LCD_Reg, u16 LCD_RegValue)
{
    LCD->LCD_REG = LCD_Reg;
    LCD->LCD_RAM = LCD_RegValue;
}

/*********************************************************************
 * @fn      LCD_ReadReg
 *
 * @brief   Read value from register
 *
 * @param   LCD_Reg - register addr
 *
 * @return  register value
 */
u16 LCD_ReadReg(u16 LCD_Reg)
{ 
    LCD_WR_REG(LCD_Reg);
    Delay_Us(5);
	return LCD_RD_DATA();
}

/*********************************************************************
 * @fn      LCD_WriteRAM
 *
 * @brief   Write GRAM
 *
 * @param   RGB_Code - colour value
 *
 * @return  none
 */
void LCD_WriteRAM(u16 RGB_Code)
{
    LCD->LCD_RAM = RGB_Code;
}

/*********************************************************************
 * @fn      LCD_WriteRAM_Prepare
 *
 * @brief   Write GRAM prepare
 *
 * @return  none
 */
void LCD_WriteRAM_Prepare(void)
{
    LCD->LCD_REG = lcddev.wramcmd;
}

/*********************************************************************
 * @fn      LCD_BGR2RGB
 *
 * @brief   The data read from ILI93xx is in GBR format, and we write it in RGB format.
 *
 * @param   c-Color value in GBR format
 *
 * @return  rgb-RGB format color value
 */
u16 LCD_BGR2RGB(u16 c)
{
    u16  r, g, b, rgb;
    b = (c >> 0) & 0x1f;
    g = (c >> 5) & 0x3f;
    r = (c >> 11) & 0x1f;
    rgb = (b << 11) + (g << 5) + (r << 0);
    return(rgb);
}

/*********************************************************************
 * @fn      LCD_ReadPoint
 *
 * @brief   Read the color value of a certain point.
 *
 * @param   x-x-axis coordinate
 *          y-y-axis coordinate
 *
 * @return  The color of this point
 */
u16 LCD_ReadPoint(u16 x,u16 y)
{
    u16 r = 0, g = 0, b = 0;
    if(x >= lcddev.width || y >= lcddev.height)
        return 0;   //Out of range, return directly
    LCD_SetCursor(x, y);
    LCD_WR_REG(0X2E);

    r = LCD_RD_DATA();          

    r = LCD_RD_DATA();          //Actual coordinate color

    //9341/5310/5510/7789   To be read out in 2 parts;
    b = LCD_RD_DATA();
    g = r & 0XFF;               // 9341/5310/5510/7789, The first read is the value of RG, R first, G last, each occupying 8 bits.
    g <<= 8;
    return (((r >> 11) << 11) | ((g >> 10) << 5) | (b >> 11));  // 9341/5310/5510/7789  Need to convert the formula.
}

/*********************************************************************
 * @fn      LCD_DisplayOn
 *
 * @brief   LCD on display.
 *
 * @param   None
 *         
 * @return  None
 */
void LCD_DisplayOn(void)
{
    LCD_WR_REG(0X29);       //Turn on display
}

/*********************************************************************
 * @fn      LCD_DisplayOff
 *
 * @brief   LCD off display.
 *
 * @param   None
 *         
 * @return  None
 */
void LCD_DisplayOff(void)
{
    LCD_WR_REG(0X28);       
}

/*********************************************************************
 * @fn      LCD_SetCursor
 *
 * @brief   Set the cursor position.
 *
 * @param   Xpos-x-axis coordinate
 *          Ypos-y-axis coordinate
 *         
 * @return  None
 */
void LCD_SetCursor(u16 Xpos, u16 Ypos)
{
        LCD_WR_REG(lcddev.setxcmd);
        LCD_WR_DATA(Xpos >> 8);
        LCD_WR_DATA(Xpos & 0XFF);
        LCD_WR_REG(lcddev.setycmd);
        LCD_WR_DATA(Ypos >> 8);
        LCD_WR_DATA(Ypos & 0XFF);
}

/*********************************************************************
 * @fn      LCD_Scan_Dir
 *
 * @brief   Set the automatic scanning direction of the LCD.
 *
 * @param   None
 *         
 * @return  None
 */
void LCD_Scan_Dir(u8 dir)
{
    u16 regval = 0;
    u16 dirreg = 0;

    switch (dir)
    {
        case L2R_U2D://From left to right, from top to bottom
            regval |= (0 << 7) | (0 << 6) | (0 << 5);
            break;

        case L2R_D2U://From left to right, from bottom to top
            regval |= (1 << 7) | (0 << 6) | (0 << 5);
            break;

        case R2L_U2D://From right to left, from top to bottom
            regval |= (0 << 7) | (1 << 6) | (0 << 5);
            break;

        case R2L_D2U://From right to left, from bottom to top
            regval |= (1 << 7) | (1 << 6) | (0 << 5);
            break;

        case U2D_L2R://Top to bottom, left to right
            regval |= (0 << 7) | (0 << 6) | (1 << 5);
            break;

        case U2D_R2L://Top to bottom, right to left
            regval |= (0 << 7) | (1 << 6) | (1 << 5);
            break;

        case D2U_L2R://From bottom to top, from left to right
            regval |= (1 << 7) | (0 << 6) | (1 << 5);
            break;

        case D2U_R2L://From bottom to top, from right to left
            regval |= (1 << 7) | (1 << 6) | (1 << 5);
            break;
    }

    dirreg = 0X36;
    regval |= 0X08;
    
    LCD_WriteReg(dirreg, regval);

    LCD_WR_REG(lcddev.setxcmd);
    LCD_WR_DATA(0);
    LCD_WR_DATA(0);
    LCD_WR_DATA((lcddev.width - 1) >> 8);
    LCD_WR_DATA((lcddev.width - 1) & 0XFF);
    LCD_WR_REG(lcddev.setycmd);
    LCD_WR_DATA(0);
    LCD_WR_DATA(0);
    LCD_WR_DATA((lcddev.height - 1) >> 8);
    LCD_WR_DATA((lcddev.height - 1) & 0XFF);
}

/*********************************************************************
 * @fn      LCD_DrawPoint
 *
 * @brief   Draw a point at the designated location
 *
 * @param   x,y    - Draw the coordinates of the points
 *          color  - Points Color
 *
 * @return  none
 */
void LCD_DrawPoint(u16 x, u16 y)
{
    LCD_SetCursor(x, y);        
    LCD_WriteRAM_Prepare(); 
    LCD->LCD_RAM = POINT_COLOR;
}

/*********************************************************************
 * @fn      LCD_Fast_DrawPoint
 *
 * @brief   Quick dot drawing
 *
 * @param   x,y    - Draw the coordinates of the points
 *          color  - Points Color
 *
 * @return  none
 */
void LCD_Fast_DrawPoint(u16 x, u16 y, u16 color)
{

    LCD_WR_REG(lcddev.setxcmd);
    LCD_WR_DATA(x >> 8);
    LCD_WR_DATA(x & 0XFF);

    LCD_WR_REG(lcddev.setycmd);
    LCD_WR_DATA(y >> 8);
    LCD_WR_DATA(y & 0XFF);

    LCD->LCD_REG=lcddev.wramcmd;
    LCD->LCD_RAM=color;
}

/*********************************************************************
 * @fn      LCD_SSD_BackLightSet
 *
 * @brief   SSD1963 backlight setting.
 *
 * @param   pwm - Rating of backlight:0~100. The brighter the better
 *
 * @return  none
 */
void LCD_SSD_BackLightSet(u8 pwm)
{
    LCD_WR_REG(0xBE);   //Configure PWM output
    LCD_WR_DATA(0x05);  //Set PWM frequency
    LCD_WR_DATA(pwm * 2.55);//Set PWM duty cycle
    LCD_WR_DATA(0x01);  
    LCD_WR_DATA(0xFF);  
    LCD_WR_DATA(0x00);  
    LCD_WR_DATA(0x00);  
}

/*********************************************************************
 * @fn      LCD_Display_Dir
 *
 * @brief   Set LCD display orientation
 *
 * @param   dir - 0:Portrait; 1:landscape
 *
 * @return  none
 */
void LCD_Display_Dir(u8 dir)
{
    lcddev.dir = dir;      

    if (dir == 0)        
    {
        lcddev.width = 240;
        lcddev.height = 320;

        lcddev.wramcmd = 0X2C;
        lcddev.setxcmd = 0X2A;
        lcddev.setycmd = 0X2B;
    }
    else    
    {
        lcddev.width = 320;
        lcddev.height = 240;

        lcddev.wramcmd = 0X2C;
        lcddev.setxcmd = 0X2A;
        lcddev.setycmd = 0X2B;
    }

    if (dir == 0)
        LCD_Scan_Dir(L2R_U2D);   // ????????跽??
    else
        LCD_Scan_Dir(U2D_R2L);   // ????????跽??
    // LCD_Scan_Dir(DFT_SCAN_DIR);     
}

/*********************************************************************
 * @fn      LCD_Set_Window
 *
 * @brief   Set the window, and automatically set the coordinates of the dot to the upper left corner of the window (sx, sy).
 *
 * @param   
 *          sx - Window start coordinates X
 *          sy - Window start coordinates Y
 *          width  - Window width
 *          height - Window height
 * @return  none
 */
void LCD_Set_Window(u16 sx,u16 sy,u16 width,u16 height)
{
    // u8 hsareg, heareg, vsareg, veareg;
    // u16 hsaval, heaval, vsaval, veaval;
    u16 twidth, theight;
    twidth = sx + width - 1;
    theight = sy + height - 1;

    LCD_WR_REG(lcddev.setxcmd);
    LCD_WR_DATA(sx >> 8);
    LCD_WR_DATA(sx & 0XFF);
    LCD_WR_DATA(twidth >> 8);
    LCD_WR_DATA(twidth & 0XFF);
    LCD_WR_REG(lcddev.setycmd);
    LCD_WR_DATA(sy >> 8);
    LCD_WR_DATA(sy & 0XFF);
    LCD_WR_DATA(theight >> 8);
    LCD_WR_DATA(theight & 0XFF);
}


/*********************************************************************
 * @fn      LCD_Init
 *
 * @brief   Init LCD
 *
 * @return  none
 */
void LCD_Init(void)
{
    //LCD reset
    LCD_Reset_GPIO_Init();
	GPIO_ResetBits(GPIOD,GPIO_Pin_3);
	Delay_Ms(100);
	GPIO_SetBits(GPIOD,GPIO_Pin_3);

    LCD_GPIO_Init();
    FMC_Init();

    LCD_WR_REG(0XD3);
    __disable_irq();
    *(vu16*)0x60040000;
    *(vu16*)0x60040000;
    lcddev.id = *(vu16*)0x60040000;
    lcddev.id <<= 8;
    lcddev.id |= *(vu16*)0x60040000;
    __enable_irq();

    printf("LCD ID: 0x%04X\r\n", lcddev.id);

    LCD_WR_REG(0xCF);
        LCD_WR_DATA(0x00);
        LCD_WR_DATA(0xC1);
        LCD_WR_DATA(0X30);
        LCD_WR_REG(0xED);
        LCD_WR_DATA(0x64);
        LCD_WR_DATA(0x03);
        LCD_WR_DATA(0X12);
        LCD_WR_DATA(0X81);
        LCD_WR_REG(0xE8);
        LCD_WR_DATA(0x85);
        LCD_WR_DATA(0x10);
        LCD_WR_DATA(0x7A);
        LCD_WR_REG(0xCB);
        LCD_WR_DATA(0x39);
        LCD_WR_DATA(0x2C);
        LCD_WR_DATA(0x00);
        LCD_WR_DATA(0x34);
        LCD_WR_DATA(0x02);
        LCD_WR_REG(0xF7);
        LCD_WR_DATA(0x20);
        LCD_WR_REG(0xEA);
        LCD_WR_DATA(0x00);
        LCD_WR_DATA(0x00);
        LCD_WR_REG(0xC0);    //Power control
        LCD_WR_DATA(0x1B);   //VRH[5:0]
        LCD_WR_REG(0xC1);    //Power control
        LCD_WR_DATA(0x01);   //SAP[2:0];BT[3:0]
        LCD_WR_REG(0xC5);    //VCM control
        LCD_WR_DATA(0x30);   //3F
        LCD_WR_DATA(0x30);   //3C
        LCD_WR_REG(0xC7);    //VCM control2
        LCD_WR_DATA(0XB7);
        LCD_WR_REG(0x36);    // Memory Access Control
        LCD_WR_DATA(0x48);
        LCD_WR_REG(0x3A);
        LCD_WR_DATA(0x55);
        LCD_WR_REG(0xB1);
        LCD_WR_DATA(0x00);
        LCD_WR_DATA(0x1A);
        LCD_WR_REG(0xB6);    // Display Function Control
        LCD_WR_DATA(0x0A);
        LCD_WR_DATA(0xA2);
        LCD_WR_REG(0xF2);    // 3Gamma Function Disable
        LCD_WR_DATA(0x00);
        LCD_WR_REG(0x26);    //Gamma curve selected
        LCD_WR_DATA(0x01);
        LCD_WR_REG(0xE0);    //Set Gamma
        LCD_WR_DATA(0x0F);
        LCD_WR_DATA(0x2A);
        LCD_WR_DATA(0x28);
        LCD_WR_DATA(0x08);
        LCD_WR_DATA(0x0E);
        LCD_WR_DATA(0x08);
        LCD_WR_DATA(0x54);
        LCD_WR_DATA(0XA9);
        LCD_WR_DATA(0x43);
        LCD_WR_DATA(0x0A);
        LCD_WR_DATA(0x0F);
        LCD_WR_DATA(0x00);
        LCD_WR_DATA(0x00);
        LCD_WR_DATA(0x00);
        LCD_WR_DATA(0x00);
        LCD_WR_REG(0XE1);    //Set Gamma
        LCD_WR_DATA(0x00);
        LCD_WR_DATA(0x15);
        LCD_WR_DATA(0x17);
        LCD_WR_DATA(0x07);
        LCD_WR_DATA(0x11);
        LCD_WR_DATA(0x06);
        LCD_WR_DATA(0x2B);
        LCD_WR_DATA(0x56);
        LCD_WR_DATA(0x3C);
        LCD_WR_DATA(0x05);
        LCD_WR_DATA(0x10);
        LCD_WR_DATA(0x0F);
        LCD_WR_DATA(0x3F);
        LCD_WR_DATA(0x3F);
        LCD_WR_DATA(0x0F);
        LCD_WR_REG(0x2B);
        LCD_WR_DATA(0x00);
        LCD_WR_DATA(0x00);
        LCD_WR_DATA(0x01);
        LCD_WR_DATA(0x3f);
        LCD_WR_REG(0x2A);
        LCD_WR_DATA(0x00);
        LCD_WR_DATA(0x00);
        LCD_WR_DATA(0x00);
        LCD_WR_DATA(0xef);
        LCD_WR_REG(0x11); //Exit Sleep
        Delay_Ms(120);
        LCD_WR_REG(0x29); //display on

    LCD_Display_Dir(0);
    GPIO_SetBits(GPIOB, GPIO_Pin_14);
    LCD_Clear(WHITE);

}

/*********************************************************************
 * @fn      LCD_SetXY
 *
 * @brief   Set the start point of LCD display
 *
 * @param    x - X coordinate
 *           y - Y coordinate
 *
 * @return  none
 */
void LCD_SetXY(u16 x, u16 y)
{
    LCD_SetCursor(x, y);
}

/*********************************************************************
 * @fn      Gui_DrawPoint
 *
 * @brief   Draw a dot
 *
 * @param    x - X coordinate
 *           y - Y coordinate
 *           color - color
 * @return  none
 */
void Gui_DrawPoint(u16 x, u16 y, u16 color)
{

    LCD_WR_REG(lcddev.setxcmd);
    LCD_WR_DATA(x >> 8);
    LCD_WR_DATA(x & 0XFF);
    LCD_WR_REG(lcddev.setycmd);
    LCD_WR_DATA(y >> 8);
    LCD_WR_DATA(y & 0XFF);
    
    LCD->LCD_REG = lcddev.wramcmd;
    LCD->LCD_RAM = color;
}

/*********************************************************************
 * @fn      Gui_DrawPoint
 *
 * @brief   clear screen 
 *
 * @param   color - Filling color
 *           
 * @return  none
 */
void LCD_Clear(u16 color)
{
    u32 index = 0;
    u32 totalpoint = lcddev.width;
    totalpoint *= lcddev.height;    

    LCD_SetCursor(0x00, 0x0000);    
    LCD_WriteRAM_Prepare();         

    for (index = 0; index < totalpoint; index++)
    {
        LCD->LCD_RAM=color;
    }
}

/*********************************************************************
 * @fn      LCD_Fill
 *
 * @brief   Fill color in designated area
 *
 * @param   xsta,ysta - Starting coordinates
 *          xend,yend - Termination coordinates
 *          color     - The color to be filled
 *
 * @return  none
 */
void LCD_Fill(u16 sx, u16 sy, u16 ex, u16 ey, u16 color)
{

    u16 i, j;
    u16 xlen = 0;

    xlen = ex - sx + 1;

    for (i = sy; i <= ey; i++)
    {
        LCD_SetCursor(sx, i);       
        LCD_WriteRAM_Prepare();     

        for (j = 0; j < xlen; j++)
        {
            LCD->LCD_RAM=color;     
        }
    }
}

//??????????????????????
//(sx,sy),(ex,ey):?????ζ??????,?????С?:(ex-sx+1)*(ey-sy+1)   
//color:????????
void LCD_Color_Fill(u16 sx,u16 sy,u16 ex,u16 ey,u16 *color)
{
    u16 height, width;
    u16 i, j;
    width = ex - sx + 1;
    height = ey - sy + 1;

    for (i = 0; i < height; i++)
    {
        LCD_SetCursor(sx, sy + i);
        LCD_WriteRAM_Prepare();

        for (j = 0; j < width; j++)
        {
            LCD_WR_DATA(color[i * width + j]);
        }
    }
}

/*********************************************************************
 * @fn      LCD_DrawLine
 *
 * @brief   Draw a line
 *
 * @param   x1,y1 - Starting coordinates
 *          x2,y2 - Termination coordinates
 *          color - Line color
 *
 * @return  none
 */
void LCD_DrawLine(u16 x1, u16 y1, u16 x2, u16 y2)
{
    u16 t;
    int xerr = 0, yerr = 0, delta_x, delta_y, distance;
    int incx, incy, uRow, uCol;
    delta_x = x2 - x1;             
    delta_y = y2 - y1;
    uRow = x1;
    uCol = y1;

    if (delta_x > 0)incx = 1;      
    else if (delta_x == 0)incx = 0; 
    else
    {
        incx = -1;
        delta_x = -delta_x;
    }

    if (delta_y > 0)incy = 1;
    else if (delta_y == 0)incy = 0; 
    else
    {
        incy = -1;
        delta_y = -delta_y;
    }

    if ( delta_x > delta_y)distance = delta_x; 
    else distance = delta_y;

    for (t = 0; t <= distance + 1; t++ )    
    {
        LCD_DrawPoint(uRow, uCol); 
        xerr += delta_x ;
        yerr += delta_y ;

        if (xerr > distance)
        {
            xerr -= distance;
            uRow += incx;
        }

        if (yerr > distance)
        {
            yerr -= distance;
            uCol += incy;
        }
    }
}

/*********************************************************************
 * @fn      LCD_DrawRectangle
 *
 * @brief   Draw a rectangle
 *
 * @param   x1,y1 - Starting coordinates
 *          x2,y2 - Termination coordinates
 *         
 *
 * @return  none
 */
void LCD_DrawRectangle(u16 x1, u16 y1, u16 x2, u16 y2)
{
    LCD_DrawLine(x1, y1, x2, y1);
    LCD_DrawLine(x1, y1, x1, y2);
    LCD_DrawLine(x1, y2, x2, y2);
    LCD_DrawLine(x2, y1, x2, y2);
}

/*********************************************************************
 * @fn      Draw_Circle
 *
 * @brief   Draw a circle
 *
 * @param   x0,y0 - Center coordinates
 *          r     - radius
 *          
 *
 * @return  none
 */
void LCD_Draw_Circle(u16 x0, u16 y0, u8 r)
{
    int a, b;
    int di;
    a = 0;
    b = r;
    di = 3 - (r << 1);             
    while(a <= b)
    {
        LCD_DrawPoint(x0 + a, y0 - b);             //5
        LCD_DrawPoint(x0 + b, y0 - a);             //0
        LCD_DrawPoint(x0 + b, y0 + a);             //4
        LCD_DrawPoint(x0 + a, y0 + b);             //6
        LCD_DrawPoint(x0 - a, y0 + b);             //1
        LCD_DrawPoint(x0 - b, y0 + a);
        LCD_DrawPoint(x0 - a, y0 - b);             //2
        LCD_DrawPoint(x0 - b, y0 - a);             //7
        a++;

        if(di < 0)
            di += 4 * a + 6;
        else
        {
            di += 10 + 4 * (a - b);
            b--;
        }
    }
}

/*********************************************************************
 * @fn      LCD_ShowString
 *
 * @brief   Display string
 *
 * @param   x   - X coordinate
 *          y   - Y coordinate
 *          num -Characters to be displayed
 *          size- font size
 *          mode- Overlapping method
 *
 * @return  none
 */
void LCD_ShowChar(u16 x, u16 y, u8 num, u8 size, u8 mode)
{
    u8 temp, t1, t;
    u16 y0 = y;
    u8 csize = (size / 8 + ((size % 8) ? 1 : 0)) * (size / 2);      
    num = num - ' ';
    for(t = 0; t < csize; t++)
    {
        if(size == 12)
            temp = asc2_1206[num][t];       
        else if(size == 16)
            temp = asc2_1608[num][t];   
        else if(size == 24)
            temp = asc2_2412[num][t];   
        else
            return;                             
        for(t1 = 0; t1 < 8; t1++)
        {
            if(temp & 0x80)
                LCD_Fast_DrawPoint(x, y, POINT_COLOR);
            else if(mode==0)
                LCD_Fast_DrawPoint(x, y, BACK_COLOR);
            temp <<= 1;
            y++;
            if(y >= lcddev.height)
                return;     
            if((y - y0) == size)
            {
                y = y0;
                x++;
                if(x >= lcddev.width)
                    return; 
                break;
            }
        }
    }
}

/*********************************************************************
 * @fn      LCD_Pow
 *
 * @brief   m^n function
 *
 * @return  result - m^n
 */
u32 LCD_Pow(u8 m, u8 n)
{
    u32 result = 1;
    while(n--)
        result *= m;
    return result;
}

/*********************************************************************
 * @fn      LCD_ShowNum
 *
 * @brief   Display number
 *
 * @param   x - X coordinate
 *          y - Y coordinate
 *          len - number lenth
 *          size -font size
 *          color - font color
 *          num - value(0~4294967295)
 *
 * @return  none
 */
void LCD_ShowNum(u16 x, u16 y, u32 num, u8 len, u8 size)
{
    u8 t, temp;
    u8 enshow = 0;
    for(t = 0;t < len; t++)
    {
        temp = (num / LCD_Pow(10, len - t - 1)) % 10;
        if(enshow == 0 && t < (len - 1))
        {
            if(temp == 0)
            {
                LCD_ShowChar(x + (size / 2) * t, y, ' ', size, 0);
                continue;
            }
            else
                enshow = 1;

        }
        LCD_ShowChar(x + (size / 2) * t, y, temp + '0', size, 0);
    }
}

/*********************************************************************
 * @fn      LCD_ShowNum
 *
 * @brief   Display number
 *
 * @param   x - X coordinate
 *          y - Y coordinate
 *          len - number lenth
 *          size -font size
 *          color - font color
 *          num - value(0~4294967295)
 *          mode[7]-0, no fill; 1, fill 0. [6:1]: keep, [0]: 0, no overlay display; , overlay display.
 *
 * @return  none
 */
void LCD_ShowxNum(u16 x, u16 y, u32 num, u8 len, u8 size, u8 mode)
{
    u8 t, temp;
    u8 enshow = 0;
    for(t = 0;t < len; t++)
    {
        temp = (num / LCD_Pow(10, len - t - 1)) % 10;
        if(enshow == 0 && t < (len - 1))
        {
            if(temp == 0)
            {
                if(mode & 0X80)
                    LCD_ShowChar(x + (size / 2) * t, y, '0', size, mode & 0X01);
                else
                    LCD_ShowChar(x + (size / 2) * t, y, ' ', size, mode & 0X01);
                continue;
            }
            else
                enshow = 1;

        }
        LCD_ShowChar(x + (size / 2) * t, y, temp + '0', size, mode & 0X01);
    }
}

/*********************************************************************
 * @fn      LCD_ShowString
 *
 * @brief   Display string
 *
 * @param   x - X coordinate
 *          y - Y coordinate
 *          width - String width
 *          height - String height
 *          size - font size
 *          *p - String start address
 *
 * @return  none
 */
void LCD_ShowString(u16 x, u16 y, u16 width, u16 height, u8 size, u8 *p)
{
    u8 x0 = x;
    width += x;
    height += y;
    while((*p <= '~') && (*p >= ' '))
    {
        if(x >= width)
        {
            x = x0;
            y += size;
        }
        if(y >= height)
            break;
        LCD_ShowChar(x, y, *p, size, 0);
        x += size / 2;
        p++;
    }
}
