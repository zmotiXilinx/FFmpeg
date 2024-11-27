#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/hdr_dynamic_metadata.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "video.h"

#include <sys/select.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>


typedef struct ExtractHdr10PlusContext {
    const AVClass *class;
    char *filename;
    size_t frame_number;
    struct json_object *json_root;
} ExtractHdr10PlusContext;

#define OFFSET(x) offsetof(ExtractHdr10PlusContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption extract_hdr10plus_options[] = {
    { "filename", "JSON file", OFFSET(filename), AV_OPT_TYPE_STRING, {.str="hdr10plus.json"}, 0, 0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(extract_hdr10plus);

// sanitize the user options and allocate memory
static av_cold int init(AVFilterContext *ctx)
{
    ExtractHdr10PlusContext *hdrCtx = ctx->priv;
    hdrCtx->frame_number = 0;
    hdrCtx->json_root = json_object_new_array();
    return 0;
}

// free up allocated memory
static av_cold void uninit(AVFilterContext *ctx)
{
    ExtractHdr10PlusContext *hdrCtx = ctx->priv;

    if (json_object_to_file_ext(hdrCtx->filename, hdrCtx->json_root, JSON_C_TO_STRING_PRETTY)) {
      av_log(ctx, AV_LOG_ERROR, "Failed to write JSON file %s\n", hdrCtx->filename);
    }
    json_object_put(hdrCtx->json_root);
}

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_NONE
    };
    return ff_set_common_formats_from_list2(ctx, cfg_in, cfg_out, pix_fmts);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    ExtractHdr10PlusContext *hdrCtx = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out = NULL;
    AVFrameSideData* sd;
    int direct = 0;

    if (av_frame_is_writable(in)) {
        direct = 1;
        out = in;
    } else {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }

    if (sd = av_frame_get_side_data(out, AV_FRAME_DATA_DYNAMIC_HDR_PLUS)) {
      AVDynamicHDRPlus *hdr_plus = (AVDynamicHDRPlus*)sd->data;
      av_dynamic_hdr_plus_to_json(hdr_plus, hdrCtx->json_root, hdrCtx->frame_number);
    }

    if (!direct)
        av_frame_free(&in);

    ++hdrCtx->frame_number;

    return ff_filter_frame(outlink, out);

}

static const AVFilterPad extract_hdr10plus_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

const AVFilter ff_vf_extract_hdr10plus = {
    .name          = "extract_hdr10plus",
    .description   = NULL_IF_CONFIG_SMALL("Extract HDR10Plus to JSON file"),
    .priv_size     = sizeof(ExtractHdr10PlusContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(extract_hdr10plus_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_QUERY_FUNC2(query_formats),
    .priv_class    = &extract_hdr10plus_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
