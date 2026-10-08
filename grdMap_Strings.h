#pragma once

typedef enum {
    StrID_NONE,
    StrID_Name,
    StrID_Description,
    StrID_Import_Param_Name,
    StrID_Import_Button_Label,
    StrID_Preview_Param_Name,
    StrID_Mapping_Source_Param_Name,
    StrID_Mapping_Source_Choices,
    StrID_Reverse_Param_Name,
    StrID_Dither_Param_Name,
    StrID_Use_Gradient_Alpha_Param_Name,
    StrID_Gradient_Index_Param_Name,
    StrID_Repeat_Count_Param_Name,
    StrID_Repeat_Mode_Param_Name,
    StrID_Repeat_Mode_Choices,
    StrID_Blend_Mode_Param_Name,
    StrID_Blend_Mode_Choices,
    StrID_Amount_Param_Name,
    StrID_NUMTYPES
} StrIDType;

const A_char* STR(StrIDType strNum);
