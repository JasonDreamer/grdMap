#include "grdMap.h"

typedef struct {
    A_u_long index;
    A_char str[256];
} TableString;

TableString g_strs[StrID_NUMTYPES] = {
    {StrID_NONE, ""},
    {StrID_Name, "grdmap"},
    {StrID_Description, "Gradient mapping with Photoshop GRD import and a read-only ramp preview."},
    {StrID_Import_Param_Name, "GRD File"},
    {StrID_Import_Button_Label, "Import .grd..."},
    {StrID_Preview_Param_Name, "Gradient Preview"},
    {StrID_Mapping_Source_Param_Name, "Mapping Source"},
    {StrID_Mapping_Source_Choices, "Luminance|Red|Green|Blue|Alpha"},
    {StrID_Reverse_Param_Name, "Reverse"},
    {StrID_Dither_Param_Name, "Dither"},
    {StrID_Use_Gradient_Alpha_Param_Name, "Use Gradient Alpha"},
    {StrID_Gradient_Index_Param_Name, "Gradient Index"},
    {StrID_Repeat_Count_Param_Name, "Repeat Count"},
    {StrID_Repeat_Mode_Param_Name, "Repeat Mode"},
    {StrID_Repeat_Mode_Choices, "Repeat|Ping-Pong"},
    {StrID_Blend_Mode_Param_Name, "Blend Mode"},
    {StrID_Blend_Mode_Choices, "Normal|Darken|Multiply|Lighten|Screen|Overlay|Soft Light|Hard Light|Difference|Color|Luminosity"},
    {StrID_Amount_Param_Name, "Amount"}
};

const A_char* STR(StrIDType strNum)
{
    return g_strs[strNum].str;
}
