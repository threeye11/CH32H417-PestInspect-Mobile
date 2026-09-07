/**
 * @file    disease_db.h
 * @brief   本地病害知识库 — 检测到病害后查询应对措施和推荐药物
 *
 *          数据来源：
 *          - K230 参考工程 Disease.c（FirstAidEntry 知识库）
 *          - label.txt（玉米病害标签与特征描述）
 *          - 病害描述.txt（马铃薯/通用病害特征）
 *          - readme.txt（番茄/蔬菜病害特征）
 *
 *          使用流程：
 *          1. K230 检测到病害 → net_task 调用 DiseaseDB_Find() 查询
 *          2. 命中则播报 tts_announce（检测 + 应对措施），随后播报推荐药物
 *          3. 连续 N 次检测 + 冷却期防重复触发（由 net_task 管理）
 */
#ifndef __DISEASE_DB_H
#define __DISEASE_DB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 知识库条目 */
typedef struct {
    const char* keyword;          /* 病害关键词（strstr 匹配 SharedPestData.pest） */
    const char* tts_announce;     /* TTS 播报文本（检测结果 + 应对措施） */
    const char* drug;             /* 推荐药物（"无" 表示无需用药） */
    const char* priority;         /* 紧急程度 "高" / "中" / "低" */
} DiseaseEntry;

/**
 * @brief  根据病害名称查找知识库条目（strstr 子串匹配）
 * @param  pest_name  病害名称（如 "蚜虫"、"锈病"），NULL/空串/"-" 返回健康条目
 * @return 匹配的条目指针，未找到返回 NULL
 */
const DiseaseEntry* DiseaseDB_Find(const char* pest_name);

/**
 * @brief  获取知识库条目总数
 */
uint8_t DiseaseDB_GetCount(void);

#ifdef __cplusplus
}
#endif

#endif
