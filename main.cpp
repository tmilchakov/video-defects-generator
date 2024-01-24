#include <iostream>
#include <map>
#include <vector>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavutil/timestamp.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/log.h>
}

#define av_err2str(errnum) #errnum

#define PTR_VALIDATE(p, message) if(p==nullptr){ exit_with_message(message); }
#define RET_VALIDATE(ret, message) if(ret < 0){ exit_with_message(message); }

#define IF_WITH_PROPERTIES(field, skip_block, block) if(properties_context->field##_step == 0) {    \
properties_context->field##_count--;                                                                \
if(properties_context->field##_count == 0) {                                                        \
properties_context->field##_step = properties->field##_step;                                        \
}                                                                                                   \
skip_block                                                                                          \
log(#field);                                                                                        \
} else {                                                                                            \
block                                                                                               \
properties_context->field##_step--;                                                                 \
properties_context->field##_count=properties->field##_count;                                        \
}

using namespace std;

struct Properties {
    string input_file;
    string output_file = "output.mp4";
    int drop_input_packet_step = -1;
    int drop_input_packet_count = 1;

    int drop_output_packet_step = -1;
    int drop_output_packet_count = 1;

    int pts_drop_step = -1;
    int pts_drop_count = 1;

    int change_pixel_format_step = -1;
    int change_pixel_format_count = 1;

    int take_frame = -1;
};

struct PropertiesContext {
    int drop_input_packet_step = -1;
    int drop_input_packet_count = 1;

    int drop_output_packet_step = -1;
    int drop_output_packet_count = 1;

    int pts_drop_step = -1;
    int pts_drop_count = 1;

    int change_pixel_format_step = -1;
    int change_pixel_format_count = 1;

    map<uint64_t, uint64_t> time_swap;
};

struct StreamingContext {
    AVFormatContext *avfc = nullptr;
    AVStream *video_avs = nullptr;
    AVCodecContext *video_avcc = nullptr;
};

auto properties_name = vector<string>{
        "--input_file - input file name",
        "--output_file - output file name, default value output.mp4",
        "--drop_input_packet_step - step in frame for drop input packets",
        "--drop_input_packet_count - count dropping input packet",
        "--drop_output_packet_step - step in frame for drop output packets",
        "--drop_output_packet_count - count dropping output packet",
        "--pts_drop_step",
        "--pts_drop_count",
        "--change_pixel_format_step",
        "--change_pixel_format_count",
        "--take_frame - number transcoder frame"
};

void print_banner() {
    cout << "usage: video-defects-generator [options] \n"
         << "options are:\n";
    for (const auto &prop_name: properties_name) {
        cout << "    " << prop_name << " \n";
    }
    exit(1);
}

void log(string message) {
    cout << message << "\n";
}

void exit_with_message(string message) {
    log(message);
    exit(1);
}

void parse_property(char **argv, Properties *prop) {

    argv++;
    char *arg;
    while ((arg = *argv++)) {
        if (!strcmp(arg, "--input_file")) {
            arg = *argv++;
            if (arg == nullptr) {
                cout << " missing argument after --input_file\n";
                return;
            }
            prop->input_file = arg;
        } else if (!strcmp(arg, "--output_file")) {
            arg = *argv++;
            if (arg == nullptr) {
                cout << " missing argument after --output_file\n";
                return;
            }
            prop->output_file = arg;
        } else if (!strcmp(arg, "--drop_input_packet_step")) {
            arg = *argv++;
            if (arg == nullptr) {
                cout << " missing argument after --drop_input_packet_step\n";
                return;
            }
            prop->drop_input_packet_step = strtoul(arg, nullptr, 10);
        } else if (!strcmp(arg, "--drop_input_packet_count")) {
            arg = *argv++;
            if (arg == nullptr) {
                cout << " missing argument after --drop_input_packet_count\n";
                return;
            }
            prop->drop_input_packet_count = strtoul(arg, nullptr, 10);
        } else if (!strcmp(arg, "--drop_output_packet_step")) {
            arg = *argv++;
            if (arg == nullptr) {
                cout << " missing argument after --drop_encode_packet_step\n";
                return;
            }
            prop->drop_output_packet_step = strtoul(arg, nullptr, 10);
        } else if (!strcmp(arg, "--drop_output_packet_count")) {
            arg = *argv++;
            if (arg == nullptr) {
                cout << " missing argument after --drop_output_packet_count\n";
                return;
            }
            prop->drop_output_packet_count = strtoul(arg, nullptr, 10);
        } else if (!strcmp(arg, "--pts_drop_count")) {
            arg = *argv++;
            if (arg == nullptr) {
                cout << " missing argument after --pts_drop_count\n";
                return;
            }
            prop->pts_drop_count = strtoul(arg, nullptr, 10);
        } else if (!strcmp(arg, "--pts_drop_step")) {
            arg = *argv++;
            if (arg == nullptr) {
                cout << " missing argument after --pts_drop_step\n";
                return;
            }
            prop->pts_drop_step = strtoul(arg, nullptr, 10);
        } else if (!strcmp(arg, "--take_frame")) {
            arg = *argv++;
            if (arg == nullptr) {
                cout << " missing argument after --take_frame\n";
                return;
            }
            prop->take_frame = strtoul(arg, nullptr, 10);
        } else if (!strcmp(arg, "--change_pixel_format_step")) {
            arg = *argv++;
            if (arg == nullptr) {
                cout << " missing argument after --change_pixel_format_step\n";
                return;
            }
            prop->change_pixel_format_step = strtoul(arg, nullptr, 10);
        } else if (!strcmp(arg, "--change_pixel_format_count")) {
            arg = *argv++;
            if (arg == nullptr) {
                cout << " missing argument after --change_pixel_format_count\n";
                return;
            }
            prop->change_pixel_format_count = strtoul(arg, nullptr, 10);
        } else if (!strcmp(arg, "--help")) {
            print_banner();
        }

    }
}


void init_outpute_context(Properties *properties, StreamingContext *input_context, StreamingContext *output_context) {
//    init encode context
    auto codec = avcodec_find_encoder_by_name("libx264");
    output_context->video_avcc = avcodec_alloc_context3(codec);

    output_context->video_avcc->pix_fmt = codec->pix_fmts[0];
    output_context->video_avcc->height = input_context->video_avcc->height;
    output_context->video_avcc->width = input_context->video_avcc->width;

    output_context->video_avcc->framerate = av_make_q(input_context->video_avs->avg_frame_rate.num,
                                                      input_context->video_avs->avg_frame_rate.den);
    output_context->video_avcc->time_base = av_make_q(1, input_context->video_avs->avg_frame_rate.num);

    output_context->video_avcc->bit_rate = 12000000;
    output_context->video_avcc->rc_buffer_size = (int) (2 * (output_context->video_avcc->bit_rate));
    output_context->video_avcc->rc_max_rate = (int64_t) (1.5 * (output_context->video_avcc->bit_rate));
    output_context->video_avcc->rc_min_rate = (int64_t) (1.5 * (output_context->video_avcc->bit_rate));
    output_context->video_avcc->thread_count = 3;


    av_opt_set(output_context->video_avcc->priv_data, "preset", "fast", 0);
    av_opt_set(output_context->video_avcc->priv_data, "x264-params", "keyint=60:min-keyint=60:scenecut=0:force-cfr=1",
               0);

    int ret = avcodec_open2(output_context->video_avcc, codec, nullptr);
    RET_VALIDATE(ret, "Error open encode context for codec " + to_string(codec->id));

    auto format = av_guess_format(nullptr, properties->output_file.c_str(), nullptr);
    AVOutputFormat *ff_mp4_muxer = new AVOutputFormat();
    ff_mp4_muxer->name = format->name;
    ff_mp4_muxer->long_name = format->long_name;
    ff_mp4_muxer->mime_type = format->mime_type;
    ff_mp4_muxer->extensions = format->extensions;
    ff_mp4_muxer->priv_data_size = format->priv_data_size;
    ff_mp4_muxer->audio_codec = format->audio_codec;
    ff_mp4_muxer->video_codec = format->video_codec;
    ff_mp4_muxer->init = format->init;
    ff_mp4_muxer->write_header = format->write_header;
    ff_mp4_muxer->write_packet = format->write_packet;
    ff_mp4_muxer->write_trailer = format->write_trailer;
    ff_mp4_muxer->deinit = format->deinit;
    ff_mp4_muxer->flags = format->flags | AVFMT_TS_NONSTRICT | AVFMT_NOTIMESTAMPS;
    ff_mp4_muxer->codec_tag = format->codec_tag;
    ff_mp4_muxer->check_bitstream = format->check_bitstream;
    ff_mp4_muxer->priv_class = format->priv_class;

    avformat_alloc_output_context2(&output_context->avfc, ff_mp4_muxer, nullptr,
                                   properties->output_file.c_str());
    PTR_VALIDATE(output_context->avfc, "Can't alloc output context")

    //    __asm ("MOV flags_ptr, flags");
    //    asm("mov flags_ptr, flags");
    output_context->video_avs = avformat_new_stream(output_context->avfc, nullptr);
    output_context->video_avs->time_base = output_context->video_avcc->time_base;
    avcodec_parameters_from_context(output_context->video_avs->codecpar, output_context->video_avcc);

    if (output_context->avfc->oformat->flags & AVFMT_GLOBALHEADER) {
        output_context->avfc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    if (!(output_context->avfc->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&output_context->avfc->pb, properties->output_file.c_str(), AVIO_FLAG_WRITE);
        RET_VALIDATE(ret, "could not open the output file")
    }
    AVDictionary *muxer_opts = nullptr;
    ret = av_dict_set(&muxer_opts, "movflags", "frag_keyframe+empty_moov+delay_moov", 0);
    RET_VALIDATE(ret, "Error init muxer option")
    ret = avformat_write_header(output_context->avfc, &muxer_opts);
    RET_VALIDATE(ret, "Error write header")
    av_dict_free(&muxer_opts);
}

void init_input_context(Properties *properties, StreamingContext *input_context) {
    input_context->avfc = avformat_alloc_context();
    if (!input_context->avfc) {
        exit_with_message("failed to alloc memory for format");
    }
    if (avformat_open_input(&input_context->avfc, properties->input_file.c_str(), nullptr, nullptr) != 0) {
        exit_with_message("failed to open input file " + properties->input_file);

    }
    if (avformat_find_stream_info(input_context->avfc, nullptr) < 0) {
        exit_with_message("failed to get stream info");
    }
    for (int i = 0; i < input_context->avfc->nb_streams; i++) {
//        find video stream
        if (input_context->avfc->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            input_context->video_avs = input_context->avfc->streams[i];
            int ret = 0;
            auto decode_codec = avcodec_find_decoder(input_context->video_avs->codecpar->codec_id);
            PTR_VALIDATE(decode_codec,
                         "Decode codec with id" + to_string(input_context->video_avs->codecpar->codec_id) +
                         " not found");

            input_context->video_avcc = avcodec_alloc_context3(decode_codec);
            PTR_VALIDATE(input_context->video_avcc,
                         "Decode context for codec with id" + to_string(input_context->video_avs->codecpar->codec_id) +
                         " not allocate");

            ret = avcodec_parameters_to_context(input_context->video_avcc, input_context->video_avs->codecpar);
            RET_VALIDATE(ret, "Error copying parameters from stream to codec with id " +
                              to_string(input_context->video_avs->codecpar->codec_id));

            ret = avcodec_open2(input_context->video_avcc, decode_codec, nullptr);
            RET_VALIDATE(ret, "Error open decode context for codec with id " +
                              to_string(input_context->video_avs->codecpar->codec_id));
            return;
        }
    }
    exit_with_message("video streams not found");
}

void resize_frame(
        AVFrame *input_frame,
        AVPixelFormat input_format,
        AVFrame *transcoded_frame,
        StreamingContext *output_context) {
    int err;
    // allocate sws_context
    auto sws_context = sws_getContext(
            input_frame->width, input_frame->height, input_format,
            output_context->video_avcc->width, output_context->video_avcc->height, output_context->video_avcc->pix_fmt,
            SWS_BICUBIC, nullptr, nullptr, nullptr);

    PTR_VALIDATE(sws_context, "Could not allocate sws_context. Error.");
    // allocate image
    err = sws_scale_frame(sws_context, transcoded_frame, input_frame);
    RET_VALIDATE(err, "Error scale frame")
    // free sws_context
    sws_freeContext(sws_context);
}

int encode_video(StreamingContext *decoder, StreamingContext *encoder, AVFrame *input_frame, Properties *properties,
                 PropertiesContext *properties_context) {
    int duration = encoder->video_avcc->time_base.den / encoder->video_avcc->time_base.num /
                   encoder->video_avcc->framerate.num * encoder->video_avcc->framerate.den;
    AVFrame *scale_frame = nullptr;
    int response = 0;
    AVPacket *output_packet = av_packet_alloc();
    PTR_VALIDATE(output_packet, "could not allocate memory for output packet")

    if (input_frame) {
        input_frame->pict_type = AV_PICTURE_TYPE_NONE;
        input_frame->pts = av_rescale_q(
                input_frame->pts,
                decoder->video_avs->time_base,
                encoder->video_avcc->time_base
        );
        scale_frame = av_frame_alloc();
        av_frame_copy_props(scale_frame, input_frame);
        auto pix_fmt = decoder->video_avcc->pix_fmt;

        IF_WITH_PROPERTIES(change_pixel_format,
                           pix_fmt = AV_PIX_FMT_YUV411P;,)

        resize_frame(input_frame, pix_fmt, scale_frame, encoder);

        IF_WITH_PROPERTIES(pts_drop,
                           auto diff = properties->pts_drop_count - properties_context->pts_drop_count;
                                   auto old_pts = scale_frame->pts;
                                   scale_frame->pts = (scale_frame->pts - diff * duration) + diff;
                                   properties_context->time_swap[scale_frame->pts] = old_pts;, ;)
    }

    if (scale_frame) {
        log("frame pts = " + to_string(scale_frame->pts));
    }
    response = avcodec_send_frame(encoder->video_avcc, scale_frame);

    while (response >= 0) {
        response = avcodec_receive_packet(encoder->video_avcc, output_packet);
        if (properties_context->time_swap.find(output_packet->pts) != properties_context->time_swap.end()) {
            output_packet->pts = properties_context->time_swap.find(output_packet->pts)->second;
        }
        if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
            break;
        } else if (response < 0) {
            RET_VALIDATE(response, "Error while receiving packet from encoder: " + string(av_err2str(response)))
        }
        output_packet->duration = duration;
        av_packet_rescale_ts(output_packet, encoder->video_avcc->time_base, encoder->video_avs->time_base);
        IF_WITH_PROPERTIES(drop_output_packet,
                           ;,
                           response = av_write_frame(encoder->avfc, output_packet);)
        RET_VALIDATE(response, "Error while receiving packet from decoder: " + string(av_err2str(response)))
    }
    av_packet_unref(output_packet);
    av_packet_free(&output_packet);
    av_frame_free(&scale_frame);
    return 0;
}


int
transcode_video(StreamingContext *decoder, StreamingContext *encoder, AVPacket *input_packet, AVFrame *input_frame,
                Properties *properties, PropertiesContext *properties_context) {
    int response = 0;

    IF_WITH_PROPERTIES(drop_input_packet,
                       response = 0;,
                       response = avcodec_send_packet(decoder->video_avcc, input_packet);)

    RET_VALIDATE(response, "Error while sending packet to decoder:" + string(av_err2str(response)))

    while (response >= 0) {
        response = avcodec_receive_frame(decoder->video_avcc, input_frame);
        if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
            break;
        } else if (response < 0) {
            RET_VALIDATE(response, "Error while sending packet to decoder:" + string(av_err2str(response)))
        }
        if (response >= 0) {
            encode_video(decoder, encoder, input_frame, properties, properties_context);
        }
        av_frame_unref(input_frame);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_banner();
    }
    Properties properties;
    PropertiesContext properties_context;
    StreamingContext input_context;
    StreamingContext output_context;
    parse_property(argv, &properties);

    properties_context.drop_input_packet_count = properties.drop_input_packet_count;
    properties_context.drop_output_packet_count = properties.drop_output_packet_count;

    properties_context.drop_input_packet_step = properties.drop_input_packet_step;
    properties_context.drop_output_packet_step = properties.drop_output_packet_step;

    properties_context.pts_drop_count = properties.pts_drop_count;
    properties_context.pts_drop_step = properties.pts_drop_step;

    properties_context.change_pixel_format_step = properties.change_pixel_format_step;
    properties_context.change_pixel_format_count = properties.change_pixel_format_count;

    init_input_context(&properties, &input_context);
    init_outpute_context(&properties, &input_context, &output_context);

    AVFrame *input_frame = av_frame_alloc();
    AVPacket *input_packet = av_packet_alloc();

    int ret = 0;
    int frame_take = 0;
    while (av_read_frame(input_context.avfc, input_packet) >= 0) {
        if (input_context.video_avs->index == input_packet->stream_index) {
            ret = transcode_video(&input_context, &output_context, input_packet, input_frame, &properties,
                                  &properties_context);
            RET_VALIDATE(ret, " Error transcode");
            frame_take++;
        }
        av_packet_unref(input_packet);
        av_frame_unref(input_frame);
        if (properties.take_frame == frame_take) {
            break;
        }
    }

    if (encode_video(&input_context, &output_context, nullptr, &properties, &properties_context))return -1;
    av_write_trailer(output_context.avfc);
    return 0;
}
