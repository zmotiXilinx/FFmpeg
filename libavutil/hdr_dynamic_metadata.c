/**
 * Copyright (c) 2018 Mohammad Izadi <moh.izadi at gmail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "avassert.h"
#include "hdr_dynamic_metadata.h"
#include "mem.h"
#include "libavcodec/defs.h"
#include "libavcodec/get_bits.h"
#include "libavcodec/put_bits.h"

static const int64_t luminance_den = 1;
static const int32_t peak_luminance_den = 15;
static const int64_t rgb_den = 100000;
static const int32_t fraction_pixel_den = 1000;
static const int32_t knee_point_den = 4095;
static const int32_t bezier_anchor_den = 1023;
static const int32_t saturation_weight_den = 8;

AVDynamicHDRPlus *av_dynamic_hdr_plus_alloc(size_t *size)
{
    AVDynamicHDRPlus *hdr_plus = av_mallocz(sizeof(AVDynamicHDRPlus));
    if (!hdr_plus)
        return NULL;

    if (size)
        *size = sizeof(*hdr_plus);

    return hdr_plus;
}

AVDynamicHDRPlus *av_dynamic_hdr_plus_create_side_data(AVFrame *frame)
{
    AVFrameSideData *side_data = av_frame_new_side_data(frame,
                                                        AV_FRAME_DATA_DYNAMIC_HDR_PLUS,
                                                        sizeof(AVDynamicHDRPlus));
    if (!side_data)
        return NULL;

    memset(side_data->data, 0, sizeof(AVDynamicHDRPlus));

    return (AVDynamicHDRPlus *)side_data->data;
}

int av_dynamic_hdr_plus_from_t35(AVDynamicHDRPlus *s, const uint8_t *data,
                                 size_t size)
{
    uint8_t padded_buf[AV_HDR_PLUS_MAX_PAYLOAD_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
    GetBitContext gbc, *gb = &gbc;
    int ret;

    if (!s)
        return AVERROR(ENOMEM);

    if (size > AV_HDR_PLUS_MAX_PAYLOAD_SIZE)
        return AVERROR(EINVAL);

    memcpy(padded_buf, data, size);
    // Zero-initialize the buffer padding to avoid overreads into uninitialized data.
    memset(padded_buf + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    ret = init_get_bits8(gb, padded_buf, size);
    if (ret < 0)
        return ret;

    if (get_bits_left(gb) < 10)
        return AVERROR_INVALIDDATA;

    s->application_version = get_bits(gb, 8);
    s->num_windows = get_bits(gb, 2);

    if (s->num_windows < 1 || s->num_windows > 3) {
        return AVERROR_INVALIDDATA;
    }

    if (get_bits_left(gb) < ((19 * 8 + 1) * (s->num_windows - 1)))
        return AVERROR_INVALIDDATA;

    for (int w = 1; w < s->num_windows; w++) {
        // The corners are set to absolute coordinates here. They should be
        // converted to the relative coordinates (in [0, 1]) in the decoder.
        AVHDRPlusColorTransformParams *params = &s->params[w];
        params->window_upper_left_corner_x =
            (AVRational){get_bits(gb, 16), 1};
        params->window_upper_left_corner_y =
            (AVRational){get_bits(gb, 16), 1};
        params->window_lower_right_corner_x =
            (AVRational){get_bits(gb, 16), 1};
        params->window_lower_right_corner_y =
            (AVRational){get_bits(gb, 16), 1};

        params->center_of_ellipse_x = get_bits(gb, 16);
        params->center_of_ellipse_y = get_bits(gb, 16);
        params->rotation_angle = get_bits(gb, 8);
        params->semimajor_axis_internal_ellipse = get_bits(gb, 16);
        params->semimajor_axis_external_ellipse = get_bits(gb, 16);
        params->semiminor_axis_external_ellipse = get_bits(gb, 16);
        params->overlap_process_option = get_bits1(gb);
    }

    if (get_bits_left(gb) < 28)
        return AVERROR_INVALIDDATA;

    s->targeted_system_display_maximum_luminance =
        (AVRational){get_bits_long(gb, 27), luminance_den};
    s->targeted_system_display_actual_peak_luminance_flag = get_bits1(gb);

    if (s->targeted_system_display_actual_peak_luminance_flag) {
        int rows, cols;
        if (get_bits_left(gb) < 10)
            return AVERROR_INVALIDDATA;
        rows = get_bits(gb, 5);
        cols = get_bits(gb, 5);
        if (((rows < 2) || (rows > 25)) || ((cols < 2) || (cols > 25))) {
            return AVERROR_INVALIDDATA;
        }
        s->num_rows_targeted_system_display_actual_peak_luminance = rows;
        s->num_cols_targeted_system_display_actual_peak_luminance = cols;

        if (get_bits_left(gb) < (rows * cols * 4))
            return AVERROR_INVALIDDATA;

        for (int i = 0; i < rows; i++) {
            for (int j = 0; j < cols; j++) {
                s->targeted_system_display_actual_peak_luminance[i][j] =
                    (AVRational){get_bits(gb, 4), peak_luminance_den};
            }
        }
    }
    for (int w = 0; w < s->num_windows; w++) {
        AVHDRPlusColorTransformParams *params = &s->params[w];
        if (get_bits_left(gb) < (3 * 17 + 17 + 4))
            return AVERROR_INVALIDDATA;

        for (int i = 0; i < 3; i++) {
            params->maxscl[i] =
                (AVRational){get_bits(gb, 17), rgb_den};
        }
        params->average_maxrgb =
            (AVRational){get_bits(gb, 17), rgb_den};
        params->num_distribution_maxrgb_percentiles = get_bits(gb, 4);

        if (get_bits_left(gb) <
            (params->num_distribution_maxrgb_percentiles * 24))
            return AVERROR_INVALIDDATA;

        for (int i = 0; i < params->num_distribution_maxrgb_percentiles; i++) {
            params->distribution_maxrgb[i].percentage = get_bits(gb, 7);
            params->distribution_maxrgb[i].percentile =
                (AVRational){get_bits(gb, 17), rgb_den};
        }

        if (get_bits_left(gb) < 10)
            return AVERROR_INVALIDDATA;

        params->fraction_bright_pixels = (AVRational){get_bits(gb, 10), fraction_pixel_den};
    }
    if (get_bits_left(gb) < 1)
        return AVERROR_INVALIDDATA;
    s->mastering_display_actual_peak_luminance_flag = get_bits1(gb);
    if (s->mastering_display_actual_peak_luminance_flag) {
        int rows, cols;
        if (get_bits_left(gb) < 10)
            return AVERROR_INVALIDDATA;
        rows = get_bits(gb, 5);
        cols = get_bits(gb, 5);
        if (((rows < 2) || (rows > 25)) || ((cols < 2) || (cols > 25))) {
            return AVERROR_INVALIDDATA;
        }
        s->num_rows_mastering_display_actual_peak_luminance = rows;
        s->num_cols_mastering_display_actual_peak_luminance = cols;

        if (get_bits_left(gb) < (rows * cols * 4))
            return AVERROR_INVALIDDATA;

        for (int i = 0; i < rows; i++) {
            for (int j = 0; j < cols; j++) {
                s->mastering_display_actual_peak_luminance[i][j] =
                    (AVRational){get_bits(gb, 4), peak_luminance_den};
            }
        }
    }

    for (int w = 0; w < s->num_windows; w++) {
        AVHDRPlusColorTransformParams *params = &s->params[w];
        if (get_bits_left(gb) < 1)
            return AVERROR_INVALIDDATA;

        params->tone_mapping_flag = get_bits1(gb);
        if (params->tone_mapping_flag) {
            if (get_bits_left(gb) < 28)
                return AVERROR_INVALIDDATA;

            params->knee_point_x =
                (AVRational){get_bits(gb, 12), knee_point_den};
            params->knee_point_y =
                (AVRational){get_bits(gb, 12), knee_point_den};
            params->num_bezier_curve_anchors = get_bits(gb, 4);

            if (get_bits_left(gb) < (params->num_bezier_curve_anchors * 10))
                return AVERROR_INVALIDDATA;

            for (int i = 0; i < params->num_bezier_curve_anchors; i++) {
                params->bezier_curve_anchors[i] =
                    (AVRational){get_bits(gb, 10), bezier_anchor_den};
            }
        }

        if (get_bits_left(gb) < 1)
            return AVERROR_INVALIDDATA;
        params->color_saturation_mapping_flag = get_bits1(gb);
        if (params->color_saturation_mapping_flag) {
            if (get_bits_left(gb) < 6)
                return AVERROR_INVALIDDATA;
            params->color_saturation_weight =
                (AVRational){get_bits(gb, 6), saturation_weight_den};
        }
    }

    return 0;
}

int av_dynamic_hdr_plus_to_t35(const AVDynamicHDRPlus *s, uint8_t **data, size_t *size)
{
    uint8_t *buf;
    size_t size_bits, size_bytes;
    PutBitContext pbc, *pb = &pbc;

    if (!s)
        return AVERROR(EINVAL);
    if ((!data || *data) && !size)
       return AVERROR(EINVAL);

    /**
     * Buffer size per CTA-861-H p.253-254:
     * 48 header bits (excluded from the serialized payload)
     * 8 bits for application_mode
     * 2 bits for num_windows
     * 153 bits for window geometry, for each window above 1
     * 27 bits for targeted_system_display_maximum_luminance
     * 1-2511 bits for targeted system display peak luminance information
     * 82-442 bits per window for pixel distribution information
     * 1-2511 bits for mastering display peak luminance information
     * 1-179 bits per window for tonemapping information
     * 1-7 bits per window for color saturation mapping information
     * Total: 123-7249 bits, excluding trimmed header bits
     */
    size_bits = 8;

    size_bits += 2;

    for (int w = 1; w < s->num_windows; w++)
        size_bits += 153;

    size_bits += 27;

    size_bits += 1;
    if (s->targeted_system_display_actual_peak_luminance_flag)
        size_bits += 10 +
                     s->num_rows_targeted_system_display_actual_peak_luminance *
                     s->num_cols_targeted_system_display_actual_peak_luminance * 4;

    for (int w = 0; w < s->num_windows; w++)
        size_bits += 72 + s->params[w].num_distribution_maxrgb_percentiles * 24 + 10;

    size_bits += 1;
    if (s->mastering_display_actual_peak_luminance_flag)
        size_bits += 10 +
                     s->num_rows_mastering_display_actual_peak_luminance *
                     s->num_cols_mastering_display_actual_peak_luminance * 4;

    for (int w = 0; w < s->num_windows; w++) {
        size_bits += 1;
        if (s->params[w].tone_mapping_flag)
            size_bits += 28 + s->params[w].num_bezier_curve_anchors * 10;

        size_bits += 1;
        if (s->params[w].color_saturation_mapping_flag)
            size_bits += 6;
    }

    size_bytes = (size_bits + 7) / 8;

    av_assert0(size_bytes <= AV_HDR_PLUS_MAX_PAYLOAD_SIZE);

    if (!data) {
        *size = size_bytes;
        return 0;
    } else if (*data) {
        if (*size < size_bytes)
            return AVERROR_BUFFER_TOO_SMALL;
        buf = *data;
    } else {
        buf = av_malloc(size_bytes);
        if (!buf)
            return AVERROR(ENOMEM);
    }

    init_put_bits(pb, buf, size_bytes);

    // application_mode is set to Application Version 1
    put_bits(pb, 8, 1);

    // Payload as per CTA-861-H p.253-254
    put_bits(pb, 2, s->num_windows);

    for (int w = 1; w < s->num_windows; w++) {
        put_bits(pb, 16, s->params[w].window_upper_left_corner_x.num / s->params[w].window_upper_left_corner_x.den);
        put_bits(pb, 16, s->params[w].window_upper_left_corner_y.num / s->params[w].window_upper_left_corner_y.den);
        put_bits(pb, 16, s->params[w].window_lower_right_corner_x.num / s->params[w].window_lower_right_corner_x.den);
        put_bits(pb, 16, s->params[w].window_lower_right_corner_y.num / s->params[w].window_lower_right_corner_y.den);
        put_bits(pb, 16, s->params[w].center_of_ellipse_x);
        put_bits(pb, 16, s->params[w].center_of_ellipse_y);
        put_bits(pb, 8, s->params[w].rotation_angle);
        put_bits(pb, 16, s->params[w].semimajor_axis_internal_ellipse);
        put_bits(pb, 16, s->params[w].semimajor_axis_external_ellipse);
        put_bits(pb, 16, s->params[w].semiminor_axis_external_ellipse);
        put_bits(pb, 1, s->params[w].overlap_process_option);
    }

    put_bits(pb, 27, s->targeted_system_display_maximum_luminance.num * luminance_den /
        s->targeted_system_display_maximum_luminance.den);
    put_bits(pb, 1, s->targeted_system_display_actual_peak_luminance_flag);
    if (s->targeted_system_display_actual_peak_luminance_flag) {
        put_bits(pb, 5, s->num_rows_targeted_system_display_actual_peak_luminance);
        put_bits(pb, 5, s->num_cols_targeted_system_display_actual_peak_luminance);
        for (int i = 0; i < s->num_rows_targeted_system_display_actual_peak_luminance; i++) {
            for (int j = 0; j < s->num_cols_targeted_system_display_actual_peak_luminance; j++)
                put_bits(pb, 4, s->targeted_system_display_actual_peak_luminance[i][j].num * peak_luminance_den /
                    s->targeted_system_display_actual_peak_luminance[i][j].den);
        }
    }

    for (int w = 0; w < s->num_windows; w++) {
        for (int i = 0; i < 3; i++)
            put_bits(pb, 17, s->params[w].maxscl[i].num * rgb_den / s->params[w].maxscl[i].den);
        put_bits(pb, 17, s->params[w].average_maxrgb.num * rgb_den / s->params[w].average_maxrgb.den);
        put_bits(pb, 4, s->params[w].num_distribution_maxrgb_percentiles);
        for (int i = 0; i < s->params[w].num_distribution_maxrgb_percentiles; i++) {
            put_bits(pb, 7, s->params[w].distribution_maxrgb[i].percentage);
            put_bits(pb, 17, s->params[w].distribution_maxrgb[i].percentile.num * rgb_den /
                s->params[w].distribution_maxrgb[i].percentile.den);
        }
        put_bits(pb, 10, s->params[w].fraction_bright_pixels.num * fraction_pixel_den /
            s->params[w].fraction_bright_pixels.den);
    }

    put_bits(pb, 1, s->mastering_display_actual_peak_luminance_flag);
    if (s->mastering_display_actual_peak_luminance_flag) {
        put_bits(pb, 5, s->num_rows_mastering_display_actual_peak_luminance);
        put_bits(pb, 5, s->num_cols_mastering_display_actual_peak_luminance);
        for (int i = 0; i < s->num_rows_mastering_display_actual_peak_luminance; i++) {
            for (int j = 0; j < s->num_cols_mastering_display_actual_peak_luminance; j++)
                put_bits(pb, 4, s->mastering_display_actual_peak_luminance[i][j].num * peak_luminance_den /
                    s->mastering_display_actual_peak_luminance[i][j].den);
        }
    }

    for (int w = 0; w < s->num_windows; w++) {
        put_bits(pb, 1, s->params[w].tone_mapping_flag);
        if (s->params[w].tone_mapping_flag) {
            put_bits(pb, 12, s->params[w].knee_point_x.num * knee_point_den / s->params[w].knee_point_x.den);
            put_bits(pb, 12, s->params[w].knee_point_y.num * knee_point_den / s->params[w].knee_point_y.den);
            put_bits(pb, 4, s->params[w].num_bezier_curve_anchors);
            for (int i = 0; i < s->params[w].num_bezier_curve_anchors; i++)
                put_bits(pb, 10, s->params[w].bezier_curve_anchors[i].num * bezier_anchor_den /
                    s->params[w].bezier_curve_anchors[i].den);
            put_bits(pb, 1, s->params[w].color_saturation_mapping_flag);
            if (s->params[w].color_saturation_mapping_flag)
                put_bits(pb, 6, s->params[w].color_saturation_weight.num * saturation_weight_den /
                    s->params[w].color_saturation_weight.den);
        }
    }

    flush_put_bits(pb);

    *data = buf;
    if (size)
        *size = size_bytes;
    return 0;
}

static struct json_object *json_av_rational_to_string(const AVRational r) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d/%d", r.num, r.den);
    return json_object_new_string(buf);
}

int av_dynamic_hdr_plus_to_json(const AVDynamicHDRPlus *s, json_object* root, size_t display_frame_number) {
    struct json_object *hdr_plus = json_object_new_object();
    json_object_object_add(hdr_plus, "display_frame_number", json_object_new_int(display_frame_number));
    json_object_object_add(hdr_plus, "application_version", json_object_new_int(s->application_version));
    json_object_object_add(hdr_plus, "num_windows", json_object_new_int(s->num_windows));
    if (s->num_windows > 0) {
        struct json_object *windows = json_object_new_array();
        for (int w = 0; w < s->num_windows; ++w) {
            struct json_object *window = json_object_new_object();
            const AVHDRPlusColorTransformParams *param = &s->params[w];
            json_object_object_add(window, "window_upper_left_corner_x", json_av_rational_to_string(param->window_upper_left_corner_x));
            json_object_object_add(window, "window_upper_left_corner_y", json_av_rational_to_string(param->window_upper_left_corner_y));
            json_object_object_add(window, "window_lower_right_corner_x", json_av_rational_to_string(param->window_lower_right_corner_x));
            json_object_object_add(window, "window_lower_right_corner_y", json_av_rational_to_string(param->window_lower_right_corner_y));
            json_object_object_add(window, "center_of_ellipse_x", json_object_new_int(param->center_of_ellipse_x));
            json_object_object_add(window, "center_of_ellipse_y", json_object_new_int(param->center_of_ellipse_y));
            json_object_object_add(window, "rotation_angle", json_object_new_int(param->rotation_angle));
            json_object_object_add(window, "semimajor_axis_internal_ellipse", json_object_new_int(param->semimajor_axis_internal_ellipse));
            json_object_object_add(window, "semimajor_axis_external_ellipse", json_object_new_int(param->semimajor_axis_external_ellipse));
            json_object_object_add(window, "semiminor_axis_external_ellipse", json_object_new_int(param->semiminor_axis_external_ellipse));
            json_object_object_add(window, "overlap_process_option", json_object_new_int(param->overlap_process_option));
            {
                struct json_object *maxscl = json_object_new_array();
                for (int i = 0; i < 3; ++i) {
                    json_object_array_add(maxscl, json_av_rational_to_string(param->maxscl[i]));
                }
                json_object_object_add(window, "maxscl", maxscl);
            }
            json_object_object_add(window, "average_maxrgb", json_av_rational_to_string(param->average_maxrgb));
            json_object_object_add(window, "num_distribution_maxrgb_percentiles", json_object_new_int(param->num_distribution_maxrgb_percentiles));
            if (param->num_distribution_maxrgb_percentiles > 0) {
                struct json_object *distribution_maxrgb = json_object_new_array();
                for (int i = 0; i < param->num_distribution_maxrgb_percentiles; ++i) {
                    struct json_object *distribution = json_object_new_object();
                    json_object_object_add(distribution, "percentage", json_object_new_int(param->distribution_maxrgb[i].percentage));
                    json_object_object_add(distribution, "percentile", json_av_rational_to_string(param->distribution_maxrgb[i].percentile));
                    json_object_array_add(distribution_maxrgb, distribution);
                }
                json_object_object_add(window, "distribution_maxrgb", distribution_maxrgb);
            }
            json_object_object_add(window, "fraction_bright_pixels", json_av_rational_to_string(param->fraction_bright_pixels));
            json_object_object_add(window, "tone_mapping_flag", json_object_new_int(param->tone_mapping_flag));
            json_object_object_add(window, "knee_point_x", json_av_rational_to_string(param->knee_point_x));
            json_object_object_add(window, "knee_point_y", json_av_rational_to_string(param->knee_point_y));
            json_object_object_add(window, "num_bezier_curve_anchors", json_object_new_int(param->num_bezier_curve_anchors));
            if (param->num_bezier_curve_anchors > 0) {
                struct json_object *bezier_curve_anchors = json_object_new_array();
                for (int i = 0; i < param->num_bezier_curve_anchors; ++i) {
                    json_object_array_add(bezier_curve_anchors, json_av_rational_to_string(param->bezier_curve_anchors[i]));
                }
                json_object_object_add(window, "bezier_curve_anchors", bezier_curve_anchors);
            }
            json_object_object_add(window, "color_saturation_mapping_flag", json_object_new_int(param->color_saturation_mapping_flag));
            json_object_object_add(window, "color_saturation_weight", json_av_rational_to_string(param->color_saturation_weight));
            json_object_array_add(windows, window);
        }
        json_object_object_add(hdr_plus, "windows", windows);
    }
    json_object_object_add(hdr_plus, "targeted_system_display_maximum_luminance", json_av_rational_to_string(s->targeted_system_display_maximum_luminance));
    if (s->targeted_system_display_actual_peak_luminance_flag) {
        av_log(NULL, AV_LOG_ERROR, "targeted_system_display_actual_peak_luminance_flag is not supported\n");
        return AVERROR(EINVAL);
    }
    if (s->mastering_display_actual_peak_luminance_flag) {
        av_log(NULL, AV_LOG_ERROR, "mastering_display_actual_peak_luminance_flag is not supported\n");
        return AVERROR(EINVAL);
    }

    json_object_array_add(root, hdr_plus);
    return 0;
}

static AVRational json_object_get_rational(json_object *obj) {
    const char *str = NULL;
    int num, den;

    if (!json_object_is_type(obj, json_type_string)) {
        av_log(NULL, AV_LOG_ERROR, "Expected a string\n");
        return (AVRational){0, 1};
    }
    str = json_object_get_string(obj);
    if (sscanf(str, "%d/%d", &num, &den) != 2) {
        av_log(NULL, AV_LOG_ERROR, "Failed to parse rational\n");
        return (AVRational){0, 1};
    }
    return (AVRational){num, den};
}


int av_dynamic_hdr_plus_from_json(AVDynamicHDRPlus *s, struct json_object* json_hdr_plus) {
    s->application_version = json_object_get_int(json_object_object_get(json_hdr_plus, "application_version"));
    s->num_windows = json_object_get_int(json_object_object_get(json_hdr_plus, "num_windows"));
    if (s->num_windows > 0) {
        struct json_object *windows = json_object_object_get(json_hdr_plus, "windows");
        for (int w = 0; w < s->num_windows; ++w) {
            struct json_object *window = json_object_array_get_idx(windows, w);
            AVHDRPlusColorTransformParams *param = &s->params[w];
            param->window_upper_left_corner_x = json_object_get_rational(json_object_object_get(window, "window_upper_left_corner_x"));
            param->window_upper_left_corner_y = json_object_get_rational(json_object_object_get(window, "window_upper_left_corner_y"));
            param->window_lower_right_corner_x = json_object_get_rational(json_object_object_get(window, "window_lower_right_corner_x"));
            param->window_lower_right_corner_y = json_object_get_rational(json_object_object_get(window, "window_lower_right_corner_y"));
            param->center_of_ellipse_x = json_object_get_int(json_object_object_get(window, "center_of_ellipse_x"));
            param->center_of_ellipse_y = json_object_get_int(json_object_object_get(window, "center_of_ellipse_y"));
            param->rotation_angle = json_object_get_int(json_object_object_get(window, "rotation_angle"));
            param->semimajor_axis_internal_ellipse = json_object_get_int(json_object_object_get(window, "semimajor_axis_internal_ellipse"));
            param->semimajor_axis_external_ellipse = json_object_get_int(json_object_object_get(window, "semimajor_axis_external_ellipse"));
            param->semiminor_axis_external_ellipse = json_object_get_int(json_object_object_get(window, "semiminor_axis_external_ellipse"));
            param->overlap_process_option = json_object_get_int(json_object_object_get(window, "overlap_process_option"));
            struct json_object *maxscl = json_object_object_get(window, "maxscl");
            for (int i = 0; i < 3; ++i) {
                param->maxscl[i] = json_object_get_rational(json_object_array_get_idx(maxscl, i));
            }
            param->average_maxrgb = json_object_get_rational(json_object_object_get(window, "average_maxrgb"));
            param->num_distribution_maxrgb_percentiles = json_object_get_int(json_object_object_get(window, "num_distribution_maxrgb_percentiles"));
            if (param->num_distribution_maxrgb_percentiles > 0) {
                struct json_object *distribution_maxrgb = json_object_object_get(window, "distribution_maxrgb");
                for (int i = 0; i < param->num_distribution_maxrgb_percentiles; ++i) {
                    struct json_object *distribution = json_object_array_get_idx(distribution_maxrgb, i);
                    param->distribution_maxrgb[i].percentage = json_object_get_int(json_object_object_get(distribution, "percentage"));
                    param->distribution_maxrgb[i].percentile = json_object_get_rational(json_object_object_get(distribution, "percentile"));
                }
            }
            param->fraction_bright_pixels = json_object_get_rational(json_object_object_get(window, "fraction_bright_pixels"));
            param->tone_mapping_flag = json_object_get_int(json_object_object_get(window, "tone_mapping_flag"));
            param->knee_point_x = json_object_get_rational(json_object_object_get(window, "knee_point_x"));
            param->knee_point_y = json_object_get_rational(json_object_object_get(window, "knee_point_y"));
            param->num_bezier_curve_anchors = json_object_get_int(json_object_object_get(window, "num_bezier_curve_anchors"));
            if (param->num_bezier_curve_anchors > 0) {
                struct json_object *bezier_curve_anchors = json_object_object_get(window, "bezier_curve_anchors");
                for (int i = 0; i < param->num_bezier_curve_anchors; ++i) {
                    param->bezier_curve_anchors[i] = json_object_get_rational(json_object_array_get_idx(bezier_curve_anchors, i));
                }
            }
            param->color_saturation_mapping_flag = json_object_get_int(json_object_object_get(window, "color_saturation_mapping_flag"));
            param->color_saturation_weight = json_object_get_rational(json_object_object_get(window, "color_saturation_weight"));
        }
    }
    s->targeted_system_display_maximum_luminance = json_object_get_rational(json_object_object_get(json_hdr_plus, "targeted_system_display_maximum_luminance"));
    if (json_object_object_get(json_hdr_plus, "targeted_system_display_actual_peak_luminance_flag")) {
        av_log(NULL, AV_LOG_ERROR, "targeted_system_display_actual_peak_luminance_flag is not supported\n");
        return AVERROR(EINVAL);
    }
    if (json_object_object_get(json_hdr_plus, "mastering_display_actual_peak_luminance_flag")) {
        av_log(NULL, AV_LOG_ERROR, "mastering_display_actual_peak_luminance_flag is not supported\n");
        return AVERROR(EINVAL);
    }
    return 0;
}

struct json_object *search_frame_in_array(struct json_object *array, size_t display_frame_number) {
    // Check if the input is a valid array
    if (!json_object_is_type(array, json_type_array)) {
        av_log(NULL, AV_LOG_ERROR, "The input is not a valid array\n");
        return NULL;
    }

    // Iterate through the array
    for (int i = 0; i < json_object_array_length(array); i++) {
        // Get the JSON object at the current index
        struct json_object *element = json_object_array_get_idx(array, i);

        // Check if the element is a JSON object
        if (json_object_is_type(element, json_type_object)) {
            // Look for the key in the object
            struct json_object *found_value;
            if (json_object_object_get_ex(element, "display_frame_number", &found_value)) {
                // Check if the value matches
                if (json_object_is_type(found_value, json_type_int) &&
                    json_object_get_int(found_value) == display_frame_number) {
                    return element; // Return the matching object
                }
            }
        }
    }

    // Return NULL if no match is found
    return NULL;
}
