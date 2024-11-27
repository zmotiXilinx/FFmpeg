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

typedef struct InjectHdr10PlusContext {
    const AVClass *class;
    char *filename;
    size_t frame_number;
    struct json_object *json_root;
} InjectHdr10PlusContext;

#define OFFSET(x) offsetof(InjectHdr10PlusContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption inject_hdr10plus_options[] = {
    { "filename", "JSON file", OFFSET(filename), AV_OPT_TYPE_STRING, {.str="hdr10plus.json"}, 0, 0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(inject_hdr10plus);

// sanitize the user options and allocate memory
static av_cold int init(AVFilterContext *ctx)
{
    InjectHdr10PlusContext *hdrCtx = ctx->priv;
    hdrCtx->frame_number = 0;
    hdrCtx->json_root = json_object_from_file(hdrCtx->filename);
    if (!hdrCtx->json_root) {
        av_log(ctx, AV_LOG_ERROR, "Failed to parse JSON file %s\n", hdrCtx->filename);
        return AVERROR(EINVAL);
    }
    return 0;
}

// free up allocated memory
static av_cold void uninit(AVFilterContext *ctx)
{
    InjectHdr10PlusContext *ccCtx = ctx->priv;
    json_object_put(ccCtx->json_root);
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
    InjectHdr10PlusContext *hdrCtx = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    // FilterLink *il = ff_filter_link(inlink);
    AVFrame *out = NULL;
    int direct = 0;
    AVDynamicHDRPlus* hdr_plus = NULL;
    AVFrameSideData* sd = NULL;
    struct json_object* json_hdr_plus;
    int ret = 0;

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

    json_hdr_plus = search_frame_in_array(hdrCtx->json_root, hdrCtx->frame_number);
    if (!json_hdr_plus) {
        goto end;
    }

    sd = av_frame_get_side_data(out, AV_FRAME_DATA_DYNAMIC_HDR_PLUS);
    if (sd) {
        av_log(ctx, AV_LOG_WARNING, "HDR10Plus already present in frame %lu\n", hdrCtx->frame_number);
        hdr_plus = (AVDynamicHDRPlus*)sd->data;
    } else {
        hdr_plus = av_dynamic_hdr_plus_create_side_data(out);
        if (!hdr_plus) {
            av_log(ctx, AV_LOG_ERROR, "Failed to create HDR10Plus side data in frame %lu\n", hdrCtx->frame_number);
            ret =  AVERROR(ENOMEM);
        }
    }
    ret = av_dynamic_hdr_plus_from_json(hdr_plus, json_hdr_plus);
    if (ret < 0) {
        av_log(ctx, AV_LOG_WARNING, "Failed to inject HDR10Plus in frame %lu\n", hdrCtx->frame_number);
    }

end:
    ++hdrCtx->frame_number;
    if (!direct)
        av_frame_free(&in);

    if (ret < 0) {
        return ret;
    }
    return ff_filter_frame(outlink, out);

}

static const AVFilterPad inject_hdr10plus_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

const AVFilter ff_vf_inject_hdr10plus = {
    .name          = "inject_hdr10plus",
    .description   = NULL_IF_CONFIG_SMALL("Inject HDR10Plus from JSON file"),
    .priv_size     = sizeof(InjectHdr10PlusContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(inject_hdr10plus_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_QUERY_FUNC2(query_formats),
    .priv_class    = &inject_hdr10plus_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
