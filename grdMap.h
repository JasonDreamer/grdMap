#pragma once

#ifndef GRDMAP_H
#define GRDMAP_H

typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned short u_int16;
typedef unsigned long u_long;
typedef short int int16;

#define PF_TABLE_BITS 12
#define PF_TABLE_SZ_16 4096
#define PF_DEEP_COLOR_AWARE 1

#include <cstddef>

#include "AEConfig.h"

#ifdef AE_OS_WIN
typedef unsigned short PixelType;
#include <Windows.h>
#endif

#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_EffectUI.h"
#include "AE_EffectCBSuites.h"
#include "AE_Macros.h"
#include "AE_GeneralPlug.h"
#include "AEFX_ChannelDepthTpl.h"
#include "AEFX_SuiteHelper.h"
#include "AEGP_SuiteHandler.h"
#include "Param_Utils.h"
#include "String_Utils.h"

#include "grdMap_Strings.h"

#define MAJOR_VERSION 1
#define MINOR_VERSION 0
#define BUG_VERSION 0
#define STAGE_VERSION PF_Stage_DEVELOP
#define BUILD_VERSION 10

#define GRDMAP_LUT_SIZE 1024
#define GRDMAP_MAX_COLOR_STOPS 64
#define GRDMAP_MAX_ALPHA_STOPS 64
#define GRDMAP_TEXT_CAPACITY 128
#define GRDMAP_PATH_CAPACITY 4096
#define GRDMAP_MAX_GRADIENTS 4096
#define GRDMAP_REPEAT_COUNT_MAX 20
#define GRDMAP_PREVIEW_WIDTH 300
#define GRDMAP_PREVIEW_HEIGHT 62
#define GRDMAP_ARB_MAGIC 0x4752444DU
#define GRDMAP_ARB_VERSION 2U
#define GRDMAP_ARB_REFCON reinterpret_cast<void*>(0x4752444D)

enum {
    GRDMAP_INPUT = 0,
    GRDMAP_IMPORT,
    GRDMAP_PREVIEW,
    GRDMAP_MAPPING_SOURCE,
    GRDMAP_REVERSE,
    GRDMAP_DITHER,
    GRDMAP_USE_GRADIENT_ALPHA,
    GRDMAP_PREVIEW_UI,
    GRDMAP_GRADIENT_INDEX,
    GRDMAP_REPEAT_COUNT,
    GRDMAP_REPEAT_MODE,
    GRDMAP_BLEND_MODE,
    GRDMAP_AMOUNT,
    GRDMAP_NUM_PARAMS
};

enum {
    IMPORT_DISK_ID = 1,
    PREVIEW_DISK_ID,
    MAPPING_SOURCE_DISK_ID,
    REVERSE_DISK_ID,
    DITHER_DISK_ID,
    USE_GRADIENT_ALPHA_DISK_ID,
    PREVIEW_UI_DISK_ID,
    GRADIENT_INDEX_DISK_ID,
    REPEAT_COUNT_DISK_ID,
    REPEAT_MODE_DISK_ID,
    BLEND_MODE_DISK_ID,
    AMOUNT_DISK_ID
};

enum GrdMapSource {
    GRDMAP_SOURCE_LUMINANCE = 0,
    GRDMAP_SOURCE_RED,
    GRDMAP_SOURCE_GREEN,
    GRDMAP_SOURCE_BLUE,
    GRDMAP_SOURCE_ALPHA
};

enum GrdMapRepeatMode {
    GRDMAP_REPEAT_MODE_REPEAT = 0,
    GRDMAP_REPEAT_MODE_PING_PONG
};

enum GrdMapBlendMode {
    GRDMAP_BLEND_NORMAL = 0,
    GRDMAP_BLEND_DARKEN,
    GRDMAP_BLEND_MULTIPLY,
    GRDMAP_BLEND_LIGHTEN,
    GRDMAP_BLEND_SCREEN,
    GRDMAP_BLEND_OVERLAY,
    GRDMAP_BLEND_SOFT_LIGHT,
    GRDMAP_BLEND_HARD_LIGHT,
    GRDMAP_BLEND_DIFFERENCE,
    GRDMAP_BLEND_COLOR,
    GRDMAP_BLEND_LUMINOSITY,
    GRDMAP_BLEND_MODE_COUNT
};

struct GrdMapColorStop {
    float location;
    float midpoint;
    float red;
    float green;
    float blue;
};

struct GrdMapAlphaStop {
    float location;
    float midpoint;
    float alpha;
};

struct GrdMapData {
    A_u_long magic;
    A_u_long version;
    A_long color_stop_count;
    A_long alpha_stop_count;
    A_char preset_name[GRDMAP_TEXT_CAPACITY];
    A_char file_name[GRDMAP_TEXT_CAPACITY];
    GrdMapColorStop color_stops[GRDMAP_MAX_COLOR_STOPS];
    GrdMapAlphaStop alpha_stops[GRDMAP_MAX_ALPHA_STOPS];
    float lut[GRDMAP_LUT_SIZE][4];
    A_long gradient_index;
    A_long gradient_count;
    A_char file_path[GRDMAP_PATH_CAPACITY];
};

PF_Err CreateDefaultGradient(PF_InData* in_data, PF_ArbitraryH* gradientPH);
PF_Err HandleArbitrary(PF_InData* in_data, PF_ArbParamsExtra* extra);
PF_Err DrawGradientPreview(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[],
    PF_EventExtra* event_extra);

#ifdef AE_OS_WIN
bool ImportGradientFromDialog(
    PF_InData* in_data,
    GrdMapData* destination,
    bool* cancelled,
    A_char* error_message,
    size_t error_message_size);
bool SelectGradientFromCache(
    const GrdMapData* current,
    A_long gradient_index,
    GrdMapData* destination,
    A_char* error_message,
    size_t error_message_size);
#endif

extern "C" bool LaunchGrdMapCUDA(
    const void* input_buffer,
    int input_row_bytes,
    void* output_buffer,
    int output_row_bytes,
    int width,
    int height,
    int dither_origin_x,
    int dither_origin_y,
    int bit_depth,
    const float* lut_rgba,
    int lut_size,
    int mapping_source,
    int repeat_count,
    int repeat_mode,
    bool reverse,
    bool dither,
    bool use_gradient_alpha,
    int blend_mode,
    float amount);

extern "C" {

DllExport
PF_Err EffectMain(
    PF_Cmd cmd,
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[],
    PF_LayerDef* output,
    void* extra);

}

#endif
