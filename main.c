#include <REGX52.H>
#include <INTRINS.H>
#include "lcd1602.h"    
#include "matrixkey.h"  
#include "delay.h"      
#include "beep.h"       
#include "timer0.h"     // 核心文件，提供 T0_Count_ms 计数
#include <string.h>

// ----------------------------------------------------
// ## 1. 函数原型声明
// ----------------------------------------------------
bit validate_password(); 
void control_lock(bit status); 
void Display_Scroll_Text();

// 【串口通信原型】
void UART_Init(void);         
void UART_SendChar(char dat); 
void UART_SendString(char *s); 
void UART_SendStatus(void);    // 发送状态函数
void UART_ISR(void);           

// ----------------------------------------------------
// ## 2. 全局变量定义
// ----------------------------------------------------

// 【滚屏变量】
unsigned char *ScrollText = "    Please Enter Password...    "; 
unsigned char ScrollIndex = 0;              
const unsigned char ScrollLength = 16;      
const unsigned char FullLength = 32;        
bit is_scrolling = 1; 

// 【安全功能变量】
unsigned char error_count = 0; 
bit lock_down = 0;             
unsigned long Alarm_Start_Time = 0; 
unsigned long Lock_Start_Time = 0;  
bit alarm_active = 0;              
unsigned long Last_Status_Send_Time = 0; 

// 【成功提示音变量】
bit success_beep_active = 0;               
unsigned long Success_Beep_Start_Time = 0; 

// 【密码/输入变量】
#define MAX_PASSWORD_LENGTH 8  
unsigned char password[MAX_PASSWORD_LENGTH] = {'1', '2', '3', '4'};  
unsigned char input[MAX_PASSWORD_LENGTH];  
unsigned char input_index = 0;  
bit lock_status = 0; // 0: Locked, 1: Unlocked

// 【串口接收变量】
#define RX_BUFFER_SIZE 10
unsigned char RX_Buffer[RX_BUFFER_SIZE]; 
unsigned char RX_Index = 0;              


// ----------------------------------------------------
// ## 3. 串口初始化和中断服务
// ----------------------------------------------------

void UART_Init(void) {
    PCON &= 0x7F;		
    SCON = 0x50;		
    TMOD &= 0x0F;		
    TMOD |= 0x20;		
    TL1 = 0xFD;			
    TH1 = 0xFD;			
    TR1 = 1;			
    EA = 1;				
    ES = 1;				
}

void UART_SendChar(char dat) {
    SBUF = dat;
    while (TI == 0); 
    TI = 0;          
}

void UART_SendString(char *s) {
    while (*s) {
        UART_SendChar(*s++);
    }
}

// 发送系统状态帧函数 (S_锁状态_错误数#)
void UART_SendStatus(void) {
    UART_SendString("S_");

    // 1. 发送锁状态: L (Locked), U (Unlocked), D (LockDown)
    if (lock_down == 1) {
        UART_SendChar('D'); 
    } else if (lock_status == 0) { 
        UART_SendChar('L'); 
    } else {
        UART_SendChar('U'); 
    }
    
    // 2. 发送分隔符
    UART_SendChar('_'); 
    
    // 3. 发送错误/自锁计数
    if (lock_down == 1) {
        UART_SendChar('3'); // 自锁时固定发送 '3'
    } else {
        UART_SendChar(error_count + '0'); 
    }
    
    // 4. 发送结束符
    UART_SendChar('#'); 
}


// 串口中断服务程序 (这里是定义，包含 interrupt 和 using 关键字)
void UART_ISR(void) interrupt 4 using 1 {
    if (RI) {
        unsigned char received_data = SBUF;  
        RI = 0;

        // 1. 处理起始符 'P'
        if (RX_Index == 0) {
            if (received_data == 'P') {
                RX_Buffer[RX_Index++] = received_data;
            }
        } 
        // 2. 接收中间数据
        else if (RX_Index > 0 && RX_Index < RX_BUFFER_SIZE - 1) {
            RX_Buffer[RX_Index++] = received_data;

            // 3. 处理结束符 '#' (协议: P + 4位密码 + #, 总长度 6)
            if (received_data == '#') {
                
                if (RX_Buffer[0] == 'P' && RX_Index == 6) { 
                    
                    input[0] = RX_Buffer[1] - '0';
                    input[1] = RX_Buffer[2] - '0';
                    input[2] = RX_Buffer[3] - '0';
                    input[3] = RX_Buffer[4] - '0';
                    input_index = 4; 

                    // 4. 调用验证函数
                    if (validate_password()) {
                        UART_SendChar('S');
                        lock_status = !lock_status; 
                        control_lock(lock_status);
                        success_beep_active = 1;
                        Success_Beep_Start_Time = T0_Count_ms;
                    } else {
                        UART_SendChar('F');
                    }
                    
                    // 远程命令处理完毕，主动发送一次状态更新
                    UART_SendStatus(); 
                }
                
                // 5. 清除 input 缓冲区，防止与键盘输入冲突
                memset(input, 0, sizeof(input)); 
                input_index = 0;
                
                // 6. 命令处理完毕，重置接收索引
                RX_Index = 0;
            }
        } 
        // 7. 溢出或无效字符序列，强制重置
        else {
            RX_Index = 0;
        }
    }
}


// ----------------------------------------------------
// ## 4. 项目功能函数
// ----------------------------------------------------

bit validate_password() {
    unsigned char i;
    for (i = 0; i < 4; i++) {
        if (input[i] != (password[i] - '0')) { 
            return 0; 
        }
    }
    return 1;
}

void control_lock(bit status) {
    if (status) { 
        P2_1 = 0;  P2_0 = 1;  
        LCD_ShowString(2, 1, "UNLOCKED        "); 
    } else { 
        P2_1 = 1;  P2_0 = 0;  
        LCD_ShowString(2, 1, "LOCKED          "); 
    }
}

void Display_Scroll_Text() {
    if (is_scrolling) {
        LCD_ShowString(2, 1, ScrollText + ScrollIndex); 
        
        ScrollIndex++;
        if (ScrollIndex >= (FullLength - ScrollLength)) { 
            ScrollIndex = 0; 
        }
    }
}


// ----------------------------------------------------
// ## 5. 主程序
// ----------------------------------------------------

void main() {
    unsigned char key;
    unsigned int scroll_timer = 0; 

    // 初始化所有外设
    LCD_Init();         
    MatrixKey_Init();   
    Beep_Init();        
    Timer0_Init();      
    UART_Init();        
    
    P2 = 0xFF; 
    
    LCD_ShowString(1, 1, "Welcome Home!   ");
    is_scrolling = 1; 

    while (1) {
        
        // 成功提示音/报警控制逻辑
        if (success_beep_active) {
            if (T0_Count_ms - Success_Beep_Start_Time >= 3000) {
                Beep_Off(); success_beep_active = 0;
            } else {
                if (T0_Count_ms % 2 == 0) { Beep_On(); } else { Beep_Off(); }
            }
        }
        
        else if (alarm_active) { 
            if (T0_Count_ms - Alarm_Start_Time >= 3000) { 
                Beep_Off(); P2_2 = 1; alarm_active = 0;
            } else {
                if (T0_Count_ms % 4 < 2) { Beep_On(); } else { Beep_Off(); }
                if (T0_Count_ms % 200 < 100) { P2_2 = 0; } else { P2_2 = 1; }
            }
        }
        
        // 锁定解除逻辑
        if (lock_down == 1) {
            if (T0_Count_ms - Lock_Start_Time >= 3000) { 
                lock_down = 0; error_count = 0;    
                LCD_ShowString(1, 1, "Welcome Home!   "); is_scrolling = 1; 
            }
            LCD_ShowString(1, 1, "SECURITY LOCKOUT");
            LCD_ShowString(2, 1, "Try Later...    ");
        }

        // 滚屏控制
        if (lock_down == 0 && is_scrolling == 1) { 
             scroll_timer++;
             if (scroll_timer >= 5000) { 
                 Display_Scroll_Text();
                 scroll_timer = 0;
             }
        }
        
        // 状态定时上报逻辑 (每 1000ms 报告一次)
        if (T0_Count_ms - Last_Status_Send_Time >= 1000) { 
            UART_SendStatus();
            Last_Status_Send_Time = T0_Count_ms;
        }
        
        // 按键处理
        key = MatrixKey_Scan();
        
        if (lock_down == 1) { continue; }
        
        if (key != 0) {
            
            if (is_scrolling == 1) {
                is_scrolling = 0; 
                LCD_ShowString(1, 1, "Welcome Home!   ");
                LCD_ShowString(2, 1, "                "); 
            }
            
            if (key == 15) { // 确认键 (S15)
                
                if (validate_password()) {
                    error_count = 0; lock_status = !lock_status; 
                    LCD_ShowString(1, 1, "Password Correct");
                    control_lock(lock_status); is_scrolling = 0;   
                    success_beep_active = 1; Success_Beep_Start_Time = T0_Count_ms;
                    UART_SendStatus(); 
                } else {
                    error_count++; 
                    if (error_count >= 3) {
                        lock_down = 1; is_scrolling = 0;   
                        Lock_Start_Time = T0_Count_ms; Alarm_Start_Time = T0_Count_ms;
                        alarm_active = 1; 
                        error_count = 0; 
                        LCD_ShowString(1, 1, "Please Try Again");
                        LCD_ShowString(2, 1, "Security Lock   ");
                    } else {
                        LCD_ShowString(1, 1, "Password Incorrect");
                        LCD_ShowString(2, 1, "Try Again!      ");
                        is_scrolling = 1; 
                    }
                    UART_SendStatus(); 
                }
                
                input_index = 0;
                memset(input, 0, sizeof(input)); 
                
            } else if (key == 16 || key == 13) { // 取消或清除键
                input_index = 0;
                memset(input, 0, sizeof(input)); 
                LCD_ShowString(1, 1, "Input Cleared!  ");
                LCD_ShowString(2, 1, "                "); 
                is_scrolling = 1; 
            } else {
                // 处理数字输入
                if ((key >= 0 && key <= 9)) {
                    if (input_index < MAX_PASSWORD_LENGTH) {
                        input[input_index++] = key; 
                        LCD_ShowChar(2, input_index, '*'); 
                    }
                }
            }
        }
    }
}
