#include "grdMap.h"

#include <algorithm>
#include <cstring>

namespace {

struct GrdMapDataV1 {
    A_u_long magic;
    A_u_long version;
    A_long color_stop_count;
    A_long alpha_stop_count;
    A_char preset_name[GRDMAP_TEXT_CAPACITY];
    A_char file_name[GRDMAP_TEXT_CAPACITY];
    GrdMapColorStop color_stops[GRDMAP_MAX_COLOR_STOPS];
    GrdMapAlphaStop alpha_stops[GRDMAP_MAX_ALPHA_STOPS];
    float lut[GRDMAP_LUT_SIZE][4];
};

static_assert(offsetof(GrdMapData, gradient_index) == sizeof(GrdMapDataV1),
              "GrdMapData v2 must append fields after the v1 payload");

PF_Err CopyGradientHandle(PF_InData* in_data, PF_ArbitraryH sourceH, PF_ArbitraryH* destinationPH)
{
    if (!destinationPH) return PF_Err_BAD_CALLBACK_PARAM;
    if (!sourceH) return CreateDefaultGradient(in_data, destinationPH);
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    PF_Handle destinationH = suites.HandleSuite1()->host_new_handle(sizeof(GrdMapData));
    if (!destinationH) return PF_Err_OUT_OF_MEMORY;
    const GrdMapData* source = reinterpret_cast<const GrdMapData*>(suites.HandleSuite1()->host_lock_handle(sourceH));
    GrdMapData* destination = reinterpret_cast<GrdMapData*>(suites.HandleSuite1()->host_lock_handle(destinationH));
    if (!source || !destination) {
        if (source) suites.HandleSuite1()->host_unlock_handle(sourceH);
        if (destination) suites.HandleSuite1()->host_unlock_handle(destinationH);
        suites.HandleSuite1()->host_dispose_handle(destinationH);
        return PF_Err_OUT_OF_MEMORY;
    }
    std::memcpy(destination, source, sizeof(GrdMapData));
    suites.HandleSuite1()->host_unlock_handle(destinationH);
    suites.HandleSuite1()->host_unlock_handle(sourceH);
    *destinationPH = destinationH;
    return PF_Err_NONE;
}

DRAWBOT_ColorRGBA AppBackgroundColor(PF_InData* in_data, PF_OutData* out_data)
{
    DRAWBOT_ColorRGBA result{0.12f, 0.12f, 0.12f, 1.0f};
    PFAppSuite4* app_suite = nullptr;
    if (AEFX_AcquireSuite(in_data, out_data, kPFAppSuite, kPFAppSuiteVersion4, nullptr,
                          reinterpret_cast<void**>(&app_suite)) == PF_Err_NONE && app_suite) {
        PF_App_Color app_color{};
        if (app_suite->PF_AppGetBgColor(&app_color) == PF_Err_NONE) {
            constexpr float scale = 1.0f / 65535.0f;
            result.red = app_color.red * scale;
            result.green = app_color.green * scale;
            result.blue = app_color.blue * scale;
        }
        AEFX_ReleaseSuite(in_data, out_data, kPFAppSuite, kPFAppSuiteVersion4, nullptr);
    }
    return result;
}

} // namespace

PF_Err CreateDefaultGradient(PF_InData* in_data, PF_ArbitraryH* gradientPH)
{
    if (!gradientPH) return PF_Err_BAD_CALLBACK_PARAM;
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    PF_Handle handle = suites.HandleSuite1()->host_new_handle(sizeof(GrdMapData));
    if (!handle) return PF_Err_OUT_OF_MEMORY;
    GrdMapData* data = reinterpret_cast<GrdMapData*>(suites.HandleSuite1()->host_lock_handle(handle));
    if (!data) {
        suites.HandleSuite1()->host_dispose_handle(handle);
        return PF_Err_OUT_OF_MEMORY;
    }
    std::memset(data, 0, sizeof(*data));
    data->magic = GRDMAP_ARB_MAGIC;
    data->version = GRDMAP_ARB_VERSION;
    data->color_stop_count = 2;
    data->alpha_stop_count = 2;
    std::memcpy(data->preset_name, "Black, White", sizeof("Black, White"));
    std::memcpy(data->file_name, "Built-in", sizeof("Built-in"));
    data->color_stops[0] = {0.0f, 0.5f, 0.0f, 0.0f, 0.0f};
    data->color_stops[1] = {1.0f, 0.5f, 1.0f, 1.0f, 1.0f};
    data->alpha_stops[0] = {0.0f, 0.5f, 1.0f};
    data->alpha_stops[1] = {1.0f, 0.5f, 1.0f};
    data->gradient_index = 0;
    data->gradient_count = 1;
    for (int i = 0; i < GRDMAP_LUT_SIZE; ++i) {
        const float value = static_cast<float>(i) / static_cast<float>(GRDMAP_LUT_SIZE - 1);
        data->lut[i][0] = value;
        data->lut[i][1] = value;
        data->lut[i][2] = value;
        data->lut[i][3] = 1.0f;
    }
    suites.HandleSuite1()->host_unlock_handle(handle);
    *gradientPH = handle;
    return PF_Err_NONE;
}

PF_Err HandleArbitrary(PF_InData* in_data, PF_ArbParamsExtra* extra)
{
    if (!extra) return PF_Err_BAD_CALLBACK_PARAM;
    PF_Err err = PF_Err_NONE;
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    switch (extra->which_function) {
        case PF_Arbitrary_NEW_FUNC:
            if (extra->u.new_func_params.refconPV != GRDMAP_ARB_REFCON) return PF_Err_UNRECOGNIZED_PARAM_TYPE;
            return CreateDefaultGradient(in_data, extra->u.new_func_params.arbPH);

        case PF_Arbitrary_DISPOSE_FUNC:
            if (extra->u.dispose_func_params.refconPV != GRDMAP_ARB_REFCON) return PF_Err_UNRECOGNIZED_PARAM_TYPE;
            if (extra->u.dispose_func_params.arbH) {
                suites.HandleSuite1()->host_dispose_handle(extra->u.dispose_func_params.arbH);
            }
            break;

        case PF_Arbitrary_COPY_FUNC:
            if (extra->u.copy_func_params.refconPV != GRDMAP_ARB_REFCON) return PF_Err_UNRECOGNIZED_PARAM_TYPE;
            return CopyGradientHandle(in_data, extra->u.copy_func_params.src_arbH,
                                      extra->u.copy_func_params.dst_arbPH);

        case PF_Arbitrary_FLAT_SIZE_FUNC:
            if (extra->u.flat_size_func_params.refconPV != GRDMAP_ARB_REFCON) return PF_Err_UNRECOGNIZED_PARAM_TYPE;
            *extra->u.flat_size_func_params.flat_data_sizePLu = sizeof(GrdMapData);
            break;

        case PF_Arbitrary_FLATTEN_FUNC: {
            if (extra->u.flatten_func_params.refconPV != GRDMAP_ARB_REFCON ||
                extra->u.flatten_func_params.buf_sizeLu < sizeof(GrdMapData)) {
                return PF_Err_BAD_CALLBACK_PARAM;
            }
            const GrdMapData* source = reinterpret_cast<const GrdMapData*>(
                suites.HandleSuite1()->host_lock_handle(extra->u.flatten_func_params.arbH));
            if (!source) return PF_Err_OUT_OF_MEMORY;
            std::memcpy(extra->u.flatten_func_params.flat_dataPV, source, sizeof(GrdMapData));
            suites.HandleSuite1()->host_unlock_handle(extra->u.flatten_func_params.arbH);
            break;
        }

        case PF_Arbitrary_UNFLATTEN_FUNC: {
            if (extra->u.unflatten_func_params.refconPV != GRDMAP_ARB_REFCON) {
                return PF_Err_BAD_CALLBACK_PARAM;
            }
            const void* source = extra->u.unflatten_func_params.flat_dataPV;
            const A_u_long source_size = extra->u.unflatten_func_params.buf_sizeLu;
            if (!source) {
                return PF_Err_INTERNAL_STRUCT_DAMAGED;
            }
            const GrdMapDataV1* header = reinterpret_cast<const GrdMapDataV1*>(source);
            const bool is_v1 = source_size == sizeof(GrdMapDataV1) &&
                               header->magic == GRDMAP_ARB_MAGIC && header->version == 1U;
            const bool is_v2 = source_size == sizeof(GrdMapData) &&
                               header->magic == GRDMAP_ARB_MAGIC && header->version == GRDMAP_ARB_VERSION;
            if (!is_v1 && !is_v2) return PF_Err_INTERNAL_STRUCT_DAMAGED;
            PF_Handle handle = suites.HandleSuite1()->host_new_handle(sizeof(GrdMapData));
            if (!handle) return PF_Err_OUT_OF_MEMORY;
            GrdMapData* destination = reinterpret_cast<GrdMapData*>(suites.HandleSuite1()->host_lock_handle(handle));
            if (!destination) {
                suites.HandleSuite1()->host_dispose_handle(handle);
                return PF_Err_OUT_OF_MEMORY;
            }
            std::memset(destination, 0, sizeof(*destination));
            std::memcpy(destination, source, is_v1 ? sizeof(GrdMapDataV1) : sizeof(GrdMapData));
            if (is_v1) {
                destination->version = GRDMAP_ARB_VERSION;
                destination->gradient_index = 0;
                destination->gradient_count = 1;
            }
            suites.HandleSuite1()->host_unlock_handle(handle);
            *extra->u.unflatten_func_params.arbPH = handle;
            break;
        }

        case PF_Arbitrary_INTERP_FUNC: {
            if (extra->u.interp_func_params.refconPV != GRDMAP_ARB_REFCON) return PF_Err_UNRECOGNIZED_PARAM_TYPE;
            const PF_ArbitraryH source = extra->u.interp_func_params.tF < 0.5
                ? extra->u.interp_func_params.left_arbH
                : extra->u.interp_func_params.right_arbH;
            return CopyGradientHandle(in_data, source, extra->u.interp_func_params.interpPH);
        }

        case PF_Arbitrary_COMPARE_FUNC: {
            if (extra->u.compare_func_params.refconPV != GRDMAP_ARB_REFCON) return PF_Err_UNRECOGNIZED_PARAM_TYPE;
            const GrdMapData* first = reinterpret_cast<const GrdMapData*>(
                suites.HandleSuite1()->host_lock_handle(extra->u.compare_func_params.a_arbH));
            const GrdMapData* second = reinterpret_cast<const GrdMapData*>(
                suites.HandleSuite1()->host_lock_handle(extra->u.compare_func_params.b_arbH));
            if (!first || !second) {
                if (first) suites.HandleSuite1()->host_unlock_handle(extra->u.compare_func_params.a_arbH);
                if (second) suites.HandleSuite1()->host_unlock_handle(extra->u.compare_func_params.b_arbH);
                return PF_Err_OUT_OF_MEMORY;
            }
            *extra->u.compare_func_params.compareP =
                std::memcmp(first, second, sizeof(GrdMapData)) == 0 ? PF_ArbCompare_EQUAL : PF_ArbCompare_NOT_EQUAL;
            suites.HandleSuite1()->host_unlock_handle(extra->u.compare_func_params.b_arbH);
            suites.HandleSuite1()->host_unlock_handle(extra->u.compare_func_params.a_arbH);
            break;
        }

        case PF_Arbitrary_PRINT_SIZE_FUNC:
            if (extra->u.print_size_func_params.refconPV != GRDMAP_ARB_REFCON) return PF_Err_UNRECOGNIZED_PARAM_TYPE;
            *extra->u.print_size_func_params.print_sizePLu = GRDMAP_TEXT_CAPACITY;
            break;

        case PF_Arbitrary_PRINT_FUNC: {
            if (extra->u.print_func_params.refconPV != GRDMAP_ARB_REFCON) return PF_Err_UNRECOGNIZED_PARAM_TYPE;
            const GrdMapData* data = reinterpret_cast<const GrdMapData*>(
                suites.HandleSuite1()->host_lock_handle(extra->u.print_func_params.arbH));
            if (!data) return PF_Err_OUT_OF_MEMORY;
            if (extra->u.print_func_params.print_bufferPC && extra->u.print_func_params.print_sizeLu > 0) {
                const size_t capacity = extra->u.print_func_params.print_sizeLu;
                const size_t count = (std::min)(capacity - 1, std::strlen(data->preset_name));
                std::memcpy(extra->u.print_func_params.print_bufferPC, data->preset_name, count);
                extra->u.print_func_params.print_bufferPC[count] = '\0';
            }
            suites.HandleSuite1()->host_unlock_handle(extra->u.print_func_params.arbH);
            break;
        }

        case PF_Arbitrary_SCAN_FUNC:
            if (extra->u.scan_func_params.refconPV != GRDMAP_ARB_REFCON) return PF_Err_UNRECOGNIZED_PARAM_TYPE;
            err = CreateDefaultGradient(in_data, extra->u.scan_func_params.arbPH);
            break;

        default:
            break;
    }
    return err;
}

PF_Err DrawGradientPreview(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[],
    PF_EventExtra* event_extra)
{
    if (!event_extra ||
        event_extra->effect_win.area != PF_EA_CONTROL ||
        event_extra->effect_win.index != GRDMAP_PREVIEW_UI) {
        return PF_Err_NONE;
    }
    if (!params || !params[GRDMAP_PREVIEW]) return PF_Err_BAD_CALLBACK_PARAM;
    PF_Err err = PF_Err_NONE;
    PF_Err err2 = PF_Err_NONE;
    DRAWBOT_Suites drawbot{};
    DRAWBOT_DrawRef drawing = nullptr;
    DRAWBOT_SupplierRef supplier = nullptr;
    DRAWBOT_SurfaceRef surface = nullptr;
    PF_EffectCustomUISuite1* custom_ui = nullptr;

    ERR(AEFX_AcquireDrawbotSuites(in_data, out_data, &drawbot));
    ERR(AEFX_AcquireSuite(in_data, out_data, kPFEffectCustomUISuite, kPFEffectCustomUISuiteVersion1,
                          nullptr, reinterpret_cast<void**>(&custom_ui)));
    if (!err && custom_ui) ERR(custom_ui->PF_GetDrawingReference(event_extra->contextH, &drawing));
    if (custom_ui) {
        ERR2(AEFX_ReleaseSuite(in_data, out_data, kPFEffectCustomUISuite, kPFEffectCustomUISuiteVersion1, nullptr));
    }
    if (!err) ERR(drawbot.drawbot_suiteP->GetSupplier(drawing, &supplier));
    if (!err) ERR(drawbot.drawbot_suiteP->GetSurface(drawing, &surface));

    const float frame_left = static_cast<float>(event_extra->effect_win.current_frame.left);
    const float frame_top = static_cast<float>(event_extra->effect_win.current_frame.top);
    const float frame_width = static_cast<float>(event_extra->effect_win.current_frame.right - event_extra->effect_win.current_frame.left);
    const float frame_height = static_cast<float>(event_extra->effect_win.current_frame.bottom + 1 - event_extra->effect_win.current_frame.top);
    DRAWBOT_RectF32 frame{frame_left, frame_top, frame_width, frame_height};
    const DRAWBOT_ColorRGBA background = AppBackgroundColor(in_data, out_data);
    if (!err) ERR(drawbot.surface_suiteP->PaintRect(surface, &background, &frame));

    AEGP_SuiteHandler suites(in_data->pica_basicP);
    PF_Handle handle = params[GRDMAP_PREVIEW]->u.arb_d.value;
    if (!handle) {
        ERR2(AEFX_ReleaseDrawbotSuites(in_data, out_data));
        return PF_Err_INTERNAL_STRUCT_DAMAGED;
    }
    const GrdMapData* data = reinterpret_cast<const GrdMapData*>(suites.HandleSuite1()->host_lock_handle(handle));
    if (!data || data->magic != GRDMAP_ARB_MAGIC) {
        if (data) suites.HandleSuite1()->host_unlock_handle(handle);
        ERR2(AEFX_ReleaseDrawbotSuites(in_data, out_data));
        return PF_Err_INTERNAL_STRUCT_DAMAGED;
    }

    const float left = frame_left + 6.0f;
    const float right = frame_left + (std::max)(frame_width - 6.0f, 16.0f);
    const float width = (std::max)(1.0f, right - left);
    const float bar_top = frame_top + 6.0f;
    const float bar_height = (std::max)(18.0f, frame_height - 12.0f);
    const int columns = (std::max)(1, static_cast<int>(width));
    for (int x = 0; x < columns && !err; ++x) {
        const float position = columns > 1 ? static_cast<float>(x) / static_cast<float>(columns - 1) : 0.0f;
        const int lut_index = (std::min)(GRDMAP_LUT_SIZE - 1,
            static_cast<int>(position * static_cast<float>(GRDMAP_LUT_SIZE - 1) + 0.5f));
        const DRAWBOT_ColorRGBA color{
            data->lut[lut_index][0], data->lut[lut_index][1], data->lut[lut_index][2], data->lut[lut_index][3]};
        const DRAWBOT_RectF32 strip{left + static_cast<float>(x), bar_top, 1.0f, bar_height};
        ERR(drawbot.surface_suiteP->PaintRect(surface, &color, &strip));
    }

    DRAWBOT_PathRef border_path = nullptr;
    DRAWBOT_PenRef border_pen = nullptr;
    const DRAWBOT_ColorRGBA border_color{0.9f, 0.9f, 0.9f, 1.0f};
    if (!err) ERR(drawbot.supplier_suiteP->NewPath(supplier, &border_path));
    const DRAWBOT_RectF32 bar_rect{left, bar_top, width, bar_height};
    if (!err) ERR(drawbot.path_suiteP->AddRect(border_path, &bar_rect));
    if (!err) ERR(drawbot.supplier_suiteP->NewPen(supplier, &border_color, 1.0f, &border_pen));
    if (!err) ERR(drawbot.surface_suiteP->StrokePath(surface, border_pen, border_path));

    if (border_pen) ERR2(drawbot.supplier_suiteP->ReleaseObject(reinterpret_cast<DRAWBOT_ObjectRef>(border_pen)));
    if (border_path) ERR2(drawbot.supplier_suiteP->ReleaseObject(reinterpret_cast<DRAWBOT_ObjectRef>(border_path)));
    suites.HandleSuite1()->host_unlock_handle(handle);
    ERR2(AEFX_ReleaseDrawbotSuites(in_data, out_data));
    event_extra->evt_out_flags |= PF_EO_HANDLED_EVENT;
    return err;
}
