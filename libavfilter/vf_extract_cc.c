#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "video.h"
#include "libavcodec/atsc_a53.h"

// libcaption
#include "libcaption/srt.h"
#include "libcaption/mpeg.h"

#include <sys/select.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#define MAX_CEA_NUM 8192
#define MAX_CAPTION_SIZE 1024

typedef struct ceaData {
  cea708_t *m_buf[MAX_CEA_NUM];
  int m_num;
} ceaData;

typedef struct ExtractCCContext {
    const AVClass *class;
    char *filename;
    int  frameCount;
    FILE *srtFile;
    ceaData ceaData;
    srt_t *srt;
    void *ccTextPrevBuffer;
    size_t ccTextPrevBufferLen;
    void *ccTextBuffer;
    size_t ccTextBufferLen;
} ExtractCCContext;

#define OFFSET(x) offsetof(ExtractCCContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption extract_cc_options[] = {
    { "filename", "srt file", OFFSET(filename), AV_OPT_TYPE_STRING, {.str="out.srt"}, 0, 0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(extract_cc);

// sanitize the user options and allocate memory
static av_cold int init(AVFilterContext *ctx)
{
    ExtractCCContext *ccCtx = ctx->priv;

    ccCtx->srtFile = fopen(ccCtx->filename, "w+");
    if (!ccCtx->srtFile) {
        av_log(ctx, AV_LOG_ERROR, "Failed to open file %s\n", ccCtx->filename);
        return AVERROR(EINVAL);
    }
    ccCtx->frameCount = 0;

    for (int i = 0; i < MAX_CEA_NUM; ++i)
      ccCtx->ceaData.m_buf[i] = NULL;
    ccCtx->ceaData.m_num = 0;

    ccCtx->srt = srt_new();

    ccCtx->ccTextBuffer = av_malloc(MAX_CAPTION_SIZE);
    if (!ccCtx->ccTextBuffer) {
      av_log(ctx, AV_LOG_ERROR, "Not enough memory for ccTextBuffer\n");
      return AVERROR(ENOMEM);
    }
    ccCtx->ccTextBufferLen = 0;
    ccCtx->ccTextPrevBuffer = av_malloc(MAX_CAPTION_SIZE);
    if (!ccCtx->ccTextPrevBuffer) {
      av_log(ctx, AV_LOG_ERROR, "Not enough memory for ccTextBuffer\n");
      return AVERROR(ENOMEM);
    }
    ccCtx->ccTextPrevBufferLen = -1;

    return 0;
}

// free up allocated memory
static av_cold void uninit(AVFilterContext *ctx)
{
    ExtractCCContext *ccCtx = ctx->priv;

    srt_dump(ccCtx->srt, ccCtx->srtFile);
    fclose(ccCtx->srtFile);

    for (int i = 0; i < ccCtx->ceaData.m_num; ++i)
      av_free(ccCtx->ceaData.m_buf[i]);

    srt_free(ccCtx->srt);
    av_free(ccCtx->ccTextBuffer);
    av_free(ccCtx->ccTextPrevBuffer);
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
    ExtractCCContext *ccCtx = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    FilterLink *il = ff_filter_link(inlink);
    AVFrame *out = NULL;

    double frameTimestamp = ccCtx->frameCount * av_q2d(av_inv_q(il->frame_rate));
    int direct = 0;
    caption_frame_t frame;
    int frameCCNum = 0;

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

    caption_frame_init(&frame);

    for (int i = 0; i < in->nb_side_data; ++i) {
      if (in->side_data[i]->type == AV_FRAME_DATA_A53_CC) {
        void* sei_data = NULL;
        size_t sei_size = 0;
        int ret = 0;
        cea708_t *cea708 = NULL;

        ++frameCCNum;

        ret = ff_alloc_a53_sei(in, 0, &sei_data, &sei_size, i);
        if (ret < 0) {
          av_log(ctx, AV_LOG_ERROR, "Not enough memory for closed captions, skipping\n");
          return AVERROR(ENOMEM);
        }

        cea708 = ccCtx->ceaData.m_buf[ccCtx->ceaData.m_num];
        cea708 = av_malloc(sizeof(cea708_t));
        if (!cea708) {
          av_log(ctx, AV_LOG_ERROR, "Not enough memory for cea708\n");
          return AVERROR(ENOMEM);
        }
        ++ccCtx->ceaData.m_num;
        cea708_init(cea708, frameTimestamp);
        if (cea708_parse_h264(sei_data, sei_size, cea708) != LIBCAPTION_OK) {
          av_log(ctx, AV_LOG_ERROR, "Failed to parse closed captions\n");
          return AVERROR(EINVAL);
        }
        cea708_to_caption_frame(&frame, cea708);
      }
    }

    if (frameCCNum > 0) {
      if (av_log_get_level() >= AV_LOG_DEBUG) {
        caption_frame_dump(&frame);  // dump frame for debugging
      }
      // remove duplicate captions and update srt timestamp/duration
      ccCtx->ccTextBufferLen = caption_frame_to_text(&frame, ccCtx->ccTextBuffer);
      if (ccCtx->ccTextBufferLen > 0) {
        if (ccCtx->ccTextBufferLen == ccCtx->ccTextPrevBufferLen &&
            memcmp(ccCtx->ccTextBuffer, ccCtx->ccTextPrevBuffer, ccCtx->ccTextBufferLen) == 0) {
          // duplicate caption, skip but increase duration
          ccCtx->srt->cue_tail->duration += av_q2d(av_inv_q(il->frame_rate));
        } else {
          // add caption to srt for dumping at the end of the video
          srt_cue_from_caption_frame(&frame, ccCtx->srt);
        }
      }
      // record previous caption text for next comparison
      ccCtx->ccTextPrevBufferLen = caption_frame_to_text(&frame, ccCtx->ccTextPrevBuffer);
    }

    ccCtx->frameCount += 1;

    
    

    if (!direct)
        av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

static const AVFilterPad extract_cc_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

const AVFilter ff_vf_extract_cc = {
    .name          = "extract_cc",
    .description   = NULL_IF_CONFIG_SMALL("Extract CC from bitstream"),
    .priv_size     = sizeof(ExtractCCContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(extract_cc_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_QUERY_FUNC2(query_formats),
    .priv_class    = &extract_cc_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
