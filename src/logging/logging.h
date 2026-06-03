#pragma once

#include <Arduino.h>

#ifndef SAFRASENSE_LOG_LEVEL
#define SAFRASENSE_LOG_LEVEL 0
#endif

#define SAFRASENSE_LOG_SILENT 0
#define SAFRASENSE_LOG_ERROR 1
#define SAFRASENSE_LOG_WARN 2
#define SAFRASENSE_LOG_INFO 3
#define SAFRASENSE_LOG_DEBUG 4

#if SAFRASENSE_LOG_LEVEL >= SAFRASENSE_LOG_ERROR
#define LOG_ERRORF(tag, fmt, ...) Serial.printf("[%s] " fmt, tag, ##__VA_ARGS__)
#else
#define LOG_ERRORF(tag, fmt, ...) do {} while (0)
#endif

#if SAFRASENSE_LOG_LEVEL >= SAFRASENSE_LOG_WARN
#define LOG_WARNF(tag, fmt, ...) Serial.printf("[%s] " fmt, tag, ##__VA_ARGS__)
#else
#define LOG_WARNF(tag, fmt, ...) do {} while (0)
#endif

#if SAFRASENSE_LOG_LEVEL >= SAFRASENSE_LOG_INFO
#define LOG_INFOF(tag, fmt, ...) Serial.printf("[%s] " fmt, tag, ##__VA_ARGS__)
#else
#define LOG_INFOF(tag, fmt, ...) do {} while (0)
#endif

#if SAFRASENSE_LOG_LEVEL >= SAFRASENSE_LOG_DEBUG
#define LOG_DEBUGF(tag, fmt, ...) Serial.printf("[%s] " fmt, tag, ##__VA_ARGS__)
#else
#define LOG_DEBUGF(tag, fmt, ...) do {} while (0)
#endif
