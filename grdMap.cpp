#include "grdMap.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

PF_Err About(PF_InData* in_data, PF_OutData* out_data)
{
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    suites.ANSICallbacksSuite1()->sprintf(
        out_data->return_msg,
        "%s v%d.%d\r%s",
        STR(StrID_Name),
        MAJOR_VERSION,
        MINOR_VERSION,
        STR(StrID_Description));
    return PF_Err_NONE;
}

PF_Err GlobalSetup(PF_OutData* out_data)
{
    out_data->my_version = PF_VERSION(
        MAJOR_VERSION, MINOR_VERSION, BUG_VERSION, STAGE_VERSION, BUILD_VERSION);
    out_data->out_flags = PF_OutFlag_PIX_INDEPENDENT |
                          PF_OutFlag_CUSTOM_UI |
                          PF_OutFlag_DEEP_COLOR_AWARE |
                          PF_OutFlag_SEND_UPDATE_PARAMS_UI;
    out_data->out_flags2 = PF_OutFlag2_FLOAT_COLOR_AWARE |
                           PF_OutFlag2_SUPPORTS_SMART_RENDER |
                           PF_OutFlag2_SUPPORTS_THREADED_RENDERING;
    return PF_Err_NONE;
}

PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data)
{
    PF_Err err = PF_Err_NONE;
    PF_ParamDef def;
    AEFX_CLR_STRUCT(def);

    PF_ADD_BUTTON(
        STR(StrID_Import_Param_Name),
        STR(StrID_Import_Button_Label),
        0,
        PF_ParamFlag_SUPERVISE,
        IMPORT_DISK_ID);

    AEFX_CLR_STRUCT(def);
    ERR(CreateDefaultGradient(in_data, &def.u.arb_d.dephault));
    if (!err) {
        PF_ADD_ARBITRARY2(
            STR(StrID_Preview_Param_Name),
            GRDMAP_PREVIEW_WIDTH,
            GRDMAP_PREVIEW_HEIGHT,
            PF_ParamFlag_CANNOT_TIME_VARY,
            PF_PUI_NO_ECW_UI,
            def.u.arb_d.dephault,
            PREVIEW_DISK_ID,
            GRDMAP_ARB_REFCON);
    }

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP(
        STR(StrID_Mapping_Source_Param_Name),
        5,
        1,
        STR(StrID_Mapping_Source_Choices),
        MAPPING_SOURCE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOX(
        STR(StrID_Reverse_Param_Name),
        "",
        FALSE,
        0,
        REVERSE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOX(
        STR(StrID_Dither_Param_Name),
        "",
        TRUE,
        0,
        DITHER_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOX(
        STR(StrID_Use_Gradient_Alpha_Param_Name),
        "",
        FALSE,
        0,
        USE_GRADIENT_ALPHA_DISK_ID);

    AEFX_CLR_STRUCT(def);
    def.ui_flags = PF_PUI_CONTROL | PF_PUI_DONT_ERASE_CONTROL;
    def.ui_width = GRDMAP_PREVIEW_WIDTH;
    def.ui_height = GRDMAP_PREVIEW_HEIGHT;
    PF_ADD_NULL(STR(StrID_Preview_Param_Name), PREVIEW_UI_DISK_ID);

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY;
    PF_ADD_SLIDER(
        STR(StrID_Gradient_Index_Param_Name),
        1,
        GRDMAP_MAX_GRADIENTS,
        1,
        GRDMAP_MAX_GRADIENTS,
        1,
        GRADIENT_INDEX_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER(
        STR(StrID_Repeat_Count_Param_Name),
        1,
        GRDMAP_REPEAT_COUNT_MAX,
        1,
        GRDMAP_REPEAT_COUNT_MAX,
        1,
        REPEAT_COUNT_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP(
        STR(StrID_Repeat_Mode_Param_Name),
        2,
        1,
        STR(StrID_Repeat_Mode_Choices),
        REPEAT_MODE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP(
        STR(StrID_Blend_Mode_Param_Name),
        GRDMAP_BLEND_MODE_COUNT,
        1,
        STR(StrID_Blend_Mode_Choices),
        BLEND_MODE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER(
        STR(StrID_Amount_Param_Name),
        0,
        100,
        0,
        100,
        100,
        AMOUNT_DISK_ID);

    if (!err) {
        PF_CustomUIInfo custom_ui{};
        custom_ui.events = PF_CustomEFlag_EFFECT;
        custom_ui.comp_ui_alignment = PF_UIAlignment_NONE;
        custom_ui.layer_ui_alignment = PF_UIAlignment_NONE;
        custom_ui.preview_ui_alignment = PF_UIAlignment_NONE;
        ERR((*(in_data->inter.register_ui))(in_data->effect_ref, &custom_ui));
    }
    out_data->num_params = GRDMAP_NUM_PARAMS;
    return err;
}

bool IsGradientDataValid(const GrdMapData* data)
{
    return data && data->magic == GRDMAP_ARB_MAGIC && data->version == GRDMAP_ARB_VERSION;
}

int ClampRepeatCount(A_long value)
{
    return static_cast<int>((std::max)(
        static_cast<A_long>(1),
        (std::min)(value, static_cast<A_long>(GRDMAP_REPEAT_COUNT_MAX))));
}

PF_Err ReadGradientData(
    PF_InData* in_data,
    PF_ParamDef* params[],
    GrdMapData* destination)
{
    if (!in_data || !params || !params[GRDMAP_PREVIEW] || !destination) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }
    PF_Handle handle = params[GRDMAP_PREVIEW]->u.arb_d.value;
    if (!handle) return PF_Err_INTERNAL_STRUCT_DAMAGED;
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    const GrdMapData* source = reinterpret_cast<const GrdMapData*>(
        suites.HandleSuite1()->host_lock_handle(handle));
    if (!source) return PF_Err_OUT_OF_MEMORY;
    const bool valid = IsGradientDataValid(source);
    if (valid) std::memcpy(destination, source, sizeof(*destination));
    suites.HandleSuite1()->host_unlock_handle(handle);
    return valid ? PF_Err_NONE : PF_Err_INTERNAL_STRUCT_DAMAGED;
}

PF_Err StoreGradientData(
    PF_InData* in_data,
    PF_ParamDef* params[],
    const GrdMapData& source)
{
    if (!in_data || !params || !params[GRDMAP_PREVIEW]) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }
    PF_Handle handle = params[GRDMAP_PREVIEW]->u.arb_d.value;
    if (!handle) return PF_Err_INTERNAL_STRUCT_DAMAGED;
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    GrdMapData* destination = reinterpret_cast<GrdMapData*>(
        suites.HandleSuite1()->host_lock_handle(handle));
    if (!destination) return PF_Err_OUT_OF_MEMORY;
    std::memcpy(destination, &source, sizeof(source));
    suites.HandleSuite1()->host_unlock_handle(handle);
    params[GRDMAP_PREVIEW]->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
    return PF_Err_NONE;
}

PF_Err UpdateGradientUI(PF_InData* in_data, PF_ParamDef* params[])
{
    if (!in_data || !params || !params[GRDMAP_GRADIENT_INDEX] ||
        !params[GRDMAP_PREVIEW_UI]) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    GrdMapData data{};
    const PF_Err read_err = ReadGradientData(in_data, params, &data);
    const A_long count = read_err == PF_Err_NONE
        ? (std::max)(static_cast<A_long>(1),
                     (std::min)(data.gradient_count, static_cast<A_long>(GRDMAP_MAX_GRADIENTS)))
        : 1;
    const A_long index = read_err == PF_Err_NONE
        ? (std::max)(static_cast<A_long>(0),
                     (std::min)(data.gradient_index, count - 1))
        : 0;

    PF_ParamDef selector = *params[GRDMAP_GRADIENT_INDEX];
    selector.u.sd.slider_min = 1;
    selector.u.sd.slider_max = count;
    if (count <= 1) selector.ui_flags |= PF_PUI_DISABLED;
    else selector.ui_flags &= ~PF_PUI_DISABLED;

    PF_ParamDef preview = *params[GRDMAP_PREVIEW_UI];
    A_char preview_name[GRDMAP_TEXT_CAPACITY * 2]{};
    const A_char* preset_name = read_err == PF_Err_NONE && data.preset_name[0]
        ? data.preset_name
        : "Black, White";
    std::snprintf(
        preview_name,
        sizeof(preview_name),
        "Preview %ld/%ld: %s",
        static_cast<long>(index + 1),
        static_cast<long>(count),
        preset_name);
    PF_STRNNCPY(preview.PF_DEF_NAME, preview_name, sizeof(preview.PF_DEF_NAME));

    AEGP_SuiteHandler suites(in_data->pica_basicP);
    PF_Err err = suites.ParamUtilsSuite3()->PF_UpdateParamUI(
        in_data->effect_ref,
        GRDMAP_GRADIENT_INDEX,
        &selector);
    if (!err) {
        err = suites.ParamUtilsSuite3()->PF_UpdateParamUI(
            in_data->effect_ref,
            GRDMAP_PREVIEW_UI,
            &preview);
    }
    return err;
}

PF_Err RenderGradient(
    PF_InData* in_data,
    PF_EffectWorld* input,
    PF_EffectWorld* output,
    int bit_depth,
    const GrdMapData* gradient,
    int mapping_source,
    int repeat_count,
    int repeat_mode,
    bool reverse,
    bool dither,
    bool use_gradient_alpha,
    int blend_mode,
    float amount)
{
    if (!output) return PF_Err_BAD_CALLBACK_PARAM;
    if (output->width <= 0 || output->height <= 0) return PF_Err_NONE;
    const size_t pixel_bytes = bit_depth == 32 ? sizeof(PF_PixelFloat)
        : bit_depth == 16 ? sizeof(PF_Pixel16) : sizeof(PF_Pixel8);
    const size_t output_active_bytes = static_cast<size_t>(output->width) * pixel_bytes;
    if (!output->data || output->rowbytes <= 0 ||
        static_cast<size_t>(output->rowbytes) < output_active_bytes) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    const auto clear_output = [&]() {
        for (A_long y = 0; y < output->height; ++y) {
            std::memset(reinterpret_cast<char*>(output->data) +
                static_cast<size_t>(y) * output->rowbytes, 0, output_active_bytes);
        }
    };
    // SmartFX can supply an empty world for a fully transparent input.
    if (!input || !input->data || input->width <= 0 || input->height <= 0) {
        clear_output();
        return PF_Err_NONE;
    }
    if (input->rowbytes <= 0 || static_cast<size_t>(input->rowbytes) <
        static_cast<size_t>(input->width) * pixel_bytes) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    // As in SDK SmartyPants, output_origin places input (0,0) in the output buffer.
    const std::int64_t origin_x = in_data->output_origin_x;
    const std::int64_t origin_y = in_data->output_origin_y;
    const std::int64_t src_x = (std::max)(std::int64_t(0), -origin_x);
    const std::int64_t src_y = (std::max)(std::int64_t(0), -origin_y);
    const std::int64_t dst_x = (std::max)(std::int64_t(0), origin_x);
    const std::int64_t dst_y = (std::max)(std::int64_t(0), origin_y);
    const std::int64_t width = (std::min)(input->width - src_x, output->width - dst_x);
    const std::int64_t height = (std::min)(input->height - src_y, output->height - dst_y);
    if (width <= 0 || height <= 0) {
        clear_output();
        return PF_Err_NONE;
    }
    if (dst_x != 0 || dst_y != 0 || width != output->width || height != output->height) {
        clear_output();
    }

    PF_Rect src_rect{
        static_cast<A_long>(src_x), static_cast<A_long>(src_y),
        static_cast<A_long>(src_x + width), static_cast<A_long>(src_y + height)};
    PF_Rect dst_rect{
        static_cast<A_long>(dst_x), static_cast<A_long>(dst_y),
        static_cast<A_long>(dst_x + width), static_cast<A_long>(dst_y + height)};
    if (!IsGradientDataValid(gradient) || amount <= 0.0f) {
        return PF_COPY(input, output, &src_rect, &dst_rect);
    }

    const char* source = reinterpret_cast<const char*>(input->data) +
        static_cast<size_t>(src_y) * input->rowbytes + static_cast<size_t>(src_x) * pixel_bytes;
    char* destination = reinterpret_cast<char*>(output->data) +
        static_cast<size_t>(dst_y) * output->rowbytes + static_cast<size_t>(dst_x) * pixel_bytes;
    const bool cuda_ok = LaunchGrdMapCUDA(
        source, input->rowbytes, destination, output->rowbytes,
        static_cast<int>(width), static_cast<int>(height),
        static_cast<int>(dst_x), static_cast<int>(dst_y), bit_depth,
        &gradient->lut[0][0], GRDMAP_LUT_SIZE, mapping_source, repeat_count, repeat_mode,
        reverse, dither, use_gradient_alpha, blend_mode, amount);
    return cuda_ok ? PF_Err_NONE : PF_COPY(input, output, &src_rect, &dst_rect);
}

PF_Err Render(PF_InData* in_data, PF_ParamDef* params[], PF_LayerDef* output)
{
    if (!params || !params[GRDMAP_INPUT] || !params[GRDMAP_PREVIEW] ||
        !params[GRDMAP_REPEAT_COUNT] || !params[GRDMAP_REPEAT_MODE] ||
        !params[GRDMAP_BLEND_MODE] || !params[GRDMAP_AMOUNT] || !output) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }
    PF_LayerDef* input = &params[GRDMAP_INPUT]->u.ld;
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    PF_Handle gradientH = params[GRDMAP_PREVIEW]->u.arb_d.value;
    const GrdMapData* gradient = gradientH ? reinterpret_cast<const GrdMapData*>(
        suites.HandleSuite1()->host_lock_handle(gradientH)) : nullptr;

    const int bit_depth = PF_WORLD_IS_DEEP(output) ? 16 : 8;
    const PF_Err err = RenderGradient(
        in_data,
        input,
        output,
        bit_depth,
        gradient,
        params[GRDMAP_MAPPING_SOURCE]->u.pd.value - 1,
        ClampRepeatCount(params[GRDMAP_REPEAT_COUNT]->u.sd.value),
        params[GRDMAP_REPEAT_MODE]->u.pd.value - 1,
        params[GRDMAP_REVERSE]->u.bd.value != FALSE,
        params[GRDMAP_DITHER]->u.bd.value != FALSE,
        params[GRDMAP_USE_GRADIENT_ALPHA]->u.bd.value != FALSE,
        params[GRDMAP_BLEND_MODE]->u.pd.value - 1,
        static_cast<float>(params[GRDMAP_AMOUNT]->u.sd.value) / 100.0f);
    if (gradient) suites.HandleSuite1()->host_unlock_handle(gradientH);
    return err;
}

PF_Err CheckoutControlParams(PF_InData* in_data, PF_ParamDef checked[], bool active[])
{
    PF_Err err = PF_Err_NONE;
    const A_long indices[] = {
        GRDMAP_PREVIEW,
        GRDMAP_MAPPING_SOURCE,
        GRDMAP_REVERSE,
        GRDMAP_DITHER,
        GRDMAP_USE_GRADIENT_ALPHA,
        GRDMAP_REPEAT_COUNT,
        GRDMAP_REPEAT_MODE,
        GRDMAP_BLEND_MODE,
        GRDMAP_AMOUNT};
    for (int i = 0; i < 9 && !err; ++i) {
        AEFX_CLR_STRUCT(checked[i]);
        ERR(PF_CHECKOUT_PARAM(
            in_data,
            indices[i],
            in_data->current_time,
            in_data->time_step,
            in_data->time_scale,
            &checked[i]));
        active[i] = !err;
    }
    return err;
}

PF_Err CheckinControlParams(PF_InData* in_data, PF_ParamDef checked[], const bool active[])
{
    PF_Err err = PF_Err_NONE;
    PF_Err err2 = PF_Err_NONE;
    for (int i = 8; i >= 0; --i) {
        if (active[i]) ERR2(PF_CHECKIN_PARAM(in_data, &checked[i]));
    }
    return err;
}

PF_Err PreRender(PF_InData* in_data, PF_PreRenderExtra* extra)
{
    PF_Err err = PF_Err_NONE;
    PF_Err err2 = PF_Err_NONE;
    PF_ParamDef checked[9];
    bool active[9]{};
    ERR(CheckoutControlParams(in_data, checked, active));

    PF_RenderRequest request = extra->input->output_request;
    request.preserve_rgb_of_zero_alpha = FALSE;
    PF_CheckoutResult input_result{};
    if (!err) {
        ERR(extra->cb->checkout_layer(
            in_data->effect_ref,
            GRDMAP_INPUT,
            GRDMAP_INPUT,
            &request,
            in_data->current_time,
            in_data->time_step,
            in_data->time_scale,
            &input_result));
    }
    if (!err) {
        // An upstream effect may return more pixels than requested. This pointwise
        // effect must report only the intersection with its own output request.
        const PF_LRect& requested = extra->input->output_request.rect;
        PF_LRect result{
            (std::max)(input_result.result_rect.left, requested.left),
            (std::max)(input_result.result_rect.top, requested.top),
            (std::min)(input_result.result_rect.right, requested.right),
            (std::min)(input_result.result_rect.bottom, requested.bottom)};
        if (result.left >= result.right || result.top >= result.bottom) {
            // Keep an empty result at the request origin, even for an offset ROI.
            result = PF_LRect{requested.left, requested.top, requested.left, requested.top};
        }
        extra->output->result_rect = result;
        // Content bounds must remain independent of the current request/ROI.
        extra->output->max_result_rect = input_result.max_result_rect;
    }
    ERR2(CheckinControlParams(in_data, checked, active));
    return err;
}

PF_Err SmartRender(PF_InData* in_data, PF_OutData* out_data, PF_SmartRenderExtra* extra)
{
    PF_Err err = PF_Err_NONE;
    PF_Err err2 = PF_Err_NONE;
    PF_ParamDef checked[9]{};
    bool active[9]{};
    PF_EffectWorld* input = nullptr;
    PF_EffectWorld* output = nullptr;
    PF_WorldSuite2* world_suite = nullptr;
    bool input_checked_out = false;

    ERR(CheckoutControlParams(in_data, checked, active));
    if (!err) {
        ERR(extra->cb->checkout_layer_pixels(in_data->effect_ref, GRDMAP_INPUT, &input));
        input_checked_out = !err;
    }
    if (!err) ERR(extra->cb->checkout_output(in_data->effect_ref, &output));
    if (!err && !output) err = PF_Err_BAD_CALLBACK_PARAM;
    if (!err) {
        ERR(AEFX_AcquireSuite(
            in_data,
            out_data,
            kPFWorldSuite,
            kPFWorldSuiteVersion2,
            nullptr,
            reinterpret_cast<void**>(&world_suite)));
    }

    PF_Handle gradientH = checked[0].u.arb_d.value;
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    const GrdMapData* gradient = nullptr;
    if (!err && gradientH) {
        gradient = reinterpret_cast<const GrdMapData*>(suites.HandleSuite1()->host_lock_handle(gradientH));
    }
    if (!err && output->width > 0 && output->height > 0) {
        PF_PixelFormat format = PF_PixelFormat_INVALID;
        ERR(world_suite->PF_GetPixelFormat(output, &format));
        int bit_depth = 0;
        if (format == PF_PixelFormat_ARGB32) bit_depth = 8;
        else if (format == PF_PixelFormat_ARGB64) bit_depth = 16;
        else if (format == PF_PixelFormat_ARGB128) bit_depth = 32;
        else err = PF_Err_BAD_CALLBACK_PARAM;

        if (!err) {
            ERR(RenderGradient(
                in_data,
                input,
                output,
                bit_depth,
                gradient,
                checked[1].u.pd.value - 1,
                ClampRepeatCount(checked[5].u.sd.value),
                checked[6].u.pd.value - 1,
                checked[2].u.bd.value != FALSE,
                checked[3].u.bd.value != FALSE,
                checked[4].u.bd.value != FALSE,
                checked[7].u.pd.value - 1,
                static_cast<float>(checked[8].u.sd.value) / 100.0f));
        }
    }

    if (gradient) suites.HandleSuite1()->host_unlock_handle(gradientH);
    if (world_suite) {
        ERR2(AEFX_ReleaseSuite(in_data, out_data, kPFWorldSuite, kPFWorldSuiteVersion2, nullptr));
    }
    if (input_checked_out) ERR2(extra->cb->checkin_layer_pixels(in_data->effect_ref, GRDMAP_INPUT));
    ERR2(CheckinControlParams(in_data, checked, active));
    return err;
}

PF_Err UserChangedParam(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[],
    const PF_UserChangedParamExtra* changed)
{
    if (!changed) return PF_Err_NONE;
    if (changed->param_index != GRDMAP_IMPORT &&
        changed->param_index != GRDMAP_GRADIENT_INDEX) {
        return PF_Err_NONE;
    }
    if (!in_data || !out_data || !params || !params[GRDMAP_PREVIEW] ||
        !params[GRDMAP_GRADIENT_INDEX]) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }
#ifdef AE_OS_WIN
    A_char error_message[PF_MAX_EFFECT_MSG_LEN + 1]{};
    GrdMapData selected{};
    bool succeeded = false;
    bool cancelled = false;

    if (changed->param_index == GRDMAP_IMPORT) {
        succeeded = ImportGradientFromDialog(
            in_data, &selected, &cancelled, error_message, sizeof(error_message));
    } else {
        GrdMapData current{};
        const PF_Err read_err = ReadGradientData(in_data, params, &current);
        if (read_err) return read_err;
        succeeded = SelectGradientFromCache(
            &current,
            params[GRDMAP_GRADIENT_INDEX]->u.sd.value - 1,
            &selected,
            error_message,
            sizeof(error_message));
        if (!succeeded) {
            params[GRDMAP_GRADIENT_INDEX]->u.sd.value = current.gradient_index + 1;
            params[GRDMAP_GRADIENT_INDEX]->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
        }
    }

    if (succeeded) {
        const PF_Err store_err = StoreGradientData(in_data, params, selected);
        if (store_err) return store_err;
        params[GRDMAP_GRADIENT_INDEX]->u.sd.value = selected.gradient_index + 1;
        params[GRDMAP_GRADIENT_INDEX]->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
        out_data->out_flags |= PF_OutFlag_REFRESH_UI | PF_OutFlag_FORCE_RERENDER;
        return UpdateGradientUI(in_data, params);
    }
    if (!cancelled) {
        AEGP_SuiteHandler suites(in_data->pica_basicP);
        suites.ANSICallbacksSuite1()->sprintf(out_data->return_msg, "%s", error_message);
        out_data->out_flags |= PF_OutFlag_DISPLAY_ERROR_MESSAGE | PF_OutFlag_REFRESH_UI;
    }
#else
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    suites.ANSICallbacksSuite1()->sprintf(out_data->return_msg, "%s", "GRD import is available on Windows.");
    out_data->out_flags |= PF_OutFlag_DISPLAY_ERROR_MESSAGE;
#endif
    return PF_Err_NONE;
}

PF_Err HandleEvent(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[],
    PF_EventExtra* event_extra)
{
    if (event_extra && event_extra->e_type == PF_Event_DRAW) {
        return DrawGradientPreview(in_data, out_data, params, event_extra);
    }
    return PF_Err_NONE;
}

} // namespace

extern "C" DllExport
PF_Err PluginDataEntryFunction2(
    PF_PluginDataPtr inPtr,
    PF_PluginDataCB2 inPluginDataCallBackPtr,
    SPBasicSuite* inSPBasicSuitePtr,
    const char* inHostName,
    const char* inHostVersion)
{
    PF_Err result = PF_Err_INVALID_CALLBACK;
    result = PF_REGISTER_EFFECT_EXT2(
        inPtr,
        inPluginDataCallBackPtr,
        "grdmap",
        "mknb:grdMap",
        "mknb_Plugin",
        AE_RESERVED_INFO,
        "EffectMain",
        "https://www.adobe.com");
    return result;
}

PF_Err EffectMain(
    PF_Cmd cmd,
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[],
    PF_LayerDef* output,
    void* extra)
{
    PF_Err err = PF_Err_NONE;
    try {
        switch (cmd) {
            case PF_Cmd_ABOUT:
                err = About(in_data, out_data);
                break;
            case PF_Cmd_GLOBAL_SETUP:
                err = GlobalSetup(out_data);
                break;
            case PF_Cmd_PARAMS_SETUP:
                err = ParamsSetup(in_data, out_data);
                break;
            case PF_Cmd_RENDER:
                err = Render(in_data, params, output);
                break;
            case PF_Cmd_SMART_PRE_RENDER:
                err = PreRender(in_data, reinterpret_cast<PF_PreRenderExtra*>(extra));
                break;
            case PF_Cmd_SMART_RENDER:
                err = SmartRender(in_data, out_data, reinterpret_cast<PF_SmartRenderExtra*>(extra));
                break;
            case PF_Cmd_USER_CHANGED_PARAM:
                err = UserChangedParam(
                    in_data, out_data, params,
                    reinterpret_cast<const PF_UserChangedParamExtra*>(extra));
                break;
            case PF_Cmd_UPDATE_PARAMS_UI:
                err = UpdateGradientUI(in_data, params);
                break;
            case PF_Cmd_EVENT:
                err = HandleEvent(
                    in_data, out_data, params,
                    reinterpret_cast<PF_EventExtra*>(extra));
                break;
            case PF_Cmd_ARBITRARY_CALLBACK:
                err = HandleArbitrary(in_data, reinterpret_cast<PF_ArbParamsExtra*>(extra));
                break;
            default:
                break;
        }
    } catch (PF_Err& thrown_err) {
        err = thrown_err;
    } catch (...) {
        err = PF_Err_INTERNAL_STRUCT_DAMAGED;
    }
    return err;
}
