/**
 * @file    car_task.c
 * @brief   小车避障 + 云平台控制任务
 *
 *          默认手动模式（云平台控制）。
 *          云平台下发 "avoid" → 切换到自动避障模式。
 *          云平台下发 "manual" → 切回手动模式。
 *          云平台下发 forward/backward/left/right/stop → 执行对应动作。
 */

#include "debug.h"
#include "FreeRTOS.h"
#include "task.h"
#include "HC_SR04.h"
#include "servo.h"
#include "PWM.h"
#include "car_control.h"
#include "tts_task.h"
#include "shared.h"

/* ======================== 配置参数 ======================== */
#define CAR_TASK_PRIO       3
#define CAR_TASK_STK_SIZE   512

#define OBSTACLE_DIST_CM    50.0f
#define TURN_SPEED          60
#define TURN_TIME_MS        500
#define REVERSE_TIME_MS     2000
#define SERVO_WAIT_MS       500
#define STOP_WAIT_MS        200

/* ======================== 速度档位映射 ======================== */
static uint8_t GetMotorSpeed(void)
{
    switch (g_car_speed_gear)
    {
        case CAR_SPEED_1: return 25;
        case CAR_SPEED_3: return 75;
        case CAR_SPEED_4: return 100;
        default:          return 50;   /* CAR_SPEED_2 */
    }
}

/* ======================== 避障状态 ======================== */
typedef enum {
    CAR_STATE_STRAIGHT,
    CAR_STATE_OBSTACLE_DETECTED
} CarState;

/* ======================== 任务句柄 ======================== */
TaskHandle_t CarTask_Handler;

/* ======================== 辅助函数 ======================== */

static void Car_Forward(void)
{
    uint8_t speed = GetMotorSpeed();
    MotorA_SetSpeed((int16_t)speed);
    MotorB_SetSpeed((int16_t)speed);
}

static void Car_Backward(void)
{
    uint8_t speed = GetMotorSpeed();
    MotorA_SetSpeed(-(int16_t)speed);
    MotorB_SetSpeed(-(int16_t)speed);
}

static void Car_TurnLeft(uint8_t speed)
{
    Turn_Left(speed, TURN_PIVOT);
}

static void Car_TurnRight(uint8_t speed)
{
    Turn_Right(speed, TURN_PIVOT);
}

static void Car_Stop(void)
{
    MotorA_SetSpeed(0);
    MotorB_SetSpeed(0);
}

static float GetDistance(void)
{
    return HC_SR04_GetDistance();
}

static CarCmd DelayWithCmdCheck(uint32_t ms)
{
    uint32_t elapsed = 0;
    while (elapsed < ms)
    {
        vTaskDelay(pdMS_TO_TICKS(50));
        elapsed += 50;
        CarCmd c = CarControl_GetCmd();
        if (c != CAR_CMD_NONE)
            return c;
    }
    return CAR_CMD_NONE;
}

static void ExecuteCmd(CarCmd cmd)
{
    switch (cmd)
    {
        case CAR_CMD_FORWARD:
            printf("CAR: 前进\r\n");
            Car_Forward();
            break;
        case CAR_CMD_BACKWARD:
            printf("CAR: 后退\r\n");
            Car_Backward();
            break;
        case CAR_CMD_LEFT:
            printf("CAR: 左转\r\n");
            Car_TurnRight(TURN_SPEED);
            break;
        case CAR_CMD_RIGHT:
            printf("CAR: 右转\r\n");
            Car_TurnLeft(TURN_SPEED);
            break;
        case CAR_CMD_STOP:
            printf("CAR: 停止\r\n");
            Car_Stop();
            break;
        default:
            break;
    }
}

/* ======================== 任务主函数 ======================== */
void car_task(void *pvParameters)
{
    float dist_front, dist_left, dist_right;
    CarMode mode = CAR_MODE_MANUAL;
    CarState state = CAR_STATE_STRAIGHT;

    vTaskDelay(pdMS_TO_TICKS(1000));
    printf("CAR: 小车任务启动\r\n");

    /* 初始化外设 — 舵机必须先于超声波初始化（避免 TIM 冲突） */
    printf("CAR: 初始化舵机...\r\n");
    Servo_Init();
    Servo_Center();
    vTaskDelay(pdMS_TO_TICKS(500));

    printf("CAR: 初始化超声波...\r\n");
    HC_SR04_Init();

    printf("CAR: 初始化电机...\r\n");
    TB6612FNG_Init();
    TB6612FNG_Standby(ENABLE);

    printf("CAR: 默认模式 = 手动控制\r\n");
    g_car_mode = 1;

    /* 初始化共享内存小车控制数据 */
    SharedCarData.car_cmd = 0;
    SharedCarData.car_mode = 1;
    SharedCarData.car_speed = 2;
    SharedCarData.car_seq = 0;
    uint8_t last_car_seq = 0;
    uint8_t last_mode = 1;

    while (1)
    {
        /* 检测 V3F 共享内存控制命令（LVGL 写入） */
        uint8_t cur_seq = SharedCarData.car_seq;
        if (cur_seq != last_car_seq)
        {
            last_car_seq = cur_seq;

            /* 同步速度档位 */
            if (SharedCarData.car_speed >= 1 && SharedCarData.car_speed <= 4)
            {
                CarControl_SetSpeedGear(SharedCarData.car_speed);
            }

            /* 映射共享内存命令到 CarCmd（方向指令） */
            switch (SharedCarData.car_cmd)
            {
                case 1: CarControl_SetCmd(CAR_CMD_FORWARD); break;
                case 2: CarControl_SetCmd(CAR_CMD_BACKWARD); break;
                case 3: CarControl_SetCmd(CAR_CMD_LEFT); break;
                case 4: CarControl_SetCmd(CAR_CMD_RIGHT); break;
                case 5: CarControl_SetCmd(CAR_CMD_STOP); break;
            }
            SharedCarData.car_cmd = 0;  /* 消费命令，防止重复触发 */

            /* 只在模式真正变化时才执行模式切换 */
            if (SharedCarData.car_mode != last_mode)
            {
                last_mode = SharedCarData.car_mode;
                switch (SharedCarData.car_mode)
                {
                    case 0: CarControl_SetCmd(CAR_CMD_AVOID); break;
                    case 1: CarControl_SetCmd(CAR_CMD_MANUAL); break;
                    case 2: CarControl_SetCmd(CAR_CMD_FOLLOW); break;
                }
            }
        }

        /* 每次循环检查云平台命令 */
        CarCmd cmd = CarControl_GetCmd();

        if (cmd == CAR_CMD_AVOID)
        {
            mode = CAR_MODE_AVOID;
            g_car_mode = 0;
            SharedCarData.car_mode = 0;
            SharedCarData.car_seq++;
            last_mode = 0;
            state = CAR_STATE_STRAIGHT;
            Servo_Center();
            printf("CAR: 切换到避障模式\r\n");
            TTS_Speak("避障模式");
        }
        else if (cmd == CAR_CMD_MANUAL)
        {
            mode = CAR_MODE_MANUAL;
            g_car_mode = 1;
            SharedCarData.car_mode = 1;
            SharedCarData.car_seq++;
            last_mode = 1;
            Car_Stop();
            Servo_Center();
            printf("CAR: 切换到手动模式\r\n");
            TTS_Speak("手动模式");
        }
        else if (cmd == CAR_CMD_FOLLOW)
        {
            mode = CAR_MODE_FOLLOW;
            g_car_mode = 2;
            SharedCarData.car_mode = 2;
            SharedCarData.car_seq++;
            last_mode = 2;
            Car_Stop();
            Servo_Center();
            printf("CAR: 跟随模式（预留）\r\n");
            TTS_Speak("跟随模式");
        }
        else if (cmd == CAR_CMD_STOP)
        {
            Car_Stop();
            mode = CAR_MODE_MANUAL;
            g_car_mode = 1;
            SharedCarData.car_mode = 1;
            SharedCarData.car_seq++;
            last_mode = 1;
            printf("CAR: 停止\r\n");
        }
        else if (cmd != CAR_CMD_NONE)
        {
            ExecuteCmd(cmd);
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        /* 手动模式：等待命令 */
        if (mode == CAR_MODE_MANUAL)
        {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (mode == CAR_MODE_FOLLOW)
        {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* 避障模式 */
        switch (state)
        {
            case CAR_STATE_STRAIGHT:
            {
                Car_Forward();
                dist_front = GetDistance();

                if (dist_front > 0 && dist_front < OBSTACLE_DIST_CM)
                {
                    printf("CAR: 前方障碍 %.0fcm\r\n", dist_front);
                    Car_Stop();
                    vTaskDelay(pdMS_TO_TICKS(STOP_WAIT_MS));
                    state = CAR_STATE_OBSTACLE_DETECTED;
                }
                else
                {
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
                break;
            }

            case CAR_STATE_OBSTACLE_DETECTED:
            {
                Servo_LookLeft();
                vTaskDelay(pdMS_TO_TICKS(SERVO_WAIT_MS));
                dist_left = GetDistance();

                Servo_LookRight();
                vTaskDelay(pdMS_TO_TICKS(SERVO_WAIT_MS));
                dist_right = GetDistance();

                printf("CAR: 左=%.0fcm 右=%.0fcm\r\n", dist_left, dist_right);

                Servo_Center();
                vTaskDelay(pdMS_TO_TICKS(300));

                if (dist_left > OBSTACLE_DIST_CM && dist_right > OBSTACLE_DIST_CM)
                {
                    printf("CAR: 两侧畅通，左转\r\n");
                    Turn_Left(TURN_SPEED, TURN_PIVOT);
                    if (DelayWithCmdCheck(TURN_TIME_MS) != CAR_CMD_NONE)
                        goto cmd_received;
                    Car_Stop();
                    vTaskDelay(pdMS_TO_TICKS(STOP_WAIT_MS));
                }
                else if (dist_left > OBSTACLE_DIST_CM)
                {
                    printf("CAR: 左侧畅通，左转\r\n");
                    Turn_Left(TURN_SPEED, TURN_PIVOT);
                    if (DelayWithCmdCheck(TURN_TIME_MS) != CAR_CMD_NONE)
                        goto cmd_received;
                    Car_Stop();
                    vTaskDelay(pdMS_TO_TICKS(STOP_WAIT_MS));
                }
                else if (dist_right > OBSTACLE_DIST_CM)
                {
                    printf("CAR: 右侧畅通，右转\r\n");
                    Turn_Right(TURN_SPEED, TURN_PIVOT);
                    if (DelayWithCmdCheck(TURN_TIME_MS) != CAR_CMD_NONE)
                        goto cmd_received;
                    Car_Stop();
                    vTaskDelay(pdMS_TO_TICKS(STOP_WAIT_MS));
                }
                else
                {
                    printf("CAR: 两侧受阻，后退\r\n");
                    Car_Backward();
                    if (DelayWithCmdCheck(REVERSE_TIME_MS) != CAR_CMD_NONE)
                        goto cmd_received;
                    Car_Stop();
                    vTaskDelay(pdMS_TO_TICKS(STOP_WAIT_MS));

                    printf("CAR: 后退完成，左转\r\n");
                    Turn_Left(TURN_SPEED, TURN_PIVOT);
                    if (DelayWithCmdCheck(TURN_TIME_MS) != CAR_CMD_NONE)
                        goto cmd_received;
                    Car_Stop();
                    vTaskDelay(pdMS_TO_TICKS(STOP_WAIT_MS));
                }

                state = CAR_STATE_STRAIGHT;
                break;
            }
        }
        continue;

cmd_received:
        Car_Stop();
        mode = CAR_MODE_MANUAL;
        printf("CAR: 动作中断，已停止\r\n");
    }
}

/* ======================== 公开接口 ======================== */

void CarTask_Create(void)
{
    xTaskCreate((TaskFunction_t)car_task,
                (const char *)"car",
                (uint16_t)CAR_TASK_STK_SIZE,
                (void *)NULL,
                (UBaseType_t)CAR_TASK_PRIO,
                (TaskHandle_t *)&CarTask_Handler);
}
