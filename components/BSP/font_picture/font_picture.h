//
// Created by tian on 2026/3/20.
//

#ifndef FRIST_FREERTOS_1_FONTS_PICTURE_H
#define FRIST_FREERTOS_1_FONTS_PICTURE_H

#include <stdint.h>

// 汉字索引（与Font_16x16_Chinese数组行索引一一对应）
typedef enum {
    CHN_WEN = 0,  // 温
    CHN_SHI = 1,  // 湿
    CHN_DU  = 2   // 度
} Chinese_Index;
// 16×16汉字点阵数组（每个汉字32字节）
extern const uint8_t Font_16x16_Chinese[][32];
/********************* 16×16 数字/符号点阵（0、1、2、4、5、6、7、8、9、:）*********************/
// 数字/符号索引（与Font_16x16_Number数组行索引一一对应）
typedef enum {
    NUM_0 = 0,    // 0
    NUM_1 = 1,    // 1
    NUM_2 = 2,    // 2
    NUM_4 = 3,    // 4（无数字3，索引3直接对应4）
    NUM_5 = 4,    // 5
    NUM_6 = 5,    // 6
    NUM_7 = 6,    // 7
    NUM_8 = 7,    // 8
    NUM_9 = 8,    // 9
    NUM_COLON = 9 // :（冒号）
} Number_Symbol_Index;
// 16×16数字/符号点阵数组（每个符号32字节）
extern const uint8_t Font_16x16_Number[][32];

/********************* 8×16 ASCII点阵（完整覆盖可打印字符：空格+0-9+A-Z+a-z+常用符号）*********************/
// ASCII字符索引（偏移量：字符ASCII码 - 0x20，如空格=0x20→索引0，'0'=0x30→索引16）
typedef enum {
    ASCII_SPACE = 0,          // 空格（0x20）
    ASCII_EXCLAMATION = 1,    // !（0x21）
    ASCII_QUOTATION = 2,      // "（0x22）
    ASCII_NUMBER_SIGN = 3,    // #（0x23）
    ASCII_DOLLAR = 4,         // $（0x24）
    ASCII_PERCENT = 5,        // %（0x25）
    ASCII_AMPERSAND = 6,      // &（0x26）
    ASCII_APOSTROPHE = 7,     // '（0x27）
    ASCII_LEFT_PAREN = 8,     // (（0x28）
    ASCII_RIGHT_PAREN = 9,    // )（0x29）
    ASCII_ASTERISK = 10,      // *（0x2A）
    ASCII_PLUS = 11,          // +（0x2B）
    ASCII_COMMA = 12,         // ,（0x2C）
    ASCII_HYPHEN = 13,        // -（0x2D）
    ASCII_PERIOD = 14,        // .（0x2E）
    ASCII_SLASH = 15,         // /（0x2F）
    ASCII_0 = 16,             // 0（0x30）
    ASCII_1 = 17,             // 1（0x31）
    ASCII_2 = 18,             // 2（0x32）
    ASCII_3 = 19,             // 3（0x33）
    ASCII_4 = 20,             // 4（0x34）
    ASCII_5 = 21,             // 5（0x35）
    ASCII_6 = 22,             // 6（0x36）
    ASCII_7 = 23,             // 7（0x37）
    ASCII_8 = 24,             // 8（0x38）
    ASCII_9 = 25,             // 9（0x39）
    ASCII_COLON = 26,         // :（0x3A）
    ASCII_SEMICOLON = 27,     // ;（0x3B）
    ASCII_LESS_THAN = 28,     // <（0x3C）
    ASCII_EQUALS = 29,        // =（0x3D）
    ASCII_GREATER_THAN = 30,  // >（0x3E）
    ASCII_QUESTION = 31,      // ?（0x3F）
    ASCII_AT = 32,            // @（0x40）
    ASCII_A = 33,             // A（0x41）
    ASCII_B = 34,             // B（0x42）
    ASCII_C = 35,             // C（0x43）
    ASCII_D = 36,             // D（0x44）
    ASCII_E = 37,             // E（0x45）
    ASCII_F = 38,             // F（0x46）
    ASCII_G = 39,             // G（0x47）
    ASCII_H = 40,             // H（0x48）
    ASCII_I = 41,             // I（0x49）
    ASCII_J = 42,             // J（0x4A）
    ASCII_K = 43,             // K（0x4B）
    ASCII_L = 44,             // L（0x4C）
    ASCII_M = 45,             // M（0x4D）
    ASCII_N = 46,             // N（0x4E）
    ASCII_O = 47,             // O（0x4F）
    ASCII_P = 48,             // P（0x50）
    ASCII_Q = 49,             // Q（0x51）
    ASCII_R = 50,             // R（0x52）
    ASCII_S = 51,             // S（0x53）
    ASCII_T = 52,             // T（0x54）
    ASCII_U = 53,             // U（0x55）
    ASCII_V = 54,             // V（0x56）
    ASCII_W = 55,             // W（0x57）
    ASCII_X = 56,             // X（0x58）
    ASCII_Y = 57,             // Y（0x59）
    ASCII_Z = 58,             // Z（0x5A）
    ASCII_LEFT_BRACKET = 59,  // [（0x5B）
    ASCII_BACKSLASH = 60,     // \（0x5C）
    ASCII_RIGHT_BRACKET = 61, // ]（0x5D）
    ASCII_CARET = 62,         // ^（0x5E）
    ASCII_UNDERSCORE = 63,    // _（0x5F）
    ASCII_GRAVE = 64,         // `（0x60）
    ASCII_a = 65,             // a（0x61）
    ASCII_b = 66,             // b（0x62）
    ASCII_c = 67,             // c（0x63）
    ASCII_d = 68,             // d（0x64）
    ASCII_e = 69,             // e（0x65）
    ASCII_f = 70,             // f（0x66）
    ASCII_g = 71,             // g（0x67）
    ASCII_h = 72,             // h（0x68）
    ASCII_i = 73,             // i（0x69）
    ASCII_j = 74,             // j（0x6A）
    ASCII_k = 75,             // k（0x6B）
    ASCII_l = 76,             // l（0x6C）
    ASCII_m = 77,             // m（0x6D）
    ASCII_n = 78,             // n（0x6E）
    ASCII_o = 79,             // o（0x6F）
    ASCII_p = 80,             // p（0x70）
    ASCII_q = 81,             // q（0x71）
    ASCII_r = 82,             // r（0x72）
    ASCII_s = 83,             // s（0x73）
    ASCII_t = 84,             // t（0x74）
    ASCII_u = 85,             // u（0x75）
    ASCII_v = 86,             // v（0x76）
    ASCII_w = 87,             // w（0x77）
    ASCII_x = 88,             // x（0x78）
    ASCII_y = 89,             // y（0x79）
    ASCII_z = 90,             // z（0x7A）
    ASCII_LEFT_BRACE = 91,    // {（0x7B）
    ASCII_VERTICAL_BAR = 92,  // |（0x7C）
    ASCII_RIGHT_BRACE = 93,   // }（0x7D）
    ASCII_TILDE = 94          // ~（0x7E）
} ASCII_Index;

// 8×16 ASCII点阵数组（每个字符16字节，共95个可打印字符）
extern const uint8_t Font_8x16[];


#endif //FRIST_FREERTOS_1_FONTS_PICTURE_H