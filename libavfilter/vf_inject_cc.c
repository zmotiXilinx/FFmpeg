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

#define MAX_SRT_SIZE (1 * 1024 * 1024)

typedef struct SrtData {
    size_t m_size;
    utf8_char_t m_data[MAX_SRT_SIZE];
} SrtData;

typedef struct InjectCCContext {
    const AVClass *class;
    char *filename;
    int   srtFd;
    SrtData *srtData;
    srt_t* srt;
    int frameCount;
    srt_cue_t* srtCue;
    int clear_cc_payload_sent;
} InjectCCContext;

#define OFFSET(x) offsetof(InjectCCContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption inject_cc_options[] = {
    { "filename", "srt file", OFFSET(filename), AV_OPT_TYPE_STRING, {.str="sample.srt"}, 0, 0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(inject_cc);

// sanitize the user options and allocate memory
static av_cold int init(AVFilterContext *ctx)
{
    InjectCCContext *ccCtx = ctx->priv;

    ccCtx->srtFd = open(ccCtx->filename, O_RDWR);
    if (ccCtx->srtFd < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to open file %s\n", ccCtx->filename);
        return AVERROR(EINVAL);
    }

    ccCtx->srtData = av_malloc(sizeof(SrtData));
    if (!ccCtx->srtData) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate memory for SRT data\n");
        return AVERROR(ENOMEM);
    }
    ccCtx->srtData->m_size = 0;
    ccCtx->frameCount = 0;
    ccCtx->clear_cc_payload_sent = 0;

    return 0;
}

// free up allocated memory
static av_cold void uninit(AVFilterContext *ctx)
{
    InjectCCContext *ccCtx = ctx->priv;
    close(ccCtx->srtFd);
    av_freep(&ccCtx->srtData);
}

// retunes number of bytes read
// negative number on error
// retursn 0 on 'not ready' and 'eof'
// eof set to 1 on end, otherwise zero
static size_t fd_read(int fd, uint8_t* data, size_t size, int* eof)
{
    fd_set rfds;
    struct timeval tv;
    int retval;

    (*eof) = 0;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 1;
    retval = select(fd + 1, &rfds, NULL, NULL, &tv);

    if (0 > retval) {
        return retval;
    }

    // not ready
    if (!(retval && FD_ISSET(fd, &rfds))) {
        return 0;
    }

    retval = read(fd, data, size);

    if (0 == retval) {
        (*eof) = 1;
    }

    return retval;
}


static srt_t* srt_from_fd(int fd, SrtData* srtData)
{
    int eof;
    uint8_t c;

    for (;;) {
        int ret = fd_read(fd, &c, 1, &eof);

        if (eof || (1 == ret && 0 == c)) {
            srt_t* srt = srt_parse(&srtData->m_data[0], srtData->m_size);
            srtData->m_size = 0;
            return srt;
        }

        if (1 == ret) {
            if (srtData->m_size >= MAX_SRT_SIZE - 1) {
                av_log(NULL, AV_LOG_WARNING, "Warning MAX_SRT_SIZE reached. Clearing buffer\n");
                srtData->m_size = 0;
            }

            srtData->m_data[srtData->m_size] = c;
            srtData->m_size += 1;
        } else {
            return 0;
        }
    }
}

static int insert_cc_text_to_frame_side_data(AVFilterContext *ctx, const char* text, AVFrame* frame, double ts)
{
    caption_frame_t ccFrame;
    sei_t sei;
    uint8_t* a53Data;
    size_t a53Size = 0;
    sei_message_t* msg = NULL;
    int ret = 1;
    int count = 0;

    sei_init(&sei, ts);
    if (text) {
        caption_frame_init(&ccFrame);
        caption_frame_from_text(&ccFrame, text);
        sei_from_caption_frame(&sei, &ccFrame);
    } else {
        sei_from_caption_clear(&sei);
    }

    // sei_dump(&sei);

    for (msg = sei.head; msg; msg = sei_message_next(msg)) {
        // get a pointer to the raw A53 bytes
        // remove the country code, provider code and user_identifier (7 bytes)
        AVBufferRef *buf = NULL;
        a53Data = &(sei_message_data(msg)[7]);
        a53Size = sei_message_size(msg) - 7;

        ret = ff_parse_a53_cc(&buf, a53Data, a53Size);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR, "Failed to parse a53 cc\n");
            goto finish;
        }
        if (!ret) {
            av_log(ctx, AV_LOG_WARNING, "No a53 cc data according to ff_parse_a53_cc\n");
        }

        if (!av_frame_new_side_data_from_buf(frame, AV_FRAME_DATA_A53_CC, buf)) {
            av_log(ctx, AV_LOG_ERROR, "Failed to add a53 cc side data\n");
            av_freep(&buf);
            goto finish;
        }
        count++;
    }
    av_log(ctx, AV_LOG_DEBUG, "Added %d cc side data frame->nb_side_data: %d\n", count, frame->nb_side_data);

finish:
    sei_free(&sei);
    return ret;
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
    InjectCCContext *ccCtx = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    FilterLink *il = ff_filter_link(inlink);
    AVFrame *out = NULL;
    double frameTimestamp = ccCtx->frameCount * av_q2d(av_inv_q(il->frame_rate));
    double nextFrameTimestamp;
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

    ccCtx->srt = srt_from_fd(ccCtx->srtFd, ccCtx->srtData);

    av_log(ctx, AV_LOG_DEBUG, "F#%d FrameTS=%f\n", ccCtx->frameCount, frameTimestamp);

    if (ccCtx->srt) {
        ccCtx->srtCue = ccCtx->srt->cue_head;
    }

    if (ccCtx->srtCue && frameTimestamp >= ccCtx->srtCue->timestamp) {
        if (frameTimestamp <= (ccCtx->srtCue->timestamp + ccCtx->srtCue->duration)) {
            const char* text = srt_cue_data(ccCtx->srtCue);
            int ret;
            av_log(ctx, AV_LOG_DEBUG, "[FrameTS=%f] [CueTs=%f+%f] text: %s\n", frameTimestamp, ccCtx->srtCue->timestamp, ccCtx->srtCue->duration, text);
            ret = insert_cc_text_to_frame_side_data(ctx, text, out, frameTimestamp);
            if (ret < 0) {
                av_log(ctx, AV_LOG_ERROR, "Failed to insert cc text\n");
                return ret;
            }
        }
    } else {
        if (!ccCtx->clear_cc_payload_sent) {
            av_log(ctx, AV_LOG_DEBUG, "Clearing CC payload\n");
            insert_cc_text_to_frame_side_data(ctx, NULL, out, frameTimestamp);
            ccCtx->clear_cc_payload_sent = 1;
        }
    }

    ccCtx->frameCount += 1;
    nextFrameTimestamp = ccCtx->frameCount * av_q2d(av_inv_q(il->frame_rate));

    if (ccCtx->srtCue && nextFrameTimestamp > (ccCtx->srtCue->timestamp + ccCtx->srtCue->duration)) {
        av_log(ctx, AV_LOG_DEBUG, "SRT: advancing to next cue\n");
        ccCtx->srtCue = ccCtx->srtCue->next;
        ccCtx->clear_cc_payload_sent = 0;
    }

    if (!direct)
        av_frame_free(&in);

    return ff_filter_frame(outlink, out);

}

static const AVFilterPad inject_cc_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

const AVFilter ff_vf_inject_cc = {
    .name          = "inject_cc",
    .description   = NULL_IF_CONFIG_SMALL("Inject CC from SRT file"),
    .priv_size     = sizeof(InjectCCContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(inject_cc_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_QUERY_FUNC2(query_formats),
    .priv_class    = &inject_cc_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
