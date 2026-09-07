/**
 * @file    my_gui.c
 * @brief   自然选择号 LVGL 图形界面 — TABVIEW 多页架构
 *
 *          该文件实现了基于 LVGL 的五页面图形用户界面：
 *            - 第1页（首页）：大时钟、日期、天气信息、环境传感器数据
 *            - 第2页（系统状态）：喷药/声光警报开关状态 + 温度/湿度/光照传感器仪表盘
 *            - 第3页（阈值设置）：温度/湿度/光照强度阈值滑动条设置
 *            - 第4页（病害检测）：当前植物、病害识别结果、置信度、检测时间
 *            - 第5页（画面预览）：实时预览外部传入的 RGB565 或 JPEG 图像
 *
 *          布局：顶部 24px 渐变栏（左时间 + 居中标题"欢迎来到自然选择号"）
 *               下方 320×216 TABVIEW（标签栏居左 76px 宽，内容区 244×216）
 *          四页通过左侧标签自由切换。
 *
 *          编码说明：所有中文标签字符串和注释均为 UTF-8 编码。
 *                   中文字库由外部 myfont.c（simhei 16px 2bpp GBK 编码）提供。
 */

#include "my_gui.h"        /* 自定义 GUI 头文件（函数声明、外部变量） */
#include "shared.h"        /* 共享内存结构体（阈值回写） */
#include <stdio.h>         /* snprintf 格式化输出 */
#include <string.h>        /* strlen 字符串长度计算 */

/* ================================================================
 *  字体声明
 * ================================================================ */

/** 声明外部定义的中文字库（定义在 myfont.c，simhei 16px 2bpp GBK 编码） */
extern const lv_font_t myfont;

/** 声明开机动画用 slogan 图片（定义在 slogan.c，256×64 RGBA） */
extern const lv_img_dsc_t slogan;

/* ================================================================
 *  颜色定义
 *  使用 lv_color_hex() 将 24 位 RGB 色值转换为 LVGL 颜色格式
 * ================================================================ */

/** 深蓝系 — 标题/重点文字 */
#define COLOR_CLOCK_TITLE  lv_color_hex(0x004198)

/** 蓝系 — 顶部栏渐变起点 */
#define COLOR_TOP_START    lv_color_hex(0x1195DB)

/** 蓝系 — 顶部栏渐变终点 */
#define COLOR_TOP_END      lv_color_hex(0x084363)

/** 灰系 — 正文 */
#define COLOR_BODY_TEXT    lv_color_hex(0x37474F)

/** 灰系 — 卡片标题 / 次要文字 */
#define COLOR_SUBTITLE     lv_color_hex(0x707070)

/** 黑系 — 卡片数值 / 主文字 */
#define COLOR_PRIMARY_TEXT lv_color_hex(0x1A1A1A)

/** 红 #E6002D — 警报 / 病害告警 / 危险 */
#define COLOR_DANGER       lv_color_hex(0xE6002D)

/** 蓝 #0047AB — 湿度 / 信息 */
#define COLOR_INFO         lv_color_hex(0x0047AB)

/** 黄 #FFD200 — 光照 / 提示 */
#define COLOR_WARNING      lv_color_hex(0xFFD200)

/** 绿 #2E7D32 — 喷药 / 健康 / 正常 */
#define COLOR_HEALTHY      lv_color_hex(0x2E7D32)

/** 橙 #FF8F00 — 操作 / 待机 */
#define COLOR_OPERATE      lv_color_hex(0xFF8F00)

/* ================================================================
 *  全局 UI 对象
 *  声明为 static 全局变量，方便各页面创建函数注册、更新函数访问
 * ================================================================ */

/* ---------- 顶部栏 ---------- */
static lv_obj_t *top_bar;          /**< 顶部栏容器（320×24，蓝系渐变背景）       */
static lv_obj_t *label_time_top;   /**< 顶部栏左侧时间标签，格式如 "14:35"        */
static lv_obj_t *label_title;      /**< 顶部栏居中标题 "欢迎来到自然选择号"         */
static lv_obj_t *label_wifi_icon;  /**< 顶部栏右侧 WiFi 图标                      */
static lv_obj_t *label_mute_icon;  /**< 顶部栏静音图标                             */

/* ---------- Tabview ---------- */
static lv_obj_t *tabview;          /**< 标签视图根对象（320×216，标签居左 76px）   */

/* ---------- 第1页：首页控件 ---------- */
static lv_obj_t *label_clock;        /**< 大时钟标签，显示 HH:MM，28px 字体         */
static lv_obj_t *label_date;         /**< 日期标签，格式如 "2024年6月12日 星期五"    */
static lv_obj_t *label_city;         /**< 城市名称标签，如 "南宁"                   */
static lv_obj_t *label_weather;      /**< 天气状况标签，如 "多云"                   */
static lv_obj_t *label_temp_card;    /**< 天气温度标签，格式如 "25°C"               */
static lv_obj_t *label_temp_env;     /**< 环境温度标签，格式如 "温度: 25°C"         */
static lv_obj_t *label_humi;         /**< 湿度标签，格式如 "湿度: 60%"              */
static lv_obj_t *label_light;        /**< 光照标签，格式如 "光照: 1200 lx"          */
static lv_obj_t *label_title_weather;/**< 天气区块蓝色标题 "天气"                   */
static lv_obj_t *label_title_env;    /**< 环境区块蓝色标题 "环境"                   */

/* ---------- 第2页：系统状态页控件 ---------- */
static lv_obj_t *label_spray_val;      /**< 喷药状态数值标签，"开启" / "关闭"      */
static lv_obj_t *label_alarm_light_val;/**< 警报灯状态数值标签，"开启" / "关闭"    */
static lv_obj_t *label_alarm_sound_val;/**< 警报声状态数值标签，"开启" / "关闭"    */
static lv_obj_t *label_sys_temp_val;   /**< 温度传感器数值标签，格式如 "25°C"      */
static lv_obj_t *label_sys_humi_val;   /**< 湿度传感器数值标签，格式如 "60%"       */
static lv_obj_t *label_sys_lux_val;    /**< 光照传感器数值标签，格式如 "1200 lx"   */
static lv_obj_t *label_wifi_val;       /**< WiFi 状态数值标签，"正常" / "异常"     */
static lv_obj_t *label_bh1750_val;     /**< BH1750 状态数值标签，"正常" / "异常"   */
static lv_obj_t *label_sht30_val;      /**< SHT30 状态数值标签，"正常" / "异常"    */
static lv_obj_t *btn_save_log;         /**< 保存当前数据按钮 */
static lv_obj_t *label_save_log;       /**< 保存按钮状态标签 */

/* ---------- 第3页：病害检测页控件 ---------- */
static lv_obj_t *label_plant_name;
static lv_obj_t *label_disease_name;
static lv_obj_t *label_confidence;
static lv_obj_t *label_detect_time;
static lv_obj_t *btn_detect;
static lv_obj_t *label_btn_detect;
static uint8_t detect_running = 0;

/* ---------- 第4页：画面预览页控件 ---------- */
static lv_obj_t *img_preview;         /**< 画面预览图像对象                        */
static lv_img_dsc_t preview_img_dsc;  /**< 画面预览图像描述符（RGB565 / JPEG）     */

/* ---------- 第5页：阈值设置页控件 ---------- */
static lv_obj_t *slider_temp;         /**< 温度阈值滑动条                           */
static lv_obj_t *label_temp_val;      /**< 温度阈值数值标签                         */
static lv_obj_t *slider_humi;         /**< 湿度阈值滑动条                           */
static lv_obj_t *label_humi_val;      /**< 湿度阈值数值标签                         */
static lv_obj_t *slider_light;        /**< 光照强度阈值滑动条                       */
static lv_obj_t *label_light_val;     /**< 光照强度阈值数值标签                     */
static uint8_t   threshold_syncing;   /**< 1=正在从共享内存同步，抑制回调回写         */

/* ---------- 第6页：小车控制页控件 ---------- */
static lv_obj_t *dd_car_mode;   /**< 模式下拉框 */
static lv_obj_t *dd_car_speed;  /**< 速度下拉框 */
static uint8_t car_syncing = 0;  /**< 防止同步时触发事件 */

/* ---------- 第7页：历史数据页控件 ---------- */
static lv_obj_t *list_history;          /**< 历史数据列表容器 */
static lv_obj_t *label_history_empty;   /**< 无数据提示标签 */
static lv_obj_t *btn_history_prev;      /**< 上一页按钮 */
static lv_obj_t *btn_history_next;      /**< 下一页按钮 */
static lv_obj_t *label_history_page;    /**< 页码显示标签 "X/Y" */
static lv_obj_t *btn_history_refresh;   /**< 刷新按钮 */
static lv_obj_t *btn_history_clear;     /**< 清空按钮 */
static uint8_t history_page = 0;        /**< 当前显示页码（0=最新） */
static uint16_t history_max_page = 0;   /**< 最大页码 */
static uint8_t clear_confirm = 0;       /**< 清空确认状态：0=未确认 1=待确认 */

/* ================================================================
 *  全局样式变量
 *  所有样式在 init_styles() 中初始化一次，各处复用。
 *  使用 static 限制作用域，避免与其他文件冲突。
 * ================================================================ */
static lv_style_t style_top_bar;      /**< 顶部栏样式（蓝色渐变背景，无圆角）       */
static lv_style_t style_text_white;   /**< 白色文字样式（顶部栏用）                 */
static lv_style_t style_clock;        /**< 大时钟样式（28px 英文数字字体）           */
static lv_style_t style_card_text;    /**< 正文文字样式（中文字体，深灰色）          */
static lv_style_t style_title_blue;   /**< 蓝色标题样式（首页天气/环境区块标题）     */
static lv_style_t style_tab_text;     /**< TABVIEW 标签文字样式                     */
static lv_style_t style_card_bg;      /**< 卡片容器样式（白色背景、圆角6、1px边框）  */
static lv_style_t style_card_title;   /**< 卡片内标题文字（中灰色 #707070）         */
static lv_style_t style_card_value;   /**< 卡片内数值文字（黑色 #1A1A1A）           */
static lv_style_t style_bar_red;      /**< 色条样式-红 #E6002D                      */
static lv_style_t style_bar_blue;     /**< 色条样式-蓝 #0047AB                      */
static lv_style_t style_bar_yellow;   /**< 色条样式-黄 #FFD200                      */
static lv_style_t style_bar_green;    /**< 色条样式-绿 #2E7D32                      */
static lv_style_t style_bar_orange;   /**< 色条样式-橙 #FF8F00                      */

/* ================================================================
 *  开机动画对象（静态变量，供 show / hide 两函数共享）
 * ================================================================ */
static lv_obj_t *boot_screen = NULL;    /**< 开机画面全屏容器                       */
static lv_obj_t *boot_img    = NULL;    /**< slogan 图片对象                        */
static lv_obj_t *boot_label  = NULL;    /**< "系统启动中..." 文字标签               */

/* ================================================================
 *  前向声明
 * ================================================================ */
static void init_styles(void);
static lv_obj_t *create_sensor_card(lv_obj_t *parent,
                                     lv_style_t *bar_style,
                                     lv_color_t border_color,
                                     const char *title,
                                     const char *value,
                                     lv_obj_t **out_val_label);
static void create_top_bar(void);
static void create_tabview(void);
static void create_home_page(lv_obj_t *parent);
static void create_sys_page(lv_obj_t *parent);
static void create_detect_page(lv_obj_t *parent);
static void create_preview_page(lv_obj_t *parent);
static void create_threshold_page(lv_obj_t *parent);
static void create_car_page(lv_obj_t *parent);
static void create_history_page(lv_obj_t *parent);
static void history_page_event_cb(lv_event_t *e);
static void create_wifi_page(lv_obj_t *parent);

/* ================================================================
 *  init_styles - 初始化所有全局样式
 *
 *  该函数在 my_gui() 中最先调用，为所有 UI 元素创建统一的视觉风格。
 *  每个样式先调用 lv_style_init() 分配内存，再逐属性设置。
 *  样式一旦初始化，后续只需通过 lv_obj_add_style() 应用即可。
 * ================================================================ */
static void init_styles(void)
{
    /* ---------- 顶部栏背景：蓝色水平渐变 ---------- */
    lv_style_init(&style_top_bar);
    lv_style_set_bg_color(&style_top_bar, COLOR_TOP_START);      /* 渐变起点 #1195DB */
    lv_style_set_bg_grad_color(&style_top_bar, COLOR_TOP_END);   /* 渐变终点 #084363 */
    lv_style_set_bg_grad_dir(&style_top_bar, LV_GRAD_DIR_HOR);   /* 水平方向渐变      */
    lv_style_set_bg_opa(&style_top_bar, LV_OPA_COVER);           /* 背景不透明        */
    lv_style_set_radius(&style_top_bar, 0);                      /* 无圆角            */
    lv_style_set_pad_all(&style_top_bar, 0);                     /* 无内边距          */

    /* ---------- 白色文字（顶部栏标题和时间） ---------- */
    lv_style_init(&style_text_white);
    lv_style_set_text_color(&style_text_white, lv_color_white());/* 白色文字          */
    lv_style_set_text_font(&style_text_white, &myfont);          /* 中文字体          */

    /* ---------- 大时钟：28px 英文数字字体，深蓝色 ---------- */
    lv_style_init(&style_clock);
    lv_style_set_text_font(&style_clock, &lv_font_montserrat_28);/* 28px 英文数字     */
    lv_style_set_text_color(&style_clock, COLOR_CLOCK_TITLE);    /* 深蓝 #004198     */

    /* ---------- 正文文字：中文字体，深灰色 ---------- */
    lv_style_init(&style_card_text);
    lv_style_set_text_font(&style_card_text, &myfont);
    lv_style_set_text_color(&style_card_text, COLOR_BODY_TEXT);  /* 深灰 #37474F     */

    /* ---------- 蓝色标题：中文字体，深蓝色 ---------- */
    lv_style_init(&style_title_blue);
    lv_style_set_text_font(&style_title_blue, &myfont);
    lv_style_set_text_color(&style_title_blue, COLOR_CLOCK_TITLE);/* 深蓝 #004198    */

    /* ---------- Tabview 标签文字：中文字体，深灰色 ---------- */
    lv_style_init(&style_tab_text);
    lv_style_set_text_font(&style_tab_text, &myfont);
    lv_style_set_text_color(&style_tab_text, COLOR_BODY_TEXT);   /* 深灰 #37474F     */

    /* ---------- 卡片通用样式：白底、圆角6、1px边框 ---------- */
    lv_style_init(&style_card_bg);
    lv_style_set_bg_color(&style_card_bg, lv_color_white());     /* 纯白背景          */
    lv_style_set_bg_opa(&style_card_bg, LV_OPA_COVER);           /* 背景不透明        */
    lv_style_set_radius(&style_card_bg, 6);                      /* 圆角半径 6px      */
    lv_style_set_pad_all(&style_card_bg, 0);                     /* 无内边距，由内容自行控制 */
    lv_style_set_border_width(&style_card_bg, 1);                /* 边框宽度 1px      */

    /* ---------- 卡片标题文字：中灰色，用作卡片内字段说明 ---------- */
    lv_style_init(&style_card_title);
    lv_style_set_text_font(&style_card_title, &myfont);
    lv_style_set_text_color(&style_card_title, COLOR_SUBTITLE);  /* 中灰 #707070     */

    /* ---------- 卡片数值文字：黑色大字，醒目的数值显示 ---------- */
    lv_style_init(&style_card_value);
    lv_style_set_text_font(&style_card_value, &myfont);
    lv_style_set_text_color(&style_card_value, COLOR_PRIMARY_TEXT);/* 黑色 #1A1A1A   */

    /* ---------- 色条：红 #E6002D — 警报声、警报灯 ---------- */
    lv_style_init(&style_bar_red);
    lv_style_set_bg_color(&style_bar_red, COLOR_DANGER);         /* 蒙德里安红        */
    lv_style_set_bg_opa(&style_bar_red, LV_OPA_COVER);           /* 不透明            */
    lv_style_set_radius(&style_bar_red, 2);                      /* 圆角半径 2px      */
    lv_style_set_pad_all(&style_bar_red, 0);                     /* 无内边距          */

    /* ---------- 色条：蓝 #0047AB — 湿度 ---------- */
    lv_style_init(&style_bar_blue);
    lv_style_set_bg_color(&style_bar_blue, COLOR_INFO);
    lv_style_set_bg_opa(&style_bar_blue, LV_OPA_COVER);
    lv_style_set_radius(&style_bar_blue, 2);
    lv_style_set_pad_all(&style_bar_blue, 0);

    /* ---------- 色条：黄 #FFD200 — 光照 ---------- */
    lv_style_init(&style_bar_yellow);
    lv_style_set_bg_color(&style_bar_yellow, COLOR_WARNING);
    lv_style_set_bg_opa(&style_bar_yellow, LV_OPA_COVER);
    lv_style_set_radius(&style_bar_yellow, 2);
    lv_style_set_pad_all(&style_bar_yellow, 0);

    /* ---------- 色条：绿 #2E7D32 — 喷药状态 ---------- */
    lv_style_init(&style_bar_green);
    lv_style_set_bg_color(&style_bar_green, COLOR_HEALTHY);
    lv_style_set_bg_opa(&style_bar_green, LV_OPA_COVER);
    lv_style_set_radius(&style_bar_green, 2);
    lv_style_set_pad_all(&style_bar_green, 0);

    /* ---------- 色条：橙 #FF8F00 — 温度 ---------- */
    lv_style_init(&style_bar_orange);
    lv_style_set_bg_color(&style_bar_orange, COLOR_OPERATE);
    lv_style_set_bg_opa(&style_bar_orange, LV_OPA_COVER);
    lv_style_set_radius(&style_bar_orange, 2);
    lv_style_set_pad_all(&style_bar_orange, 0);
}

/* ================================================================
 *  create_sensor_card - 传感器卡片工厂函数
 *
 *  @param  parent        父容器对象
 *  @param  bar_style     顶部色条样式指针（&style_bar_red 等）
 *  @param  border_color  卡片边框颜色（与色条颜色配套）
 *  @param  title         卡片标题文本（如 "喷药状态"、"温度"）
 *  @param  value         初始数值文本（如 "关闭"、"25°C"）
 *  @param  out_val_label 输出参数，返回数值标签对象指针，供外部更新
 *
 *  @return 卡片容器对象指针（用完后通常不需要保存）
 *
 *  系统状态页的 6 个卡片结构完全相同，仅标题、初始值、色条颜色、
 *  边框颜色不同。本函数封装共性代码，避免重复。
 *
 *  卡片内部布局（70×85）：
 *  ┌────────────────────────┐
 *  │     色条 54×4 (y=8)    │  ← 顶部色条，4px 高，宽度 54
 *  │     标题 (y=22)        │  ← 中灰色小字
 *  │     数值 (y=48)        │  ← 黑色大字
 *  └────────────────────────┘
 * ================================================================ */
static lv_obj_t *create_sensor_card(lv_obj_t *parent,
                                     lv_style_t *bar_style,
                                     lv_color_t border_color,
                                     const char *title,
                                     const char *value,
                                     lv_obj_t **out_val_label)
{
    /* 创建卡片容器 */
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_add_style(card, &style_card_bg, 0);                 /* 应用白色卡片样式    */
    lv_obj_set_size(card, 70, 85);                             /* 卡片尺寸 70×85     */
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);           /* 禁用滚动条          */
    lv_obj_set_style_border_color(card, border_color, 0);      /* 设置彩色边框        */

    /* 顶部色条：4px 高的横条，用对应颜色填充 */
    lv_obj_t *bar = lv_obj_create(card);
    lv_obj_add_style(bar, bar_style, 0);                       /* 应用色条样式        */
    lv_obj_set_size(bar, 54, 4);                               /* 尺寸 54×4          */
    lv_obj_set_style_border_width(bar, 0, 0);                  /* 清除默认边框，避免灰色覆盖 */
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);            /* 禁用色条滚动条      */
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 8);                 /* 顶部居中，向下偏移 8 */

    /* 卡片标题：中灰色小字 */
    lv_obj_t *label_title = lv_label_create(card);
    lv_obj_add_style(label_title, &style_card_title, 0);
    lv_label_set_text(label_title, title);
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 22);        /* 色条下方 8px 处    */

    /* 数值标签：黑色大字，醒目显示，返回给调用者供外部更新 */
    lv_obj_t *label_val = lv_label_create(card);
    lv_obj_add_style(label_val, &style_card_value, 0);
    lv_label_set_text(label_val, value);
    lv_obj_align(label_val, LV_ALIGN_TOP_MID, 0, 48);          /* 标题下方           */

    if (out_val_label) *out_val_label = label_val;              /* 输出数值标签指针    */

    return card;
}

/* ================================================================
 *  create_top_bar - 创建顶部信息栏
 *
 *  布局（屏幕宽 320，高 24）：
 *  ┌──────────────────────────────────┐
 *  │ 14:35    欢迎来到自然选择号      │  ← 左时间 + 居中标题
 *  └──────────────────────────────────┘
 *
 *  背景为蓝色水平渐变（#1195DB → #084363），文字白色。
 *  该栏固定在屏幕最顶部 (0,0)，不受 TABVIEW 切页影响。
 * ================================================================ */
static void create_top_bar(void)
{
    /* 顶部栏容器 */
    top_bar = lv_obj_create(lv_scr_act());                     /* 在屏幕根对象上创建    */
    lv_obj_add_style(top_bar, &style_top_bar, 0);              /* 蓝色渐变背景          */
    lv_obj_set_size(top_bar, 320, 24);                         /* 全宽 320，高 24      */
    lv_obj_set_pos(top_bar, 0, 0);                             /* 左上角固定            */
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);        /* 禁用滚动条            */

    /* 左侧时间标签（白色，左对齐，左内边距 6px） */
    label_time_top = lv_label_create(top_bar);
    lv_obj_add_style(label_time_top, &style_text_white, 0);
    lv_label_set_text(label_time_top, "--:--");                /* 初始占位符          */
    lv_obj_align(label_time_top, LV_ALIGN_LEFT_MID, 6, 0);

    /* 居中标题标签（白色） */
    label_title = lv_label_create(top_bar);
    lv_obj_add_style(label_title, &style_text_white, 0);
    lv_label_set_text(label_title, "欢迎来到自然选择号");       /* 居中标题              */
    lv_obj_align(label_title, LV_ALIGN_CENTER, 0, 0);

    /* 右侧 WiFi 图标（白色，初始隐藏） */
    label_wifi_icon = lv_label_create(top_bar);
    lv_label_set_text(label_wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(label_wifi_icon, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_wifi_icon, &lv_font_montserrat_14, 0);
    lv_obj_align(label_wifi_icon, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_add_flag(label_wifi_icon, LV_OBJ_FLAG_HIDDEN);

    /* WiFi 左侧静音图标（白色，非静音时隐藏） */
    label_mute_icon = lv_label_create(top_bar);
    lv_label_set_text(label_mute_icon, LV_SYMBOL_MUTE);
    lv_obj_set_style_text_color(label_mute_icon, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_mute_icon, &lv_font_montserrat_14, 0);
    lv_obj_align(label_mute_icon, LV_ALIGN_RIGHT_MID, -30, 0);
    lv_obj_add_flag(label_mute_icon, LV_OBJ_FLAG_HIDDEN);
}

static void reset_clear_confirm(void)
{
    if (clear_confirm)
    {
        clear_confirm = 0;
        if (btn_history_clear)
        {
            lv_label_set_text(lv_obj_get_child(btn_history_clear, 0), "清空");
            lv_obj_set_style_bg_color(btn_history_clear, lv_color_hex(0xE6002D), 0);
        }
    }
}

static void tab_change_reset_cb(lv_event_t *e)
{
    (void)e;
    reset_clear_confirm();
}

/* ================================================================
 *  create_tabview - 创建标签视图，管理四个页面
 *
 *  布局：
 *  ┌──────┬───────────────────────┐
 *  │ 首页  │                       │
 *  │ 系统  │    内容区 244×216      │
 *  │ 状态  │    （页面在此切换）     │
 *  │ 病害  │                       │
 *  │ 检测  │                       │
 *  │ 画面  │                       │
 *  │ 预览  │                       │
 *  └──────┴───────────────────────┘
 *   76px           244px
 *
 *  四个标签页的内容由各自的 create_xxx_page() 函数构建。
 *  禁用内容区滚动条和标签栏左右滑动。     ← 未实现左右滑动禁止
 * ================================================================ */
static void create_tabview(void)
{
    tabview = lv_tabview_create(lv_scr_act(), LV_DIR_LEFT, 76);/* 标签在左，宽 76px    */
    lv_obj_set_size(tabview, 320, 216);                        /* 总尺寸 320×216      */
    lv_obj_set_pos(tabview, 0, 24);                            /* 顶部栏下方紧接      */

    /* 统一设置标签按钮的文字样式 */
    lv_obj_t *tab_btns = lv_tabview_get_tab_btns(tabview);
    lv_obj_add_style(tab_btns, &style_tab_text, 0);

    /* 创建七个标签页 */
    lv_obj_t *tab_home      = lv_tabview_add_tab(tabview, "首页");
    lv_obj_t *tab_sys       = lv_tabview_add_tab(tabview, "系统状态");
    lv_obj_t *tab_threshold = lv_tabview_add_tab(tabview, "阈值设置");
    lv_obj_t *tab_car       = lv_tabview_add_tab(tabview, "小车控制");
    lv_obj_t *tab_detect    = lv_tabview_add_tab(tabview, "病害检测");
    lv_obj_t *tab_preview   = lv_tabview_add_tab(tabview, "画面预览");
    lv_obj_t *tab_history   = lv_tabview_add_tab(tabview, "历史数据");
    lv_obj_t *tab_wifi      = lv_tabview_add_tab(tabview, "设置");

    /* 所有页面内容区设为白色背景 */
    lv_obj_set_style_bg_color(tab_home,      lv_color_white(), 0);
    lv_obj_set_style_bg_color(tab_sys,       lv_color_white(), 0);
    lv_obj_set_style_bg_color(tab_threshold, lv_color_white(), 0);
    lv_obj_set_style_bg_color(tab_car,       lv_color_white(), 0);
    lv_obj_set_style_bg_color(tab_detect,    lv_color_white(), 0);
    lv_obj_set_style_bg_color(tab_preview,   lv_color_white(), 0);
    lv_obj_set_style_bg_color(tab_history,   lv_color_white(), 0);
    lv_obj_set_style_bg_color(tab_wifi,      lv_color_white(), 0);

    /* 禁用 TABVIEW 内容区的滚动条 */
    lv_obj_clear_flag(lv_tabview_get_content(tabview), LV_OBJ_FLAG_SCROLLABLE);

    /* 构建各页面内容 */
    create_home_page(tab_home);
    create_sys_page(tab_sys);
    create_threshold_page(tab_threshold);
    create_car_page(tab_car);
    create_detect_page(tab_detect);
    create_preview_page(tab_preview);
    create_history_page(tab_history);
    create_wifi_page(tab_wifi);

    /* 页面切换时重置清空确认状态 */
    lv_obj_add_event_cb(tabview, tab_change_reset_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

/* ================================================================
 *  create_home_page - 构建第1页：首页
 *
 *  页面布局（内容区 244×216）：
 *
 *  ┌──────────────────────────────────┐ Y=0
 *  │           14:35                   │  ← 大时钟（28px 英文数字，深蓝色）
 *  │    2024年6月12日 星期五           │  ← 日期（中文字体，深灰色）
 *  ├──────────────┬───────────────────┤ Y=102
 *  │  天气        │  环境              │  ← 蓝色标题
 *  │  南宁        │  温度: 25°C       │  ← 天气信息 / 环境数据
 *  │  多云        │  湿度: 60%        │
 *  │  25°C        │  光照: 1200 lx    │
 *  │              │  光照: 1200 lx    │
 *  └──────────────┴───────────────────┘
 *
 *  所有 label 直接放置在父容器上，不嵌套子卡片容器。
 *  这样布局更紧凑，适合 244px 窄内容区。
 * ================================================================ */
static void create_home_page(lv_obj_t *parent)
{
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);          /* 禁用页面滚动条      */

    /* 大时钟：28px 英文数字，顶部居中，向下偏移 6px */
    label_clock = lv_label_create(parent);
    lv_obj_add_style(label_clock, &style_clock, 0);
    lv_label_set_text(label_clock, "--:--:--");                /* 初始占位符          */
    lv_obj_align(label_clock, LV_ALIGN_TOP_MID, 0, 6);

    /* 日期：中文字体，时钟下方 50px */
    label_date = lv_label_create(parent);
    lv_obj_add_style(label_date, &style_card_text, 0);
    lv_label_set_text(label_date, "----年--月--日 星期-");   /* 初始占位符          */
    lv_obj_align(label_date, LV_ALIGN_TOP_MID, 0, 56);

    /* —— 左下：天气区块 —— */

    /* 蓝色标题 "天气" */
    label_title_weather = lv_label_create(parent);
    lv_obj_add_style(label_title_weather, &style_title_blue, 0);
    lv_label_set_text(label_title_weather, "天气");
    lv_obj_align(label_title_weather, LV_ALIGN_TOP_LEFT, 30, 102);

    /* 城市名 */
    label_city = lv_label_create(parent);
    lv_obj_add_style(label_city, &style_card_text, 0);
    lv_label_set_text(label_city, "--");                      /* 初始占位符          */
    lv_obj_align(label_city, LV_ALIGN_TOP_LEFT, 30, 142);

    /* 天气状况 */
    label_weather = lv_label_create(parent);
    lv_obj_add_style(label_weather, &style_card_text, 0);
    lv_label_set_text(label_weather, "--");                   /* 初始占位符          */
    lv_obj_align(label_weather, LV_ALIGN_TOP_LEFT, 30, 162);

    /* 天气温度（天气状况下方） */
    label_temp_card = lv_label_create(parent);
    lv_obj_add_style(label_temp_card, &style_card_text, 0);
    lv_label_set_text(label_temp_card, "--°C");                /* 初始占位符          */
    lv_obj_align(label_temp_card, LV_ALIGN_TOP_LEFT, 30, 182);

    /* —— 右下：环境区块 —— */

    /* 蓝色标题 "环境" */
    label_title_env = lv_label_create(parent);
    lv_obj_add_style(label_title_env, &style_title_blue, 0);
    lv_label_set_text(label_title_env, "环境");
    lv_obj_align(label_title_env, LV_ALIGN_TOP_LEFT, 120, 102);

    /* 环境温度 */
    label_temp_env = lv_label_create(parent);
    lv_obj_add_style(label_temp_env, &style_card_text, 0);
    lv_label_set_text(label_temp_env, "温度: 25°C");
    lv_obj_align(label_temp_env, LV_ALIGN_TOP_LEFT, 120, 142);

    /* 湿度 */
    label_humi = lv_label_create(parent);
    lv_obj_add_style(label_humi, &style_card_text, 0);
    lv_label_set_text(label_humi, "湿度: 60%");
    lv_obj_align(label_humi, LV_ALIGN_TOP_LEFT, 120, 162);

    /* 光照 */
    label_light = lv_label_create(parent);
    lv_obj_add_style(label_light, &style_card_text, 0);
    lv_label_set_text(label_light, "光照: 1200 lx");
    lv_obj_align(label_light, LV_ALIGN_TOP_LEFT, 120, 182);
}

static void save_log_btn_cb(lv_event_t *e)
{
    (void)e;
    if (SharedLogData.save_sensor_log) return;  /* 防止重复触发 */
    SharedLogData.save_sensor_log = 1;
    lv_obj_add_state(btn_save_log, LV_STATE_DISABLED);
    lv_label_set_text(label_save_log, "保存中...");
    lv_obj_set_style_text_color(label_save_log, COLOR_WARNING, 0);
    printf("V3F: 请求V5F保存当前传感器数据\r\n");
}

/* ================================================================
 *  create_sys_page - 构建第2页：系统状态
 *
 *  页面布局（内容区 244×216）：
 *
 *  ┌──────────┬──────────┬──────────┐
 *  │ 喷药状态  │  警报灯   │  警报声   │  ← 上排：开关状态 (y=8)
 *  │  色条绿   │  色条红   │  色条红   │     卡片 70×85
 *  │  关闭     │  关闭    │  关闭     │
 *  ├──────────┼──────────┼──────────┤
 *  │  温度     │  湿度    │  光照     │  ← 下排：传感器 (y=105)
 *  │  色条橙   │  色条蓝   │  色条黄   │     卡片 70×85
 *  │  25°C    │  60%     │  1200 lx │
 *  └──────────┴──────────┴──────────┘
 *
 *  6 个卡片通过 create_sensor_card() 工厂函数统一创建，
 *  卡片间距：上排 y 从 8 开始，三列 x = 0 / 75 / 150
 *            下排 y 从 105 开始，三列 x = 0 / 75 / 150
 * ================================================================ */
static void create_sys_page(lv_obj_t *parent)
{
    lv_obj_set_scroll_dir(parent, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_AUTO);

    /* ====== 上排：开关状态卡片 (x=0 / 75 / 150, y=8) ====== */
    /* 喷药状态 — 绿色边框 + 绿色色条 */
    lv_obj_t *card_spray = create_sensor_card(parent, &style_bar_green,
        COLOR_HEALTHY, "喷药状态", "关闭", &label_spray_val);
    lv_obj_align(card_spray, LV_ALIGN_TOP_LEFT, 0, 8);

    /* 警报灯 — 红色边框 + 红色色条 */
    lv_obj_t *card_alight = create_sensor_card(parent, &style_bar_red,
        COLOR_DANGER, "警报灯", "关闭", &label_alarm_light_val);
    lv_obj_align(card_alight, LV_ALIGN_TOP_LEFT, 75, 8);

    /* 警报声 — 红色边框 + 红色色条 */
    lv_obj_t *card_asound = create_sensor_card(parent, &style_bar_red,
        COLOR_DANGER, "警报声", "关闭", &label_alarm_sound_val);
    lv_obj_align(card_asound, LV_ALIGN_TOP_LEFT, 150, 8);

    /* ====== 第二排：模块状态卡片 (x=0 / 75 / 150, y=105) ====== */
    /* WiFi — 蓝色边框 + 蓝色色条 */
    lv_obj_t *card_wifi = create_sensor_card(parent, &style_bar_blue,
        COLOR_INFO, "WiFi", "正常", &label_wifi_val);
    lv_obj_align(card_wifi, LV_ALIGN_TOP_LEFT, 0, 105);

    /* SHT30 — 橙色边框 + 橙色色条（与温度卡片一致） */
    lv_obj_t *card_sht30 = create_sensor_card(parent, &style_bar_orange,
        COLOR_OPERATE, "SHT30", "正常", &label_sht30_val);
    lv_obj_align(card_sht30, LV_ALIGN_TOP_LEFT, 75, 105);

    /* BH1750 — 黄色边框 + 黄色色条（与光照卡片一致） */
    lv_obj_t *card_bh1750 = create_sensor_card(parent, &style_bar_yellow,
        COLOR_WARNING, "BH1750", "正常", &label_bh1750_val);
    lv_obj_align(card_bh1750, LV_ALIGN_TOP_LEFT, 150, 105);

    /* ====== 第三排：传感器卡片 (x=0 / 75 / 150, y=202) ====== */
    /* 温度传感器 — 橙色边框 + 橙色色条 */
    lv_obj_t *card_temp = create_sensor_card(parent, &style_bar_orange,
        COLOR_OPERATE, "温度", "25°C", &label_sys_temp_val);
    lv_obj_align(card_temp, LV_ALIGN_TOP_LEFT, 0, 202);

    /* 湿度传感器 — 蓝色边框 + 蓝色色条 */
    lv_obj_t *card_humi = create_sensor_card(parent, &style_bar_blue,
        COLOR_INFO, "湿度", "60%", &label_sys_humi_val);
    lv_obj_align(card_humi, LV_ALIGN_TOP_LEFT, 75, 202);

    /* 光照传感器 — 黄色边框 + 黄色色条 */
    lv_obj_t *card_lux = create_sensor_card(parent, &style_bar_yellow,
        COLOR_WARNING, "光照", "1200 lx", &label_sys_lux_val);
    lv_obj_align(card_lux, LV_ALIGN_TOP_LEFT, 150, 202);

    /* ====== 保存按钮 (y=295) ====== */
    btn_save_log = lv_btn_create(parent);
    lv_obj_set_size(btn_save_log, 140, 32);
    lv_obj_align(btn_save_log, LV_ALIGN_TOP_LEFT, 52, 295);
    lv_obj_set_style_bg_color(btn_save_log, lv_color_hex(0x2196F3), 0);
    lv_obj_set_style_radius(btn_save_log, 6, 0);
    lv_obj_add_event_cb(btn_save_log, save_log_btn_cb, LV_EVENT_CLICKED, NULL);
    label_save_log = lv_label_create(btn_save_log);
    lv_label_set_text(label_save_log, "保存当前数据");
    lv_obj_set_style_text_font(label_save_log, &myfont, 0);
    lv_obj_center(label_save_log);
}

/* ================================================================
 *  create_detect_page - 构建第3页：病害检测
 *
 *  页面布局（内容区 244×216）：
 *
 *  ┌──────────────────────────────────┐ Y=4
 *  │ 当前植物                  番茄    │  ← 植物卡片 240×38
 *  ├──────────────────────────────────┤ Y=46
 *  │ 病害                       无    │  ← 检测结果卡片 240×90
 *  │ 置信度                    --%    │     (病害名 + 置信度 + 检测时间)
 *  │ 上次检测                 --:--   │
 *  └──────────────────────────────────┘
 *
 *  每张卡片内采用左右对齐布局：左侧灰色字段名，右侧黑色数值。
 *  病害名在有病害时切换为红色（#E6002D），无病害时显示绿色 "无"。
 *
 *  底部添加"开始检测"按钮，点击切换检测状态。
 * ================================================================ */

static void btn_detect_event_cb(lv_event_t *e)
{
    (void)e;
    detect_running = !detect_running;
    /* 统一走 k230_cmd 通路：1=开始 2=停止 */
    SharedPestData.k230_cmd = detect_running ? 1 : 2;
    SharedPestData.k230_cmd_seq++;

    if (detect_running) {
        lv_label_set_text(label_btn_detect, "停止检测");
        lv_obj_set_style_bg_color(btn_detect, lv_color_hex(0xE6002D), 0);
    } else {
        lv_label_set_text(label_btn_detect, "开始检测");
        lv_obj_set_style_bg_color(btn_detect, lv_color_hex(0x2E7D32), 0);
        /* 停止时恢复占位符 */
        SharedPestData.plant[0] = '-';
        SharedPestData.plant[1] = '\0';
        SharedPestData.pest[0] = '-';
        SharedPestData.pest[1] = '\0';
        SharedPestData.confidence = 0;
        SharedPestData.pest_valid = 0;
        SharedPestData.pest_seq++;
        update_detect_result("-", "-", 0);
    }
}

void sync_detect_button(uint8_t detect_cmd)
{
    uint8_t new_state = detect_cmd ? 1 : 0;
    if (new_state == detect_running) return;
    detect_running = new_state;

    if (detect_running) {
        lv_label_set_text(label_btn_detect, "停止检测");
        lv_obj_set_style_bg_color(btn_detect, lv_color_hex(0xE6002D), 0);
    } else {
        lv_label_set_text(label_btn_detect, "开始检测");
        lv_obj_set_style_bg_color(btn_detect, lv_color_hex(0x2E7D32), 0);
        SharedPestData.plant[0] = '-';
        SharedPestData.plant[1] = '\0';
        SharedPestData.pest[0] = '-';
        SharedPestData.pest[1] = '\0';
        SharedPestData.confidence = 0;
        SharedPestData.pest_valid = 0;
        SharedPestData.pest_seq++;
        update_detect_result("-", "-", 0);
    }
}

static void create_detect_page(lv_obj_t *parent)
{
    /* 允许滚动以容纳更多控件 */
    lv_obj_set_style_pad_bottom(parent, 8, 0);

    /* ====== 植物卡片 240×38 (y=4) ====== */
    {
        lv_obj_t *card = lv_obj_create(parent);
        lv_obj_add_style(card, &style_card_bg, 0);
        lv_obj_set_size(card, 240, 38);
        lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 4);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lab = lv_label_create(card);
        lv_obj_add_style(lab, &style_card_title, 0);
        lv_label_set_text(lab, "当前植物");
        lv_obj_align(lab, LV_ALIGN_LEFT_MID, 8, 0);

        label_plant_name = lv_label_create(card);
        lv_obj_add_style(label_plant_name, &style_card_value, 0);
        lv_label_set_text(label_plant_name, "-");
        lv_obj_align(label_plant_name, LV_ALIGN_RIGHT_MID, -8, 0);
    }

    /* ====== 检测结果卡片 240×90 (y=46) ====== */
    {
        lv_obj_t *card = lv_obj_create(parent);
        lv_obj_add_style(card, &style_card_bg, 0);
        lv_obj_set_size(card, 240, 90);
        lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 46);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lab1 = lv_label_create(card);
        lv_obj_add_style(lab1, &style_card_title, 0);
        lv_label_set_text(lab1, "病害");
        lv_obj_align(lab1, LV_ALIGN_TOP_LEFT, 8, 10);

        label_disease_name = lv_label_create(card);
        lv_obj_add_style(label_disease_name, &style_card_value, 0);
        lv_label_set_text(label_disease_name, "-");
        lv_obj_set_style_text_color(label_disease_name, COLOR_HEALTHY, 0);
        lv_obj_align(label_disease_name, LV_ALIGN_TOP_RIGHT, -8, 10);

        lv_obj_t *lab2 = lv_label_create(card);
        lv_obj_add_style(lab2, &style_card_title, 0);
        lv_label_set_text(lab2, "置信度");
        lv_obj_align(lab2, LV_ALIGN_TOP_LEFT, 8, 38);

        label_confidence = lv_label_create(card);
        lv_obj_add_style(label_confidence, &style_card_value, 0);
        lv_label_set_text(label_confidence, "-");
        lv_obj_align(label_confidence, LV_ALIGN_TOP_RIGHT, -8, 38);

        lv_obj_t *lab3 = lv_label_create(card);
        lv_obj_add_style(lab3, &style_card_title, 0);
        lv_label_set_text(lab3, "上次检测");
        lv_obj_align(lab3, LV_ALIGN_TOP_LEFT, 8, 66);

        label_detect_time = lv_label_create(card);
        lv_obj_add_style(label_detect_time, &style_card_value, 0);
        lv_label_set_text(label_detect_time, "-");
        lv_obj_align(label_detect_time, LV_ALIGN_TOP_RIGHT, -8, 66);
    }

    /* ====== 开始检测按钮 200×36 (y=142) ====== */
    btn_detect = lv_btn_create(parent);
    lv_obj_set_size(btn_detect, 200, 32);
    lv_obj_align(btn_detect, LV_ALIGN_TOP_MID, 0, 140);
    lv_obj_set_style_bg_color(btn_detect, lv_color_hex(0x2E7D32), 0);
    lv_obj_set_style_radius(btn_detect, 16, 0);
    lv_obj_clear_flag(btn_detect, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_detect, btn_detect_event_cb, LV_EVENT_CLICKED, NULL);

    label_btn_detect = lv_label_create(btn_detect);
    lv_label_set_text(label_btn_detect, "开始检测");
    lv_obj_set_style_text_color(label_btn_detect, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_btn_detect, &myfont, 0);
    lv_obj_center(label_btn_detect);
}

/* ================================================================
 *  create_preview_page - 构建第4页：画面预览
 *
 *  页面布局（内容区 244×216）：
 *
 *  ┌──────────────────────────────────┐ Y=6
 *  │                                  │
 *  │       240 × 188 图像预览区       │  ← 白色卡片背景
 *  │       (lv_img 图像对象)          │     带 "等待图像..." 提示
 *  │                                  │
 *  └──────────────────────────────────┘
 *
 *  图像数据通过外部 update_preview_image() 接口注入。
 *  支持格式：LV_IMG_CF_TRUE_COLOR（RGB565）/ LV_IMG_CF_RAW（JPEG）。
 *  无图像时提示标签居中显示，图像置顶覆盖提示文字。
 * ================================================================ */
static void create_preview_page(lv_obj_t *parent)
{
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* 图像卡片容器 240×188，居中靠上 */
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_add_style(card, &style_card_bg, 0);
    lv_obj_set_size(card, 240, 188);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* 初始化空图像描述符（等待外部数据注入） */
    memset(&preview_img_dsc, 0, sizeof(lv_img_dsc_t));
    preview_img_dsc.header.always_zero = 0;                     /* LVGL 要求此字段为 0 */
    preview_img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;           /* 默认 RGB565 真彩   */

    /* 图像对象，居中于卡片内（初始无数据） */
    img_preview = lv_img_create(card);
    lv_img_set_src(img_preview, &preview_img_dsc);
    lv_obj_center(img_preview);

    /* 提示标签（无图像时显示，置于图像下层） */
    lv_obj_t *hint = lv_label_create(card);
    lv_obj_add_style(hint, &style_card_title, 0);
    lv_label_set_text(hint, "等待图像...");
    lv_obj_center(hint);
    lv_obj_move_foreground(img_preview);                        /* 图像置顶，提示在下 */
}

/* ================================================================
 *  threshold_slider_cb - 阈值滑动条统一回调
 *
 *  三个滑动条共用此回调，通过比较事件目标区分是哪个滑动条。
 *  拖动时实时更新右侧数值标签。
 * ================================================================ */
static void threshold_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t val = lv_slider_get_value(slider);

    if (threshold_syncing) return;

    if (slider == slider_temp) {
        char buf[32];
        snprintf(buf, sizeof(buf), "温度阈值 %" LV_PRId32 "°C", val);
        lv_label_set_text(label_temp_val, buf);
        SharedThresholdData.temp_threshold = (uint16_t)val;
        SharedThresholdData.threshold_seq++;
        printf("V3F: 温度阈值 → %d°C\r\n", val);
    } else if (slider == slider_humi) {
        char buf[32];
        snprintf(buf, sizeof(buf), "湿度阈值 %" LV_PRId32 "%%", val);
        lv_label_set_text(label_humi_val, buf);
        SharedThresholdData.humi_threshold = (uint16_t)val;
        SharedThresholdData.threshold_seq++;
        printf("V3F: 湿度阈值 → %d%%\r\n", val);
    } else if (slider == slider_light) {
        char buf[32];
        snprintf(buf, sizeof(buf), "光照阈值 %" LV_PRId32 " lx", val);
        lv_label_set_text(label_light_val, buf);
        SharedThresholdData.light_threshold = (uint16_t)val;
        SharedThresholdData.threshold_seq++;
        printf("V3F: 光照阈值 → %d lx\r\n", val);
    }
}

/* ================================================================
 *  create_threshold_page - 构建第5页：阈值设置
 *
 *  页面布局（内容区 244×216）：
 *
 *        [============●============]  Y=22
 *            温度阈值 20°C             Y=48
 *
 *        [============●============]  Y=86
 *            湿度阈值 50%              Y=112
 *
 *        [============●============]  Y=150
 *         光照阈值 0 lx                Y=176
 *
 *  三个滑动条居中，数值标签（黑色中文）置于滑动条正下方。
 *  滑动条规格：
 *    - 温度阈值：范围 0 ~ 50，默认 20
 *    - 湿度阈值：范围 0 ~ 100，默认 50
 *    - 光照阈值：范围 0 ~ 5000，默认 0（禁用）
 * ================================================================ */
static void create_threshold_page(lv_obj_t *parent)
{
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* 滑动条通用尺寸：宽 200，高 10 */
    const int slider_w = 200;
    const int slider_h = 10;
    const int slider_x = 22;       /* 内容区 244，200 宽居中偏移 = (244-200)/2 */

    /* ====== 第1行：温度阈值 ====== */
    {
        slider_temp = lv_slider_create(parent);
        lv_slider_set_range(slider_temp, 0, 50);
        lv_slider_set_value(slider_temp, 20, LV_ANIM_OFF);
        lv_obj_set_size(slider_temp, slider_w, slider_h);
        lv_obj_set_style_bg_color(slider_temp, COLOR_DANGER, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider_temp, COLOR_DANGER, LV_PART_KNOB);
        lv_obj_align(slider_temp, LV_ALIGN_TOP_LEFT, slider_x, 22);
        lv_obj_add_event_cb(slider_temp, threshold_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

        label_temp_val = lv_label_create(parent);
        lv_obj_add_style(label_temp_val, &style_card_value, 0);
        lv_label_set_text(label_temp_val, "温度阈值 20°C");
        lv_obj_align(label_temp_val, LV_ALIGN_TOP_MID, 0, 48);
    }

    /* ====== 第2行：湿度阈值 ====== */
    {
        slider_humi = lv_slider_create(parent);
        lv_slider_set_range(slider_humi, 0, 100);
        lv_slider_set_value(slider_humi, 50, LV_ANIM_OFF);
        lv_obj_set_size(slider_humi, slider_w, slider_h);
        lv_obj_set_style_bg_color(slider_humi, COLOR_INFO, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider_humi, COLOR_INFO, LV_PART_KNOB);
        lv_obj_align(slider_humi, LV_ALIGN_TOP_LEFT, slider_x, 86);
        lv_obj_add_event_cb(slider_humi, threshold_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

        label_humi_val = lv_label_create(parent);
        lv_obj_add_style(label_humi_val, &style_card_value, 0);
        lv_label_set_text(label_humi_val, "湿度阈值 50%");
        lv_obj_align(label_humi_val, LV_ALIGN_TOP_MID, 0, 112);
    }

    /* ====== 第3行：光照阈值 ====== */
    {
        slider_light = lv_slider_create(parent);
        lv_slider_set_range(slider_light, 0, 5000);
        lv_slider_set_value(slider_light, 0, LV_ANIM_OFF);
        lv_obj_set_size(slider_light, slider_w, slider_h);
        lv_obj_set_style_bg_color(slider_light, COLOR_WARNING, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider_light, COLOR_WARNING, LV_PART_KNOB);
        lv_obj_align(slider_light, LV_ALIGN_TOP_LEFT, slider_x, 150);
        lv_obj_add_event_cb(slider_light, threshold_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

        label_light_val = lv_label_create(parent);
        lv_obj_add_style(label_light_val, &style_card_value, 0);
        lv_label_set_text(label_light_val, "光照阈值 0 lx");
        lv_obj_align(label_light_val, LV_ALIGN_TOP_MID, 0, 176);
    }
}

/* ================================================================
 *  公共接口函数 — 供外部 C 代码调用来更新界面数据
 *
 *  以下函数由外部模块（如串口解析、网络请求、传感器采集）调用，
 *  传入新数据后立即刷新对应标签的显示内容。
 * ================================================================ */

/**
 * @brief  更新时间信息（顶部栏时间 + 首页时钟 + 首页日期）
 *
 * @param  datetime  日期时间字符串，格式 "HH:MM YYYY年MM月DD日 星期X"
 *                   前半部分（到第一个空格）为时间，后半部分为日期
 *
 *  解析 datetime 字符串：空格前为 HH:MM 时钟，空格后为完整日期。
 *  同时更新顶部栏时间和首页大时钟、日期标签。
 */
void update_time_info(const char *datetime)
{
    /* 如果时间相关标签全为空，说明界面尚未初始化，安全退出 */
    if (label_time_top == NULL && label_clock == NULL) return;

    char clock_str[16] = {0};   /* "HH:MM:SS" 缓冲区 */
    char date_str[48] = {0};    /* 日期缓冲区（需容纳 "2026年6月16日 星期一" + '\0'） */

    /* 解析时间部分：取到第一个空格或最多 5 个字符 */
    int len = 0;
    while (datetime[len] && datetime[len] != ' ') len++;
    if (len > 8) len = 8;
    for (int i = 0; i < len; i++) clock_str[i] = datetime[i];

    /* 解析日期部分：跳过第一个空格后的所有内容 */
    int j = 0;
    const char *p = datetime;
    while (*p && *p != ' ') p++;
    if (*p == ' ') p++;
    while (*p && j < (int)sizeof(date_str) - 1) date_str[j++] = *p++;
    date_str[j] = '\0';

    /* 更新：顶部栏只显示 HH:MM，首页大时钟显示 HH:MM:SS */
    if (label_time_top) {
        char top_str[8] = {0};
        int top_len = len > 5 ? 5 : len;
        for (int i = 0; i < top_len; i++) top_str[i] = clock_str[i];
        lv_label_set_text(label_time_top, top_str);
    }
    if (label_clock)    lv_label_set_text(label_clock, clock_str);
    if (label_date)     lv_label_set_text(label_date, date_str);
}

/**
 * @brief  更新首页天气信息（城市、天气、温度）
 *
 * @param  city     城市名称（如 "南宁"）
 * @param  weather  天气状况（如 "多云"、"晴"）
 * @param  temp     气温（℃），有符号整型
 */
void update_weather_info(const char *city, const char *weather, int8_t temp)
{
    if (label_city)    lv_label_set_text(label_city, city);      /* 城市名            */
    if (label_weather) lv_label_set_text(label_weather, weather); /* 天气状况          */

    if (label_temp_card) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d°C", temp);               /* 格式化为 "25°C"  */
        lv_label_set_text(label_temp_card, buf);
    }
}

/**
 * @brief  一次性更新首页全部信息（合并时间 + 天气）
 *
 * @param  datetime 日期时间字符串
 * @param  city     城市名称
 * @param  weather  天气状况
 * @param  temp     气温
 *
 *  内部调用 update_time_info() 和 update_weather_info()。
 */
void update_home_info(const char *datetime, const char *city,
                      const char *weather, int8_t temp)
{
    update_time_info(datetime);
    update_weather_info(city, weather, temp);
}

/**
 * @brief  更新传感器数据（首页 + 系统状态页同步更新）
 *
 * @param  temp   温度值（℃）
 * @param  humi   湿度值（%）
 * @param  light  光照强度（lux）
 *
 *  同时更新首页的 "环境" 区块和系统状态页的温度/湿度/光照卡片。
 */
void update_sensor_data(float temp, float humi, uint16_t light)
{
    /* —— 首页环境区块 —— */
    if (label_temp_env) {
        char buf[16];
        snprintf(buf, sizeof(buf), "温度: %.1f°C", temp);
        lv_label_set_text(label_temp_env, buf);
    }
    if (label_humi) {
        char buf[16];
        snprintf(buf, sizeof(buf), "湿度: %.1f%%", humi);
        lv_label_set_text(label_humi, buf);
    }
    if (label_light) {
        char buf[16];
        snprintf(buf, sizeof(buf), "光照: %u lx", light);
        lv_label_set_text(label_light, buf);
    }

    /* —— 系统状态页传感器卡片 —— */
    if (label_sys_temp_val) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%.1f°C", temp);
        lv_label_set_text(label_sys_temp_val, buf);
    }
    if (label_sys_humi_val) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%.1f%%", humi);
        lv_label_set_text(label_sys_humi_val, buf);
    }
    if (label_sys_lux_val) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%u lx", light);
        lv_label_set_text(label_sys_lux_val, buf);
    }
}

/**
 * @brief  更新喷药开关状态
 *
 * @param  active  true="开启"，false="关闭"
 */
void update_spray_status(bool active)
{
    if (label_spray_val)
        lv_label_set_text(label_spray_val, active ? "开启" : "关闭");
}

/**
 * @brief  更新声光警报开关状态
 *
 * @param  sound_active  警报声是否开启
 * @param  light_active  警报灯是否开启
 */
void update_alarm_status(bool sound_active, bool light_active)
{
    if (label_alarm_sound_val)
        lv_label_set_text(label_alarm_sound_val, sound_active ? "开启" : "关闭");
    if (label_alarm_light_val)
        lv_label_set_text(label_alarm_light_val, light_active ? "开启" : "关闭");
}

void update_module_status(uint8_t wifi, uint8_t bh1750, uint8_t sht30)
{
    if (label_wifi_val) {
        lv_label_set_text(label_wifi_val, wifi ? "正常" : "异常");
        lv_obj_set_style_text_color(label_wifi_val, wifi ? COLOR_HEALTHY : COLOR_DANGER, 0);
    }
    if (label_bh1750_val) {
        lv_label_set_text(label_bh1750_val, bh1750 ? "正常" : "异常");
        lv_obj_set_style_text_color(label_bh1750_val, bh1750 ? COLOR_HEALTHY : COLOR_DANGER, 0);
    }
    if (label_sht30_val) {
        lv_label_set_text(label_sht30_val, sht30 ? "正常" : "异常");
        lv_obj_set_style_text_color(label_sht30_val, sht30 ? COLOR_HEALTHY : COLOR_DANGER, 0);
    }
}

/**
 * @brief  更新病害检测页的检测结果
 *
 * @param  plant       植物名称（如 "番茄"），NULL 不更新
 * @param  disease     病害名称（如 "晚疫病"），空字符串或 NULL 表示 "无"
 * @param  confidence  识别置信度（0 ~ 100）
 */
void update_detect_result(const char *plant, const char *disease,
                          uint8_t confidence)
{
    char buf[16];

    /* 更新植物名称（仅当非 NULL 时更新） */
    if (label_plant_name && plant) {
        lv_label_set_text(label_plant_name, plant);
    }

    /* 更新病害名称，根据是否有病害切换颜色 */
    if (label_disease_name) {
        if (confidence == 0) {
            /* K230 未检测到病害 */
            lv_label_set_text(label_disease_name, "未检测到病害");
            lv_obj_set_style_text_color(label_disease_name, COLOR_HEALTHY, 0);
        } else if (disease && strlen(disease) > 0) {
            lv_label_set_text(label_disease_name, disease);
            lv_obj_set_style_text_color(label_disease_name, COLOR_DANGER, 0);
        } else {
            lv_label_set_text(label_disease_name, "-");
            lv_obj_set_style_text_color(label_disease_name, COLOR_HEALTHY, 0);
        }
    }

    /* 更新置信度百分比 */
    if (label_confidence) {
        if (confidence > 0) {
            snprintf(buf, sizeof(buf), "%d%%", confidence);
            lv_label_set_text(label_confidence, buf);
        } else {
            lv_label_set_text(label_confidence, "-");
        }
    }

    /* 检测时间固定为 "刚刚" */
    if (label_detect_time) {
        lv_label_set_text(label_detect_time, "刚刚");
    }
}

/**
 * @brief  更新画面预览图像
 *
 * @param  data       RGB565 像素数据指针（或 JPEG 原始数据）
 * @param  w          图像宽度（像素）
 * @param  h          图像高度（像素）
 * @param  cf         颜色格式：LV_IMG_CF_TRUE_COLOR（RGB565）或 LV_IMG_CF_RAW（JPEG）
 * @param  data_size  数据字节数（RGB565 = w*h*2，JPEG = 文件大小）
 *
 *  动态更新第4页（画面预览）中显示的图像。
 *  调用后预览区域立即刷新为新图像，图像对象置顶覆盖提示文字。
 */
void update_preview_image(const uint8_t *data, uint16_t w, uint16_t h,
                          lv_img_cf_t cf, uint32_t data_size)
{
    /* 填充图像描述符字段 */
    preview_img_dsc.header.always_zero = 0;
    preview_img_dsc.header.w = w;
    preview_img_dsc.header.h = h;
    preview_img_dsc.header.cf = cf;
    preview_img_dsc.data_size = data_size;
    preview_img_dsc.data = data;

    /* 刷新图像对象以显示新数据 */
    lv_img_set_src(img_preview, &preview_img_dsc);
}

/**
 * @brief  设置预览图像缩放倍率
 *
 * @param  zoom  缩放因子：256=1x, 512=2x, 768=3x ...
 */
void set_preview_zoom(uint16_t zoom)
{
    if (img_preview) lv_img_set_zoom(img_preview, zoom);
}

/* ================================================================
 *  show_boot_animation - 显示开机动画
 *
 *  在屏幕中央显示 slogan 图片（256×64），图片下方显示
 *  "系统启动中..." 提示文字。全屏黑色背景遮盖后续初始化过程。
 *
 *  调用方式：LVGL 初始化完成后、WiFi/网络等耗时初始化之前调用。
 *           调用期间仍需周期性执行 lv_task_handler() 刷新画面。
 * ================================================================ */
void show_boot_animation(void)
{
    /* 创建全屏黑色背景容器 */
    boot_screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(boot_screen, 320, 240);
    lv_obj_set_pos(boot_screen, 0, 0);
    lv_obj_set_style_bg_color(boot_screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(boot_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(boot_screen, 0, 0);
    lv_obj_set_style_pad_all(boot_screen, 0, 0);
    lv_obj_clear_flag(boot_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* slogan 图片：256×64，水平居中，y=70（偏上留出文字空间） */
    boot_img = lv_img_create(boot_screen);
    lv_img_set_src(boot_img, &slogan);
    lv_obj_align(boot_img, LV_ALIGN_TOP_MID, 0, 70);

    /* "系统启动中..." 提示文字，图片下方居中 */
    boot_label = lv_label_create(boot_screen);
    lv_obj_set_style_text_color(boot_label, lv_color_black(), 0);
    lv_obj_set_style_text_font(boot_label, &myfont, 0);
    lv_label_set_text(boot_label, "系统启动中...");
    lv_obj_align(boot_label, LV_ALIGN_TOP_MID, -4, 145);
}

/* ================================================================
 *  hide_boot_animation - 隐藏开机动画并切换到主界面
 *
 *  删除开机画面所有对象，调用 my_gui() 创建正常四页 TABVIEW 界面。
 *
 *  调用时机：WiFi 连接、OneNET 注册、天气获取、RTC 同步等
 *           全部网络初始化完成后调用。
 * ================================================================ */
void hide_boot_animation(void)
{
    if (boot_screen != NULL) {
        lv_obj_del(boot_screen);
        boot_screen = NULL;
        boot_img    = NULL;
        boot_label  = NULL;
    }
}

/* ================================================================
 *  my_gui - GUI 初始化入口函数
 *
 *  由 main.c 中的 WinMain() 函数调用，完成整个图形界面的初始化：
 *    1. 初始化所有全局样式
 *    2. 创建顶部信息栏
 *    3. 创建 TABVIEW 并构建四个页面
 *
 *  该函数只调用一次，后续数据更新通过各个 update_xxx() 接口完成。
 * ================================================================ */
void my_gui(void)
{
    /* 步骤 1：初始化所有全局样式 */
    init_styles();

    /* 步骤 2：创建顶部栏（固定 24px 高） */
    create_top_bar();

    /* 步骤 3：创建 TABVIEW 并构建全部四个页面 */
    create_tabview();
}

/* ================================================================
 *  update_threshold_from_shared - 从共享内存同步阈值到 LVGL 滑动条
 *
 *  当 V5F 通过云平台下发阈值设置时，共享内存中的阈值会更新。
 *  此函数被 lvgl_task 周期性调用，检测阈值变化并更新 LVGL 显示。
 * ================================================================ */
void update_threshold_from_shared(uint16_t temp_th, uint16_t humi_th, uint16_t light_th)
{
    char buf[32];

    threshold_syncing = 1;

    lv_slider_set_value(slider_temp, temp_th, LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "温度阈值 %u°C", temp_th);
    lv_label_set_text(label_temp_val, buf);

    lv_slider_set_value(slider_humi, humi_th, LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "湿度阈值 %u%%", humi_th);
    lv_label_set_text(label_humi_val, buf);

    lv_slider_set_value(slider_light, light_th, LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "光照阈值 %u lx", light_th);
    lv_label_set_text(label_light_val, buf);

    threshold_syncing = 0;
}

/* ================================================================
 *  小车控制页 — 事件回调
 * ================================================================ */

/** 模式下拉框回调 */
static void car_mode_dd_cb(lv_event_t *e)
{
    if (car_syncing) return;
    lv_obj_t *dd = lv_event_get_target(e);
    uint16_t sel = lv_dropdown_get_selected(dd);
    if (sel > 2) return;

    SharedCarData.car_mode = sel;
    SharedCarData.car_seq++;
}

/** 速度下拉框回调 */
static void car_speed_dd_cb(lv_event_t *e)
{
    if (car_syncing) return;
    lv_obj_t *dd = lv_event_get_target(e);
    uint16_t sel = lv_dropdown_get_selected(dd);
    if (sel > 3) return;

    uint8_t gear = sel + 1;
    SharedCarData.car_speed = gear;
    SharedCarData.car_seq++;
}

/** 方向按钮按下回调 */
static void car_dir_pressed_cb(lv_event_t *e)
{
    uint8_t cmd = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    SharedCarData.car_cmd = cmd;
    SharedCarData.car_seq++;
}

/** 方向按钮松开回调 — 发送停止命令 */
static void car_dir_released_cb(lv_event_t *e)
{
    SharedCarData.car_cmd = 5;  /* CAR_CMD_STOP */
    SharedCarData.car_seq++;
}

/* ================================================================
 *  create_car_page - 构建第6页：小车控制
 *
 *  页面布局（内容区 244×216）：
 *
 *  ┌──────────────────────────────────┐ Y=0
 *  │  模式            速度             │  ← 标签行 (y=2)
 *  │  [避障▼手动▼跟随] [前进一▼...]   │  ← 下拉框行 (y=20)
 *  ├──────────────────────────────────┤ Y=56
 *  │            [▲]                   │
 *  │        [◀] [STOP] [▶]           │  ← 十字方向键
 *  │            [▼]                   │
 *  └──────────────────────────────────┘ Y=216
 * ================================================================ */
static void create_car_page(lv_obj_t *parent)
{
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* ====== 左半区：模式 ====== */
    lv_obj_t *lbl_mode = lv_label_create(parent);
    lv_obj_add_style(lbl_mode, &style_card_title, 0);
    lv_label_set_text(lbl_mode, "模式");
    lv_obj_align(lbl_mode, LV_ALIGN_TOP_LEFT, 4, 2);

    dd_car_mode = lv_dropdown_create(parent);
    lv_dropdown_set_options(dd_car_mode, "避障\n手动\n跟随");
    lv_dropdown_set_selected(dd_car_mode, 1);
    lv_obj_set_size(dd_car_mode, 100, 28);
    lv_obj_align(dd_car_mode, LV_ALIGN_TOP_LEFT, 4, 16);
    lv_obj_set_style_text_font(dd_car_mode, &myfont, LV_PART_MAIN);
    lv_obj_set_style_text_font(dd_car_mode, LV_FONT_DEFAULT, LV_PART_INDICATOR);
    lv_obj_t *dd_list_mode = lv_dropdown_get_list(dd_car_mode);
    if (dd_list_mode) {
        lv_obj_set_style_text_font(dd_list_mode, &myfont, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_width(dd_list_mode, 100);
        lv_obj_clear_flag(dd_list_mode, LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_add_event_cb(dd_car_mode, car_mode_dd_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* ====== 右半区：速度 ====== */
    lv_obj_t *lbl_speed = lv_label_create(parent);
    lv_obj_add_style(lbl_speed, &style_card_title, 0);
    lv_label_set_text(lbl_speed, "速度");
    lv_obj_align(lbl_speed, LV_ALIGN_TOP_LEFT, 124, 2);

    dd_car_speed = lv_dropdown_create(parent);
    lv_dropdown_set_options(dd_car_speed, "前进一\n前进二\n前进三\n前进四");
    lv_dropdown_set_selected(dd_car_speed, 1);
    lv_obj_set_size(dd_car_speed, 100, 28);
    lv_obj_align(dd_car_speed, LV_ALIGN_TOP_LEFT, 124, 16);
    lv_obj_set_style_text_font(dd_car_speed, &myfont, LV_PART_MAIN);
    lv_obj_set_style_text_font(dd_car_speed, LV_FONT_DEFAULT, LV_PART_INDICATOR);
    lv_obj_t *dd_list_speed = lv_dropdown_get_list(dd_car_speed);
    if (dd_list_speed) {
        lv_obj_set_style_text_font(dd_list_speed, &myfont, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_width(dd_list_speed, 100);
        lv_obj_clear_flag(dd_list_speed, LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_add_event_cb(dd_car_speed, car_speed_dd_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* ====== 十字方向键 ====== */
    int cx = 114, cy = 127;  /* 中心点 */
    int btn_sz = 38;
    int gap = 47;

    /* 上 */
    lv_obj_t *btn_up = lv_btn_create(parent);
    lv_obj_set_size(btn_up, btn_sz, btn_sz);
    lv_obj_align(btn_up, LV_ALIGN_TOP_LEFT, cx - btn_sz/2, cy - gap - btn_sz/2);
    lv_obj_set_style_bg_color(btn_up, lv_color_hex(0x66BB6A), 0);
    lv_obj_set_style_radius(btn_up, 10, 0);
    lv_obj_add_event_cb(btn_up, car_dir_pressed_cb, LV_EVENT_PRESSED, (void *)(uintptr_t)1);
    lv_obj_add_event_cb(btn_up, car_dir_released_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_t *lbl_up = lv_label_create(btn_up);
    lv_label_set_text(lbl_up, LV_SYMBOL_UP);
    lv_obj_center(lbl_up);

    /* 下 */
    lv_obj_t *btn_down = lv_btn_create(parent);
    lv_obj_set_size(btn_down, btn_sz, btn_sz);
    lv_obj_align(btn_down, LV_ALIGN_TOP_LEFT, cx - btn_sz/2, cy + gap - btn_sz/2);
    lv_obj_set_style_bg_color(btn_down, lv_color_hex(0xFFA726), 0);
    lv_obj_set_style_radius(btn_down, 10, 0);
    lv_obj_add_event_cb(btn_down, car_dir_pressed_cb, LV_EVENT_PRESSED, (void *)(uintptr_t)2);
    lv_obj_add_event_cb(btn_down, car_dir_released_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_t *lbl_down = lv_label_create(btn_down);
    lv_label_set_text(lbl_down, LV_SYMBOL_DOWN);
    lv_obj_center(lbl_down);

    /* 左 */
    lv_obj_t *btn_left = lv_btn_create(parent);
    lv_obj_set_size(btn_left, btn_sz, btn_sz);
    lv_obj_align(btn_left, LV_ALIGN_TOP_LEFT, cx - gap - btn_sz/2, cy - btn_sz/2);
    lv_obj_set_style_bg_color(btn_left, lv_color_hex(0x42A5F5), 0);
    lv_obj_set_style_radius(btn_left, 10, 0);
    lv_obj_add_event_cb(btn_left, car_dir_pressed_cb, LV_EVENT_PRESSED, (void *)(uintptr_t)3);
    lv_obj_add_event_cb(btn_left, car_dir_released_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_t *lbl_left = lv_label_create(btn_left);
    lv_label_set_text(lbl_left, LV_SYMBOL_LEFT);
    lv_obj_center(lbl_left);

    /* 右 */
    lv_obj_t *btn_right = lv_btn_create(parent);
    lv_obj_set_size(btn_right, btn_sz, btn_sz);
    lv_obj_align(btn_right, LV_ALIGN_TOP_LEFT, cx + gap - btn_sz/2, cy - btn_sz/2);
    lv_obj_set_style_bg_color(btn_right, lv_color_hex(0xAB47BC), 0);
    lv_obj_set_style_radius(btn_right, 10, 0);
    lv_obj_add_event_cb(btn_right, car_dir_pressed_cb, LV_EVENT_PRESSED, (void *)(uintptr_t)4);
    lv_obj_add_event_cb(btn_right, car_dir_released_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_t *lbl_right = lv_label_create(btn_right);
    lv_label_set_text(lbl_right, LV_SYMBOL_RIGHT);
    lv_obj_center(lbl_right);

    /* 停止（中心） */
    lv_obj_t *btn_stop = lv_btn_create(parent);
    lv_obj_set_size(btn_stop, 46, 46);
    lv_obj_align(btn_stop, LV_ALIGN_TOP_LEFT, cx - 23, cy - 23);
    lv_obj_set_style_bg_color(btn_stop, lv_color_hex(0xEF5350), 0);
    lv_obj_set_style_radius(btn_stop, 23, 0);
    lv_obj_add_event_cb(btn_stop, car_dir_pressed_cb, LV_EVENT_PRESSED, (void *)(uintptr_t)5);
    lv_obj_t *lbl_stop = lv_label_create(btn_stop);
    lv_label_set_text(lbl_stop, "STOP");
    lv_obj_set_style_text_font(lbl_stop, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl_stop);
}

/* ================================================================
 *  update_car_status - 更新小车控制页状态显示
 *
 *  由 lvgl_task 调用，同步 V5F 小车状态到 LVGL 显示。
 * ================================================================ */
void update_car_status(uint8_t mode, uint8_t speed)
{
    car_syncing = 1;
    if (dd_car_mode && mode <= 2)
        lv_dropdown_set_selected(dd_car_mode, mode);

    if (dd_car_speed && speed >= 1 && speed <= 4)
        lv_dropdown_set_selected(dd_car_speed, speed - 1);
    car_syncing = 0;
}

/* ================================================================
 *  WiFi 配置页面 — 第7页
 *
 *  两个输入框（SSID / 密码）+ 键盘 + 连接按钮 + 状态显示
 * ================================================================ */
static lv_obj_t *ta_ssid;
static lv_obj_t *ta_pass;
static lv_obj_t *btn_connect;
static lv_obj_t *label_wifi_status;
static lv_obj_t *sw_mute;
static lv_obj_t *slider_vol;
static lv_obj_t *slider_tone;
static lv_obj_t *slider_spd;
static lv_obj_t *lbl_vol_val;
static lv_obj_t *lbl_tone_val;
static lv_obj_t *lbl_spd_val;
static lv_obj_t *ta_weather_loc;
static lv_obj_t *label_weather_status;
static lv_obj_t *dd_interval;

static void wifi_connect_cb(lv_event_t *e)
{
    (void)e;
    const char *ssid = lv_textarea_get_text(ta_ssid);
    const char *pass = lv_textarea_get_text(ta_pass);
    if (ssid[0] == '\0') return;

    strncpy((char *)SharedWiFiData.ssid, ssid, sizeof(SharedWiFiData.ssid) - 1);
    SharedWiFiData.ssid[sizeof(SharedWiFiData.ssid) - 1] = '\0';
    strncpy((char *)SharedWiFiData.password, pass, sizeof(SharedWiFiData.password) - 1);
    SharedWiFiData.password[sizeof(SharedWiFiData.password) - 1] = '\0';
    SharedWiFiData.wifi_cmd_seq++;

    lv_label_set_text(label_wifi_status, "连接中...");
    lv_obj_set_style_text_color(label_wifi_status, COLOR_WARNING, 0);
}

static void mute_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    _Bool is_on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    SharedThresholdData.mute = is_on ? 1 : 0;
    SharedThresholdData.threshold_seq++;

    SharedTtsData.tts_seq++;
    SharedTtsData.tts_pending = 1;
    strncpy((char *)SharedTtsData.text,
            is_on ? "已进入静音模式" : "已关闭静音模式",
            sizeof(SharedTtsData.text) - 1);
    SharedTtsData.text[sizeof(SharedTtsData.text) - 1] = '\0';
}

static void vol_slider_cb(lv_event_t *e)
{
    (void)e;
    int32_t v = lv_slider_get_value(slider_vol);
    SharedThresholdData.tts_volume = (uint8_t)v;
    SharedThresholdData.threshold_seq++;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", (int)v);
    lv_label_set_text(lbl_vol_val, buf);
}

static void tone_slider_cb(lv_event_t *e)
{
    (void)e;
    int32_t v = lv_slider_get_value(slider_tone);
    SharedThresholdData.tts_tone = (uint8_t)v;
    SharedThresholdData.threshold_seq++;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", (int)v);
    lv_label_set_text(lbl_tone_val, buf);
}

static void spd_slider_cb(lv_event_t *e)
{
    (void)e;
    int32_t v = lv_slider_get_value(slider_spd);
    SharedThresholdData.tts_speed = (uint8_t)v;
    SharedThresholdData.threshold_seq++;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", (int)v);
    lv_label_set_text(lbl_spd_val, buf);
}

static void interval_select_cb(lv_event_t *e)
{
    (void)e;
    /* 下拉选项：0=1分钟 1=2分钟 2=5分钟 3=10分钟 4=30分钟 5=60分钟 */
    static const uint16_t interval_table[] = {60, 120, 300, 600, 1800, 3600};
    static const char *interval_text[] = {"1分钟", "2分钟", "5分钟", "10分钟", "30分钟", "60分钟"};
    uint16_t idx = lv_dropdown_get_selected(dd_interval);
    if (idx < 6)
    {
        SharedThresholdData.log_interval_s = interval_table[idx];
        SharedThresholdData.threshold_seq++;
        printf("V3F: 日志间隔设为 %u 秒\r\n", interval_table[idx]);

        /* TTS 播报间隔已更改 */
        SharedTtsData.tts_seq++;
        SharedTtsData.tts_pending = 1;
        static char tts_buf[32];
        snprintf(tts_buf, sizeof(tts_buf), "保存间隔已设为%s", interval_text[idx]);
        strncpy((char *)SharedTtsData.text, tts_buf, sizeof(SharedTtsData.text) - 1);
        SharedTtsData.text[sizeof(SharedTtsData.text) - 1] = '\0';
    }
}

static void weather_loc_save_cb(lv_event_t *e)
{
    (void)e;
    const char *loc = lv_textarea_get_text(ta_weather_loc);
    if (loc[0] == '\0') return;

    strncpy((char *)SharedWeatherCmd.location, loc, sizeof(SharedWeatherCmd.location) - 1);
    SharedWeatherCmd.location[sizeof(SharedWeatherCmd.location) - 1] = '\0';
    SharedWeatherCmd.weather_cmd_seq++;

    lv_label_set_text(label_weather_status, "设置中...");
    lv_obj_set_style_text_color(label_weather_status, COLOR_WARNING, 0);
    printf("V3F: 天气地点设置请求=%s\r\n", loc);
}

static void keyboard_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL)
    {
        /* 确认或取消时销毁键盘释放内存 */
        lv_obj_t *kb = lv_event_get_target(e);
        if (kb)
        {
            /* 先解除 textarea 关联并清除焦点，防止再次点击无法弹出键盘 */
            lv_obj_t *ta = lv_keyboard_get_textarea(kb);
            if (ta) lv_obj_clear_state(ta, LV_STATE_FOCUSED);
            lv_obj_del(kb);
        }
    }
}

static void ta_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED)
    {
        /* 查找已存在的键盘，避免重复创建 */
        lv_obj_t *parent = lv_layer_top();
        lv_obj_t *kb = NULL;
        uint32_t cnt = lv_obj_get_child_cnt(parent);
        for (uint32_t i = 0; i < cnt; i++)
        {
            lv_obj_t *child = lv_obj_get_child(parent, i);
            if (lv_obj_check_type(child, &lv_keyboard_class))
            {
                kb = child;
                break;
            }
        }
        if (kb)
        {
            lv_keyboard_set_textarea(kb, lv_event_get_target(e));
        }
        else
        {
            kb = lv_keyboard_create(parent);
            lv_keyboard_set_textarea(kb, lv_event_get_target(e));
            lv_obj_set_size(kb, 320, 120);
            lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
            lv_obj_add_event_cb(kb, keyboard_event_cb, LV_EVENT_READY, NULL);
            lv_obj_add_event_cb(kb, keyboard_event_cb, LV_EVENT_CANCEL, NULL);
        }
    }
}

/* ================================================================
 *  create_history_page - 构建第7页：历史数据
 *
 *  布局：标题行 + 可滚动列表 + 底部翻页栏（上一页/页码/下一页）
 * ================================================================ */
/* 本地 SensorLog 解析结构体（与 V5F storage.h 中 SensorLog_t 布局一致） */
typedef struct {
    uint8_t  year_off;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    int16_t  temp_x10;
    uint16_t humi_x10;
    uint8_t  light_h;
    uint8_t  light_l;
    uint8_t  reserved[5];
} __attribute__((packed)) local_sensor_log_t;

#define HISTORY_PAGE_SIZE  SHARED_LOG_PAGE_SIZE

static void history_page_event_cb(lv_event_t *e)
{
    reset_clear_confirm();
    lv_obj_t *btn = lv_event_get_target(e);
    if (btn == btn_history_prev)
    {
        if (history_page > 0)
            history_page--;
    }
    else if (btn == btn_history_next)
    {
        if (history_page < history_max_page)
            history_page++;
    }
    /* 立即更新页码显示（不等 V5F 响应） */
    char page_str[16];
    snprintf(page_str, sizeof(page_str), "%u/%u", history_page + 1, history_max_page + 1);
    lv_label_set_text(label_history_page, page_str);
    /* 发送翻页请求 */
    SharedLogData.log_page = history_page;
    SharedLogData.log_req_count = HISTORY_PAGE_SIZE;
    SharedLogData.log_req_seq++;
    printf("V3F: 翻页请求 page=%u\r\n", history_page);
}

static void history_refresh_cb(lv_event_t *e)
{
    (void)e;
    reset_clear_confirm();
    /* 重新请求当前页数据 */
    SharedLogData.log_page = history_page;
    SharedLogData.log_req_count = HISTORY_PAGE_SIZE;
    SharedLogData.log_req_seq++;
    printf("V3F: 刷新历史数据 page=%u\r\n", history_page);
}

static void history_clear_cb(lv_event_t *e)
{
    (void)e;
    if (clear_confirm == 0)
    {
        clear_confirm = 1;
        lv_label_set_text(lv_obj_get_child(btn_history_clear, 0), "确认?");
        lv_obj_set_style_bg_color(btn_history_clear, lv_color_hex(0xFF9800), 0);
    }
    else
    {
        clear_confirm = 0;
        lv_label_set_text(lv_obj_get_child(btn_history_clear, 0), "清空");
        lv_obj_set_style_bg_color(btn_history_clear, lv_color_hex(0xE6002D), 0);
        SharedLogData.save_sensor_log = 3;
        printf("V3F: 请求清空所有历史数据\r\n");
    }
}

static void create_history_page(lv_obj_t *parent)
{
    /* 禁用 tab 页默认内边距，避免内容溢出产生滚动条 */
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_pad_row(parent, 0, 0);
    lv_obj_set_style_pad_column(parent, 0, 0);

    /* 标题 */
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "传感器历史数据");
    lv_obj_set_style_text_font(title, &myfont, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x0047AB), 0);
    lv_obj_set_style_text_decor(title, LV_TEXT_DECOR_UNDERLINE, 0);
    lv_obj_set_pos(title, 4, 2);

    /* 刷新按钮（页面最右侧） */
    btn_history_refresh = lv_btn_create(parent);
    lv_obj_set_size(btn_history_refresh, 40, 20);
    lv_obj_set_pos(btn_history_refresh, 202, 1);
    lv_obj_set_style_bg_color(btn_history_refresh, lv_color_hex(0x0047AB), 0);
    lv_obj_set_style_radius(btn_history_refresh, 4, 0);
    lv_obj_set_style_pad_all(btn_history_refresh, 0, 0);
    lv_obj_t *lbl_refresh = lv_label_create(btn_history_refresh);
    lv_label_set_text(lbl_refresh, "刷新");
    lv_obj_set_style_text_font(lbl_refresh, &myfont, 0);
    lv_obj_center(lbl_refresh);
    lv_obj_add_event_cb(btn_history_refresh, history_refresh_cb, LV_EVENT_CLICKED, NULL);

    /* 清空按钮（刷新按钮左侧） */
    btn_history_clear = lv_btn_create(parent);
    lv_obj_set_size(btn_history_clear, 50, 20);
    lv_obj_set_pos(btn_history_clear, 148, 1);
    lv_obj_set_style_bg_color(btn_history_clear, lv_color_hex(0xE6002D), 0);
    lv_obj_set_style_radius(btn_history_clear, 4, 0);
    lv_obj_set_style_pad_all(btn_history_clear, 0, 0);
    lv_obj_t *lbl_clear = lv_label_create(btn_history_clear);
    lv_label_set_text(lbl_clear, "清空");
    lv_obj_set_style_text_font(lbl_clear, &myfont, 0);
    lv_obj_center(lbl_clear);
    lv_obj_add_event_cb(btn_history_clear, history_clear_cb, LV_EVENT_CLICKED, NULL);

    /* 列表容器（可滚动） */
    list_history = lv_obj_create(parent);
    lv_obj_set_size(list_history, 232, 152);
    lv_obj_set_pos(list_history, 4, 22);
    lv_obj_set_style_bg_color(list_history, lv_color_white(), 0);
    lv_obj_set_style_border_width(list_history, 0, 0);
    lv_obj_set_style_pad_all(list_history, 2, 0);
    lv_obj_set_flex_flow(list_history, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list_history, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /* 无数据提示 */
    lv_obj_set_flex_align(list_history, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    label_history_empty = lv_label_create(list_history);
    lv_label_set_text(label_history_empty, "暂无历史数据");
    lv_obj_set_style_text_font(label_history_empty, &myfont, 0);
    lv_obj_set_style_text_color(label_history_empty, lv_color_hex(0x999999), 0);

    /* 底部翻页栏 */
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, 232, 26);
    lv_obj_set_pos(bar, 4, 178);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xF0F0F0), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    /* 上一页按钮 */
    btn_history_prev = lv_btn_create(bar);
    lv_obj_set_size(btn_history_prev, 48, 22);
    lv_obj_set_pos(btn_history_prev, 4, 2);
    lv_obj_set_style_bg_color(btn_history_prev, lv_color_hex(0x0047AB), 0);
    lv_obj_set_style_radius(btn_history_prev, 4, 0);
    lv_obj_set_style_pad_all(btn_history_prev, 0, 0);
    lv_obj_t *lbl_prev = lv_label_create(btn_history_prev);
    lv_label_set_text(lbl_prev, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(lbl_prev, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl_prev);
    lv_obj_add_event_cb(btn_history_prev, history_page_event_cb, LV_EVENT_CLICKED, NULL);

    /* 页码标签 */
    label_history_page = lv_label_create(bar);
    lv_label_set_text(label_history_page, "1/1");
    lv_obj_set_style_text_font(label_history_page, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label_history_page, lv_color_hex(0x333333), 0);
    lv_obj_center(label_history_page);

    /* 下一页按钮 */
    btn_history_next = lv_btn_create(bar);
    lv_obj_set_size(btn_history_next, 48, 22);
    lv_obj_set_pos(btn_history_next, 180, 2);
    lv_obj_set_style_bg_color(btn_history_next, lv_color_hex(0x0047AB), 0);
    lv_obj_set_style_radius(btn_history_next, 4, 0);
    lv_obj_set_style_pad_all(btn_history_next, 0, 0);
    lv_obj_t *lbl_next = lv_label_create(btn_history_next);
    lv_label_set_text(lbl_next, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(lbl_next, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl_next);
    lv_obj_add_event_cb(btn_history_next, history_page_event_cb, LV_EVENT_CLICKED, NULL);
}

/* ================================================================
 *  update_history_data - 更新历史数据列表
 *
 *  从 SharedLogData 解析 SensorLog 记录并显示
 * ================================================================ */
void update_history_data(void)
{
    static uint8_t last_resp_seq = 0xFF;
    uint8_t cur_resp_seq = SharedLogData.log_resp_seq;

    /* 仅在新数据到达时更新 */
    if (cur_resp_seq == last_resp_seq)
        return;
    last_resp_seq = cur_resp_seq;

    /* 清空列表 */
    lv_obj_clean(list_history);

    uint16_t total = SharedLogData.log_total;
    if (total == 0)
    {
        lv_obj_set_flex_align(list_history, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        label_history_empty = lv_label_create(list_history);
        lv_label_set_text(label_history_empty, "暂无历史数据");
        lv_obj_set_style_text_font(label_history_empty, &myfont, 0);
        lv_obj_set_style_text_color(label_history_empty, lv_color_hex(0x999999), 0);
        history_max_page = 0;
        history_page = 0;
        lv_label_set_text(label_history_page, "1/1");
        printf("V3F: 历史数据为空\r\n");
        return;
    }

    /* 有数据时恢复左对齐 */
    lv_obj_set_flex_align(list_history, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /* 计算最大页码（log_page 为 uint8_t，最大 255） */
    uint16_t calc_max = (total > HISTORY_PAGE_SIZE) ?
                        ((total - 1) / HISTORY_PAGE_SIZE) : 0;
    history_max_page = (calc_max > 255) ? 255 : calc_max;

    /* 更新页码显示（1-based） */
    char page_str[16];
    snprintf(page_str, sizeof(page_str), "%u/%u", history_page + 1, history_max_page + 1);
    lv_label_set_text(label_history_page, page_str);

    /* 填充列表（最新记录在最上面） */
    uint8_t count = HISTORY_PAGE_SIZE;
    for (uint8_t i = count; i > 0; i--)
    {
        local_sensor_log_t *log = (local_sensor_log_t *)&SharedLogData.log_data[(i - 1) * 16];
        if (log->month == 0 && log->day == 0 && log->hour == 0 && log->minute == 0)
            continue;

        uint16_t light = ((uint16_t)log->light_h << 8) | log->light_l;
        uint16_t year = 2024 + log->year_off;

        char temp_str[8], humi_str[8], light_str[8];
        /* 0 值表示传感器异常，显示 -- */
        if (log->temp_x10 == 0 && log->humi_x10 == 0)
            { snprintf(temp_str, sizeof(temp_str), "--"); snprintf(humi_str, sizeof(humi_str), "--"); }
        else
            { snprintf(temp_str, sizeof(temp_str), "%d", log->temp_x10 / 10);
              snprintf(humi_str, sizeof(humi_str), "%d", log->humi_x10 / 10); }
        if (light == 0)
            snprintf(light_str, sizeof(light_str), "--");
        else
            snprintf(light_str, sizeof(light_str), "%u", light);

        char buf[64];
        snprintf(buf, sizeof(buf), "%02u/%02u/%02u %02u:%02u T:%s H:%s L:%s",
                 year % 100, log->month, log->day, log->hour, log->minute,
                 temp_str, humi_str, light_str);

        lv_obj_t *item = lv_label_create(list_history);
        lv_label_set_text(item, buf);
        lv_obj_set_style_text_font(item, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(item, lv_color_hex(0x333333), 0);
        lv_obj_set_style_pad_ver(item, 2, 0);
    }

    printf("V3F: 历史数据页更新 page=%u total=%u\r\n",
           history_page, total);
}

/* ================================================================
 *  create_wifi_page - 构建第8页：设置
 *
 *  布局：WiFi 配置 + 静音开关 + 语音设置 + 天气设置
 * ================================================================ */
static void create_wifi_page(lv_obj_t *parent)
{
    lv_obj_set_style_pad_all(parent, 4, 0);

    /* 标题 */
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "WiFi 配置");
    lv_obj_set_style_text_font(title, &myfont, 0);
    lv_obj_set_style_text_color(title, COLOR_CLOCK_TITLE, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    /* 连接状态（标题行最右侧） */
    label_wifi_status = lv_label_create(parent);
    lv_label_set_text(label_wifi_status, "未连接");
    lv_obj_set_style_text_font(label_wifi_status, &myfont, 0);
    lv_obj_set_style_text_color(label_wifi_status, COLOR_SUBTITLE, 0);
    lv_obj_align(label_wifi_status, LV_ALIGN_TOP_RIGHT, 0, 0);

    /* SSID 输入框 */
    lv_obj_t *lbl_ssid = lv_label_create(parent);
    lv_label_set_text(lbl_ssid, "名称:");
    lv_obj_set_style_text_font(lbl_ssid, &myfont, 0);
    lv_obj_align(lbl_ssid, LV_ALIGN_TOP_LEFT, 0, 28);

    ta_ssid = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta_ssid, true);
    lv_textarea_set_placeholder_text(ta_ssid, "WiFi名称");
    lv_obj_set_size(ta_ssid, 175, 28);
    lv_obj_align(ta_ssid, LV_ALIGN_TOP_LEFT, 46, 24);
    lv_obj_clear_flag(ta_ssid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(ta_ssid, &myfont, 0);
    lv_obj_set_style_text_font(ta_ssid, &myfont, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_add_event_cb(ta_ssid, ta_event_cb, LV_EVENT_FOCUSED, NULL);

    /* 密码输入框 */
    lv_obj_t *lbl_pass = lv_label_create(parent);
    lv_label_set_text(lbl_pass, "密码:");
    lv_obj_set_style_text_font(lbl_pass, &myfont, 0);
    lv_obj_align(lbl_pass, LV_ALIGN_TOP_LEFT, 0, 62);

    ta_pass = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta_pass, true);
    lv_textarea_set_password_mode(ta_pass, true);
    lv_textarea_set_placeholder_text(ta_pass, "WiFi密码");
    lv_obj_set_size(ta_pass, 175, 28);
    lv_obj_align(ta_pass, LV_ALIGN_TOP_LEFT, 46, 58);
    lv_obj_clear_flag(ta_pass, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(ta_pass, &myfont, 0);
    lv_obj_set_style_text_font(ta_pass, &myfont, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_add_event_cb(ta_pass, ta_event_cb, LV_EVENT_FOCUSED, NULL);

    /* 连接按钮（居中） */
    btn_connect = lv_btn_create(parent);
    lv_obj_set_size(btn_connect, 100, 32);
    lv_obj_align(btn_connect, LV_ALIGN_TOP_MID, 0, 94);
    lv_obj_set_style_bg_color(btn_connect, lv_color_hex(0x2196F3), 0);
    lv_obj_set_style_radius(btn_connect, 6, 0);
    lv_obj_add_event_cb(btn_connect, wifi_connect_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_btn = lv_label_create(btn_connect);
    lv_label_set_text(lbl_btn, "连接");
    lv_obj_set_style_text_font(lbl_btn, &myfont, 0);
    lv_obj_center(lbl_btn);

    /* ---- 静音模式 ---- */
    lv_obj_t *sep = lv_obj_create(parent);
    lv_obj_set_size(sep, 230, 1);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_pad_all(sep, 0, 0);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(sep, LV_ALIGN_TOP_LEFT, 0, 136);

    lv_obj_t *lbl_mute = lv_label_create(parent);
    lv_label_set_text(lbl_mute, "静音模式");
    lv_obj_set_style_text_font(lbl_mute, &myfont, 0);
    lv_obj_set_style_text_color(lbl_mute, COLOR_BODY_TEXT, 0);
    lv_obj_align(lbl_mute, LV_ALIGN_TOP_LEFT, 0, 146);

    sw_mute = lv_switch_create(parent);
    lv_obj_set_size(sw_mute, 50, 26);
    lv_obj_align(sw_mute, LV_ALIGN_TOP_RIGHT, -10, 144);
    lv_obj_add_event_cb(sw_mute, mute_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* ---- 天气设置分隔线 ---- */
    lv_obj_t *sep2 = lv_obj_create(parent);
    lv_obj_set_size(sep2, 230, 1);
    lv_obj_set_style_bg_color(sep2, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_border_width(sep2, 0, 0);
    lv_obj_set_style_pad_all(sep2, 0, 0);
    lv_obj_clear_flag(sep2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(sep2, LV_ALIGN_TOP_LEFT, 0, 178);

    lv_obj_t *lbl_weather_title = lv_label_create(parent);
    lv_label_set_text(lbl_weather_title, "天气设置");
    lv_obj_set_style_text_font(lbl_weather_title, &myfont, 0);
    lv_obj_set_style_text_color(lbl_weather_title, COLOR_CLOCK_TITLE, 0);
    lv_obj_align(lbl_weather_title, LV_ALIGN_TOP_LEFT, 0, 184);

    /* 状态标签（天气设置最右边） */
    label_weather_status = lv_label_create(parent);
    lv_label_set_text(label_weather_status, "");
    lv_obj_set_style_text_font(label_weather_status, &myfont, 0);
    lv_obj_align(label_weather_status, LV_ALIGN_TOP_RIGHT, -4, 184);

    /* 城市输入框 */
    lv_obj_t *lbl_city = lv_label_create(parent);
    lv_label_set_text(lbl_city, "城市:");
    lv_obj_set_style_text_font(lbl_city, &myfont, 0);
    lv_obj_align(lbl_city, LV_ALIGN_TOP_LEFT, 0, 210);

    ta_weather_loc = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta_weather_loc, true);
    lv_textarea_set_placeholder_text(ta_weather_loc, "拼音 如nanning");
    lv_obj_set_size(ta_weather_loc, 150, 28);
    lv_obj_align(ta_weather_loc, LV_ALIGN_TOP_LEFT, 46, 206);
    lv_obj_clear_flag(ta_weather_loc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(ta_weather_loc, &myfont, 0);
    lv_obj_set_style_text_font(ta_weather_loc, &myfont, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_add_event_cb(ta_weather_loc, ta_event_cb, LV_EVENT_FOCUSED, NULL);

    /* 保存按钮（输入框下方居中） */
    lv_obj_t *btn_weather_save = lv_btn_create(parent);
    lv_obj_set_size(btn_weather_save, 80, 28);
    lv_obj_align(btn_weather_save, LV_ALIGN_TOP_LEFT, 66, 238);
    lv_obj_set_style_bg_color(btn_weather_save, lv_color_hex(0x4CAF50), 0);
    lv_obj_set_style_radius(btn_weather_save, 6, 0);
    lv_obj_add_event_cb(btn_weather_save, weather_loc_save_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_save = lv_label_create(btn_weather_save);
    lv_label_set_text(lbl_save, "保存");
    lv_obj_set_style_text_font(lbl_save, &myfont, 0);
    lv_obj_center(lbl_save);

    /* 日志间隔下拉框 */
    lv_obj_t *lbl_interval = lv_label_create(parent);
    lv_label_set_text(lbl_interval, "保存间隔");
    lv_obj_set_style_text_font(lbl_interval, &myfont, 0);
    lv_obj_align(lbl_interval, LV_ALIGN_TOP_LEFT, 0, 272);

    dd_interval = lv_dropdown_create(parent);
    lv_dropdown_set_options(dd_interval, "1分钟\n2分钟\n5分钟\n10分钟\n30分钟\n60分钟");
    lv_obj_set_size(dd_interval, 120, 28);
    lv_obj_align(dd_interval, LV_ALIGN_TOP_LEFT, 80, 268);
    lv_obj_set_style_text_font(dd_interval, &myfont, LV_PART_MAIN);
    lv_obj_set_style_text_font(dd_interval, LV_FONT_DEFAULT, LV_PART_INDICATOR);
    lv_obj_t *dd_list_interval = lv_dropdown_get_list(dd_interval);
    if (dd_list_interval) {
        lv_obj_set_style_text_font(dd_list_interval, &myfont, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_width(dd_list_interval, 120);
        lv_obj_clear_flag(dd_list_interval, LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_dropdown_set_selected(dd_interval, 2);  /* 默认5分钟 */
    lv_obj_add_event_cb(dd_interval, interval_select_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* ---- 语音设置分隔线 ---- */
    lv_obj_t *sep3 = lv_obj_create(parent);
    lv_obj_set_size(sep3, 230, 1);
    lv_obj_set_style_bg_color(sep3, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_border_width(sep3, 0, 0);
    lv_obj_set_style_pad_all(sep3, 0, 0);
    lv_obj_clear_flag(sep3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(sep3, LV_ALIGN_TOP_LEFT, 0, 302);

    lv_obj_t *lbl_voice_title = lv_label_create(parent);
    lv_label_set_text(lbl_voice_title, "语音设置");
    lv_obj_set_style_text_font(lbl_voice_title, &myfont, 0);
    lv_obj_set_style_text_color(lbl_voice_title, COLOR_CLOCK_TITLE, 0);
    lv_obj_align(lbl_voice_title, LV_ALIGN_TOP_LEFT, 0, 308);

    /* 音量：y=334 */
    lv_obj_t *lbl_v = lv_label_create(parent);
    lv_label_set_text(lbl_v, "音量");
    lv_obj_set_style_text_font(lbl_v, &myfont, 0);
    lv_obj_align(lbl_v, LV_ALIGN_TOP_LEFT, 0, 334);

    slider_vol = lv_slider_create(parent);
    lv_obj_set_size(slider_vol, 160, 8);
    lv_slider_set_range(slider_vol, 0, 9);
    lv_slider_set_value(slider_vol, 5, LV_ANIM_OFF);
    lv_obj_align(slider_vol, LV_ALIGN_TOP_LEFT, 46, 336);
    lv_obj_add_event_cb(slider_vol, vol_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lbl_vol_val = lv_label_create(parent);
    lv_label_set_text(lbl_vol_val, "5");
    lv_obj_set_style_text_font(lbl_vol_val, &myfont, 0);
    lv_obj_align(lbl_vol_val, LV_ALIGN_TOP_RIGHT, -10, 332);

    /* 语调：y=360 */
    lv_obj_t *lbl_t = lv_label_create(parent);
    lv_label_set_text(lbl_t, "语调");
    lv_obj_set_style_text_font(lbl_t, &myfont, 0);
    lv_obj_align(lbl_t, LV_ALIGN_TOP_LEFT, 0, 360);

    slider_tone = lv_slider_create(parent);
    lv_obj_set_size(slider_tone, 160, 8);
    lv_slider_set_range(slider_tone, 0, 9);
    lv_slider_set_value(slider_tone, 5, LV_ANIM_OFF);
    lv_obj_align(slider_tone, LV_ALIGN_TOP_LEFT, 46, 362);
    lv_obj_add_event_cb(slider_tone, tone_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lbl_tone_val = lv_label_create(parent);
    lv_label_set_text(lbl_tone_val, "5");
    lv_obj_set_style_text_font(lbl_tone_val, &myfont, 0);
    lv_obj_align(lbl_tone_val, LV_ALIGN_TOP_RIGHT, -10, 358);

    /* 语速：y=386 */
    lv_obj_t *lbl_s = lv_label_create(parent);
    lv_label_set_text(lbl_s, "语速");
    lv_obj_set_style_text_font(lbl_s, &myfont, 0);
    lv_obj_align(lbl_s, LV_ALIGN_TOP_LEFT, 0, 386);

    slider_spd = lv_slider_create(parent);
    lv_obj_set_size(slider_spd, 160, 8);
    lv_slider_set_range(slider_spd, 0, 9);
    lv_slider_set_value(slider_spd, 5, LV_ANIM_OFF);
    lv_obj_align(slider_spd, LV_ALIGN_TOP_LEFT, 46, 388);
    lv_obj_add_event_cb(slider_spd, spd_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lbl_spd_val = lv_label_create(parent);
    lv_label_set_text(lbl_spd_val, "5");
    lv_obj_set_style_text_font(lbl_spd_val, &myfont, 0);
    lv_obj_align(lbl_spd_val, LV_ALIGN_TOP_RIGHT, -10, 384);
}

void update_wifi_status(uint8_t connected)
{
    /* 顶部栏 WiFi 图标 */
    if (label_wifi_icon)
    {
        if (connected)
            lv_obj_clear_flag(label_wifi_icon, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(label_wifi_icon, LV_OBJ_FLAG_HIDDEN);
    }
    /* WiFi 配置页状态标签 */
    if (!label_wifi_status) return;
    if (connected)
    {
        lv_label_set_text(label_wifi_status, "已连接");
        lv_obj_set_style_text_color(label_wifi_status, COLOR_HEALTHY, 0);
    }
    else
    {
        lv_label_set_text(label_wifi_status, "未连接");
        lv_obj_set_style_text_color(label_wifi_status, COLOR_DANGER, 0);
    }
}

void update_mute_switch(uint8_t mute)
{
    if (sw_mute) {
        if (mute)
            lv_obj_add_state(sw_mute, LV_STATE_CHECKED);
        else
            lv_obj_clear_state(sw_mute, LV_STATE_CHECKED);
    }
    if (label_mute_icon) {
        lv_label_set_text(label_mute_icon,
                          mute ? LV_SYMBOL_MUTE : LV_SYMBOL_VOLUME_MAX);
        lv_obj_clear_flag(label_mute_icon, LV_OBJ_FLAG_HIDDEN);
    }
}

void update_tts_settings(uint8_t vol, uint8_t tone, uint8_t spd)
{
    if (slider_vol) {
        lv_slider_set_value(slider_vol, vol, LV_ANIM_OFF);
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", vol);
        lv_label_set_text(lbl_vol_val, buf);
    }
    if (slider_tone) {
        lv_slider_set_value(slider_tone, tone, LV_ANIM_OFF);
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", tone);
        lv_label_set_text(lbl_tone_val, buf);
    }
    if (slider_spd) {
        lv_slider_set_value(slider_spd, spd, LV_ANIM_OFF);
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", spd);
        lv_label_set_text(lbl_spd_val, buf);
    }
}

void update_weather_location_status(void)
{
    if (!label_weather_status) return;
    uint8_t status = SharedWeatherCmd.weather_cmd_status;
    if (status == 1)
    {
        lv_label_set_text(label_weather_status, "设置中...");
        lv_obj_set_style_text_color(label_weather_status, COLOR_WARNING, 0);
    }
    else if (status == 2)
    {
        lv_label_set_text(label_weather_status, "设置成功");
        lv_obj_set_style_text_color(label_weather_status, COLOR_HEALTHY, 0);
        /* 用中文城市名更新输入框 */
        if (ta_weather_loc && SharedWeatherCmd.location_cn[0] != '\0')
        {
            lv_textarea_set_text(ta_weather_loc, SharedWeatherCmd.location_cn);
        }
    }
    else if (status == 3)
    {
        lv_label_set_text(label_weather_status, "设置失败");
        lv_obj_set_style_text_color(label_weather_status, COLOR_DANGER, 0);
    }
}

void update_save_log_status(void)
{
    if (!label_save_log) return;
    if (SharedLogData.save_sensor_log == 2)
    {
        SharedLogData.save_sensor_log = 0;
        lv_obj_clear_state(btn_save_log, LV_STATE_DISABLED);
        lv_label_set_text(label_save_log, "保存成功");
        lv_obj_set_style_text_color(label_save_log, COLOR_HEALTHY, 0);
    }
}

void update_log_interval(void)
{
    if (!dd_interval) return;
    uint16_t sec = SharedThresholdData.log_interval_s;
    /* 反查下拉索引：60/120/300/600/1800/3600 → 0/1/2/3/4/5 */
    static const uint16_t table[] = {60, 120, 300, 600, 1800, 3600};
    for (uint8_t i = 0; i < 6; i++)
    {
        if (sec == table[i])
        {
            if (lv_dropdown_get_selected(dd_interval) != i)
                lv_dropdown_set_selected(dd_interval, i);
            return;
        }
    }
}
